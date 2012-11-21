/*
 * kirkwood-i2s.c
 *
 * (c) 2010 Arnaud Patard <apatard@mandriva.com>
 * (c) 2010 Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#define DEBUG 1
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_data/asoc-kirkwood.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mbus.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <sound/asoundef.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "kirkwood.h"

#define DRV_NAME	"kirkwood-i2s"

#define KIRKWOOD_I2S_RATES \
	(SNDRV_PCM_RATE_44100 | \
	 SNDRV_PCM_RATE_48000 | \
	 SNDRV_PCM_RATE_96000)

#define KIRKWOOD_I2S_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | \
	 SNDRV_PCM_FMTBIT_S24_LE | \
	 SNDRV_PCM_FMTBIT_S32_LE | \
	 SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE | \
	 SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE)

static void kirkwood_i2s_dump_spdif(struct device *dev,
	struct kirkwood_dma_data *priv)
{
	u32 reg;

	reg = readl(priv->io + KIRKWOOD_SPDIF_PLAYCTL);
	dev_dbg(dev, "SPDIF Playback Ctrl = %08x\n", reg);
	dev_dbg(dev, " - Non-PCM             = %d\n", (reg & KIRKWOOD_SPDIF_NON_PCM) ? 1 : 0);
	dev_dbg(dev, " - Register Validity   = %d\n", (reg & KIRKWOOD_SPDIF_REG_VALIDITY) ? 1 : 0);
	dev_dbg(dev, " - Force Parity Error  = %d\n", (reg & KIRKWOOD_SPDIF_FORCE_PARERR) ? 1 : 0);
	dev_dbg(dev, " - Mem User Enable     = %d\n", (reg & KIRKWOOD_SPDIF_MEM_USER_EN) ? 1 : 0);
	dev_dbg(dev, " - Mem Validity Enable = %d\n", (reg & KIRKWOOD_SPDIF_MEM_VALIDITY_EN) ? 1 : 0);
	dev_dbg(dev, " - Block Start Mode    = %d\n", (reg & KIRKWOOD_SPDIF_BLOCK_START_MODE) ? 1 : 0);
}

static void kirkwood_i2s_dump_iec958(struct device *dev,
	struct snd_aes_iec958 *iec)
{
	char status[sizeof(" 00") * 4 + 1], *p, *e;
	int i;

	p = status;
	e = status + sizeof(status);

	for (i = 0; i < 4; i++)
		p += snprintf(p, e - p, " %02x", iec->status[i]);

	dev_dbg(dev, "status  = %s\n", status);
	dev_dbg(dev, "con/pro = %s, non-audio = %d\n",
	       (iec->status[0] & IEC958_AES0_PROFESSIONAL) ? "pro" : "con",
	       (iec->status[0] & IEC958_AES0_NONAUDIO) ? 1 : 0);
}

static int kirkwood_i2s_control_iec958_dflt_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);

	dev_dbg(dai->dev, "%s\n", __func__);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int kirkwood_i2s_control_iec958_dflt_get(struct snd_kcontrol *kcontrol,
	 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(cpu_dai);
	u32 reg;
	int n;

	dev_dbg(cpu_dai->dev, "%s: kcontrol = %p, kcontrol->private_data = %p, ucontrol = %p\n",
		__func__, kcontrol, kcontrol->private_data, ucontrol);
	kirkwood_i2s_dump_iec958(cpu_dai->dev, &ucontrol->value.iec958);

#if 0
	reg = readl(priv->io + KIRKWOOD_SPDIF_PLAYCTL);
	ucontrol->value.iec958.status[0] &=
		~(IEC958_AES0_PROFESSIONAL | IEC958_AES0_NONAUDIO);
	if (reg & KIRKWOOD_SPDIF_NON_PCM)
		ucontrol->value.iec958.status[0] |= IEC958_AES0_NONAUDIO;
#endif

	for (n = 0; n < 6; n++) {
		reg = readl(priv->io + KIRKWOOD_SPDIF_STATUS0_L + (4 * n));
		ucontrol->value.iec958.status[4 * n + 3] = (uint8_t)(reg >> 24) & 0xff;
		ucontrol->value.iec958.status[4 * n + 2] = (uint8_t)(reg >> 16) & 0xff;
		ucontrol->value.iec958.status[4 * n + 1] = (uint8_t)(reg >>  8) & 0xff;
		ucontrol->value.iec958.status[4 * n + 0] = (uint8_t)(reg >>  0) & 0xff;
	}

	return 0;
}

static int kirkwood_i2s_control_iec958_dflt_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(cpu_dai);
	u32 reg, nreg;
	int n;

	dev_dbg(cpu_dai->dev, "%s: kcontrol = %p, ucontrol = %p\n",
		__func__, kcontrol, ucontrol);
	kirkwood_i2s_dump_iec958(cpu_dai->dev, &ucontrol->value.iec958);

	reg  = readl(priv->io + KIRKWOOD_SPDIF_PLAYCTL);
	nreg = reg & ~(KIRKWOOD_SPDIF_NON_PCM | KIRKWOOD_SPDIF_REG_VALIDITY);
	if (ucontrol->value.iec958.status[0] & IEC958_AES0_NONAUDIO)
		nreg |= KIRKWOOD_SPDIF_NON_PCM | KIRKWOOD_SPDIF_REG_VALIDITY;
	writel(nreg, priv->io + KIRKWOOD_SPDIF_PLAYCTL);

	for (n = 0; n < 6; n++) {
		reg = (ucontrol->value.iec958.status[4 * n + 3] << 24) |
			(ucontrol->value.iec958.status[4 * n + 2] << 16) |
			(ucontrol->value.iec958.status[4 * n + 1] << 8) |
			(ucontrol->value.iec958.status[4 * n + 0]);
		writel(reg, priv->io + KIRKWOOD_SPDIF_STATUS0_L + (4 * n));
		writel(reg, priv->io + KIRKWOOD_SPDIF_STATUS0_R + (4 * n));
	}
	return (nreg != reg);
}

static const struct snd_kcontrol_new kirkwood_i2s_iec958_controls[] = {
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_PCM,
		.name	= SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info	= kirkwood_i2s_control_iec958_dflt_info,
		.get	= kirkwood_i2s_control_iec958_dflt_get,
		.put	= kirkwood_i2s_control_iec958_dflt_put,
	},
};

static int kirkwood_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long mask;
	unsigned long value;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		mask = KIRKWOOD_I2S_CTL_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		mask = KIRKWOOD_I2S_CTL_LJ;
		break;
	case SND_SOC_DAIFMT_I2S:
		mask = KIRKWOOD_I2S_CTL_I2S;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Set same format for playback and record
	 * This avoids some troubles.
	 */
	value = readl(priv->io+KIRKWOOD_I2S_PLAYCTL);
	value &= ~KIRKWOOD_I2S_CTL_JUST_MASK;
	value |= mask;
	writel(value, priv->io+KIRKWOOD_I2S_PLAYCTL);

	value = readl(priv->io+KIRKWOOD_I2S_RECCTL);
	value &= ~KIRKWOOD_I2S_CTL_JUST_MASK;
	value |= mask;
	writel(value, priv->io+KIRKWOOD_I2S_RECCTL);

	return 0;
}

static inline void kirkwood_set_dco(void __iomem *io, unsigned long rate)
{
	unsigned long value;

	value = KIRKWOOD_DCO_CTL_OFFSET_0;
	switch (rate) {
	default:
	case 44100:
		value |= KIRKWOOD_DCO_CTL_FREQ_11;
		break;
	case 48000:
		value |= KIRKWOOD_DCO_CTL_FREQ_12;
		break;
	case 96000:
		value |= KIRKWOOD_DCO_CTL_FREQ_24;
		break;
	}
	writel(value, io + KIRKWOOD_DCO_CTL);

	/* wait for dco locked */
	do {
		cpu_relax();
		value = readl(io + KIRKWOOD_DCO_SPCR_STATUS);
		value &= KIRKWOOD_DCO_SPCR_STATUS_DCO_LOCK;
	} while (value == 0);
}

static void kirkwood_set_rate(struct snd_soc_dai *dai,
	struct kirkwood_dma_data *priv, unsigned long rate)
{
	uint32_t clks_ctrl;

	if (rate == 44100 || rate == 48000 || rate == 96000) {
		/* use internal dco for supported rates */
		dev_dbg(dai->dev, "%s: dco set rate = %lu\n",
			__func__, rate);
		kirkwood_set_dco(priv->io, rate);

		clks_ctrl = KIRKWOOD_MCLK_SOURCE_DCO;
	} else if (!IS_ERR(priv->extclk)) {
		/* use optional external clk for other rates */
		dev_dbg(dai->dev, "%s: extclk set rate = %lu -> %lu\n",
			__func__, rate, 256 * rate);
		clk_set_rate(priv->extclk, 256 * rate);

		clks_ctrl = KIRKWOOD_MCLK_SOURCE_EXTCLK;
	}
	writel(clks_ctrl, priv->io + KIRKWOOD_CLOCKS_CTRL);
}

static int kirkwood_i2s_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_set_dma_data(dai, substream, priv);
	return 0;
}

static int kirkwood_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(dai);
	uint32_t ctl_play, ctl_rec;
	unsigned int i2s_reg;
	unsigned long i2s_value;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		i2s_reg = KIRKWOOD_I2S_PLAYCTL;
	} else {
		i2s_reg = KIRKWOOD_I2S_RECCTL;
	}

	kirkwood_set_rate(dai, priv, params_rate(params));

	i2s_value = readl(priv->io+i2s_reg);
	i2s_value &= ~KIRKWOOD_I2S_CTL_SIZE_MASK;

	/*
	 * Size settings in play/rec i2s control regs and play/rec control
	 * regs must be the same.
	 */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		i2s_value |= KIRKWOOD_I2S_CTL_SIZE_16;
		ctl_play = KIRKWOOD_PLAYCTL_SIZE_16_C | 
			   KIRKWOOD_PLAYCTL_I2S_EN |
			   KIRKWOOD_PLAYCTL_SPDIF_EN;
		ctl_rec = KIRKWOOD_RECCTL_SIZE_16_C |
			  KIRKWOOD_RECCTL_I2S_EN;
		break;
	/*
	 * doesn't work... S20_3LE != kirkwood 20bit format ?
	 *
	case SNDRV_PCM_FORMAT_S20_3LE:
		i2s_value |= KIRKWOOD_I2S_CTL_SIZE_20;
		ctl_play = KIRKWOOD_PLAYCTL_SIZE_20 | 
			   KIRKWOOD_PLAYCTL_I2S_EN |
			   KIRKWOOD_PLAYCTL_SPDIF_EN;
		ctl_rec = KIRKWOOD_RECCTL_SIZE_20 |
			  KIRKWOOD_RECCTL_I2S_EN;
		break;
	*/
	case SNDRV_PCM_FORMAT_S24_LE:
		i2s_value |= KIRKWOOD_I2S_CTL_SIZE_24;
		ctl_play = KIRKWOOD_PLAYCTL_SIZE_24 | 
			   KIRKWOOD_PLAYCTL_I2S_EN |
			   KIRKWOOD_PLAYCTL_SPDIF_EN;
		ctl_rec = KIRKWOOD_RECCTL_SIZE_24 |
			  KIRKWOOD_RECCTL_I2S_EN;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		i2s_value |= KIRKWOOD_I2S_CTL_SIZE_32;
		ctl_play = KIRKWOOD_PLAYCTL_SIZE_32 | 
			   KIRKWOOD_PLAYCTL_I2S_EN;
		ctl_rec = KIRKWOOD_RECCTL_SIZE_32 |
			  KIRKWOOD_RECCTL_I2S_EN;
		break;
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			return -EINVAL;
		i2s_value |= KIRKWOOD_I2S_CTL_SIZE_16;
		ctl_play = KIRKWOOD_PLAYCTL_SIZE_16_C |
			   KIRKWOOD_PLAYCTL_SPDIF_EN;
		ctl_rec = 0;
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (params_channels(params) == 1)
			ctl_play |= KIRKWOOD_PLAYCTL_MONO_BOTH;
		else
			ctl_play |= KIRKWOOD_PLAYCTL_MONO_OFF;

		priv->ctl_play &= ~(KIRKWOOD_PLAYCTL_MONO_MASK |
				    KIRKWOOD_PLAYCTL_I2S_EN |
				    KIRKWOOD_PLAYCTL_SPDIF_EN |
				    KIRKWOOD_PLAYCTL_SIZE_MASK);
		priv->ctl_play |= ctl_play;
	} else {
		priv->ctl_rec &= ~KIRKWOOD_RECCTL_SIZE_MASK;
		priv->ctl_rec |= ctl_rec;
	}

	writel(i2s_value, priv->io+i2s_reg);

	return 0;
}

static int kirkwood_i2s_play_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(dai);
	uint32_t ctl, value;

	ctl = readl(priv->io + KIRKWOOD_PLAYCTL);
	if (ctl & KIRKWOOD_PLAYCTL_PAUSE) {
		unsigned timeout = 5000;
		/*
		 * The Armada510 spec says that if we enter pause mode, the
		 * busy bit must be read back as clear _twice_.  Make sure
		 * we respect that otherwise we get DMA underruns.
		 */
		do {
			value = ctl;
			ctl = readl(priv->io + KIRKWOOD_PLAYCTL);
			if (!((ctl | value) & KIRKWOOD_PLAYCTL_PLAY_BUSY))
				break;
			udelay(1);
		} while (timeout--);

		if ((ctl | value) & KIRKWOOD_PLAYCTL_PLAY_BUSY)
			dev_notice(dai->dev, "timed out waiting for busy to deassert: %08x\n",
				   ctl);
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* configure */
		ctl = priv->ctl_play;
		value = ctl & ~(KIRKWOOD_PLAYCTL_I2S_EN |
				KIRKWOOD_PLAYCTL_SPDIF_EN);
		writel(value, priv->io + KIRKWOOD_PLAYCTL);

		/* enable interrupts */
		value = readl(priv->io + KIRKWOOD_INT_MASK);
		value |= KIRKWOOD_INT_CAUSE_PLAY_BYTES;
		writel(value, priv->io + KIRKWOOD_INT_MASK);

		/* enable playback */
		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);

		kirkwood_i2s_dump_spdif(dai->dev, priv);
		
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		/* stop audio, disable interrupts */
//		ctl |= KIRKWOOD_PLAYCTL_PAUSE | KIRKWOOD_PLAYCTL_I2S_MUTE;
//		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);

		value = readl(priv->io + KIRKWOOD_INT_MASK);
		value &= ~KIRKWOOD_INT_CAUSE_PLAY_BYTES;
		writel(value, priv->io + KIRKWOOD_INT_MASK);

		/* disable all playbacks */
		ctl &= ~(KIRKWOOD_PLAYCTL_I2S_EN | KIRKWOOD_PLAYCTL_SPDIF_EN);
		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		ctl |= KIRKWOOD_PLAYCTL_PAUSE | KIRKWOOD_PLAYCTL_I2S_MUTE;
		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ctl &= ~(KIRKWOOD_PLAYCTL_PAUSE | KIRKWOOD_PLAYCTL_I2S_MUTE);
		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int kirkwood_i2s_rec_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(dai);
	uint32_t ctl, value;

	value = readl(priv->io + KIRKWOOD_RECCTL);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* configure */
		ctl = priv->ctl_rec;
		value = ctl & ~KIRKWOOD_RECCTL_I2S_EN;
		writel(value, priv->io + KIRKWOOD_RECCTL);

		/* enable interrupts */
		value = readl(priv->io + KIRKWOOD_INT_MASK);
		value |= KIRKWOOD_INT_CAUSE_REC_BYTES;
		writel(value, priv->io + KIRKWOOD_INT_MASK);

		/* enable record */
		writel(ctl, priv->io + KIRKWOOD_RECCTL);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		/* stop audio, disable interrupts */
		value = readl(priv->io + KIRKWOOD_RECCTL);
		value |= KIRKWOOD_RECCTL_PAUSE | KIRKWOOD_RECCTL_MUTE;
		writel(value, priv->io + KIRKWOOD_RECCTL);

		value = readl(priv->io + KIRKWOOD_INT_MASK);
		value &= ~KIRKWOOD_INT_CAUSE_REC_BYTES;
		writel(value, priv->io + KIRKWOOD_INT_MASK);

		/* disable all records */
		value = readl(priv->io + KIRKWOOD_RECCTL);
		value &= ~(KIRKWOOD_RECCTL_I2S_EN | KIRKWOOD_RECCTL_SPDIF_EN);
		writel(value, priv->io + KIRKWOOD_RECCTL);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		value = readl(priv->io + KIRKWOOD_RECCTL);
		value |= KIRKWOOD_RECCTL_PAUSE | KIRKWOOD_RECCTL_MUTE;
		writel(value, priv->io + KIRKWOOD_RECCTL);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		value = readl(priv->io + KIRKWOOD_RECCTL);
		value &= ~(KIRKWOOD_RECCTL_PAUSE | KIRKWOOD_RECCTL_MUTE);
		writel(value, priv->io + KIRKWOOD_RECCTL);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int kirkwood_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return kirkwood_i2s_play_trigger(substream, cmd, dai);
	else
		return kirkwood_i2s_rec_trigger(substream, cmd, dai);

	return 0;
}

static int kirkwood_i2s_probe(struct snd_soc_dai *dai)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(dai);
	unsigned long value;
	unsigned int reg_data;
	int ret;

	ret = snd_soc_add_dai_controls(dai, kirkwood_i2s_iec958_controls,
				ARRAY_SIZE(kirkwood_i2s_iec958_controls));
	if (ret) {
		dev_err(dai->dev, "unable to add soc card controls\n");
		return ret;
	}
	/* put system in a "safe" state : */
	/* disable audio interrupts */
	writel(0xffffffff, priv->io + KIRKWOOD_INT_CAUSE);
	writel(0, priv->io + KIRKWOOD_INT_MASK);

	reg_data = readl(priv->io + 0x120c);
	reg_data = readl(priv->io + 0x1200);
	reg_data &= (~(0x333FF8));
	reg_data |= 0x111D18;
	writel(reg_data, priv->io + 0x1200);
	msleep(500);
	reg_data = readl(priv->io + 0x1200);
	reg_data &= (~(0x333FF8));
	reg_data |= 0x111D18;
	msleep(500);
	writel(reg_data, priv->io + 0x1200);

	/* disable playback/record */
	value = readl(priv->io + KIRKWOOD_PLAYCTL);
	value &= ~(KIRKWOOD_PLAYCTL_I2S_EN|KIRKWOOD_PLAYCTL_SPDIF_EN);
	writel(value, priv->io + KIRKWOOD_PLAYCTL);

	value = readl(priv->io + KIRKWOOD_RECCTL);
	value &= ~(KIRKWOOD_RECCTL_I2S_EN | KIRKWOOD_RECCTL_SPDIF_EN);
	writel(value, priv->io + KIRKWOOD_RECCTL);

	return 0;

}

static int kirkwood_i2s_remove(struct snd_soc_dai *dai)
{
	return 0;
}

static const struct snd_soc_dai_ops kirkwood_i2s_dai_ops = {
	.startup	= kirkwood_i2s_startup,
	.trigger	= kirkwood_i2s_trigger,
	.hw_params      = kirkwood_i2s_hw_params,
	.set_fmt        = kirkwood_i2s_set_fmt,
};


static struct snd_soc_dai_driver kirkwood_i2s_dai = {
	.probe = kirkwood_i2s_probe,
	.remove = kirkwood_i2s_remove,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = KIRKWOOD_I2S_RATES,
		.formats = KIRKWOOD_I2S_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = KIRKWOOD_I2S_RATES,
		.formats = KIRKWOOD_I2S_FORMATS,
	},
	.ops = &kirkwood_i2s_dai_ops,
};

static struct snd_soc_dai_driver kirkwood_i2s_dai_extclk = {
	.probe = kirkwood_i2s_probe,
	.remove = kirkwood_i2s_remove,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000 |
			 SNDRV_PCM_RATE_CONTINUOUS |
			 SNDRV_PCM_RATE_KNOT,
		.formats = KIRKWOOD_I2S_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000 |
			 SNDRV_PCM_RATE_CONTINUOUS |
			 SNDRV_PCM_RATE_KNOT,
		.formats = KIRKWOOD_I2S_FORMATS,
	},
	.ops = &kirkwood_i2s_dai_ops,
};

static __devinit int kirkwood_i2s_dev_probe(struct platform_device *pdev)
{
	struct kirkwood_asoc_platform_data *data = pdev->dev.platform_data;
	struct snd_soc_dai_driver *soc_dai = &kirkwood_i2s_dai;
	struct kirkwood_dma_data *priv;
	struct resource *mem;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "allocation failed\n");
		return -ENOMEM;
	}
	dev_set_drvdata(&pdev->dev, priv);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "platform_get_resource failed\n");
		return -ENXIO;
	}

	priv->io = devm_request_and_ioremap(&pdev->dev, mem);
	if (!priv->io) {
		dev_err(&pdev->dev, "devm_request_and_ioremap failed\n");
		return -ENOMEM;
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq <= 0) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		return -ENXIO;
	}

	if (pdev->dev.of_node) {
		if (of_property_read_u32(pdev->dev.of_node, "marvell,burst-size",
					  &priv->burst) < 0) {
			dev_err(&pdev->dev,
				"missing marvell,burst-size property\n");
			return -EINVAL;
		}
	} else {
		if (!data) {
			dev_err(&pdev->dev, "no platform data ?!\n");
			return -EINVAL;
		}
		priv->burst = data->burst;
	}

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "no clock\n");
		return PTR_ERR(priv->clk);
	}

	err = clk_prepare_enable(priv->clk);
	if (err < 0)
		return err;

	writel(KIRKWOOD_MCLK_SOURCE_DCO, 
	       priv->io+KIRKWOOD_CLOCKS_CTRL);

	priv->extclk = clk_get(&pdev->dev, "extclk");
	if (!IS_ERR(priv->extclk)) {
		if (priv->extclk == priv->clk) {
			clk_put(priv->extclk);
			priv->extclk = ERR_PTR(-EINVAL);
		} else {
			dev_info(&pdev->dev, "found external clock\n");
			clk_prepare_enable(priv->extclk);
			soc_dai = &kirkwood_i2s_dai_extclk;
		}
	}

	/* Some sensible defaults - this reflects the powerup values */
	priv->ctl_play = KIRKWOOD_PLAYCTL_SIZE_24;
	priv->ctl_rec = KIRKWOOD_RECCTL_SIZE_24;

	/* Select the burst size */
	if (priv->burst == 32) {
		priv->ctl_play |= KIRKWOOD_PLAYCTL_BURST_32;
		priv->ctl_rec |= KIRKWOOD_RECCTL_BURST_32;
	} else {
		priv->ctl_play |= KIRKWOOD_PLAYCTL_BURST_128;
		priv->ctl_rec |= KIRKWOOD_RECCTL_BURST_128;
	}

	err = snd_soc_register_dai(&pdev->dev, soc_dai);
	if (err) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", err);
		goto err_clk_put;
	}

	err = snd_soc_register_platform(&pdev->dev, &kirkwood_soc_platform);
	if (err) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", err);
		goto err_unregister_dai;
	}
	return 0;

err_unregister_dai:
	snd_soc_unregister_dai(&pdev->dev);
err_clk_put:
	if (!IS_ERR(priv->extclk)) {
		clk_disable_unprepare(priv->extclk);
		clk_put(priv->extclk);
	}
	clk_disable_unprepare(priv->clk);

	return err;
}

static __devexit int kirkwood_i2s_dev_remove(struct platform_device *pdev)
{
	struct kirkwood_dma_data *priv = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_platform(&pdev->dev);
	snd_soc_unregister_dai(&pdev->dev);

	if (!IS_ERR(priv->extclk)) {
		clk_disable_unprepare(priv->extclk);
		clk_put(priv->extclk);
	}
	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id kirkwood_i2s_of_match[] __devinitconst = {
	{ .compatible = "marvell,kirkwood-i2s", },
	{},
};

static struct platform_driver kirkwood_i2s_driver = {
	.probe  = kirkwood_i2s_dev_probe,
	.remove = __devexit_p(kirkwood_i2s_dev_remove),
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = kirkwood_i2s_of_match,
	},
};

module_platform_driver(kirkwood_i2s_driver);

/* Module information */
MODULE_AUTHOR("Arnaud Patard, <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("Kirkwood I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:kirkwood-i2s");
