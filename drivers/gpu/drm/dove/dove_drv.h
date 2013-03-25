/*
 * Copyright (C) 2013
 *   Jean-Francois Moine <moinejf@free.fr>
 *   Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DOVE_DRM_DRV_H__
#define __DOVE_DRM_DRV_H__

#define DOVE_DBG(fmt, args...)	printk(">>> %s :: " fmt, __func__, ## args)

#define MAX_CRTC	2
#define MAX_EXTCLK	2

#define DOVE_CRTC_LCD0	(1 << 0)
#define DOVE_CRTC_LCD1	(1 << 1)

struct clk;
struct device;
struct drm_crtc;
struct drm_fbdev_cma;
struct list_head;

struct dove_drm_lcd;

struct dove_drm_private {
	void __iomem *mmio;
	struct clk *lcdclk;
	struct clk *extclk[MAX_EXTCLK];
	struct dove_drm_lcd *crtc_lcd[MAX_CRTC];

	struct drm_fbdev_cma *fbdev;
	struct list_head pageflip_event_list;
};

extern struct platform_driver dove_lcd_platform_driver;

int dove_drm_attach_lcd(struct dove_drm_lcd *lcd);
void dove_drm_detach_lcd(struct dove_drm_lcd *lcd);

#endif
