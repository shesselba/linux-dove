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
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include <linux/module.h>

#include "dove_drv.h"
#include "dove_lcd.h"

#define DRIVER_NAME	"dove-drm"
#define DRIVER_DESC	"Marvell Dove DRM"
#define DRIVER_DATE	"20130319"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

/*
 * DRM framebuffer
 */

static struct drm_framebuffer *dove_fb_create(struct drm_device *ddev,
					      struct drm_file *file_priv,
					      struct drm_mode_fb_cmd2 *mode_cmd)
{
	switch (mode_cmd->pixel_format) {
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV420:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	return drm_fb_cma_create(ddev, file_priv, mode_cmd);
}

static void dove_fb_output_poll_changed(struct drm_device *ddev)
{
	struct dove_drm_private *priv = ddev->dev_private;

	if (priv->fbdev)
		drm_fbdev_cma_hotplug_event(priv->fbdev);
}

static const struct drm_mode_config_funcs dove_mode_config_fn = {
	.fb_create = dove_fb_create,
	.output_poll_changed = dove_fb_output_poll_changed,
};

/*
 * DRM driver
 */

static LIST_HEAD(dove_drm_lcd_list);

int dove_drm_attach_lcd(struct dove_drm_lcd *lcd)
{
	if (!lcd)
		return -EINVAL;

	list_add_tail(&lcd->list, &dove_drm_lcd_list);

	return 0;
}

void dove_drm_detach_lcd(struct dove_drm_lcd *lcd)
{
	if (!lcd)
		return;

	list_del(&lcd->list);
}

#ifdef CONFIG_DEBUG_FS
static struct drm_info_list dove_drm_debugfs_list[] = {
	{ "fb", drm_fb_cma_debugfs_show, 0 },
};

static int dove_drm_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(dove_drm_debugfs_list,
					ARRAY_SIZE(dove_drm_debugfs_list),
					minor->debugfs_root, minor);
}

static void dove_drm_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(dove_drm_debugfs_list,
				 ARRAY_SIZE(dove_drm_debugfs_list), minor);
}
#endif

u32 dove_drm_crtc_vblank_count(struct drm_device *ddev, int nr)
{
	struct dove_drm_private *priv = ddev->dev_private;

	DOVE_DBG("ddev = %p, nr = %d\n", ddev, nr);

	return dove_lcd_crtc_vblank_count(priv->crtc_lcd[nr]);
}

int dove_drm_crtc_enable_vblank(struct drm_device *ddev, int nr)
{
	struct dove_drm_private *priv = ddev->dev_private;

	DOVE_DBG("ddev = %p, nr = %d\n", ddev, nr);

	return dove_lcd_crtc_enable_vblank(priv->crtc_lcd[nr], true);
}

void dove_drm_crtc_disable_vblank(struct drm_device *ddev, int nr)
{
	struct dove_drm_private *priv = ddev->dev_private;

	DOVE_DBG("ddev = %p, nr = %d\n", ddev, nr);

	dove_lcd_crtc_enable_vblank(priv->crtc_lcd[nr], false);
}

static int dove_drm_load(struct drm_device *ddev, unsigned long flags)
{
	struct dove_drm_private *priv = platform_get_drvdata(ddev->platformdev);
	struct dove_drm_lcd *lcd, *t;
	int ret, n;

	ddev->dev_private = priv;
	drm_mode_config_init(ddev);

	ddev->mode_config.min_width = 0;
	ddev->mode_config.max_width = 2048;
	ddev->mode_config.min_height = 0;
	ddev->mode_config.max_height = 2048;
	ddev->mode_config.funcs = &dove_mode_config_fn;

	n = 0;
	list_for_each_entry_safe(lcd, t, &dove_drm_lcd_list, list) {
		ret = dove_lcd_create(ddev, lcd, n);
		if (ret) {
			DRM_ERROR("failed to create crtc for lcd%d\n",
				  lcd->num);
			continue;
		}

		priv->crtc_lcd[n++] = lcd;
		if (n > MAX_CRTC)
			break;
	}

	if (!ddev->mode_config.num_crtc || !ddev->mode_config.num_connector)
		return -ENODEV;

	ret = drm_vblank_init(ddev, ddev->mode_config.num_crtc);
	if (ret) {
		DRM_ERROR("failed to init vblank\n");
		return ret;
	}

	priv->fbdev = drm_fbdev_cma_init(ddev, 24,
					 ddev->mode_config.num_crtc,
					 ddev->mode_config.num_connector);
	drm_kms_helper_poll_init(ddev);

	DOVE_DBG("num_crtc = %d, num_encoder = %d, num_connector = %d, num_fb = %d, num_plane = %d\n",
		 ddev->mode_config.num_crtc, ddev->mode_config.num_encoder, ddev->mode_config.num_connector,
		 ddev->mode_config.num_crtc, ddev->mode_config.num_plane);

	return 0;
}

static int dove_drm_unload(struct drm_device *ddev)
{
	drm_kms_helper_poll_fini(ddev);
	drm_mode_config_cleanup(ddev);
	drm_vblank_cleanup(ddev);

	ddev->dev_private = NULL;

	return 0;
}

static void dove_drm_preclose(struct drm_device *ddev, struct drm_file *file)
{
	struct dove_drm_private *priv = ddev->dev_private;
	struct drm_pending_vblank_event *e, *t;
	unsigned long flags;

	/* release events of current file */
	spin_lock_irqsave(&ddev->event_lock, flags);
	list_for_each_entry_safe(e, t, &priv->pageflip_event_list, base.link) {
		if (e->base.file_priv == file) {
			list_del(&e->base.link);
			e->base.destroy(&e->base);
		}
	}
	spin_unlock_irqrestore(&ddev->event_lock, flags);
}

static void dove_drm_lastclose(struct drm_device *ddev)
{
	struct dove_drm_private *priv = ddev->dev_private;
	drm_fbdev_cma_restore_mode(priv->fbdev);
}

static const struct file_operations dove_drm_fops = {
	.owner			= THIS_MODULE,
	.open			= drm_open,
	.release		= drm_release,
	.unlocked_ioctl		= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= drm_compat_ioctl,
#endif
	.poll			= drm_poll,
	.read			= drm_read,
	.fasync			= drm_fasync,
	.llseek			= no_llseek,
	.mmap			= drm_gem_cma_mmap,
};

static struct drm_driver dove_drm_driver = {
	.driver_features	= DRIVER_HAVE_IRQ | DRIVER_GEM | DRIVER_MODESET,
	.load			= dove_drm_load,
	.unload			= dove_drm_unload,
	.preclose		= dove_drm_preclose,
	.lastclose		= dove_drm_lastclose,
	.get_vblank_counter	= dove_drm_crtc_vblank_count,
	.enable_vblank		= dove_drm_crtc_enable_vblank,
	.disable_vblank		= dove_drm_crtc_disable_vblank,
	.gem_free_object	= drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.dumb_destroy		= drm_gem_cma_dumb_destroy,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= dove_drm_debugfs_init,
	.debugfs_cleanup	= dove_drm_debugfs_cleanup,
#endif
	.fops			= &dove_drm_fops,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
};

static int dove_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct dove_drm_private *priv;

	if (!dev->of_node)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "unable to allocate private data\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, priv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "unable to get iomem\n");
		return -ENOENT;
	}

	priv->mmio = devm_request_and_ioremap(dev, res);
	if (IS_ERR(priv->mmio)) {
		dev_err(&pdev->dev, "unable to map iomem\n");
		return PTR_ERR(priv->mmio);
	}

	dev->coherent_dma_mask = DMA_BIT_MASK(32);

	return drm_platform_init(&dove_drm_driver, pdev);
}

static int dove_drm_remove(struct platform_device *pdev)
{
	drm_platform_exit(&dove_drm_driver, pdev);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id dove_drm_match[] = {
	{ .compatible = "marvell,dove-dcon", },
	{ }
};
#endif

static struct platform_driver dove_drm_platform_driver = {
	.probe = dove_drm_probe,
	.remove = dove_drm_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dove_drm_match),
	},
};

static int __init dove_drm_init(void)
{
	int ret;

	ret = platform_driver_register(&dove_lcd_platform_driver);
	if (ret)
		return ret;

	return platform_driver_register(&dove_drm_platform_driver);
}

static void __exit dove_drm_exit(void)
{
	platform_driver_unregister(&dove_lcd_platform_driver);
	platform_driver_unregister(&dove_drm_platform_driver);
}

/* late_initcall() allows to get loaded after external clk generators */
late_initcall(dove_drm_init);
module_exit(dove_drm_exit);

MODULE_AUTHOR("Jean-Francois Moine <moinejf@free.fr>");
MODULE_AUTHOR("Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC " Driver");
MODULE_LICENSE("GPL");
