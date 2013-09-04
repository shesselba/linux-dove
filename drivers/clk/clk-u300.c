/*
 * U300 clock implementation
 * Copyright (C) 2007-2012 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_data/u300-syscon.h>
#include <linux/spinlock.h>

/*
 * The clocking hierarchy currently looks like this.
 * NOTE: the idea is NOT to show how the clocks are routed on the chip!
 * The ideas is to show dependencies, so a clock higher up in the
 * hierarchy has to be on in order for another clock to be on. Now,
 * both CPU and DMA can actually be on top of the hierarchy, and that
 * is not modeled currently. Instead we have the backbone AMBA bus on
 * top. This bus cannot be programmed in any way but conceptually it
 * needs to be active for the bridges and devices to transport data.
 *
 * Please be aware that a few clocks are hw controlled, which mean that
 * the hw itself can turn on/off or change the rate of the clock when
 * needed!
 *
 *  AMBA bus
 *  |
 *  +- CPU
 *  +- FSMC NANDIF NAND Flash interface
 *  +- SEMI Shared Memory interface
 *  +- ISP Image Signal Processor (U335 only)
 *  +- CDS (U335 only)
 *  +- DMA Direct Memory Access Controller
 *  +- AAIF APP/ACC Inteface (Mobile Scalable Link, MSL)
 *  +- APEX
 *  +- VIDEO_ENC AVE2/3 Video Encoder
 *  +- XGAM Graphics Accelerator Controller
 *  +- AHB
 *  |
 *  +- ahb:0 AHB Bridge
 *  |  |
 *  |  +- ahb:1 INTCON Interrupt controller
 *  |  +- ahb:3 MSPRO  Memory Stick Pro controller
 *  |  +- ahb:4 EMIF   External Memory interface
 *  |
 *  +- fast:0 FAST bridge
 *  |  |
 *  |  +- fast:1 MMCSD MMC/SD card reader controller
 *  |  +- fast:2 I2S0  PCM I2S channel 0 controller
 *  |  +- fast:3 I2S1  PCM I2S channel 1 controller
 *  |  +- fast:4 I2C0  I2C channel 0 controller
 *  |  +- fast:5 I2C1  I2C channel 1 controller
 *  |  +- fast:6 SPI   SPI controller
 *  |  +- fast:7 UART1 Secondary UART (U335 only)
 *  |
 *  +- slow:0 SLOW bridge
 *     |
 *     +- slow:1 SYSCON (not possible to control)
 *     +- slow:2 WDOG Watchdog
 *     +- slow:3 UART0 primary UART
 *     +- slow:4 TIMER_APP Application timer - used in Linux
 *     +- slow:5 KEYPAD controller
 *     +- slow:6 GPIO controller
 *     +- slow:7 RTC controller
 *     +- slow:8 BT Bus Tracer (not used currently)
 *     +- slow:9 EH Event Handler (not used currently)
 *     +- slow:a TIMER_ACC Access style timer (not used currently)
 *     +- slow:b PPM (U335 only, what is that?)
 */

/* Global syscon virtual base */
static void __iomem *syscon_vbase;

static void __init u300_set_syscon_base(void)
{
	struct device_node *np =
		of_find_compatible_node(NULL, NULL, "stericsson,u300-syscon");
	syscon_vbase = of_iomap(np, 0);
	if (!syscon_vbase)
		pr_crit("could not remap syscon\n");
	of_node_put(np);
}

/**
 * struct clk_syscon - U300 syscon clock
 * @hw: corresponding clock hardware entry
 * @hw_ctrld: whether this clock is hardware controlled (for refcount etc)
 *	and does not need any magic pokes to be enabled/disabled
 * @reset: state holder, whether this block's reset line is asserted or not
 * @res_reg: reset line enable/disable flag register
 * @res_bit: bit for resetting or taking this consumer out of reset
 * @en_reg: clock line enable/disable flag register
 * @en_bit: bit for enabling/disabling this consumer clock line
 * @clk_val: magic value to poke in the register to enable/disable
 *	this one clock
 */
struct clk_syscon {
	struct clk_hw hw;
	bool hw_ctrld;
	bool reset;
	void __iomem *res_reg;
	u8 res_bit;
	void __iomem *en_reg;
	u8 en_bit;
	u16 clk_val;
};

#define to_syscon(_hw) container_of(_hw, struct clk_syscon, hw)

static DEFINE_SPINLOCK(syscon_resetreg_lock);

/*
 * Reset control functions. We remember if a block has been
 * taken out of reset and don't remove the reset assertion again
 * and vice versa. Currently we only remove resets so the
 * enablement function is defined out.
 */
static void syscon_block_reset_enable(struct clk_syscon *sclk)
{
	unsigned long iflags;
	u16 val;

	/* Not all blocks support resetting */
	if (!sclk->res_reg)
		return;
	spin_lock_irqsave(&syscon_resetreg_lock, iflags);
	val = readw(sclk->res_reg);
	val |= BIT(sclk->res_bit);
	writew(val, sclk->res_reg);
	spin_unlock_irqrestore(&syscon_resetreg_lock, iflags);
	sclk->reset = true;
}

static void syscon_block_reset_disable(struct clk_syscon *sclk)
{
	unsigned long iflags;
	u16 val;

	/* Not all blocks support resetting */
	if (!sclk->res_reg)
		return;
	spin_lock_irqsave(&syscon_resetreg_lock, iflags);
	val = readw(sclk->res_reg);
	val &= ~BIT(sclk->res_bit);
	writew(val, sclk->res_reg);
	spin_unlock_irqrestore(&syscon_resetreg_lock, iflags);
	sclk->reset = false;
}

static int syscon_clk_prepare(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* If the block is in reset, bring it out */
	if (sclk->reset)
		syscon_block_reset_disable(sclk);
	return 0;
}

static void syscon_clk_unprepare(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* Please don't force the console into reset */
	if (sclk->clk_val == U300_SYSCON_SBCER_UART_CLK_EN)
		return;
	/* When unpreparing, force block into reset */
	if (!sclk->reset)
		syscon_block_reset_enable(sclk);
}

static int syscon_clk_enable(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* Don't touch the hardware controlled clocks */
	if (sclk->hw_ctrld)
		return 0;
	/* These cannot be controlled */
	if (sclk->clk_val == 0xFFFFU)
		return 0;

	writew(sclk->clk_val, syscon_vbase + U300_SYSCON_SBCER);
	return 0;
}

static void syscon_clk_disable(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* Don't touch the hardware controlled clocks */
	if (sclk->hw_ctrld)
		return;
	if (sclk->clk_val == 0xFFFFU)
		return;
	/* Please don't disable the console port */
	if (sclk->clk_val == U300_SYSCON_SBCER_UART_CLK_EN)
		return;

	writew(sclk->clk_val, syscon_vbase + U300_SYSCON_SBCDR);
}

static int syscon_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);
	u16 val;

	/* If no enable register defined, it's always-on */
	if (!sclk->en_reg)
		return 1;

	val = readw(sclk->en_reg);
	val &= BIT(sclk->en_bit);

	return val ? 1 : 0;
}

static u16 syscon_get_perf(void)
{
	u16 val;

	val = readw(syscon_vbase + U300_SYSCON_CCR);
	val &= U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK;
	return val;
}

static unsigned long
syscon_clk_recalc_rate(struct clk_hw *hw,
		       unsigned long parent_rate)
{
	struct clk_syscon *sclk = to_syscon(hw);
	u16 perf = syscon_get_perf();

	switch(sclk->clk_val) {
	case U300_SYSCON_SBCER_FAST_BRIDGE_CLK_EN:
	case U300_SYSCON_SBCER_I2C0_CLK_EN:
	case U300_SYSCON_SBCER_I2C1_CLK_EN:
	case U300_SYSCON_SBCER_MMC_CLK_EN:
	case U300_SYSCON_SBCER_SPI_CLK_EN:
		/* The FAST clocks have one progression */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 13000000;
		default:
			return parent_rate; /* 26 MHz */
		}
	case U300_SYSCON_SBCER_DMAC_CLK_EN:
	case U300_SYSCON_SBCER_NANDIF_CLK_EN:
	case U300_SYSCON_SBCER_XGAM_CLK_EN:
		/* AMBA interconnect peripherals */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 6500000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
			return 26000000;
		default:
			return parent_rate; /* 52 MHz */
		}
	case U300_SYSCON_SBCER_SEMI_CLK_EN:
	case U300_SYSCON_SBCER_EMIF_CLK_EN:
		/* EMIF speeds */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 13000000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
			return 52000000;
		default:
			return 104000000;
		}
	case U300_SYSCON_SBCER_CPU_CLK_EN:
		/* And the fast CPU clock */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 13000000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
			return 52000000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
			return 104000000;
		default:
			return parent_rate; /* 208 MHz */
		}
	default:
		/*
		 * The SLOW clocks and default just inherit the rate of
		 * their parent (typically PLL13 13 MHz).
		 */
		return parent_rate;
	}
}

static long
syscon_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		      unsigned long *prate)
{
	struct clk_syscon *sclk = to_syscon(hw);

	if (sclk->clk_val != U300_SYSCON_SBCER_CPU_CLK_EN)
		return *prate;
	/* We really only support setting the rate of the CPU clock */
	if (rate <= 13000000)
		return 13000000;
	if (rate <= 52000000)
		return 52000000;
	if (rate <= 104000000)
		return 104000000;
	return 208000000;
}

static int syscon_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_syscon *sclk = to_syscon(hw);
	u16 val;

	/* We only support setting the rate of the CPU clock */
	if (sclk->clk_val != U300_SYSCON_SBCER_CPU_CLK_EN)
		return -EINVAL;
	switch (rate) {
	case 13000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER;
		break;
	case 52000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE;
		break;
	case 104000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH;
		break;
	case 208000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST;
		break;
	default:
		return -EINVAL;
	}
	val |= readw(syscon_vbase + U300_SYSCON_CCR) &
		~U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK ;
	writew(val, syscon_vbase + U300_SYSCON_CCR);
	return 0;
}

static const struct clk_ops syscon_clk_ops = {
	.prepare = syscon_clk_prepare,
	.unprepare = syscon_clk_unprepare,
	.enable = syscon_clk_enable,
	.disable = syscon_clk_disable,
	.is_enabled = syscon_clk_is_enabled,
	.recalc_rate = syscon_clk_recalc_rate,
	.round_rate = syscon_clk_round_rate,
	.set_rate = syscon_clk_set_rate,
};

static struct clk * __init
syscon_clk_register(struct device *dev, const char *name,
		    const char *parent_name, unsigned long flags,
		    bool hw_ctrld,
		    void __iomem *res_reg, u8 res_bit,
		    void __iomem *en_reg, u8 en_bit,
		    u16 clk_val)
{
	struct clk *clk;
	struct clk_syscon *sclk;
	struct clk_init_data init;

	sclk = kzalloc(sizeof(struct clk_syscon), GFP_KERNEL);
	if (!sclk) {
		pr_err("could not allocate syscon clock %s\n",
			name);
		return ERR_PTR(-ENOMEM);
	}
	init.name = name;
	init.ops = &syscon_clk_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	sclk->hw.init = &init;
	sclk->hw_ctrld = hw_ctrld;
	/* Assume the block is in reset at registration */
	sclk->reset = true;
	sclk->res_reg = res_reg;
	sclk->res_bit = res_bit;
	sclk->en_reg = en_reg;
	sclk->en_bit = en_bit;
	sclk->clk_val = clk_val;

	clk = clk_register(dev, &sclk->hw);
	if (IS_ERR(clk))
		kfree(sclk);

	return clk;
}

#define U300_CLK_TYPE_SLOW 0
#define U300_CLK_TYPE_FAST 1
#define U300_CLK_TYPE_REST 2

/**
 * struct u300_clock - defines the bits and pieces for a certain clock
 * @type: the clock type, slow fast or rest
 * @id: the bit in the slow/fast/rest register for this clock
 * @hw_ctrld: whether the clock is hardware controlled
 * @clk_val: a value to poke in the one-write enable/disable registers
 */
struct u300_clock {
	u8 type;
	u8 id;
	bool hw_ctrld;
	u16 clk_val;
};

static struct u300_clock const u300_clk_lookup[] __initconst = {
	{
		.type = U300_CLK_TYPE_REST,
		.id = 3,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_CPU_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 4,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_DMAC_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 5,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_EMIF_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 6,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_NANDIF_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 8,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_XGAM_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 9,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_SEMI_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 10,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_AHB_SUBSYS_BRIDGE_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 12,
		.hw_ctrld = false,
		/* INTCON: cannot be enabled, just taken out of reset */
		.clk_val = 0xFFFFU,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 0,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_FAST_BRIDGE_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 1,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_I2C0_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 2,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_I2C1_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 5,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_MMC_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 6,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_SPI_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 0,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_SLOW_BRIDGE_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 1,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_UART_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 4,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_GPIO_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 6,
		.hw_ctrld = true,
		/* No clock enable register bit */
		.clk_val = 0xFFFFU,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 7,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_APP_TMR_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 8,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_ACC_TMR_CLK_EN,
	},
};

static void __init of_u300_syscon_clk_init(struct device_node *np)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = np->name;
	const char *parent_name;
	void __iomem *res_reg;
	void __iomem *en_reg;
	u32 clk_type;
	u32 clk_id;
	int i;

	if (!syscon_vbase)
		u300_set_syscon_base();

	if (of_property_read_u32(np, "clock-type", &clk_type)) {
		pr_err("%s: syscon clock \"%s\" missing clock-type property\n",
		       __func__, clk_name);
		return;
	}
	if (of_property_read_u32(np, "clock-id", &clk_id)) {
		pr_err("%s: syscon clock \"%s\" missing clock-id property\n",
		       __func__, clk_name);
		return;
	}
	parent_name = of_clk_get_parent_name(np, 0);

	switch (clk_type) {
	case U300_CLK_TYPE_SLOW:
		res_reg = syscon_vbase + U300_SYSCON_RSR;
		en_reg = syscon_vbase + U300_SYSCON_CESR;
		break;
	case U300_CLK_TYPE_FAST:
		res_reg = syscon_vbase + U300_SYSCON_RFR;
		en_reg = syscon_vbase + U300_SYSCON_CEFR;
		break;
	case U300_CLK_TYPE_REST:
		res_reg = syscon_vbase + U300_SYSCON_RRR;
		en_reg = syscon_vbase + U300_SYSCON_CERR;
		break;
	default:
		pr_err("unknown clock type %x specified\n", clk_type);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(u300_clk_lookup); i++) {
		const struct u300_clock *u3clk = &u300_clk_lookup[i];

		if (u3clk->type == clk_type && u3clk->id == clk_id)
			clk = syscon_clk_register(NULL,
						  clk_name, parent_name,
						  0, u3clk->hw_ctrld,
						  res_reg, u3clk->id,
						  en_reg, u3clk->id,
						  u3clk->clk_val);
	}

	if (!IS_ERR(clk)) {
		of_clk_add_provider(np, of_clk_src_simple_get, clk);

		/*
		 * Some few system clocks - device tree does not
		 * represent clocks without a corresponding device node.
		 * for now we add these three clocks here.
		 */
		if (clk_type == U300_CLK_TYPE_REST && clk_id == 5)
			clk_register_clkdev(clk, NULL, "pl172");
		if (clk_type == U300_CLK_TYPE_REST && clk_id == 9)
			clk_register_clkdev(clk, NULL, "semi");
		if (clk_type == U300_CLK_TYPE_REST && clk_id == 12)
			clk_register_clkdev(clk, NULL, "intcon");
	}
}
CLK_OF_DECLARE(u300_syscon_clk,
	"stericsson,u300-syscon-clk", of_u300_syscon_clk_init);

/**
 * struct clk_mclk - U300 MCLK clock (MMC/SD clock)
 * @hw: corresponding clock hardware entry
 * @is_mspro: if this is the memory stick clock rather than MMC/SD
 */
struct clk_mclk {
	struct clk_hw hw;
	bool is_mspro;
};

#define to_mclk(_hw) container_of(_hw, struct clk_mclk, hw)

static int mclk_clk_prepare(struct clk_hw *hw)
{
	struct clk_mclk *mclk = to_mclk(hw);
	u16 val;

	/* The MMC and MSPRO clocks need some special set-up */
	if (!mclk->is_mspro) {
		/* Set default MMC clock divisor to 18.9 MHz */
		writew(0x0054U, syscon_vbase + U300_SYSCON_MMF0R);
		val = readw(syscon_vbase + U300_SYSCON_MMCR);
		/* Disable the MMC feedback clock */
		val &= ~U300_SYSCON_MMCR_MMC_FB_CLK_SEL_ENABLE;
		/* Disable MSPRO frequency */
		val &= ~U300_SYSCON_MMCR_MSPRO_FREQSEL_ENABLE;
		writew(val, syscon_vbase + U300_SYSCON_MMCR);
	} else {
		val = readw(syscon_vbase + U300_SYSCON_MMCR);
		/* Disable the MMC feedback clock */
		val &= ~U300_SYSCON_MMCR_MMC_FB_CLK_SEL_ENABLE;
		/* Enable MSPRO frequency */
		val |= U300_SYSCON_MMCR_MSPRO_FREQSEL_ENABLE;
		writew(val, syscon_vbase + U300_SYSCON_MMCR);
	}

	return 0;
}

static unsigned long
mclk_clk_recalc_rate(struct clk_hw *hw,
		     unsigned long parent_rate)
{
	u16 perf = syscon_get_perf();

	switch (perf) {
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		/*
		 * Here, the 208 MHz PLL gets shut down and the always
		 * on 13 MHz PLL used for RTC etc kicks into use
		 * instead.
		 */
		return 13000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST:
	{
		/*
		 * This clock is under program control. The register is
		 * divided in two nybbles, bit 7-4 gives cycles-1 to count
		 * high, bit 3-0 gives cycles-1 to count low. Distribute
		 * these with no more than 1 cycle difference between
		 * low and high and add low and high to get the actual
		 * divisor. The base PLL is 208 MHz. Writing 0x00 will
		 * divide by 1 and 1 so the highest frequency possible
		 * is 104 MHz.
		 *
		 * e.g. 0x54 =>
		 * f = 208 / ((5+1) + (4+1)) = 208 / 11 = 18.9 MHz
		 */
		u16 val = readw(syscon_vbase + U300_SYSCON_MMF0R) &
			U300_SYSCON_MMF0R_MASK;
		switch (val) {
		case 0x0054:
			return 18900000;
		case 0x0044:
			return 20800000;
		case 0x0043:
			return 23100000;
		case 0x0033:
			return 26000000;
		case 0x0032:
			return 29700000;
		case 0x0022:
			return 34700000;
		case 0x0021:
			return 41600000;
		case 0x0011:
			return 52000000;
		case 0x0000:
			return 104000000;
		default:
			break;
		}
	}
	default:
		break;
	}
	return parent_rate;
}

static long
mclk_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		    unsigned long *prate)
{
	if (rate <= 18900000)
		return 18900000;
	if (rate <= 20800000)
		return 20800000;
	if (rate <= 23100000)
		return 23100000;
	if (rate <= 26000000)
		return 26000000;
	if (rate <= 29700000)
		return 29700000;
	if (rate <= 34700000)
		return 34700000;
	if (rate <= 41600000)
		return 41600000;
	/* Highest rate */
	return 52000000;
}

static int mclk_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	u16 val;
	u16 reg;

	switch (rate) {
	case 18900000:
		val = 0x0054;
		break;
	case 20800000:
		val = 0x0044;
		break;
	case 23100000:
		val = 0x0043;
		break;
	case 26000000:
		val = 0x0033;
		break;
	case 29700000:
		val = 0x0032;
		break;
	case 34700000:
		val = 0x0022;
		break;
	case 41600000:
		val = 0x0021;
		break;
	case 52000000:
		val = 0x0011;
		break;
	case 104000000:
		val = 0x0000;
		break;
	default:
		return -EINVAL;
	}

	reg = readw(syscon_vbase + U300_SYSCON_MMF0R) &
		~U300_SYSCON_MMF0R_MASK;
	writew(reg | val, syscon_vbase + U300_SYSCON_MMF0R);
	return 0;
}

static const struct clk_ops mclk_ops = {
	.prepare = mclk_clk_prepare,
	.recalc_rate = mclk_clk_recalc_rate,
	.round_rate = mclk_clk_round_rate,
	.set_rate = mclk_clk_set_rate,
};

static struct clk * __init
mclk_clk_register(struct device *dev, const char *name,
		  const char *parent_name, bool is_mspro)
{
	struct clk *clk;
	struct clk_mclk *mclk;
	struct clk_init_data init;

	mclk = kzalloc(sizeof(struct clk_mclk), GFP_KERNEL);
	if (!mclk) {
		pr_err("could not allocate MMC/SD clock %s\n",
		       name);
		return ERR_PTR(-ENOMEM);
	}
	init.name = "mclk";
	init.ops = &mclk_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	mclk->hw.init = &init;
	mclk->is_mspro = is_mspro;

	clk = clk_register(dev, &mclk->hw);
	if (IS_ERR(clk))
		kfree(mclk);

	return clk;
}

static void __init of_u300_syscon_mclk_init(struct device_node *np)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = np->name;
	const char *parent_name;

	if (!syscon_vbase)
		u300_set_syscon_base();

	parent_name = of_clk_get_parent_name(np, 0);
	clk = mclk_clk_register(NULL, clk_name, parent_name, false);
	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(u300_syscon_mclk,
	"stericsson,u300-syscon-mclk", of_u300_syscon_mclk_init);
