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

#ifndef __DOVE_DRM_LCD_H__
#define __DOVE_DRM_LCD_H__

#include <drm/drm_encoder_slave.h>
#include <linux/bitops.h>

struct clk;
struct device;
struct drm_device;
struct drm_crtc;
struct drm_encoder;
struct drm_connector;
struct list_head;

struct dove_drm_private;
struct dove_drm_lcd;

/*
 * Encoder/Connector
 */

enum dove_ec_type {
	DISABLED = 0,
	RGB_DUMB = 1,
	I2C_SLAVE = 2,
	SMARTPANEL = 3,
	VGADAC = 4,
};

enum dove_ec_dumb_rgb_mode {
	RGB16_LO = 0,
	RGB16_HI = 1,
	RGB18_LO = 2,
	RGB18_HI = 3,
	RGB12_LO = 4,
	RGB12_HI = 5,
	RGB24 = 6,
};

#define ECMASK_RGB_DUMB		BIT(RGB_DUMB)
#define ECMASK_I2C_SLAVE	BIT(I2C_SLAVE)
#define ECMASK_SMARTPANEL	BIT(SMARTPANEL)
#define ECMASK_VGADAC		BIT(VGADAC)

struct dove_drm_ec {
	enum dove_ec_type type;
	unsigned int conn_mode;
};

/*
 * LCD Crtc
 */

#define DOVE_LCD_REG_BASE_MASK	0xfffff
#define DOVE_LCD0_REG_BASE	0x20000
#define DOVE_LCD1_REG_BASE	0x10000

#define DOVE_LCD0	BIT(0)
#define DOVE_LCD1	BIT(1)

#define MAX_LCD_CLK	4

struct dove_lcd_caps {
	unsigned int ectypes;
	unsigned int outputs;
	unsigned int clones;
};

struct dove_drm_lcd {
	struct list_head list;

	struct device *dev;
	struct drm_device *drm_dev;
	struct drm_crtc drm_crtc;

	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_encoder_slave slave;

	struct dove_drm_ec ec;
	int lcd_mode;
	const struct dove_lcd_caps *caps;
	struct display_timings *timings;

	char name[16];
	void __iomem *mmio;
	struct clk *clk[MAX_LCD_CLK];
	int irq;
	unsigned int num;
	unsigned int crtc;
	unsigned int dpms;

	wait_queue_head_t wait_vsync_queue;
	atomic_t wait_vsync_event;
};

inline u32 dove_lcd_read(struct dove_drm_lcd *lcd, u32 reg);
inline void dove_lcd_write(struct dove_drm_lcd *lcd, u32 reg, u32 val);

int dove_lcd_create(struct drm_device *ddev,
		    struct dove_drm_lcd *lcd, int crtc);
u32 dove_lcd_crtc_vblank_count(struct dove_drm_lcd *lcd);
int dove_lcd_crtc_enable_vblank(struct dove_drm_lcd *lcd, bool enable);

int dove_ec_create(struct drm_device *ddev, struct dove_drm_lcd *lcd);
int dove_ec_type_is_valid(enum dove_ec_type type);

#endif
