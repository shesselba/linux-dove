/*
 * Marvell Dove DRM driver
 *
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

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>

#include <linux/of_i2c.h>

#include <video/of_display_timing.h>

#include "dove_drv.h"
#include "dove_lcd.h"
#include "dove_lcd_regs.h"

/*
 * Encoder/Connector: helper
 */

#define lcd_from_encoder(x)	container_of(x, struct dove_drm_lcd, encoder)
#define lcd_from_connector(x)	container_of(x, struct dove_drm_lcd, connector)
#define lcd_from_slave(x)	container_of(x, struct dove_drm_lcd, slave)

static bool dumb_rgb_mode_is_valid(enum dove_ec_dumb_rgb_mode mode)
{
	switch (mode) {
	case RGB16_LO:
	case RGB16_HI:
	case RGB18_LO:
	case RGB18_HI:
	case RGB12_LO:
	case RGB12_HI:
	case RGB24:
		return true;
	default:
		return false;
	}
}

/*
 * Encoder/Connector: "Dumb" RGB output
 */

static int dove_ec_rgb_conn_get_modes(struct drm_connector *connector)
{
	struct dove_drm_lcd *lcd = lcd_from_connector(connector);

	DOVE_DBG("connector = %p, lcd = %p\n", connector, lcd);

	return 0;
}

static int dove_ec_rgb_conn_mode_valid(struct drm_connector *connector,
				       struct drm_display_mode *mode)
{
	struct dove_drm_lcd *lcd = lcd_from_connector(connector);

	DOVE_DBG("connector = %p, lcd = %p\n", connector, lcd);

	if (mode->clock > 148500)
		return MODE_CLOCK_HIGH;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	return MODE_OK;
}

static struct drm_encoder *
dove_ec_rgb_conn_best_encoder(struct drm_connector *connector)
{
	struct dove_drm_lcd *lcd = lcd_from_connector(connector);
	return &lcd->encoder;
}

static const struct drm_connector_helper_funcs dove_ec_rgb_conn_helper_fn = {
	.get_modes = dove_ec_rgb_conn_get_modes,
	.mode_valid = dove_ec_rgb_conn_mode_valid,
	.best_encoder = dove_ec_rgb_conn_best_encoder,
};

static void dove_ec_rgb_conn_destroy(struct drm_connector *connector)
{
	DOVE_DBG("connector = %p\n", connector);

	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
dove_ec_rgb_conn_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void dove_ec_rgb_conn_dpms(struct drm_connector *connector, int mode)
{
	struct dove_drm_lcd *lcd = lcd_from_connector(connector);

	DOVE_DBG("connector = %p, lcd = %p, mode = %d\n",
		 connector, lcd, mode);
}

static int dove_ec_rgb_conn_fill_modes(struct drm_connector *connector,
				       uint32_t max_width, uint32_t max_height)
{
	struct dove_drm_lcd *lcd = lcd_from_connector(connector);

	DOVE_DBG("connector = %p, lcd = %p, max_w = %d, max_h = %d\n",
		 connector, lcd, max_width, max_height);

	return drm_helper_probe_single_connector_modes(connector,
						       max_width, max_height);
}

static const struct drm_connector_funcs dove_ec_rgb_conn_fn = {
	.destroy = dove_ec_rgb_conn_destroy,
	.detect = dove_ec_rgb_conn_detect,
	.dpms = dove_ec_rgb_conn_dpms,
	.fill_modes = dove_ec_rgb_conn_fill_modes,
};

static void dove_ec_rgb_enc_dpms(struct drm_encoder *encoder, int mode)
{
	struct dove_drm_lcd *lcd = lcd_from_encoder(encoder);
	u32 reg;

	reg = dove_lcd_read(lcd, LCD_SPU_DUMB_CTRL);
	if (mode == DRM_MODE_DPMS_ON)
		reg |= DUMB_ENABLE;
	else
		reg &= ~DUMB_ENABLE;
	dove_lcd_write(lcd, LCD_SPU_DUMB_CTRL, reg);
}

static bool dove_ec_rgb_enc_mode_fixup(struct drm_encoder *encoder,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	struct dove_drm_lcd *lcd = lcd_from_encoder(encoder);

	DOVE_DBG("encoder = %p, lcd = %p, mode = %p, adj_mode = %p\n",
		 encoder, lcd, mode, adjusted_mode);

	return true;
}

static void dove_ec_rgb_enc_prepare(struct drm_encoder *encoder)
{
	struct dove_drm_lcd *lcd = lcd_from_encoder(encoder);

	DOVE_DBG("encoder = %p, lcd = %p\n", encoder, lcd);
}

static void dove_ec_rgb_enc_commit(struct drm_encoder *encoder)
{
	struct dove_drm_lcd *lcd = lcd_from_encoder(encoder);

	DOVE_DBG("encoder = %p, lcd = %p\n", encoder, lcd);
}

static void dove_ec_rgb_enc_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct dove_drm_lcd *lcd = lcd_from_encoder(encoder);
	u32 hactive, hfp, hbp, htotal;
	u32 vactive, vfp, vbp, vtotal;
	u32 reg;

	/* VIDEO */

	htotal = mode->htotal;
	hactive = mode->hdisplay;
	hfp = mode->hsync_start - hactive;
	hbp = htotal - mode->hsync_end;
	vtotal = mode->vtotal;
	vactive = mode->vdisplay;
	vfp = mode->vsync_start - vactive;
	vbp = vtotal - mode->vsync_end;

	dove_lcd_write(lcd, LCD_SPUT_V_H_TOTAL, LCD_H_V(htotal, vtotal));
	dove_lcd_write(lcd, LCD_SPUT_V_H_ACTIVE, LCD_H_V(hactive, vactive));
	dove_lcd_write(lcd, LCD_SPU_GRA_HPXL_VLN, LCD_H_V(hactive, vactive));
	dove_lcd_write(lcd, LCD_SPU_GZM_HPXL_VLN, LCD_H_V(hactive, vactive));
	dove_lcd_write(lcd, LCD_SPU_H_PORCH, LCD_F_B(hfp, hbp));
	dove_lcd_write(lcd, LCD_SPU_V_PORCH, LCD_F_B(vfp, vbp));

	reg = dove_lcd_read(lcd, LCD_TV_CONTROL1);
	reg |= VSYNC_OFFSET_EN;
	reg = SET_VSYNC_L_OFFSET(reg, hactive + hfp);
	reg = SET_VSYNC_H_OFFSET(reg, hactive + hfp);
	dove_lcd_write(lcd, LCD_TV_CONTROL1, reg);

	reg = dove_lcd_read(lcd, LCD_SPU_DUMB_CTRL);
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		reg &= ~DUMB_INVERT_VSYNC;
	else
		reg |= DUMB_INVERT_VSYNC;

	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		reg &= ~DUMB_INVERT_HSYNC;
	else
		reg |= DUMB_INVERT_HSYNC;
	dove_lcd_write(lcd, LCD_SPU_DUMB_CTRL, reg);
}

static const struct drm_encoder_helper_funcs dove_ec_rgb_enc_helper_fn = {
	.dpms = dove_ec_rgb_enc_dpms,
	.mode_fixup = dove_ec_rgb_enc_mode_fixup,
	.prepare = dove_ec_rgb_enc_prepare,
	.commit = dove_ec_rgb_enc_commit,
	.mode_set = dove_ec_rgb_enc_mode_set,
};

static void dove_ec_rgb_enc_destroy(struct drm_encoder *encoder)
{
	struct dove_drm_lcd *lcd = lcd_from_encoder(encoder);
	DOVE_DBG("encoder = %p, lcd = %p\n", encoder, lcd);
}

static const struct drm_encoder_funcs dove_ec_rgb_enc_fn = {
	.destroy = dove_ec_rgb_enc_destroy,
};

int dove_ec_rgb_of_parse(struct drm_device *ddev, struct dove_drm_lcd *lcd)
{
	enum dove_ec_dumb_rgb_mode mode = -1;
	u32 val[2];
	int ret;

	/* required: output-config */
	ret = of_property_read_u32_array(lcd->dev->of_node,
					 "marvell,lcd-config", val, 2);
	if (ret) {
		dev_err(lcd->dev, "error parsing lcd-config\n");
		return ret;
	}

	mode = val[1];
	if (!dumb_rgb_mode_is_valid(mode)) {
		dev_err(lcd->dev, "invalid lcd mode %d\n", mode);
		return -EINVAL;
	}
        lcd->lcd_mode = mode;

#if 0
	lcd->timings = of_get_display_timings(lcd->dev->of_node);
	if (!ec->timings)
		DRM_DEBUG_DRIVER("unable to parse DT display timings");
#endif

	return 0;
}

static void dove_ec_rgb_setup(struct dove_drm_lcd *lcd)
{
	u32 reg;

/* LCD_SPU_DMA_CTRL1 : DMA_VSYNC_SRC_DUMB */

	reg = dove_lcd_read(lcd, LCD_SPU_DUMB_CTRL);
	reg = SET_DUMB_MODE(reg, lcd->lcd_mode);
	reg |= DUMB_INVERT_PIXCLK;
	dove_lcd_write(lcd, LCD_SPU_DUMB_CTRL, reg);
}

int dove_ec_rgb_create(struct drm_device *ddev, struct dove_drm_lcd *lcd)
{
	int ret;

	ret = dove_ec_rgb_of_parse(ddev, lcd);
	if (ret)
		return ret;

	dove_ec_rgb_setup(lcd);

	lcd->encoder.possible_crtcs = (1 << lcd->num);
	ret = drm_encoder_init(ddev, &lcd->encoder,
			       &dove_ec_rgb_enc_fn,
			       DRM_MODE_ENCODER_LVDS);
	if (ret) {
		DRM_ERROR("unable to init encoder\n");
		return ret;
	}
	drm_encoder_helper_add(&lcd->encoder,
			       &dove_ec_rgb_enc_helper_fn);

	ret = drm_connector_init(ddev, &lcd->connector,
				 &dove_ec_rgb_conn_fn,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret) {
		DRM_ERROR("unable to init connector\n");
		return ret;
	}
	drm_connector_helper_add(&lcd->connector,
				 &dove_ec_rgb_conn_helper_fn);

	ret = drm_mode_connector_attach_encoder(&lcd->connector, &lcd->encoder);
	if (ret) {
		DRM_ERROR("lcd%d unable to attach connector and encoder\n",
			  lcd->num);
		return ret;
	}
	lcd->connector.encoder = &lcd->encoder;

	return 0;
}

/*
 * Encoder/Connector: Slave RGB output to external i2c video encoder
 */

static int dove_ec_i2c_conn_get_modes(struct drm_connector *connector)
{
	struct dove_drm_lcd *lcd = lcd_from_connector(connector);
	struct drm_encoder *encoder = &lcd->slave.base;
	return lcd->slave.slave_funcs->get_modes(encoder, connector);
}

static int dove_ec_i2c_conn_mode_valid(struct drm_connector *connector,
				       struct drm_display_mode *mode)
{
	struct dove_drm_lcd *lcd = lcd_from_connector(connector);
	struct drm_encoder *encoder = &lcd->slave.base;
	int ret;

	ret = dove_ec_rgb_conn_mode_valid(connector, mode);
	if (ret != MODE_OK)
		return ret;

	return lcd->slave.slave_funcs->mode_valid(encoder, mode);
}

static struct drm_encoder *
dove_ec_i2c_conn_best_encoder(struct drm_connector *connector)
{
	struct dove_drm_lcd *lcd = lcd_from_connector(connector);
	struct drm_encoder *encoder = &lcd->slave.base;
	return encoder;
}

static const struct drm_connector_helper_funcs dove_ec_i2c_conn_helper_fn = {
	.get_modes = dove_ec_i2c_conn_get_modes,
	.mode_valid = dove_ec_i2c_conn_mode_valid,
	.best_encoder = dove_ec_i2c_conn_best_encoder,
};

static void dove_ec_i2c_conn_destroy(struct drm_connector *connector)
{
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
dove_ec_i2c_conn_detect(struct drm_connector *connector, bool force)
{
	struct dove_drm_lcd *lcd = lcd_from_connector(connector);
	struct drm_encoder *encoder = &lcd->slave.base;
	return lcd->slave.slave_funcs->detect(encoder, connector);
}

static const struct drm_connector_funcs dove_ec_i2c_conn_fn = {
	.destroy = dove_ec_i2c_conn_destroy,
	.detect = dove_ec_i2c_conn_detect,
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
};

static void dove_ec_i2c_enc_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_encoder_slave *slave = to_encoder_slave(encoder);
	struct dove_drm_lcd *lcd = lcd_from_slave(slave);
	dove_ec_rgb_enc_dpms(&lcd->encoder, mode);
	drm_i2c_encoder_dpms(encoder, mode);
}

static void dove_ec_i2c_enc_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct drm_encoder_slave *slave = to_encoder_slave(encoder);
	struct dove_drm_lcd *lcd = lcd_from_slave(slave);
	dove_ec_rgb_enc_mode_set(&lcd->encoder, mode, adjusted_mode);
	drm_i2c_encoder_mode_set(encoder, mode, adjusted_mode);
}

static const struct drm_encoder_helper_funcs dove_ec_i2c_enc_helper_fn = {
	.dpms = dove_ec_i2c_enc_dpms,
	.mode_fixup = drm_i2c_encoder_mode_fixup,
	.prepare = drm_i2c_encoder_prepare,
	.commit = drm_i2c_encoder_commit,
	.mode_set = dove_ec_i2c_enc_mode_set,
};

static void dove_ec_i2c_enc_destroy(struct drm_encoder *encoder)
{
	struct drm_encoder_slave *slave = to_encoder_slave(encoder);
	struct dove_drm_lcd *lcd = lcd_from_slave(slave);
	if (lcd->slave.slave_funcs && lcd->slave.slave_funcs->destroy)
		lcd->slave.slave_funcs->destroy(encoder);
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs dove_ec_i2c_enc_fn = {
	.destroy = dove_ec_i2c_enc_destroy,
};

int dove_ec_i2c_create(struct drm_device *ddev, struct dove_drm_lcd *lcd)
{
	struct drm_i2c_encoder_driver *encdrv;
	struct i2c_client *client;
	struct device_node *np;
	int ret;

	np = of_parse_phandle(lcd->dev->of_node, "marvell,external-encoder", 0);
	if (!np) {
		DRM_ERROR("cannot find external-encoder node\n");
		return -ENODEV;
	}

	client = of_find_i2c_device_by_node(np);
	of_node_put(np);
	if (!client) {
		DRM_ERROR("cannot find i2c device\n");
		return -ENODEV;
	}

	lcd->slave.bus_priv = client;
	lcd->slave.base.possible_crtcs = (1 << lcd->num);

	ret = dove_ec_rgb_of_parse(ddev, lcd);
	if (ret)
		return ret;

	dove_ec_rgb_setup(lcd);

	ret = drm_encoder_init(ddev, &lcd->slave.base,
			       &dove_ec_i2c_enc_fn,
			       DRM_MODE_ENCODER_TMDS);
	if (ret) {
		DRM_ERROR("unable to init slave encoder\n");
		return ret;
	}
	drm_encoder_helper_add(&lcd->slave.base,
			       &dove_ec_i2c_enc_helper_fn);

	encdrv = to_drm_i2c_encoder_driver(client->driver);
	ret = encdrv->encoder_init(client, ddev, &lcd->slave);
	if (ret) {
		DRM_ERROR("cannot init slave encoder\n");
		goto err_put;
	}

	lcd->connector.polled = DRM_CONNECTOR_POLL_CONNECT |
		DRM_CONNECTOR_POLL_DISCONNECT;

	ret = drm_connector_init(ddev, &lcd->connector,
				 &dove_ec_i2c_conn_fn,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		DRM_ERROR("unable to init connector\n");
		return ret;
	}
	drm_connector_helper_add(&lcd->connector,
				 &dove_ec_i2c_conn_helper_fn);

	lcd->slave.slave_funcs->create_resources(&lcd->slave.base,
						 &lcd->connector);

	ret = drm_mode_connector_attach_encoder(&lcd->connector, &lcd->slave.base);
	if (ret) {
		DRM_ERROR("lcd%d unable to attach connector and encoder\n",
			  lcd->num);
		return ret;
	}
	lcd->connector.encoder = &lcd->slave.base;

	return 0;

err_put:
	put_device(&client->dev);
	return ret;
}

/*
 * Encoder/Connector: SPI SmartPanel
 */

int dove_ec_smartpanel_create(struct drm_device *ddev, struct dove_drm_lcd *lcd)
{
	DOVE_DBG("ddev = %p, lcd = %p\n", ddev, lcd);

	return 0;
}

/*
 * Encoder/Connector: VGA DAC
 */

int dove_ec_vgadac_create(struct drm_device *ddev, struct dove_drm_lcd *lcd)
{
	DOVE_DBG("ddev = %p, lcd = %p\n", ddev, lcd);

	/* DRM_MODE_ENCODER_DAC */
	/* DRM_MODE_CONNECTOR_VGA */

	return 0;
}

/*
 * Encoder/Connector: common
 */

int dove_ec_create(struct drm_device *ddev, struct dove_drm_lcd *lcd)
{
	int ret;

	switch (lcd->ec.type) {
	case RGB_DUMB:
		ret = dove_ec_rgb_create(ddev, lcd);
		break;
	case I2C_SLAVE:
		ret = dove_ec_i2c_create(ddev, lcd);
		break;
	case SMARTPANEL:
		ret = dove_ec_smartpanel_create(ddev, lcd);
		break;
	case VGADAC:
		ret = dove_ec_vgadac_create(ddev, lcd);
		break;
	default:
		ret = -EINVAL;
	};

	if (ret)
		return ret;

	ret = drm_sysfs_connector_add(&lcd->connector);
	if (ret) {
		DRM_ERROR("lcd%d unable to add connector sysfs\n", lcd->num);
		return ret;
	}

	drm_helper_connector_dpms(&lcd->connector, DRM_MODE_DPMS_OFF);
	ret = drm_object_property_set_value(&lcd->connector.base,
					    ddev->mode_config.dpms_property,
					    DRM_MODE_DPMS_OFF);
	if (ret) {
		DRM_ERROR("lcd%d unable to set connector dpms\n", lcd->num);
		return ret;
	}

	return 0;
}
