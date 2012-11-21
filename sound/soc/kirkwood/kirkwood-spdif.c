/*
 * kirkwood-spdif.c
 *
 * (c) 2012 Sebastian Hesselbarth <sebastian.hesselbarth@googlemail.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_data/asoc-kirkwood.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/soc.h>

static int kirkwood_spdif_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	unsigned int freq;

	dev_dbg(card->dev, "%s: substream = %p, params = %p\n",
		__func__, substream, params);
	dev_dbg(card->dev, "%s: rate = %d\n",
	 	__func__, params_rate(params));
	dev_dbg(card->dev, "%s: codec_dai = %s\n",
		__func__, codec_dai->name);
	return 0;

	switch (params_rate(params)) {
	default:
	case 44100:
		freq = 11289600;
		break;
	case 48000:
		freq = 12288000;
		break;
	case 96000:
		freq = 24576000;
		break;
	}
	
	return snd_soc_dai_set_sysclk(codec_dai, 0, freq, SND_SOC_CLOCK_IN);
}

static struct snd_soc_ops kirkwood_spdif_ops = {
	.hw_params = kirkwood_spdif_hw_params,
};

static struct snd_soc_dai_link kirkwood_spdif_dai0[] = {
	{
		.name = "SPDIF0",
		.stream_name = "SPDIF0 PCM Playback",
		.platform_name = "kirkwood-pcm-audio.0",
		.cpu_dai_name = "kirkwood-i2s.0",
		.codec_dai_name = "dit-hifi",
		.codec_name = "spdif-dit",
		.ops = &kirkwood_spdif_ops,
	},
};

static struct snd_soc_dai_link kirkwood_spdif_dai1[] = {
	{
		.name = "SPDIF1",
		.stream_name = "IEC958 Playback",
		.platform_name = "kirkwood-pcm-audio.1",
		.cpu_dai_name = "kirkwood-i2s.1",
		.codec_dai_name = "dit-hifi",
		.codec_name = "spdif-dit",
		.ops = &kirkwood_spdif_ops,
	},
};

static int __devinit kirkwood_spdif_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	int ret;

	dev_dbg(&pdev->dev, "%s: pdev = %p, pdev->id = %d",
		__func__, pdev, pdev->id);

	if (!pdev->dev.of_node && (pdev->id < 0 || pdev->id > 1))
		return -EINVAL;

	card = devm_kzalloc(&pdev->dev, sizeof(struct snd_soc_card), GFP_KERNEL);
	if (card == NULL) {
		dev_err(&pdev->dev, "unable to allocate soc card\n");
		ret = -ENOMEM;
		goto kirkwood_spdif_err_card_alloc;
	}

	card->name = "Kirkwood SPDIF";
	card->owner = THIS_MODULE;
	if (pdev->id == 0)
		card->dai_link = kirkwood_spdif_dai0;
	else
		card->dai_link = kirkwood_spdif_dai1;
	card->num_links = 1;
	card->dev = &pdev->dev;

	if (pdev->dev.of_node) {
		card->dai_link->codec_name = NULL;
		card->dai_link->platform_name = NULL;
		card->dai_link->cpu_dai_name = NULL;

		card->dai_link->codec_of_node = of_parse_phandle(
			pdev->dev.of_node, "marvell,audio-codec", 0);
		if (!card->dai_link->codec_of_node) {
			dev_err(&pdev->dev,
			      "missing/invalid property marvell,audio-codec\n");
			return -EINVAL;
		}

		card->dai_link->cpu_of_node = of_parse_phandle(
			pdev->dev.of_node, "marvell,audio-controller", 0);
		if (!card->dai_link->cpu_of_node) {
			dev_err(&pdev->dev,
			 "missing/invalid property marvell,audio-controller\n");
			return -EINVAL;
		}

		card->dai_link->platform_of_node = card->dai_link->cpu_of_node;
	}

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "failed to register card\n");
		return ret;
	}
	return 0;

kirkwood_spdif_err_card_alloc:	
	return ret;
}

static int __devexit kirkwood_spdif_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	snd_soc_unregister_card(card);
	return 0;
}

static const struct of_device_id kirkwood_spdif_of_match[] __devinitconst = {
	{ .compatible = "marvell,kirkwood-spdif", },
	{},
};

static struct platform_driver kirkwood_spdif_driver = {
	.driver		= {
		.name	= "kirkwood-spdif-audio",
		.owner	= THIS_MODULE,
		.of_match_table = kirkwood_spdif_of_match,
	},
	.probe		= kirkwood_spdif_probe,
	.remove		= __devexit_p(kirkwood_spdif_remove),
};
module_platform_driver(kirkwood_spdif_driver);

MODULE_AUTHOR("Sebastian Hesselbarth <sebastian.hesselbarth@googlemail.com>");
MODULE_DESCRIPTION("ALSA SoC kirkwood SPDIF audio driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:kirkwood-spdif-audio");
