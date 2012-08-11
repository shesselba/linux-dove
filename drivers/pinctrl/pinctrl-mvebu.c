/*
 * Marvell MVEBU pinctrl core driver
 *
 * Authors: Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *          Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinctrl-mvebu.h"

struct mvebu_pinctrl_function {
	unsigned fid;
	const char *name;
	const char *setting;
	const char **groups;
	unsigned num_groups;
};

struct mvebu_pinctrl_group {
	const char *name;
	struct mvebu_mpp_ctrl *ctrl;
	struct mvebu_mpp_ctrl_setting *settings;
	unsigned num_settings;
	unsigned gid;
	unsigned *pins;
	unsigned npins;
};

struct mvebu_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc desc;
	void __iomem *base;
	struct mvebu_pinctrl_group *groups;
	unsigned num_groups;
	struct mvebu_pinctrl_function *functions;
	unsigned num_functions;
	unsigned variant;
};

struct mvebu_pinctrl_group *mvebu_pinctrl_find_group_by_pid(
	struct mvebu_pinctrl *pctl, unsigned pid)
{
	unsigned n;
	for (n = 0; n < pctl->num_groups; n++) {
		if (pid >= pctl->groups[n].pins[0] &&
		    pid < pctl->groups[n].pins[0] +
			pctl->groups[n].npins)
			return &pctl->groups[n];
	}
	return NULL;
}

struct mvebu_pinctrl_group *mvebu_pinctrl_find_group_by_name(
	struct mvebu_pinctrl *pctl, const char *name)
{
	unsigned n;
	for (n = 0; n < pctl->num_groups; n++) {
		if (strcmp(name, pctl->groups[n].name) == 0)
			return &pctl->groups[n];
	}
	return NULL;
}

struct mvebu_mpp_ctrl_setting *mvebu_pinctrl_find_setting_by_val(
	struct mvebu_pinctrl *pctl, struct mvebu_pinctrl_group *grp,
	unsigned long config)
{
	unsigned n;
	for (n = 0; n < grp->num_settings; n++) {
		if (config == grp->settings[n].val) {
			if (!pctl->variant || (pctl->variant &
					       grp->settings[n].variant))
				return &grp->settings[n];
		}
	}
	return NULL;
}

struct mvebu_mpp_ctrl_setting *mvebu_pinctrl_find_setting_by_name(
	struct mvebu_pinctrl *pctl, struct mvebu_pinctrl_group *grp,
	const char *name)
{
	unsigned n;
	for (n = 0; n < grp->num_settings; n++) {
		if (strcmp(name, grp->settings[n].name) == 0) {
			if (!pctl->variant || (pctl->variant &
					       grp->settings[n].variant))
				return &grp->settings[n];
		}
	}
	return NULL;
}

struct mvebu_mpp_ctrl_setting *mvebu_pinctrl_find_gpio_setting(
	struct mvebu_pinctrl *pctl, struct mvebu_pinctrl_group *grp)
{
	unsigned n;
	for (n = 0; n < grp->num_settings; n++) {
		if (grp->settings[n].flags &
			(MVEBU_SETTING_GPO | MVEBU_SETTING_GPI)) {
			if (!pctl->variant || (pctl->variant &
						grp->settings[n].variant))
				return &grp->settings[n];
		}
	}
	return NULL;
}

struct mvebu_pinctrl_function *mvebu_pinctrl_find_function_by_name(
	struct mvebu_pinctrl *pctl, const char *name)
{
	unsigned n;
	for (n = 0; n < pctl->num_functions; n++) {
		if (strcmp(name, pctl->functions[n].name) == 0)
			return &pctl->functions[n];
	}
	return NULL;
}

static int mvebu_common_mpp_get(struct mvebu_pinctrl *pctl,
				struct mvebu_pinctrl_group *grp,
				unsigned long *config)
{
	unsigned pin = grp->gid;
	unsigned off = (pin / 8) * 4;
	unsigned shift = (pin % 8) * 4;

	*config = readl(pctl->base + off);
	*config >>= shift;
	*config &= 0xf;

	return 0;
}

static int mvebu_common_mpp_set(struct mvebu_pinctrl *pctl,
				struct mvebu_pinctrl_group *grp,
				unsigned long config)
{
	unsigned pin = grp->gid;
	unsigned off = (pin / 8) * 4;
	unsigned shift = (pin % 8) * 4;
	unsigned long reg;

	reg = readl(pctl->base + off);
	reg &= ~(0xf << shift);
	reg |= (config << shift);
	writel(reg, pctl->base + off);

	return 0;
}

static int mvebu_pinconf_group_get(struct pinctrl_dev *pctldev,
				unsigned gid, unsigned long *config)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp = &pctl->groups[gid];

	if (!grp->ctrl)
		return -EINVAL;

	if (grp->ctrl->mpp_get)
		return grp->ctrl->mpp_get(grp->ctrl, config);

	return mvebu_common_mpp_get(pctl, grp, config);
}

static int mvebu_pinconf_group_set(struct pinctrl_dev *pctldev,
				unsigned gid, unsigned long config)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp = &pctl->groups[gid];

	if (!grp->ctrl)
		return -EINVAL;

	if (grp->ctrl->mpp_set)
		return grp->ctrl->mpp_set(grp->ctrl, config);

	return mvebu_common_mpp_set(pctl, grp, config);
}

static void mvebu_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s, unsigned gid)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp = &pctl->groups[gid];
	struct mvebu_mpp_ctrl_setting *curr;
	unsigned long config;
	unsigned n;

	if (mvebu_pinconf_group_get(pctldev, gid, &config))
		return;

	curr = mvebu_pinctrl_find_setting_by_val(pctl, grp, config);

	if (curr) {
		seq_printf(s, "current: %s", curr->name);
		if (curr->subname)
			seq_printf(s, "(%s)", curr->subname);
		if (curr->flags & (MVEBU_SETTING_GPO | MVEBU_SETTING_GPI)) {
			seq_printf(s, "(");
			if (curr->flags & MVEBU_SETTING_GPI)
				seq_printf(s, "i");
			if (curr->flags & MVEBU_SETTING_GPO)
				seq_printf(s, "o");
			seq_printf(s, ")");
		}
	} else
		seq_printf(s, "current: UNKNOWN");

	if (grp->num_settings > 1) {
		seq_printf(s, ", available = [");
		for (n = 0; n < grp->num_settings; n++) {
			if (curr == &grp->settings[n])
				continue;

			/* skip unsupported settings for this variant */
			if (pctl->variant &&
			    !(pctl->variant & grp->settings[n].variant))
				continue;

			seq_printf(s, " %s", grp->settings[n].name);
			if (grp->settings[n].subname)
				seq_printf(s, "(%s)", grp->settings[n].subname);
			if (grp->settings[n].flags &
				(MVEBU_SETTING_GPO | MVEBU_SETTING_GPI)) {
				seq_printf(s, "(");
				if (grp->settings[n].flags & MVEBU_SETTING_GPI)
					seq_printf(s, "i");
				if (grp->settings[n].flags & MVEBU_SETTING_GPO)
					seq_printf(s, "o");
				seq_printf(s, ")");
			}
		}
		seq_printf(s, " ]");
	}
	return;
}

struct pinconf_ops mvebu_pinconf_ops = {
	.pin_config_group_get = mvebu_pinconf_group_get,
	.pin_config_group_set = mvebu_pinconf_group_set,
	.pin_config_group_dbg_show = mvebu_pinconf_group_dbg_show,
};

static int mvebu_pinmux_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->num_functions;
}

static const char *mvebu_pinmux_get_func_name(struct pinctrl_dev *pctldev,
					unsigned fid)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->functions[fid].name;
}

static int mvebu_pinmux_get_groups(struct pinctrl_dev *pctldev, unsigned fid,
				const char * const **groups,
				unsigned * const num_groups)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctl->functions[fid].groups;
	*num_groups = pctl->functions[fid].num_groups;
	return 0;
}

static int mvebu_pinmux_enable(struct pinctrl_dev *pctldev, unsigned fid,
			unsigned gid)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_function *func = &pctl->functions[fid];
	struct mvebu_pinctrl_group *grp = &pctl->groups[gid];
	struct mvebu_mpp_ctrl_setting *setting;
	int ret;

	setting = mvebu_pinctrl_find_setting_by_name(pctl, grp,
						     func->setting);
	if (!setting) {
		dev_err(pctl->dev,
			"unable to find setting %s in group %s\n",
			func->setting, func->groups[gid]);
		return -EINVAL;
	}

	ret = mvebu_pinconf_group_set(pctldev, grp->gid, setting->val);
	if (ret) {
		dev_err(pctl->dev, "cannot set group %s to %s\n",
			func->groups[gid], func->setting);
		return ret;
	}

	return 0;
}

static int mvebu_pinmux_gpio_request_enable(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range, unsigned offset)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp;
	struct mvebu_mpp_ctrl_setting *setting;

	grp = mvebu_pinctrl_find_group_by_pid(pctl, offset);
	if (!grp)
		return -EINVAL;

	if (grp->ctrl->mpp_gpio_req)
		return grp->ctrl->mpp_gpio_req(grp->ctrl, offset);

	setting = mvebu_pinctrl_find_gpio_setting(pctl, grp);
	if (!setting)
		return -ENOTSUPP;

	return mvebu_pinconf_group_set(pctldev, grp->gid, setting->val);
}

static int mvebu_pinmux_gpio_set_direction(struct pinctrl_dev *pctldev,
	   struct pinctrl_gpio_range *range, unsigned offset, bool input)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp;
	struct mvebu_mpp_ctrl_setting *setting;

	grp = mvebu_pinctrl_find_group_by_pid(pctl, offset);
	if (!grp)
		return -EINVAL;

	if (grp->ctrl->mpp_gpio_dir)
		return grp->ctrl->mpp_gpio_dir(grp->ctrl, offset, input);

	setting = mvebu_pinctrl_find_gpio_setting(pctl, grp);
	if (!setting)
		return -ENOTSUPP;

	if ((input && (setting->flags & MVEBU_SETTING_GPI)) ||
	    (!input && (setting->flags & MVEBU_SETTING_GPO)))
		return 0;

	return -ENOTSUPP;
}

static struct pinmux_ops mvebu_pinmux_ops = {
	.get_functions_count = mvebu_pinmux_get_funcs_count,
	.get_function_name = mvebu_pinmux_get_func_name,
	.get_function_groups = mvebu_pinmux_get_groups,
	.gpio_request_enable = mvebu_pinmux_gpio_request_enable,
	.gpio_set_direction = mvebu_pinmux_gpio_set_direction,
	.enable = mvebu_pinmux_enable,
};

static int mvebu_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	return pctl->num_groups;
}

static const char *mvebu_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned gid)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	return pctl->groups[gid].name;
}

static int mvebu_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned gid, const unsigned **pins,
					unsigned *num_pins)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	*pins = pctl->groups[gid].pins;
	*num_pins = pctl->groups[gid].npins;
	return 0;
}

static int mvebu_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					struct device_node *np,
					struct pinctrl_map **map,
					unsigned *num_maps)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct property *prop;
	const char *function;
	const char *group;
	int ret, nmaps, n;

	*map = NULL;
	*num_maps = 0;

	ret = of_property_read_string(np, "marvell,function", &function);
	if (ret) {
		dev_err(pctl->dev,
			"missing marvell,function in node %s\n", np->name);
		return 0;
	}

	nmaps = of_property_count_strings(np, "marvell,pins");
	if (nmaps < 0) {
		dev_err(pctl->dev,
			"missing marvell,pins in node %s\n", np->name);
		return 0;
	}

	*map = kmalloc(nmaps * sizeof(struct pinctrl_map), GFP_KERNEL);
	if (map == NULL) {
		dev_err(pctl->dev,
			"cannot allocate pinctrl_map memory for %s\n",
			np->name);
		return -ENOMEM;
	}

	n = 0;
	of_property_for_each_string(np, "marvell,pins", prop, group) {
		struct mvebu_pinctrl_group *grp =
			mvebu_pinctrl_find_group_by_name(pctl, group);

		if (!grp) {
			dev_err(pctl->dev, "unknown pin %s", group);
			continue;
		}

		if (!mvebu_pinctrl_find_setting_by_name(pctl, grp, function)) {
			dev_err(pctl->dev, "unsupported function %s on pin %s",
				function, group);
			continue;
		}

		(*map)[n].type = PIN_MAP_TYPE_MUX_GROUP;
		(*map)[n].data.mux.group = group;
		(*map)[n].data.mux.function = np->name;
		n++;
	}

	*num_maps = nmaps;

	return 0;
}

static void mvebu_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned num_maps)
{
	kfree(map);
}

static struct pinctrl_ops mvebu_pinctrl_ops = {
	.get_groups_count = mvebu_pinctrl_get_groups_count,
	.get_group_name = mvebu_pinctrl_get_group_name,
	.get_group_pins = mvebu_pinctrl_get_group_pins,
	.dt_node_to_map = mvebu_pinctrl_dt_node_to_map,
	.dt_free_map = mvebu_pinctrl_dt_free_map,
};

static int __devinit mvebu_pinctrl_dt_parse_function(struct mvebu_pinctrl *pctl,
					struct device_node *np, unsigned idx)
{
	struct mvebu_pinctrl_function *func = &pctl->functions[idx];
	struct property *prop;
	const char *setting;
	const char *group;
	unsigned n = 0;
	int ret, num_groups;

	ret = of_property_read_string(np, "marvell,function", &setting);
	if (ret) {
		dev_err(pctl->dev,
			"missing marvell,function in node %s\n", np->name);
		return -EINVAL;
	}

	num_groups = of_property_count_strings(np, "marvell,pins");
	if (num_groups <= 0) {
		dev_err(pctl->dev,
			"missing marvell,pins in node %s\n", np->name);
		return -EINVAL;
	}

	func->fid = idx;
	func->name = np->name;
	func->setting = setting;
	func->num_groups = num_groups;
	func->groups = devm_kzalloc(pctl->dev, num_groups * sizeof(char *),
				    GFP_KERNEL);
	if (!func->groups) {
		dev_err(pctl->dev,
			"unable to alloc function groups\n");
		return -ENOMEM;
	}

	of_property_for_each_string(np, "marvell,pins", prop, group) {
		struct mvebu_pinctrl_group *grp =
			mvebu_pinctrl_find_group_by_name(pctl, group);

		if (!grp) {
			dev_err(pctl->dev, "unknown pin %s", group);
			return -EINVAL;
		}

		if (!mvebu_pinctrl_find_setting_by_name(pctl, grp, setting)) {
			dev_err(pctl->dev, "unsupported function %s on pin %s",
				setting, group);
			return -EINVAL;
		}

		func->groups[n++] = group;
	}
	return 0;
}

static int __devinit mvebu_pinctrl_dt_parse(struct platform_device *pdev,
					struct mvebu_pinctrl *pctl)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *np;
	unsigned nfuncs = 0, n = 0;
	int ret;

	if (!node)
		return -ENODEV;

	nfuncs = of_get_child_count(node);
	if (nfuncs <= 0) {
		dev_warn(pctl->dev, "no function defined in device node\n");
		return 0;
	}

	pctl->num_functions = nfuncs;
	pctl->functions = devm_kzalloc(&pdev->dev, nfuncs *
				sizeof(struct mvebu_pinctrl_function),
				GFP_KERNEL);
	if (!pctl->functions) {
		dev_err(pctl->dev, "unable to alloc functions\n");
		return -ENOMEM;
	}

	for_each_child_of_node(node, np) {
		ret = mvebu_pinctrl_dt_parse_function(pctl, np, n++);
		if (ret) {
			dev_warn(pctl->dev, "failed to parse function %s\n",
				np->name);
			return ret;
		}
	}

	return 0;
}

int __devinit mvebu_pinctrl_probe(struct platform_device *pdev)
{
	struct mvebu_pinctrl_soc_info *soc = dev_get_platdata(&pdev->dev);
	struct mvebu_pinctrl *pctl;
	struct resource *res;
	void __iomem *iomem;
	struct pinctrl_pin_desc *pdesc;
	unsigned gid, n, k;
	int ret;

	if (!soc || !soc->controls || !soc->modes) {
		dev_err(&pdev->dev, "wrong pinctrl soc info\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "unable to get resource\n");
		return -ENODEV;
	}

	iomem = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!iomem) {
		dev_err(&pdev->dev, "unable to ioremap\n");
		return -ENODEV;
	}

	pctl = devm_kzalloc(&pdev->dev, sizeof(struct mvebu_pinctrl),
			GFP_KERNEL);
	if (!pctl) {
		dev_err(&pdev->dev, "unable to alloc driver\n");
		return -ENOMEM;
	}

	pctl->desc.name = dev_name(&pdev->dev);
	pctl->desc.owner = THIS_MODULE;
	pctl->desc.pctlops = &mvebu_pinctrl_ops;
	pctl->desc.pmxops = &mvebu_pinmux_ops;
	pctl->desc.confops = &mvebu_pinconf_ops;
	pctl->variant = soc->variant;
	pctl->base = iomem;
	pctl->dev = &pdev->dev;
	platform_set_drvdata(pdev, pctl);

	/* count controls and create names for mvebu generic
	   register controls; also does sanity checks */
	pctl->num_groups = 0;
	pctl->desc.npins = 0;
	for (n = 0; n < soc->ncontrols; n++) {
		struct mvebu_mpp_ctrl *ctrl = &soc->controls[n];
		char *names;

		pctl->desc.npins += ctrl->npins;
		/* initial control pins */
		for (k = 0; k < ctrl->npins; k++)
			ctrl->pins[k] = ctrl->pid + k;

		/* special soc specific control */
		if (ctrl->mpp_get || ctrl->mpp_set) {
			if (!ctrl->name || !ctrl->mpp_set || !ctrl->mpp_set) {
				dev_err(&pdev->dev, "wrong soc control info\n");
				return -EINVAL;
			}
			pctl->num_groups += 1;
			continue;
		}

		/* generic mvebu register control */
		names = devm_kzalloc(&pdev->dev, ctrl->npins * 8, GFP_KERNEL);
		if (!names) {
			dev_err(&pdev->dev, "failed to alloc mpp names\n");
			return -ENOMEM;
		}
		for (k = 0; k < ctrl->npins; k++)
			sprintf(names + 8*k, "mpp%d", ctrl->pid+k);
		ctrl->name = names;
		pctl->num_groups += ctrl->npins;
	}

	pdesc = devm_kzalloc(&pdev->dev, pctl->desc.npins *
			     sizeof(struct pinctrl_pin_desc), GFP_KERNEL);
	if (!pdesc) {
		dev_err(&pdev->dev, "failed to alloc pinctrl pins\n");
		return -ENOMEM;
	}

	for (n = 0; n < pctl->desc.npins; n++)
		pdesc[n].number = n;
	pctl->desc.pins = pdesc;

	pctl->groups = devm_kzalloc(&pdev->dev, pctl->num_groups *
			     sizeof(struct mvebu_pinctrl_group), GFP_KERNEL);
	if (!pctl->groups) {
		dev_err(&pdev->dev, "failed to alloc pinctrl groups\n");
		return -ENOMEM;
	}

	/* assign mpp controls to groups */
	gid = 0;
	for (n = 0; n < soc->ncontrols; n++) {
		struct mvebu_mpp_ctrl *ctrl = &soc->controls[n];
		pctl->groups[gid].gid = gid;
		pctl->groups[gid].ctrl = ctrl;
		pctl->groups[gid].name = ctrl->name;
		pctl->groups[gid].pins = ctrl->pins;
		pctl->groups[gid].npins = ctrl->npins;

		/* generic mvebu register control maps to a number of groups */
		if (!ctrl->mpp_get && !ctrl->mpp_set) {
			pctl->groups[gid].npins = 1;

			for (k = 1; k < ctrl->npins; k++) {
				gid++;
				pctl->groups[gid].gid = gid;
				pctl->groups[gid].ctrl = ctrl;
				pctl->groups[gid].name = &ctrl->name[8*k];
				pctl->groups[gid].pins = &ctrl->pins[k];
				pctl->groups[gid].npins = 1;
			}
		}
		gid++;
	}

	/* assign mpp modes to groups */
	for (n = 0; n < soc->nmodes; n++) {
		struct mvebu_mpp_mode *mode = &soc->modes[n];
		struct mvebu_pinctrl_group *grp =
			mvebu_pinctrl_find_group_by_pid(pctl, mode->pid);
		unsigned num_settings;

		if (!grp) {
			dev_warn(&pdev->dev, "unknown pinctrl group %d\n",
				mode->pid);
			continue;
		}

		for (num_settings = 0; ;) {
			struct mvebu_mpp_ctrl_setting *set =
				&mode->settings[num_settings];

			if (!set->name)
				break;
			num_settings++;

			/* skip unsupported settings for this variant */
			if (pctl->variant && !(pctl->variant & set->variant))
				continue;

			/* find gpio/gpo/gpi settings */
			if (strcmp(set->name, "gpio") == 0)
				set->flags = MVEBU_SETTING_GPI |
					MVEBU_SETTING_GPO;
			else if (strcmp(set->name, "gpo") == 0)
				set->flags = MVEBU_SETTING_GPO;
			else if (strcmp(set->name, "gpi") == 0)
				set->flags = MVEBU_SETTING_GPI;
		}

		grp->settings = mode->settings;
		grp->num_settings = num_settings;
	}

	ret = mvebu_pinctrl_dt_parse(pdev, pctl);
	if (ret) {
		/* look for pinmux functions in platform_device data */
		dev_err(&pdev->dev, "unable to parse device tree\n");
		return ret;
	}

	pctl->pctldev = pinctrl_register(&pctl->desc, &pdev->dev, pctl);
	if (!pctl->pctldev) {
		dev_err(&pdev->dev, "unable to register pinctrl driver\n");
		return -EINVAL;
	}

	dev_info(&pdev->dev, "registered pinctrl driver\n");

	/* register gpio ranges */
	for (n = 0; n < soc->ngpioranges; n++)
		pinctrl_add_gpio_range(pctl->pctldev, &soc->gpioranges[n]);

	return 0;
}

int __devexit mvebu_pinctrl_remove(struct platform_device *pdev)
{
	struct mvebu_pinctrl *pctl = platform_get_drvdata(pdev);
	pinctrl_unregister(pctl->pctldev);
	return 0;
}
