/*
 * Marvell Dove pinctrl driver based on mvebu pinctrl core
 *
 * Author: Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-mvebu.h"

#define DOVE_SB_REGS_VIRT_BASE		0xfde00000
#define DOVE_MPP_VIRT_BASE		(DOVE_SB_REGS_VIRT_BASE | 0xd0200)
#define DOVE_PMU_MPP_GENERAL_CTRL	(DOVE_MPP_VIRT_BASE + 0x10)
#define  DOVE_AU0_AC97_SEL		(1 << 16)
#define DOVE_GLOBAL_CONFIG_1		(DOVE_SB_REGS_VIRT_BASE | 0xe802C)
#define  DOVE_TWSI_ENABLE_OPTION1	(1 << 7)
#define DOVE_GLOBAL_CONFIG_2		(DOVE_SB_REGS_VIRT_BASE | 0xe8030)
#define  DOVE_TWSI_ENABLE_OPTION2	(1 << 20)
#define  DOVE_TWSI_ENABLE_OPTION3	(1 << 21)
#define  DOVE_TWSI_OPTION3_GPIO		(1 << 22)
#define DOVE_SSP_CTRL_STATUS_1		(DOVE_SB_REGS_VIRT_BASE | 0xe8034)
#define  DOVE_SSP_ON_AU1		(1 << 0)
#define DOVE_MPP_GENERAL_VIRT_BASE	(DOVE_SB_REGS_VIRT_BASE | 0xe803c)
#define  DOVE_AU1_SPDIFO_GPIO_EN	(1 << 1)
#define  DOVE_NAND_GPIO_EN		(1 << 0)
#define DOVE_GPIO_LO_VIRT_BASE		(DOVE_SB_REGS_VIRT_BASE | 0xd0400)
#define DOVE_MPP_CTRL4_VIRT_BASE	(DOVE_GPIO_LO_VIRT_BASE + 0x40)
#define  DOVE_SPI_GPIO_SEL		(1 << 5)
#define  DOVE_UART1_GPIO_SEL		(1 << 4)
#define  DOVE_AU1_GPIO_SEL		(1 << 3)
#define  DOVE_CAM_GPIO_SEL		(1 << 2)
#define  DOVE_SD1_GPIO_SEL		(1 << 1)
#define  DOVE_SD0_GPIO_SEL		(1 << 0)

static int dove_pmu_mpp_ctrl_get(struct mvebu_mpp_ctrl *ctrl,
				 unsigned long *config)
{
	unsigned off = (ctrl->pid / 8) * 4;
	unsigned shift = (ctrl->pid % 8) * 4;
	unsigned long pmu = readl(DOVE_PMU_MPP_GENERAL_CTRL);
	unsigned long mpp = readl(DOVE_MPP_VIRT_BASE + off);

	if (pmu & (1 << ctrl->pid))
		*config = 0x10;
	else
		*config = (mpp >> shift) & 0xf;
	return 0;
}

static int dove_pmu_mpp_ctrl_set(struct mvebu_mpp_ctrl *ctrl,
				 unsigned long config)
{
	unsigned off = (ctrl->pid / 8) * 4;
	unsigned shift = (ctrl->pid % 8) * 4;
	unsigned long pmu = readl(DOVE_PMU_MPP_GENERAL_CTRL);
	unsigned long mpp = readl(DOVE_MPP_VIRT_BASE + off);

	if (config == 0x10)
		writel(pmu | (1 << ctrl->pid), DOVE_PMU_MPP_GENERAL_CTRL);
	else {
		writel(pmu & ~(1 << ctrl->pid), DOVE_PMU_MPP_GENERAL_CTRL);
		mpp &= ~(0xf << shift);
		mpp |= config << shift;
		writel(mpp, DOVE_MPP_VIRT_BASE + off);
	}
	return 0;
}

static int dove_mpp4_ctrl_get(struct mvebu_mpp_ctrl *ctrl,
			      unsigned long *config)
{
	unsigned long mpp4 = readl(DOVE_MPP_CTRL4_VIRT_BASE);
	unsigned long mask;

	if (ctrl->pid == 24)
		mask = DOVE_CAM_GPIO_SEL;
	else if (ctrl->pid == 40)
		mask = DOVE_SD0_GPIO_SEL;
	else if (ctrl->pid == 46)
		mask = DOVE_SD1_GPIO_SEL;
	else if (ctrl->pid == 58)
		mask = DOVE_SPI_GPIO_SEL;
	else if (ctrl->pid == 62)
		mask = DOVE_UART1_GPIO_SEL;
	else
		return -EINVAL;

	*config = ((mpp4 & mask) != 0);

	return 0;
}

static int dove_mpp4_ctrl_set(struct mvebu_mpp_ctrl *ctrl,
			      unsigned long config)
{
	unsigned long mpp4 = readl(DOVE_MPP_CTRL4_VIRT_BASE);
	unsigned long mask;

	if (ctrl->pid == 24)
		mask = DOVE_CAM_GPIO_SEL;
	else if (ctrl->pid == 40)
		mask = DOVE_SD0_GPIO_SEL;
	else if (ctrl->pid == 46)
		mask = DOVE_SD1_GPIO_SEL;
	else if (ctrl->pid == 58)
		mask = DOVE_SPI_GPIO_SEL;
	else if (ctrl->pid == 62)
		mask = DOVE_UART1_GPIO_SEL;
	else
		return -EINVAL;

	mpp4 &= ~mask;
	if (config)
		mpp4 |= mask;

	writel(mpp4, DOVE_MPP_CTRL4_VIRT_BASE);

	return 0;
}

static int dove_nand_ctrl_get(struct mvebu_mpp_ctrl *ctrl,
			      unsigned long *config)
{
	unsigned long gmpp = readl(DOVE_MPP_GENERAL_VIRT_BASE);

	*config = ((gmpp & DOVE_NAND_GPIO_EN) != 0);

	return 0;
}

static int dove_nand_ctrl_set(struct mvebu_mpp_ctrl *ctrl,
			      unsigned long config)
{
	unsigned long gmpp = readl(DOVE_MPP_GENERAL_VIRT_BASE);

	gmpp &= ~DOVE_NAND_GPIO_EN;
	if (config)
		gmpp |= DOVE_NAND_GPIO_EN;

	writel(gmpp, DOVE_MPP_GENERAL_VIRT_BASE);

	return 0;
}

static int dove_audio0_ctrl_get(struct mvebu_mpp_ctrl *ctrl,
				unsigned long *config)
{
	unsigned long pmu = readl(DOVE_PMU_MPP_GENERAL_CTRL);

	*config = ((pmu & DOVE_AU0_AC97_SEL) != 0);

	return 0;
}

static int dove_audio0_ctrl_set(struct mvebu_mpp_ctrl *ctrl,
				unsigned long config)
{
	unsigned long pmu = readl(DOVE_PMU_MPP_GENERAL_CTRL);

	pmu &= ~DOVE_AU0_AC97_SEL;
	if (config)
		pmu |= DOVE_AU0_AC97_SEL;
	writel(pmu, DOVE_PMU_MPP_GENERAL_CTRL);

	return 0;
}

static int dove_audio1_ctrl_get(struct mvebu_mpp_ctrl *ctrl,
				unsigned long *config)
{
	unsigned long mpp4 = readl(DOVE_MPP_CTRL4_VIRT_BASE);
	unsigned long sspc1 = readl(DOVE_SSP_CTRL_STATUS_1);
	unsigned long gmpp = readl(DOVE_MPP_GENERAL_VIRT_BASE);
	unsigned long gcfg2 = readl(DOVE_GLOBAL_CONFIG_2);

	*config = 0;
	if (mpp4 & DOVE_AU1_GPIO_SEL)
		*config |= 0x8;
	if (sspc1 & DOVE_SSP_ON_AU1)
		*config |= 0x4;
	if (gmpp & DOVE_AU1_SPDIFO_GPIO_EN)
		*config |= 0x2;
	if (gcfg2 & DOVE_TWSI_OPTION3_GPIO)
		*config |= 0x1;

	/* SSP/TWSI only if I2S1 not set*/
	if ((*config & 0x8) == 0)
		*config &= ~(0x4 | 0x1);
	/* TWSI only if SPDIFO not set*/
	if ((*config & 0x2) == 0)
		*config &= ~(0x1);
	return 0;
}

static int dove_audio1_ctrl_set(struct mvebu_mpp_ctrl *ctrl,
				unsigned long config)
{
	unsigned long mpp4 = readl(DOVE_MPP_CTRL4_VIRT_BASE);
	unsigned long sspc1 = readl(DOVE_SSP_CTRL_STATUS_1);
	unsigned long gmpp = readl(DOVE_MPP_GENERAL_VIRT_BASE);
	unsigned long gcfg2 = readl(DOVE_GLOBAL_CONFIG_2);

	if (config & 0x1)
		gcfg2 |= DOVE_TWSI_OPTION3_GPIO;
	if (config & 0x2)
		gmpp |= DOVE_AU1_SPDIFO_GPIO_EN;
	if (config & 0x4)
		sspc1 |= DOVE_SSP_ON_AU1;
	if (config & 0x8)
		mpp4 |= DOVE_AU1_GPIO_SEL;

	writel(mpp4, DOVE_MPP_CTRL4_VIRT_BASE);
	writel(sspc1, DOVE_SSP_CTRL_STATUS_1);
	writel(gmpp, DOVE_MPP_GENERAL_VIRT_BASE);
	writel(gcfg2, DOVE_GLOBAL_CONFIG_2);

	return 0;
}

/* mpp[52:57] gpio pins depend heavily on current config;
 * gpio_req does not try to mux in gpio capabilities to not
 * break other functions. If you require all mpps as gpio
 * enforce gpio setting by pinctrl mapping.
 */
static int dove_audio1_ctrl_gpio_req(struct mvebu_mpp_ctrl *ctrl, u8 pid)
{
	unsigned long config;

	dove_audio1_ctrl_get(ctrl, &config);

	switch (config) {
	/* mode 0x00 i2s1/spdifo : no gpio */
	/* mode 0x0c ssp/spdifo  : no gpio */
	/* mode 0x0f ssp/twsi    : no gpio */
	case 0x00:
	case 0x0c:
	case 0x0f:
		return -ENOTSUPP;

	/* mode 0x02 i2s1 : gpio[56:57] */
	/* mode 0x0e ssp  : gpio[56:57] */
	case 0x02:
	case 0x0e:
		if (pid >= 56)
			return 0;

	/* mode 0x08 spdifo : gpio[52:55] */
	/* mode 0x0b twsi   : gpio[52:55] */
	case 0x08:
	case 0x0b:
		if (pid <= 55)
			return 0;

	/* mode 0x0a/gpio : all gpio */
	case 0x0a:
		return 0;
	}

	return -ENOTSUPP;
}

/* mpp[52:57] has gpio pins capable of in and out */
static int dove_audio1_ctrl_gpio_dir(struct mvebu_mpp_ctrl *ctrl, u8 pid,
				bool input)
{
	return 0;
}

static int dove_twsi_ctrl_get(struct mvebu_mpp_ctrl *ctrl,
			      unsigned long *config)
{
	unsigned long gcfg1 = readl(DOVE_GLOBAL_CONFIG_1);
	unsigned long gcfg2 = readl(DOVE_GLOBAL_CONFIG_2);

	*config = 0;
	if (gcfg1 & DOVE_TWSI_ENABLE_OPTION1)
		*config = 0x1;
	else if (gcfg2 & DOVE_TWSI_ENABLE_OPTION2)
		*config = 0x2;
	else if (gcfg2 & DOVE_TWSI_ENABLE_OPTION3)
		*config = 0x3;

	return 0;
}

static int dove_twsi_ctrl_set(struct mvebu_mpp_ctrl *ctrl,
				unsigned long config)
{
	unsigned long gcfg1 = readl(DOVE_GLOBAL_CONFIG_1);
	unsigned long gcfg2 = readl(DOVE_GLOBAL_CONFIG_2);

	gcfg1 &= ~DOVE_TWSI_ENABLE_OPTION1;
	gcfg2 &= ~(DOVE_TWSI_ENABLE_OPTION2 | DOVE_TWSI_ENABLE_OPTION2);

	switch (config) {
	case 1:
		gcfg1 |= DOVE_TWSI_ENABLE_OPTION1;
		break;
	case 2:
		gcfg2 |= DOVE_TWSI_ENABLE_OPTION2;
		break;
	case 3:
		gcfg2 |= DOVE_TWSI_ENABLE_OPTION3;
		break;
	}

	writel(gcfg1, DOVE_GLOBAL_CONFIG_1);
	writel(gcfg2, DOVE_GLOBAL_CONFIG_2);

	return 0;
}

static struct mvebu_mpp_ctrl dove_mpp_controls[] = {
	MPP_FUNC_CTRL(0, 0, "mpp0", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(1, 1, "mpp1", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(2, 2, "mpp2", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(3, 3, "mpp3", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(4, 4, "mpp4", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(5, 5, "mpp5", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(6, 6, "mpp6", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(7, 7, "mpp7", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(8, 8, "mpp8", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(9, 9, "mpp9", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(10, 10, "mpp10", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(11, 11, "mpp11", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(12, 12, "mpp12", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(13, 13, "mpp13", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(14, 14, "mpp14", dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(15, 15, "mpp15", dove_pmu_mpp_ctrl),
	MPP_REG_CTRL(16, 23),
	MPP_FUNC_CTRL(24, 39, "mpp_camera", dove_mpp4_ctrl),
	MPP_FUNC_CTRL(40, 45, "mpp_sdio0", dove_mpp4_ctrl),
	MPP_FUNC_CTRL(46, 51, "mpp_sdio1", dove_mpp4_ctrl),
	MPP_FUNC_GPIO_CTRL(52, 57, "mpp_audio1", dove_audio1_ctrl),
	MPP_FUNC_CTRL(58, 61, "mpp_spi0", dove_mpp4_ctrl),
	MPP_FUNC_CTRL(62, 63, "mpp_uart1", dove_mpp4_ctrl),
	MPP_FUNC_CTRL(64, 71, "mpp_nand", dove_nand_ctrl),
	MPP_FUNC_CTRL(72, 72, "audio0", dove_audio0_ctrl),
	MPP_FUNC_CTRL(73, 73, "twsi", dove_twsi_ctrl),
};

static struct mvebu_mpp_mode dove_mpp_modes[] = {
	MPP_MODE(0,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "rts"),
		MPP_FUNCTION(0x03, "sdio0", "cd"),
		MPP_FUNCTION(0x0f, "lcd0", "pwm"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(1,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "cts"),
		MPP_FUNCTION(0x03, "sdio0", "wp"),
		MPP_FUNCTION(0x0f, "lcd1", "pwm"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(2,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "sata", "prsnt"),
		MPP_FUNCTION(0x02, "uart2", "txd"),
		MPP_FUNCTION(0x03, "sdio0", "buspwr"),
		MPP_FUNCTION(0x04, "uart1", "rts"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(3,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "sata", "act"),
		MPP_FUNCTION(0x02, "uart2", "rxd"),
		MPP_FUNCTION(0x03, "sdio0", "ledctrl"),
		MPP_FUNCTION(0x04, "uart1", "cts"),
		MPP_FUNCTION(0x0f, "lcd-spi", "cs1"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(4,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "rts"),
		MPP_FUNCTION(0x03, "sdio1", "cd"),
		MPP_FUNCTION(0x04, "spi1", "miso"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(5,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "cts"),
		MPP_FUNCTION(0x03, "sdio1", "wp"),
		MPP_FUNCTION(0x04, "spi1", "cs"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(6,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "txd"),
		MPP_FUNCTION(0x03, "sdio1", "buspwr"),
		MPP_FUNCTION(0x04, "spi1", "mosi"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(7,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "rxd"),
		MPP_FUNCTION(0x03, "sdio1", "ledctrl"),
		MPP_FUNCTION(0x04, "spi1", "sck"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(8,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "watchdog", "rstout"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(9,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x05, "pex1", "clkreq"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(10,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x05, "ssp", "sclk"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(11,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "sata", "prsnt"),
		MPP_FUNCTION(0x02, "sata-1", "act"),
		MPP_FUNCTION(0x03, "sdio0", "ledctrl"),
		MPP_FUNCTION(0x04, "sdio1", "ledctrl"),
		MPP_FUNCTION(0x05, "pex0", "clkreq"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(12,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "sata", "act"),
		MPP_FUNCTION(0x02, "uart2", "rts"),
		MPP_FUNCTION(0x03, "audio0", "extclk"),
		MPP_FUNCTION(0x04, "sdio1", "cd"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(13,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "cts"),
		MPP_FUNCTION(0x03, "audio1", "extclk"),
		MPP_FUNCTION(0x04, "sdio1", "wp"),
		MPP_FUNCTION(0x05, "ssp", "extclk"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(14,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "txd"),
		MPP_FUNCTION(0x04, "sdio1", "buspwr"),
		MPP_FUNCTION(0x05, "ssp", "rxd"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(15,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "rxd"),
		MPP_FUNCTION(0x04, "sdio1", "ledctrl"),
		MPP_FUNCTION(0x05, "ssp", "sfrm"),
		MPP_FUNCTION(0x10, "pmu", NULL)),
	MPP_MODE(16,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "rts"),
		MPP_FUNCTION(0x03, "sdio0", "cd"),
		MPP_FUNCTION(0x04, "lcd-spi", "cs1"),
		MPP_FUNCTION(0x05, "ac97", "sdi1")),
	MPP_MODE(17,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "ac97-1", "sysclko"),
		MPP_FUNCTION(0x02, "uart3", "cts"),
		MPP_FUNCTION(0x03, "sdio0", "wp"),
		MPP_FUNCTION(0x04, "twsi", "sda"),
		MPP_FUNCTION(0x05, "ac97", "sdi2")),
	MPP_MODE(18,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "txd"),
		MPP_FUNCTION(0x03, "sdio0", "buspwr"),
		MPP_FUNCTION(0x04, "lcd0", "pwm"),
		MPP_FUNCTION(0x05, "ac97", "sdi3")),
	MPP_MODE(19,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "rxd"),
		MPP_FUNCTION(0x03, "sdio0", "ledctrl"),
		MPP_FUNCTION(0x04, "twsi", "sck")),
	MPP_MODE(20,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "ac97", "sysclko"),
		MPP_FUNCTION(0x02, "lcd-spi", "miso"),
		MPP_FUNCTION(0x03, "sdio1", "cd"),
		MPP_FUNCTION(0x05, "sdio0", "cd"),
		MPP_FUNCTION(0x06, "spi1", "miso")),
	MPP_MODE(21,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "uart1", "rts"),
		MPP_FUNCTION(0x02, "lcd-spi", "cs0"),
		MPP_FUNCTION(0x03, "sdio1", "wp"),
		MPP_FUNCTION(0x04, "ssp", "sfrm"),
		MPP_FUNCTION(0x05, "sdio0", "wp"),
		MPP_FUNCTION(0x06, "spi1", "cs")),
	MPP_MODE(22,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "uart1", "cts"),
		MPP_FUNCTION(0x02, "lcd-spi", "mosi"),
		MPP_FUNCTION(0x03, "sdio1", "buspwr"),
		MPP_FUNCTION(0x04, "ssp", "txd"),
		MPP_FUNCTION(0x05, "sdio0", "buspwr"),
		MPP_FUNCTION(0x06, "spi1", "mosi")),
	MPP_MODE(23,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "lcd-spi", "sck"),
		MPP_FUNCTION(0x03, "sdio1", "ledctrl"),
		MPP_FUNCTION(0x04, "ssp", "sclk"),
		MPP_FUNCTION(0x05, "sdio0", "ledctrl"),
		MPP_FUNCTION(0x06, "spi1", "sck")),
	MPP_MODE(24,
		MPP_FUNCTION(0x00, "camera", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(40,
		MPP_FUNCTION(0x00, "sdio0", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(46,
		MPP_FUNCTION(0x00, "sdio1", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(52,
		MPP_FUNCTION(0x00, "i2s1/spdifo", NULL),
		MPP_FUNCTION(0x02, "i2s1", NULL),
		MPP_FUNCTION(0x08, "spdifo", NULL),
		MPP_FUNCTION(0x0a, "gpio", NULL),
		MPP_FUNCTION(0x0b, "twsi", NULL),
		MPP_FUNCTION(0x0c, "ssp/spdifo", NULL),
		MPP_FUNCTION(0x0e, "ssp", NULL),
		MPP_FUNCTION(0x0f, "ssp/twsi", NULL)),
	MPP_MODE(58,
		MPP_FUNCTION(0x00, "spi0", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(62,
		MPP_FUNCTION(0x00, "uart1", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(64,
		MPP_FUNCTION(0x00, "nand", NULL),
		MPP_FUNCTION(0x01, "gpo", NULL)),
	MPP_MODE(72,
		MPP_FUNCTION(0x00, "i2s", NULL),
		MPP_FUNCTION(0x01, "ac97", NULL)),
	MPP_MODE(73,
		MPP_FUNCTION(0x00, "none", NULL),
		MPP_FUNCTION(0x01, "opt1", NULL),
		MPP_FUNCTION(0x02, "opt2", NULL),
		MPP_FUNCTION(0x03, "opt3", NULL)),
};

static struct pinctrl_gpio_range dove_mpp_gpio_ranges[] = {
	MPP_GPIO_RANGE(0,  0,  0, 32),
	MPP_GPIO_RANGE(1, 32, 32, 32),
	MPP_GPIO_RANGE(2, 64, 64,  8),
};

static struct mvebu_pinctrl_soc_info dove_pinctrl_info = {
	.controls = dove_mpp_controls,
	.ncontrols = ARRAY_SIZE(dove_mpp_controls),
	.modes = dove_mpp_modes,
	.nmodes = ARRAY_SIZE(dove_mpp_modes),
	.gpioranges = dove_mpp_gpio_ranges,
	.ngpioranges = ARRAY_SIZE(dove_mpp_gpio_ranges),
	.variant = 0,
};

static struct clk *pdma_clk;

static struct of_device_id dove_pinctrl_of_match[] __devinitdata = {
	{ .compatible = "marvell,dove-pinctrl", .data = &dove_pinctrl_info },
	{ }
};

static int __devinit dove_pinctrl_probe(struct platform_device *pdev)
{
	const struct of_device_id *match =
		of_match_device(dove_pinctrl_of_match, &pdev->dev);
	int ret;

	if (match)
		pdev->dev.platform_data = match->data;

	/*
	 * General MPP Configuration Register is part of pdma registers.
	 * grab clk to make sure it is clocked.
	 */
	pdma_clk = clk_get_sys("dove-pdma", NULL);
	if (IS_ERR(pdma_clk)) {
		dev_err(&pdev->dev, "unable to get pdma clk\n");
		return -ENODEV;
	}

	clk_prepare_enable(pdma_clk);
	ret = mvebu_pinctrl_probe(pdev);
	if (!ret)
		return 0;

	clk_disable_unprepare(pdma_clk);
	clk_put(pdma_clk);
	return ret;
}

static int __devexit dove_pinctrl_remove(struct platform_device *pdev)
{
	int ret;

	ret = mvebu_pinctrl_remove(pdev);
	if (!IS_ERR(pdma_clk)) {
		clk_disable_unprepare(pdma_clk);
		clk_put(pdma_clk);
	}
	return ret;
}

static struct platform_driver dove_pinctrl_driver = {
	.driver = {
		.name = "dove-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dove_pinctrl_of_match),
	},
	.probe = dove_pinctrl_probe,
	.remove = __devexit_p(dove_pinctrl_remove),
};

module_platform_driver(dove_pinctrl_driver);

MODULE_AUTHOR("Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>");
MODULE_DESCRIPTION("Marvell Dove pinctrl driver");
MODULE_LICENSE("GPL v2");
