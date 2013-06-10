/*
 * Copyright (C) 2013
 *   Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static struct resource armada_drm_resources[5];
static struct platform_device armada_drm_platform_device = {
	.name = "armada-510-drm",
	.dev = { .coherent_dma_mask = ~0, },
	.resource = armada_drm_resources,
};

static int dove_card_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *lcdnp;
	struct resource *res = armada_drm_resources;
	int ret, crtcs = 0;

	/* get video memory resource */
	if (of_address_to_resource(np, 0, res++)) {
		dev_err(&pdev->dev, "invalid or missing video memory\n");
		return -EINVAL;
	}

	/* get reg and irq resource from each enabled lcdc */
	for_each_compatible_node(lcdnp, NULL, "marvell,dove-lcd") {
		struct clk_lookup *cl;
		struct clk *clk;

		if (!of_device_is_available(lcdnp))
			continue;

		ret = of_address_to_resource(lcdnp, 0, res++);
		if (ret)
			return ret;

		ret = of_irq_to_resource(lcdnp, 0, res++);
		if (ret < 0)
			return ret;

		clk = of_clk_get(lcdnp, 0);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			if (ret == -ENOENT)
				return -EPROBE_DEFER;
			return ret;
		}

		/* add clock alias for dovefb.0 */
		cl = clkdev_alloc(clk, "ext_ref_clk_1", "armada-510-drm.0");
		if (cl)
			clkdev_add(cl);
		clk_put(clk);

		crtcs++;
	}

	if (!crtcs)
		return -ENODEV;

	armada_drm_platform_device.num_resources = 1 + (2*crtcs);
	ret = platform_device_register(&armada_drm_platform_device);
	if (ret) {
		dev_err(&pdev->dev, "unable to register drm device\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id dove_card_of_ids[] = {
	{ .compatible = "marvell,dove-video-card", },
	{ }
};
MODULE_DEVICE_TABLE(of, dove_card_of_ids);

static struct platform_driver dove_card_driver = {
	.probe	= dove_card_probe,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "dove-drm-card",
		.of_match_table = of_match_ptr(dove_card_of_ids),
	},
};
module_platform_driver(dove_card_driver);

MODULE_AUTHOR("Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>");
MODULE_DESCRIPTION("Armada DRM Graphics Card");
MODULE_LICENSE("GPL");
