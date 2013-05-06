/*
 * Allwinner A1X SoCs IRQ chip driver.
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/exception.h>
#include <asm/mach/irq.h>

#include "irqchip.h"

#define SUN4I_IRQ_VECTOR_REG		0x00
#define SUN4I_IRQ_PROTECTION_REG	0x08
#define SUN4I_IRQ_NMI_CTRL_REG		0x0c
#define SUN4I_IRQ_PENDING_REG(x)	(0x10 + 0x4 * x)
#define SUN4I_IRQ_FIQ_PENDING_REG(x)	(0x20 + 0x4 * x)
#define SUN4I_IRQ_ENABLE_REG(x)		(0x40 + 0x4 * x)
#define SUN4I_IRQ_MASK_REG(x)		(0x50 + 0x4 * x)
#define SUN4I_NUM_CHIPS			3
#define SUN4I_IRQS_PER_CHIP		32

static void __iomem *sun4i_irq_base;
static struct irq_domain *sun4i_irq_domain;

static asmlinkage void __exception_irq_entry sun4i_handle_irq(struct pt_regs *regs);

static int __init sun4i_init_domain_chips(void)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct irq_chip_generic *gc;
	int i, ret, base = 0;

	ret = irq_alloc_domain_generic_chips(d, SUN4I_IRQS_PER_CHIP, 1,
					     "sun4i_irq", handle_level_irq,
					     clr, 0, IRQ_GC_INIT_MASK_CACHE);
	if (ret)
		return ret;

	for (i = 0; i < SUN4I_NUM_CHIPS; i++, base += SUN4I_IRQS_PER_CHIP) {
		gc = irq_get_domain_generic_chip(sun4i_irq_domain, base);
		gc->reg_base = sun4i_irq_base;
		gc->chip_types[0].regs.mask = SUN4I_IRQ_ENABLE_REG(i);
		gc->chip_types[0].regs.ack = SUN4I_IRQ_PENDING_REG(i);
		gc->chip_types[0].chip.mask = irq_gc_mask_clr_bit;
		gc->chip_types[0].chip.ack = irq_gc_ack_set_bit;
		gc->chip_types[0].chip.unmask = irq_gc_mask_set_bit;

		/* Disable, mask and clear all pending interrupts */
		writel(0, sun4i_irq_base + SUN4I_IRQ_ENABLE_REG(i));
		writel(0, sun4i_irq_base + SUN4I_IRQ_MASK_REG(i));
		writel(0xffffffff, sun4i_irq_base + SUN4I_IRQ_PENDING_REG(i));
	}
	return 0;
}

static int __init sun4i_of_init(struct device_node *node,
				struct device_node *parent)
{
	sun4i_irq_base = of_iomap(node, 0);
	if (!sun4i_irq_base)
		panic("%s: unable to map IC registers\n",
			node->full_name);

	/* Enable protection mode */
	writel(0x01, sun4i_irq_base + SUN4I_IRQ_PROTECTION_REG);

	/* Configure the external interrupt source type */
	writel(0x00, sun4i_irq_base + SUN4I_IRQ_NMI_CTRL_REG);

	sun4i_irq_domain = irq_domain_add_linear(node, 3 * 32,
						 &irq_generic_chip_ops, NULL);
	if (!sun4i_irq_domain || sun4i_init_domain_chips())
		panic("%s: unable to create IRQ domain\n", node->full_name);

	set_handle_irq(sun4i_handle_irq);

	return 0;
}
IRQCHIP_DECLARE(allwinner_sun4i_ic, "allwinner,sun4i-ic", sun4i_of_init);

static asmlinkage void __exception_irq_entry sun4i_handle_irq(struct pt_regs *regs)
{
	u32 irq, hwirq;

	hwirq = readl(sun4i_irq_base + SUN4I_IRQ_VECTOR_REG) >> 2;
	while (hwirq != 0) {
		irq = irq_find_mapping(sun4i_irq_domain, hwirq);
		handle_IRQ(irq, regs);
		hwirq = readl(sun4i_irq_base + SUN4I_IRQ_VECTOR_REG) >> 2;
	}
}
