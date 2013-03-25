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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include "dove_drv.h"
#include "dove_lcd.h"
#include "dove_lcd_regs.h"

const struct dove_lcd_caps dove_lcd_capabilities[2] = {
	[0] = {
		.ectypes = ECMASK_RGB_DUMB | ECMASK_SMARTPANEL |
			ECMASK_I2C_SLAVE,
		.outputs = DOVE_LCD0,
		.clones  = 0,
	},
	[1] = {
		.ectypes = ECMASK_VGADAC,
		.outputs = DOVE_LCD0 | DOVE_LCD1,
		.clones  = DOVE_LCD0,
	},
};

#define crtc_to_dove_lcd(x)	container_of(x, struct dove_drm_lcd, drm_crtc)

inline u32 dove_lcd_read(struct dove_drm_lcd *lcd, u32 reg)
{
	return readl(lcd->mmio + reg);
}

inline void dove_lcd_write(struct dove_drm_lcd *lcd, u32 reg, u32 val)
{
	writel(val, lcd->mmio + reg);
}

u32 dove_lcd_crtc_vblank_count(struct dove_drm_lcd *lcd)
{
	return STA_GRA_FRAME_COUNT(dove_lcd_read(lcd, SPU_IRQ_ISR));
}

int dove_lcd_crtc_enable_vblank(struct dove_drm_lcd *lcd, bool enable)
{
	u32 val;

	DOVE_DBG("lcd = %p, crtc = %d, enable = %d\n",
		 lcd, lcd->crtc, enable);

	val = dove_lcd_read(lcd, SPU_IRQ_ENA);
	if (enable)
		val |= IRQ_GRA_FRAME_DONE;
	else
		val &= ~IRQ_GRA_FRAME_DONE;
	dove_lcd_write(lcd, SPU_IRQ_ENA, val);

	return 0;
}

static void dove_lcd_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct dove_drm_lcd *lcd = crtc_to_dove_lcd(crtc);
	u32 val;

	if (lcd->dpms == mode)
		return;
	lcd->dpms = mode;

	val = dove_lcd_read(lcd, LCD_SPU_DMA_CTRL0);
	if (lcd->dpms == DRM_MODE_DPMS_ON)
		val |= GRA_ENABLE;
	else
		val &= ~GRA_ENABLE;
	dove_lcd_write(lcd, LCD_SPU_DMA_CTRL0, val);
}

static bool dove_lcd_crtc_mode_fixup(struct drm_crtc *crtc,
				     const struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	/* width must be multiple of 16 */
	if (mode->hdisplay & 0xf) {
		adjusted_mode->hdisplay = mode->hdisplay & ~0xf;
		return false;
	}

	return true;
}

static void dove_lcd_crtc_prepare(struct drm_crtc *crtc)
{
	struct dove_drm_lcd *lcd = crtc_to_dove_lcd(crtc);

	DOVE_DBG("crtc = %p, lcd = %p\n", crtc, lcd);
	dove_lcd_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
//	dove_lcd_crtc_enable_vblank(lcd, false); 
}

static int dove_lcd_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
				       struct drm_framebuffer *old_fb)
{
	struct dove_drm_lcd *lcd = crtc_to_dove_lcd(crtc);

	DOVE_DBG("crtc = %p, lcd = %p, x = %d, y = %d, old_fb = %p\n",
		 crtc, lcd, x, y, old_fb);

	return 0;
}

static int dove_lcd_crtc_mode_set(struct drm_crtc *crtc,
	  struct drm_display_mode *mode, struct drm_display_mode *adjusted_mode,
	  int x, int y, struct drm_framebuffer *old_fb)
{
	struct dove_drm_lcd *lcd = crtc_to_dove_lcd(crtc);
	struct drm_gem_cma_object *gem;
	u32 depth, bpp, rate, refclk, div;
	u32 gra_start, gra_pitch;
	u32 reg;

	/* GRAPHICS */

	drm_fb_get_bpp_depth(crtc->fb->pixel_format, &depth, &bpp);
	gem = drm_fb_cma_get_gem_obj(crtc->fb, 0);

	gra_start = gem->paddr + crtc->fb->offsets[0] +
		(crtc->y * crtc->fb->pitches[0]) +
		(crtc->x * bpp / 8);
	gra_pitch = crtc->fb->width * bpp / 8;

	dove_lcd_write(lcd, LCD_CFG_GRA_START_ADDR0, gra_start);
	dove_lcd_write(lcd, LCD_CFG_GRA_START_ADDR1, gra_start);

	reg = dove_lcd_read(lcd, LCD_CFG_GRA_PITCH);
	reg = SET_GRA_PITCH(reg, gra_pitch);
	dove_lcd_write(lcd, LCD_CFG_GRA_PITCH, reg);

	reg = dove_lcd_read(lcd, LCD_SPU_DMA_CTRL0);
	reg &= ~(DMA_PALETTE | GRA_SWAP_R_B | DMA_FRAME_TOGGLE);
	reg |= GRA_UV_HSMOOTH;

	switch (crtc->fb->pixel_format) {
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV420:
		reg |= GRA_SWAP_R_B;
	}

	switch (crtc->fb->pixel_format) {
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		reg = SET_GRA_FORMAT(reg, GRA_FORMAT_RGB888_24);
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
		reg = SET_GRA_FORMAT(reg, GRA_FORMAT_RGB888_32);
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		reg = SET_GRA_FORMAT(reg, GRA_FORMAT_ARGB8888);
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
		reg = SET_GRA_FORMAT(reg, GRA_FORMAT_YUYV422);
		break;
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
		reg = SET_GRA_FORMAT(reg, GRA_FORMAT_YUV422_PLANAR);
		break;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		reg = SET_GRA_FORMAT(reg, GRA_FORMAT_YUV420_PLANAR);
		break;
	}
	dove_lcd_write(lcd, LCD_SPU_DMA_CTRL0, reg);

	reg = dove_lcd_read(lcd, LCD_SPU_DMA_CTRL1);
	reg = SET_DMA_VSYNC_SRC(reg, DMA_VSYNC_SRC_DUMB);
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		reg &= ~DMA_VSYNC_FALLING;
	else
		reg |= DMA_VSYNC_FALLING;
	dove_lcd_write(lcd, LCD_SPU_DMA_CTRL1, reg);

	/* CLOCK */

	rate = 1000UL * mode->clock;
	refclk = clk_get_rate(lcd->clk[3]);

	DOVE_DBG("pixclk = %u, refclk = %u\n", rate, refclk);

	clk_set_rate(lcd->clk[3], rate);
	refclk = clk_get_rate(lcd->clk[3]);

	div = (refclk + (rate/2)) / rate;
	if (div < 1)
		div = 1;

	DOVE_DBG("pixclk = %u, refclk = %u, div = %d\n", rate, refclk, div);

	reg = dove_lcd_read(lcd, LCD_CFG_SCLK_DIV);
	reg = SET_SCLK_SRC(reg, SCLK_SRC_EXTCLK1);
	reg = SET_SCLK_DIV(reg, div, 0);
	dove_lcd_write(lcd, LCD_CFG_SCLK_DIV, reg);

	return 0;
}

static void dove_lcd_crtc_commit(struct drm_crtc *crtc)
{
	struct dove_drm_lcd *lcd = crtc_to_dove_lcd(crtc);
	DOVE_DBG("crtc = %p, lcd = %p\n", crtc, lcd);
//	dove_lcd_crtc_enable_vblank(lcd, true); 
	dove_lcd_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static void dove_lcd_crtc_load_lut(struct drm_crtc *crtc)
{
	struct dove_drm_lcd *lcd = crtc_to_dove_lcd(crtc);

	DOVE_DBG("crtc = %p, lcd = %p\n", crtc, lcd);
}

static int dove_lcd_crtc_page_flip(struct drm_crtc *crtc,
				   struct drm_framebuffer *fb,
				   struct drm_pending_vblank_event *event)
{
	struct drm_device *ddev = crtc->dev;
	struct dove_drm_private *priv = ddev->dev_private;
	struct dove_drm_lcd *lcd = crtc_to_dove_lcd(crtc);
	struct drm_framebuffer *old_fb = crtc->fb;
	int ret = 0;

	DOVE_DBG("crtc = %p, lcd = %p, fb = %p, event = %p\n",
		 crtc, lcd, fb, event);

	/* when the page flip is requested, crtc's dpms should be on */
	if (lcd->dpms > DRM_MODE_DPMS_ON) {
		DRM_ERROR("failed page flip request.\n");
		return -EINVAL;
	}

	mutex_lock(&ddev->struct_mutex);

	if (event) {
		ret = drm_vblank_get(ddev, lcd->crtc);
		if (ret) {
			DRM_DEBUG("failed to acquire vblank counter\n");
			list_del(&event->base.link);
			goto out;
		}

		spin_lock_irq(&ddev->event_lock);
		list_add_tail(&event->base.link, &priv->pageflip_event_list);
		spin_unlock_irq(&ddev->event_lock);

		crtc->fb = fb;
		ret = dove_lcd_crtc_mode_set_base(crtc, crtc->x, crtc->y, NULL);
		if (ret) {
			crtc->fb = old_fb;

			spin_lock_irq(&ddev->event_lock);
			drm_vblank_put(ddev, lcd->crtc);
			list_del(&event->base.link);
			spin_unlock_irq(&ddev->event_lock);
			goto out;
		}
	}
out:
	mutex_unlock(&ddev->struct_mutex);
	return ret;
}

static void dove_lcd_crtc_destroy(struct drm_crtc *crtc)
{
	struct dove_drm_lcd *lcd = crtc_to_dove_lcd(crtc);

	DOVE_DBG("crtc = %p, lcd = %p\n", crtc, lcd);
}

static const struct drm_crtc_helper_funcs dove_lcd_crtc_helper_fn = {
	.commit		= dove_lcd_crtc_commit,
	.dpms		= dove_lcd_crtc_dpms,
	.load_lut	= dove_lcd_crtc_load_lut,
	.mode_fixup	= dove_lcd_crtc_mode_fixup,
	.mode_set	= dove_lcd_crtc_mode_set,
	.mode_set_base	= dove_lcd_crtc_mode_set_base,
	.prepare	= dove_lcd_crtc_prepare,
};

static const struct drm_crtc_funcs dove_lcd_crtc_fn = {
	.destroy	= dove_lcd_crtc_destroy,
	.page_flip	= dove_lcd_crtc_page_flip,
	.set_config	= drm_crtc_helper_set_config,
};

void dove_lcd_crtc_finish_pageflip(struct drm_device *ddev, int crtc)
{
	struct dove_drm_private *priv = ddev->dev_private;
	struct drm_pending_vblank_event *e, *t;
	struct timeval now;
	unsigned long flags;

//	DOVE_DBG("ddev = %p, crtc = %d\n", ddev, crtc);

	spin_lock_irqsave(&ddev->event_lock, flags);

	list_for_each_entry_safe(e, t, &priv->pageflip_event_list, base.link) {
		do_gettimeofday(&now);
		e->event.sequence = 0;
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;

		list_move_tail(&e->base.link, &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
		drm_vblank_put(ddev, crtc);
	}

	spin_unlock_irqrestore(&ddev->event_lock, flags);
}

static irqreturn_t dove_lcd_irq_handler(int irq, void *dev_id)
{
	struct dove_drm_lcd *lcd = (struct dove_drm_lcd *)dev_id;
	struct drm_device *ddev = lcd->drm_dev;
	u32 val;

	val = dove_lcd_read(lcd, SPU_IRQ_ISR);
	dove_lcd_write(lcd, SPU_IRQ_ISR, 0);

//	DOVE_DBG("ddev = %p, lcd = %p, isr = %08x\n", ddev, lcd, val);

	if (val & IRQ_GRA_FRAME_DONE) {
		drm_handle_vblank(ddev, lcd->crtc);
		dove_lcd_crtc_finish_pageflip(ddev, lcd->crtc);

		/* set wait vsync event to zero and wake up queue. */
		if (atomic_read(&lcd->wait_vsync_event)) {
			atomic_set(&lcd->wait_vsync_event, 0);
			DRM_WAKEUP(&lcd->wait_vsync_queue);
		}
	}

	return IRQ_HANDLED;
}

int dove_lcd_create(struct drm_device *ddev, struct dove_drm_lcd *lcd, int crtc)
{
	int ret;

	ret = drm_crtc_init(ddev, &lcd->drm_crtc, &dove_lcd_crtc_fn);
	if (ret) {
		DRM_ERROR("unable to init crtc for lcd%d\n", lcd->num);
		return ret;
	}
	drm_crtc_helper_add(&lcd->drm_crtc, &dove_lcd_crtc_helper_fn);

	lcd->crtc = lcd->drm_crtc.base.id;
	lcd->drm_dev = ddev;
	lcd->dpms = DRM_MODE_DPMS_OFF;
	DRM_INIT_WAITQUEUE(&lcd->wait_vsync_queue);
	atomic_set(&lcd->wait_vsync_event, 0);

	ret = dove_ec_create(ddev, lcd);
	if (ret) {
		DRM_ERROR("lcd%d unable to create encoder/connector (%d)\n",
			  lcd->num, ret);
		return ret;
	}

	return 0;
}

static bool ec_type_is_valid(enum dove_ec_type type)
{
	switch (type) {
	case DISABLED:
	case RGB_DUMB:
	case I2C_SLAVE:
	case SMARTPANEL:
	case VGADAC:
		return true;
	default:
		return false;
	}
}

static int dove_lcd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct dove_drm_lcd *lcd;
	enum dove_ec_type type = DISABLED;
	int n, ret;
	u32 val;

	if (!dev->of_node)
		return -EINVAL;

	if (of_property_read_u32(dev->of_node, "marvell,lcd-config", &type)) {
		dev_err(dev, "missing lcd-config property\n");
		return -EINVAL;
	}

	if (!ec_type_is_valid(type)) {
		dev_err(dev, "invalid lcd type %d\n", type);
		return -EINVAL;
	}

	/* bail out if lcd-type is DISABLED */
	if (type == DISABLED) {
		dev_dbg(dev, "ignoring disabled lcd\n");
		return 0;
	}

	lcd = devm_kzalloc(dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd) {
		dev_err(dev, "unable to allocate lcd data\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, lcd);
	lcd->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "unable to get lcd iomem\n");
		return -ENOENT;
	}

	lcd->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(lcd->mmio)) {
		dev_err(dev, "unable to map iomem\n");
		return PTR_ERR(lcd->mmio);
	}

	lcd->irq = irq_of_parse_and_map(dev->of_node, 0);
	if (lcd->irq < 0 || lcd->irq == NO_IRQ) {
		dev_err(dev, "unable to get lcd irq\n");
		return -ENOENT;
	}

	for (n = 0; n < MAX_LCD_CLK; n++) {
		lcd->clk[n] = of_clk_get(dev->of_node, n);
		if (!IS_ERR(lcd->clk[n]))
			clk_prepare_enable(lcd->clk[n]);
	}

	val = ((u32)lcd->mmio) & DOVE_LCD_REG_BASE_MASK;
	switch (val) {
	case DOVE_LCD0_REG_BASE:
		lcd->num = 0;
		break;
	case DOVE_LCD1_REG_BASE:
		lcd->num = 1;
		break;
	default:
		dev_err(dev, "unknown lcd reg base %08x\n", val);
		return -EINVAL;
	}

	lcd->caps = &dove_lcd_capabilities[lcd->num];
	lcd->ec.type = type;

	if (!(lcd->caps->ectypes & BIT(type))) {
		dev_err(dev, "unsupported lcd type %d on lcd%d\n",
			type, lcd->num);
		return -EINVAL;
	}

	snprintf(lcd->name, sizeof(lcd->name), "dove-lcd%d", lcd->num);

	dove_lcd_write(lcd, SPU_IRQ_ENA, 0);
	ret = devm_request_irq(dev, lcd->irq, dove_lcd_irq_handler, 0,
			       lcd->name, lcd);
	if (ret) {
		dev_err(dev, "unable to request irq %d\n", lcd->irq);
		return ret;
	}

	dev->coherent_dma_mask = DMA_BIT_MASK(32);

	/* set defaults */
	val = SET_SCLK_SRC(0, SCLK_SRC_PLLDIV);
	val = SET_SCLK_DIV(val, 1, 0);
	dove_lcd_write(lcd, LCD_CFG_SCLK_DIV, val);

	/*
	 * NOTE: There seems to be an issue with DE to active offset.
	 *       Blanking starts one pixel to early. Set to non-black to see it.
	 *
	 * dove_lcd_write(lcd, LCD_SPU_BLANKCOLOR, BLANKCOLOR(0x00, 0x40, 0xf0));
	 */
	dove_lcd_write(lcd, LCD_SPU_BLANKCOLOR, BLANKCOLOR(0x00, 0x00, 0x00));

	dove_lcd_write(lcd, SPU_IOPAD_CONTROL, AXI_BURST_BOUNDARY_4K |
		       AXI_BURST_SIZE_128B | IOPAD_MODE_DUMB_24);
	dove_lcd_write(lcd, LCD_CFG_GRA_START_ADDR1, 0);
	dove_lcd_write(lcd, LCD_SPU_GRA_OVSA_HPXL_VLN, LCD_H_V(0, 0));

	dove_lcd_write(lcd, LCD_SPU_SRAM_PARA0, 0);
	dove_lcd_write(lcd, LCD_SPU_SRAM_PARA1, ALWAYS_ON_SRAM_HWCURSOR |
		       ALWAYS_ON_SRAM_PALETTE | ALWAYS_ON_SRAM_GAMMA);
	val = GATED_CLOCK_ENABLE | POWERDOWN_ENABLE | ALPHA_PATH_CONFIGURABLE;
	val = SET_ALPHA_VALUE(val, 0xff);
	val = SET_PIXEL_CMD(val, 0x81);
	dove_lcd_write(lcd, LCD_SPU_DMA_CTRL1, val);

	/* lower watermark increases axi bus priority */
	val = dove_lcd_read(lcd, LCD_CFG_RDREG4F);
	val &= ~LCD_SRAM_WAIT;
	val |= DMA_WATERMARK_ENABLE;
	val = SET_DMA_WATERMARK(val, 0x20);
	dove_lcd_write(lcd, LCD_CFG_RDREG4F, val);

	dove_drm_attach_lcd(lcd);

	return 0;
}

static int dove_lcd_remove(struct platform_device *pdev)
{
	struct dove_drm_lcd *lcd = platform_get_drvdata(pdev);
	int n;

	dove_drm_detach_lcd(lcd);

	for (n = 0; n < MAX_LCD_CLK; n++) {
		if (!IS_ERR(lcd->clk[n])) {
			clk_disable_unprepare(lcd->clk[n]);
			clk_put(lcd->clk[n]);
		}
	}

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id dove_lcd_match[] = {
	{ .compatible = "marvell,dove-lcd", },
	{ }
};
#endif

struct platform_driver dove_lcd_platform_driver = {
	.probe = dove_lcd_probe,
	.remove = dove_lcd_remove,
	.driver = {
		.name = "dove-lcd",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dove_lcd_match),
	},
};
