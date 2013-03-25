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

#ifndef __DOVE_DRM_LCD_REGS_H__
#define __DOVE_DRM_LCD_REGS_H__

#include <linux/bitops.h>

#define LCD_TV_CONTROL1			0x084
#define   GET_VSYNC_L_OFFSET(x)		((x) & VSYNC_L_OFFSET_MASK)
#define   SET_VSYNC_L_OFFSET(x, o)	\
	(((x) & ~VSYNC_L_OFFSET_MASK) | ((o) << 20))
#define   VSYNC_L_OFFSET_MASK		(0xfff << 20)
#define   VSYNC_OFFSET_EN		BIT(12)
#define   GET_VSYNC_H_OFFSET(x)		((x) & VSYNC_H_OFFSET_MASK)
#define   SET_VSYNC_H_OFFSET(x, o)	(((x) & ~VSYNC_H_OFFSET_MASK) | (o))
#define   VSYNC_H_OFFSET_MASK		(0xfff)

#define LCD_CFG_GRA_START_ADDR0		0x0f4
#define LCD_CFG_GRA_START_ADDR1		0x0f8
#define LCD_CFG_GRA_PITCH		0x0fc
#define   GET_DUTY_CYCLE_CTRL(x)	((x) & DUTY_CYCLE_CTRL_MASK)
#define   SET_DUTY_CYCLE_CTRL(x, d)	\
	(((x) & ~DUTY_CYCLE_CTRL_MASK) | ((d) << 28))
#define   DUTY_CYCLE_CTRL_MASK		(0xf << 28)
#define   GET_BACKLIGHT_DIV(x)		((x) & BACKLIGHT_DIV_MASK)
#define   SET_BACKLIGHT_DIV(x, d)	\
	(((x) & ~BACKLIGHT_DIV_MASK) | ((d) << 16))
#define   BACKLIGHT_DIV_MASK		(0xfff << 16)
#define   GET_GRA_PITCH(x)		((x) & GRA_PITCH_MASK)
#define   SET_GRA_PITCH(x, p)		(((x) & ~GRA_PITCH_MASK) | (p))
#define   GRA_PITCH_MASK		(0xff)

#define LCD_SPU_GRA_OVSA_HPXL_VLN	0x100
#define LCD_SPU_GRA_HPXL_VLN		0x104
#define   LCD_H_V(h, v)			(((v) << 16) | (h))
#define   LCD_F_B(f, b)			LCD_H_V(f, b)

#define LCD_SPU_GRA_HPXL_VLN		0x104
#define LCD_SPU_GZM_HPXL_VLN		0x108
#define LCD_SPUT_V_H_TOTAL		0x114
#define LCD_SPUT_V_H_ACTIVE		0x118
#define LCD_SPU_H_PORCH			0x11c
#define LCD_SPU_V_PORCH			0x120
#define LCD_SPU_BLANKCOLOR		0x124
#define   BLANKCOLOR_R(r)		(r & 0xff)
#define   BLANKCOLOR_G(g)		((g & 0xff) << 8)
#define   BLANKCOLOR_B(b)		((b & 0xff) << 16)
#define   BLANKCOLOR(r, g, b)		(BLANKCOLOR_R(r) | \
					 BLANKCOLOR_G(g) | \
					 BLANKCOLOR_B(b))

#define LCD_CFG_RDREG4F			0x13c
#define   LCD_SRAM_WAIT			BIT(11)
#define   SMARTPANEL_FASTTX		BIT(10)
#define   DMA_ARB_BATCH			BIT(9)
#define   DMA_WATERMARK_ENABLE		BIT(8)
#define   GET_DMA_WATERMARK(x)		((x) & DMA_WATERMARK_MASK)
#define   SET_DMA_WATERMARK(x, w)	(((x) & ~DMA_WATERMARK_MASK) | (w))
#define   DMA_WATERMARK_MASK		(0xff)

#define LCD_SPU_DMA_CTRL0		0x190
#define   DMA_DISABLE_BLENDING		BIT(31)
#define   DMA_GAMMA			BIT(30)
#define   DMA_CBSH			BIT(29)
#define   DMA_PALETTE			BIT(28)
#define   DMA_AXI_PIPELINE		BIT(27)
#define   DMA_HWCURSOR_1BIT_MODE	BIT(26)
#define   DMA_HWCURSOR_1BIT		BIT(25)
#define   DMA_HWCURSOR			BIT(24)
#define   GET_DMA_FORMAT(x)		((x) & DMA_FORMAT_MASK)
#define   SET_DMA_FORMAT(x, f)		(((x) & ~DMA_FORMAT_MASK) | (f))
#define   DMA_FORMAT_MASK		(0xf << 20)
#define   DMA_FORMAT_RGB565		(0 << 20)
#define   DMA_FORMAT_RGB1555		(1 << 20)
#define   DMA_FORMAT_RGB888_24		(2 << 20)
#define   DMA_FORMAT_RGB888_32		(3 << 20)
#define   DMA_FORMAT_ARGB8888		(4 << 20)
#define   DMA_FORMAT_YUYV422		(5 << 20)
#define   DMA_FORMAT_YUV422_PLANAR	(6 << 20)
#define   DMA_FORMAT_YUV420_PLANAR	(7 << 20)
#define   DMA_FORMAT_SMPNCMD		(8 << 20)
#define   DMA_FORMAT_PALETTE_4BIT	(9 << 20)
#define   DMA_FORMAT_PALETTE_8BIT	(10 << 20)
#define   GET_GRA_FORMAT(x)		((x) & GRA_FORMAT_MASK)
#define   SET_GRA_FORMAT(x, f)		(((x) & ~GRA_FORMAT_MASK) | (f))
#define   GRA_FORMAT_MASK		(0xf << 16)
#define   GRA_FORMAT_RGB565		(0 << 16)
#define   GRA_FORMAT_RGB1555		(1 << 16)
#define   GRA_FORMAT_RGB888_24		(2 << 16)
#define   GRA_FORMAT_RGB888_32		(3 << 16)
#define   GRA_FORMAT_ARGB8888		(4 << 16)
#define   GRA_FORMAT_YUYV422		(5 << 16)
#define   GRA_FORMAT_YUV422_PLANAR	(6 << 16)
#define   GRA_FORMAT_YUV420_PLANAR	(7 << 16)
#define   GRA_FORMAT_SMPNCMD		(8 << 16)
#define   GRA_FORMAT_PALETTE_4BIT	(9 << 16)
#define   GRA_FORMAT_PALETTE_8BIT	(10 << 16)
#define   GRA_FRAME_TOGGLE		BIT(15)
#define   GRA_UV_HSMOOTH		BIT(14)
#define   GRA_TESTMODE			BIT(13)
#define   GRA_SWAP_R_B			BIT(12)
#define   GRA_SWAP_U_V			BIT(11)
#define   GRA_SWAP_Y_U			BIT(10)
#define   GRA_YUV2RGB			BIT(9)
#define   GRA_ENABLE			BIT(8)
#define   DMA_FRAME_TOGGLE		BIT(7)
#define   DMA_UV_HSMOOTH		BIT(6)
#define   DMA_TESTMODE			BIT(5)
#define   DMA_SWAP_R_B			BIT(4)
#define   DMA_SWAP_U_V			BIT(3)
#define   DMA_SWAP_Y_U			BIT(2)
#define   DMA_YUV2RGB			BIT(1)
#define   DMA_ENABLE			BIT(0)

#define LCD_SPU_DMA_CTRL1		0x194
#define   DMA_FRAME_TRIGGER		BIT(31)
#define   GET_DMA_VSYNC_SRC(x)		((x) & DMA_VSYNC_SRC_MASK)
#define   SET_DMA_VSYNC_SRC(x, s)	(((x) & ~DMA_VSYNC_SRC_MASK) | (s))
#define   DMA_VSYNC_SRC_MASK		(7 << 28)
#define   DMA_VSYNC_SRC_SMARTPANEL	(0 << 28)
#define   DMA_VSYNC_SRC_SMARTPANEL_IRQ	(1 << 28)
#define   DMA_VSYNC_SRC_DUMB		(2 << 28)
#define   DMA_VSYNC_SRC_IRE		(4 << 28)
#define   DMA_VSYNC_SRC_FRAME_TRIGGER	(7 << 28)
#define   DMA_VSYNC_FALLING		BIT(27)
#define   GET_COLOR_KEY_MODE(x)		((x) & COLOR_KEY_MODE_MASK)
#define   SET_COLOR_KEY_MODE(x, m)	(((x) & ~COLOR_KEY_MODE_MASK) | (m))
#define   COLOR_KEY_MODE_MASK		(7 << 24)
#define   COLOR_KEY_MODE_DISABLE	(0 << 24)
#define   COLOR_KEY_MODE_Y		(1 << 24)
#define   COLOR_KEY_MODE_U		(2 << 24)
#define   COLOR_KEY_MODE_RGB		(3 << 24)
#define   COLOR_KEY_MODE_V		(4 << 24)
#define   COLOR_KEY_MODE_R		(5 << 24)
#define   COLOR_KEY_MODE_G		(6 << 24)
#define   COLOR_KEY_MODE_B		(7 << 24)
#define   CARRY_EXTENSION		BIT(23)
#define   GATED_CLOCK_ENABLE		BIT(21)
#define   POWERDOWN_ENABLE		BIT(20)
#define   GET_DOWNSCALE(x)		((x) & DOWNSCALE_MASK)
#define   SET_DOWNSCALE(x, d)		(((x) & ~DOWNSCALE_MASK) | (d))
#define   DOWNSCALE_MASK		(3 << 18)
#define   DOWNSCALE_NONE		(0 << 18)
#define   DOWNSCALE_HALF		(1 << 18)
#define   DOWNSCALE_QUARTER		(2 << 18)
#define   GET_ALPHA_PATH(x)		((x) & ALPHA_PATH_MASK)
#define   SET_ALPHA_PATH(x, a)		(((x) & ~ALPHA_PATH_MASK) | (a))
#define   ALPHA_PATH_MASK		(3 << 16)
#define   ALPHA_PATH_VIDEO		(0 << 16)
#define   ALPHA_PATH_GRAPHIC		(1 << 16)
#define   ALPHA_PATH_CONFIGURABLE	(2 << 16)
#define   GET_ALPHA_VALUE(x)		(((x) & ALPHA_VALUE_MASK) >> 8)
#define   SET_ALPHA_VALUE(x, a)	(((x) & ~ALPHA_VALUE_MASK) | ((a) << 8))
#define   ALPHA_VALUE_MASK		(0xff << 8)
#define   GET_PIXEL_CMD(x)		((x) & PIXEL_CMD_MASK)
#define   SET_PIXEL_CMD(x, p)		(((x) & ~PIXEL_CMD_MASK) | (p))
#define   PIXEL_CMD_MASK		(0xff)

#define LCD_SPU_SRAM_PARA0		0x1a0
#define LCD_SPU_SRAM_PARA1		0x1a4
#define   ALWAYS_ON_SRAM_HWCURSOR	BIT(15)
#define   ALWAYS_ON_SRAM_PALETTE	BIT(14)
#define   ALWAYS_ON_SRAM_GAMMA		BIT(13)
#define   POWERDOWN_SRAM_VSCALE		BIT(8)
#define   POWERDOWN_SRAM_HWCURSOR	BIT(7)
#define   POWERDOWN_SRAM_PALETTE	BIT(6)
#define   POWERDOWN_SRAM_GAMMA		BIT(5)
#define   POWERDOWN_SRAM_HWCURSOR_ALL	BIT(4)
#define   POWERDOWN_SRAM_SMARTPANEL	BIT(3)
#define   POWERDOWN_SRAM_FIFO_UV	BIT(2)
#define   POWERDOWN_SRAM_FIFO_Y		BIT(1)
#define   POWERDOWN_SRAM_FIFO_GRA	BIT(0)

#define LCD_CFG_SCLK_DIV		0x1a8
#define   GET_SCLK_SRC(x)		((x) & SCLK_SRC_MASK)
#define   SET_SCLK_SRC(x, s)		(((x) & ~SCLK_SRC_MASK) | (s))
#define   SCLK_SRC_MASK			(3 << 30)
#define   SCLK_SRC_AXI			(0 << 30)
#define   SCLK_SRC_EXTCLK0		(1 << 30)
#define   SCLK_SRC_PLLDIV		(2 << 30)
#define   SCLK_SRC_EXTCLK1		(3 << 30)
#define   SCLK_DIV_CHANGE_EVENT		BIT(29)
#define   GET_SCLK_FRAC_DIV(x)		(((x) & SCLK_FRAC_DIV_MASK) >> 16)
#define   SET_SCLK_FRAC_DIV(x, f)	(((x) & ~SCLK_FRAC_DIV_MASK) | ((f) << 16))
#define   SCLK_FRAC_DIV_MASK		(0xfff << 16)
#define   GET_SCLK_INT_DIV(x)		((x) & SCLK_INT_DIV_MASK)
#define   SET_SCLK_INT_DIV(x, i)	(((x) & ~SCLK_INT_DIV_MASK) | (i))
#define   SCLK_INT_DIV_MASK		(0xffff)
#define   SET_SCLK_DIV(x, i, f) \
	(((x) & ~(SCLK_INT_DIV_MASK | SCLK_FRAC_DIV_MASK)) | ((f) << 16) | (i))

#define LCD_SPU_DUMB_CTRL		0x1b8
#define   GET_DUMB_MODE(x)		(((x) & DUMB_MODE_MASK) >> 28)
#define   SET_DUMB_MODE(x, d)		(((x) & ~DUMB_MODE_MASK) | (d << 28))
#define   DUMB_MODE_MASK		(0xf << 28)
#define   DUMB_MODE_RGB16_LO		0
#define   DUMB_MODE_RGB16_HI		1
#define   DUMB_MODE_RGB18_LO		2
#define   DUMB_MODE_RGB18_HI		3
#define   DUMB_MODE_RGB12_LO		4
#define   DUMB_MODE_RGB12_HI		5
#define   DUMB_MODE_RGB24		6
#define   GET_DUMB_GPIO_VAL(x)		((x) & DUMB_GPIO_VAL_MASK)
#define   SET_DUMB_GPIO_VAL(x, v)	(((x) & ~DUMB_GPIO_VAL_MASK) | (v))
#define   DUMB_GPIO_VAL_MASK		(0xff << 20)
#define   GET_DUMB_GPIO_OE(x)		((x) & DUMB_GPIO_OE_MASK)
#define   SET_DUMB_GPIO_OE(x, o)		(((x) & ~DUMB_GPIO_OE_MASK) | (o))
#define   DUMB_GPIO_OE_MASK		(0xff << 12)
#define   DUMB_BIAS_OUT			BIT(8)
#define   DUMB_REVERSE_RGB		BIT(7)
#define   DUMB_INVERT_COMPOSITE_BLANK	BIT(6)
#define   DUMB_INVERT_COMPOSITE_SYNC	BIT(5)
#define   DUMB_INVERT_VALID		BIT(4)
#define   DUMB_INVERT_VSYNC		BIT(3)
#define   DUMB_INVERT_HSYNC		BIT(2)
#define   DUMB_INVERT_PIXCLK		BIT(1)
#define   DUMB_ENABLE			BIT(0)

#define SPU_IOPAD_CONTROL		0x1bc
#define   GET_VSCALE_LIN(x)		((x) & VSCALE_LINE_MASK)
#define   SET_VSCALE_LIN(x, s)		(((x) & ~VSCALE_LINE_MASK) | (s))
#define   VSCALE_LIN_MASK		(3 << 18)
#define   VSCALE_LIN_DISABLE		(0 << 18)
#define   VSCALE_LIN_BILINEAR		(3 << 18)
#define   GRA_VMIRROR			BIT(15)
#define   DMA_VMIRROR			BIT(13)
#define   CMD_VMIRROR			BIT(11)
#define   GET_CSC(x)			((x) & CSC_MASK)
#define   SET_CSC(x, c)			(((x) & ~CSC_MASK) | (c))
#define   CSC_MASK			(3 << 8)
#define   CSC_BT601_COMPUTER		(0 << 8)
#define   CSC_BT601_STUDIO		(1 << 8)
#define   CSC_BT709_COMPUTER		(2 << 8)
#define   CSC_BT709_STUDIO		(3 << 8)
#define   GET_AXI_BURST_BOUNDARY(x)	((x) & AXI_BURST_BOUNDARY_MASK)
#define   SET_AXI_BURST_BOUNDARY(x, b)	(((x) & ~AXI_BURST_BOUNDARY_MASK) | (b))
#define   AXI_BURST_BOUNDARY_MASK	(BIT(7) | BIT(5))
#define   AXI_BURST_BOUNDARY_4K		0
#define   AXI_BURST_BOUNDARY_1K		BIT(5)
#define   AXI_BURST_BOUNDARY_128B	BIT(7)
#define   GET_AXI_BURST_SIZE(x)		((x) & AXI_BURST_SIZE_MASK)
#define   SET_AXI_BURST_SIZE(x, s)	(((x) & ~AXI_BURST_SIZE_MASK) | (s))
#define   AXI_BURST_SIZE_MASK		(BIT(6) | BIT(4))
#define   AXI_BURST_SIZE_64B		0
#define   AXI_BURST_SIZE_32B		BIT(4)
#define   AXI_BURST_SIZE_128B		BIT(6)
#define   SET_AXI_BURST(x, s, b) \
	(((x) & ~(AXI_BURST_BOUNDARY_MASK | AXI_BURST_SIZE_MASK)) | (s) | (b))
#define   GET_IOPAD_MODE(x)		((x) & IOPAD_MODE_MASK)
#define   SET_IOPAD_MODE(x, m)		(((x) & ~IOPAD_MODE_MASK) | (m))
#define   IOPAD_MODE_MASK		0xf
#define   IOPAD_MODE_DUMB_24		0
#define   IOPAD_MODE_DUMB_18_SPI	1
#define   IOPAD_MODE_DUMB_18_GPIO	2
#define   IOPAD_MODE_DUMB_16_SPI	3
#define   IOPAD_MODE_DUMB_16_GPIO	4
#define   IOPAD_MODE_DUMB_12_SPI_GPIO	5
#define   IOPAD_MODE_SMART_18_SPI	6
#define   IOPAD_MODE_SMART_16_SPI	7
#define   IOPAD_MODE_SMART_8_SPI_GPIO	8

#define SPU_IRQ_ENA			0x1c0
#define SPU_IRQ_ISR			0x1c4
#define   IRQ_DMA_FRAME0		BIT(31)
#define   IRQ_DMA_FRAME1		BIT(30)
#define   IRQ_DMA_FIFO_UNDERFLOW	BIT(29)
#define   IRQ_GRA_FRAME0		BIT(27)
#define   IRQ_GRA_FRAME1		BIT(26)
#define   IRQ_GRA_FIFO_UNDERFLOW	BIT(25)
#define   IRQ_SMART_VSYNC		BIT(23)
#define   IRQ_DUMB_FRAME_DONE		BIT(22)
#define   IRQ_SMART_FRAME_DONE		BIT(21)
#define   IRQ_HWCURSOR_FRAME_DONE	BIT(20)
#define   IRQ_AHB_CMD_EMPTY		BIT(19)
#define   IRQ_SPI_TRANSFER_DONE		BIT(18)
#define   IRQ_POWERDOWN			BIT(17)
#define   IRQ_AXI_ERROR			BIT(16)
/* read only status */
#define   STA_DMA_FRAME0		BIT(15)
#define   STA_DMA_FRAME1		BIT(14)
#define   STA_DMA_FRAME_COUNT(x)	(((x) & (BIT(13) | BIT(12))) >> 12)
#define   STA_GRA_FRAME0		BIT(11)
#define   STA_GRA_FRAME1		BIT(10)
#define   STA_GRA_FRAME_COUNT(x)	(((x) & (BIT(9) | BIT(8))) >> 8)
#define   STA_SMART_VSYNC		BIT(7)
#define   STA_DUMB_FRAME_DONE		BIT(6)
#define   STA_SMART_FRAME_DONE		BIT(5)
#define   STA_HWCURSOR_FRAME_DONE	BIT(4)
#define   STA_AHB_CMD_EMPTY		BIT(3)
#define   STA_DMA_FIFO_EMPTY		BIT(2)
#define   STA_GRA_FIFO_EMPTY		BIT(1)
#define   STA_POWERDOWN			BIT(0)

#define IRQ_DMA_FRAME_DONE		(IRQ_DMA_FRAME0 | IRQ_DMA_FRAME1)
#define IRQ_GRA_FRAME_DONE		\
	(IRQ_GRA_FRAME0 | IRQ_GRA_FRAME1 | IRQ_SMART_VSYNC)

#endif
