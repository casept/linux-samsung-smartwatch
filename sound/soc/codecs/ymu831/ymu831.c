// SPDX-License-Identifier: Zlib
/*
 * YMU831 ASoC codec driver
 *
 * Copyright (c) 2012-2013 Yamaha Corporation
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.	In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *	claim that you wrote the original software. If you use this software
 *	in a product, an acknowledgment in the product documentation would be
 *	appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *	misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/jack.h>

#include "ymu831_cfg.h"
#include "ymu831.h"
#include "ymu831_priv.h"
#include "mcdefs.h"
#include "mcresctrl.h"
#include "ymu831_dapm.h"
#include <linux/spi/spi.h>

#define MC_ASOC_DRIVER_VERSION	"2.0.1"

#define MC_ASOC_IMPCLASS_THRESHOLD	3

#define MC_ASOC_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S20_3LE | \
			 SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S24_3LE | \
			 SNDRV_PCM_FMTBIT_S32_LE)

#define MC_ASOC_HWDEP_ID	"ymu831"


#define	DIO_MUSIC	(0)
#define	DIO_VOICE	(1)
#define	DIO_EXT		(2)
#define	LOUT1		(3)
#define	LOUT2		(4)
#define	LIN1		(3)
#define	LIN1_LOUT1	(3)
#define	LIN1_LOUT2	(5)

#define	DSP_PRM_OUTPUT	(0)
#define	DSP_PRM_INPUT	(1)
#define	DSP_PRM_VC_1MIC	(2)
#define	DSP_PRM_VC_2MIC	(3)
#define	DSP_PRM_BASE	(0)
#define	DSP_PRM_USER	(1)



/* volmap for Digital Volumes */
static const SINT16	volmap_digital[]	= {
	0xa000, 0xa100, 0xa200, 0xa300, 0xa400, 0xa500, 0xa600, 0xa700,
	0xa800, 0xa900, 0xaa00, 0xab00, 0xac00, 0xad00, 0xae00, 0xaf00,
	0xb000, 0xb100, 0xb200, 0xb300, 0xb400, 0xb500, 0xb600, 0xb700,
	0xb800, 0xb900, 0xba00, 0xbb00, 0xbc00, 0xbd00, 0xbe00, 0xbf00,
	0xc000, 0xc100, 0xc200, 0xc300, 0xc400, 0xc500, 0xc600, 0xc700,
	0xc800, 0xc900, 0xca00, 0xcb00, 0xcc00, 0xcd00, 0xce00, 0xcf00,
	0xd000, 0xd100, 0xd200, 0xd300, 0xd400, 0xd500, 0xd600, 0xd700,
	0xd800, 0xd900, 0xda00, 0xdb00, 0xdc00, 0xdd00, 0xde00, 0xdf00,
	0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600, 0xe700,
	0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00, 0xef00,
	0xf000, 0xf100, 0xf200, 0xf300, 0xf400, 0xf500, 0xf600, 0xf700,
	0xf800, 0xf900, 0xfa00, 0xfb00, 0xfc00, 0xfd00, 0xfe00, 0xff00,
	0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700,
	0x0800, 0x0900, 0x0a00, 0x0b00, 0x0c00, 0x0d00, 0x0e00, 0x0f00,
	0x1000, 0x1100, 0x1200
};

/* volmap for LINE/MIC Input Volumes */
static const SINT16	volmap_ain[]	= {
	0xa000, 0xa000, 0xa000, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600,
	0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00,
	0xef00, 0xf000, 0xf100, 0xf200, 0xf300, 0xf400, 0xf500, 0xf600,
	0xf700, 0xf800, 0xf900, 0xfa00, 0xfb00, 0xfc00, 0xfd00, 0xfe00,
	0xff00, 0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600,
	0x0700, 0x0800, 0x0900, 0x0a00, 0x0b00, 0x0c00, 0x0d00, 0x0e00,
	0x0f00, 0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500, 0x1580,
	0x1600, 0x1680, 0x1700, 0x1780, 0x1800, 0x1a00, 0x1c00, 0x1e00
};

/* volmap for Analog Output Volumes */
static const SINT16	volmap_aout[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdc00,
	0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
	0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
	0xed00, 0xee00, 0xef00, 0xf000, 0xf080, 0xf100, 0xf180, 0xf200,
	0xf280, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
	0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
	0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
	0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
	0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000
};

/* volmap for SP Volumes */
static const SINT16	volmap_sp[5][128]	= {
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdc00,
		0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
		0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
		0xed00, 0xee00, 0xef00, 0xf000, 0xf080, 0xf100, 0xf180, 0xf200,
		0xf280, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
		0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
		0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
		0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
		0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
		0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
		0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380, 0x03c0, 0x0400
	},
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdd00,
		0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500,
		0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00,
		0xee00, 0xef00, 0xf000, 0xf100, 0xf180, 0xf200, 0xf280, 0xf300,
		0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600, 0xf680, 0xf700,
		0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00, 0xfa80, 0xfb00,
		0xfb40, 0xfb80, 0xfbc0, 0xfc00, 0xfc40, 0xfc80, 0xfcc0, 0xfd00,
		0xfd40, 0xfd80, 0xfdc0, 0xfe00, 0xfe40, 0xfe80, 0xfec0, 0xff00,
		0xff40, 0xff80, 0xffc0, 0x0000, 0x0040, 0x0080, 0x00c0, 0x0100,
		0x0140, 0x0180, 0x01c0, 0x0200, 0x0240, 0x0280, 0x02c0, 0x0300,
		0x0340, 0x0380, 0x03c0, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
	},
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xde00,
		0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600,
		0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00,
		0xef00, 0xf000, 0xf100, 0xf200, 0xf280, 0xf300, 0xf380, 0xf400,
		0xf480, 0xf500, 0xf580, 0xf600, 0xf680, 0xf700, 0xf780, 0xf800,
		0xf880, 0xf900, 0xf980, 0xfa00, 0xfa80, 0xfb00, 0xfb80, 0xfc00,
		0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
		0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
		0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
		0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380, 0x03c0, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
	},
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdf00,
		0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600, 0xe700,
		0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00, 0xef00,
		0xf000, 0xf100, 0xf200, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500,
		0xf580, 0xf600, 0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900,
		0xf980, 0xfa00, 0xfa80, 0xfb00, 0xfb80, 0xfc00, 0xfc80, 0xfd00,
		0xfd40, 0xfd80, 0xfdc0, 0xfe00, 0xfe40, 0xfe80, 0xfec0, 0xff00,
		0xff40, 0xff80, 0xffc0, 0x0000, 0x0040, 0x0080, 0x00c0, 0x0100,
		0x0140, 0x0180, 0x01c0, 0x0200, 0x0240, 0x0280, 0x02c0, 0x0300,
		0x0340, 0x0380, 0x03c0, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
	},
	{
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
		0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xe000,
		0xe100, 0xe200, 0xe300, 0xe400, 0xe500, 0xe600, 0xe700, 0xe800,
		0xe900, 0xea00, 0xeb00, 0xec00, 0xed00, 0xee00, 0xef00, 0xf000,
		0xf100, 0xf200, 0xf300, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
		0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
		0xfa80, 0xfb00, 0xfb80, 0xfc00, 0xfc80, 0xfd00, 0xfd80, 0xfe00,
		0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
		0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
		0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380, 0x03c0, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
	}
};

/* volmap for LineOut Volumes */
static const SINT16	volmap_lineout[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdc00,
	0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
	0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
	0xed00, 0xee00, 0xef00, 0xf000, 0xf080, 0xf100, 0xf180, 0xf200,
	0xf280, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
	0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
	0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
	0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
	0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
	0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
};

/* volmap for LineOut2 Volumes */
static const SINT16	volmap_lineout2[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xda00,
	0xdc00, 0xdc00, 0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200,
	0xe300, 0xe400, 0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00,
	0xeb00, 0xec00, 0xed00, 0xee00, 0xee00, 0xef00, 0xef00, 0xf000,
	0xf080, 0xf100, 0xf180, 0xf200, 0xf280, 0xf300, 0xf380, 0xf400,
	0xf480, 0xf500, 0xf580, 0xf600, 0xf680, 0xf700, 0xf780, 0xf800,
	0xf800, 0xf880, 0xf880, 0xf900, 0xf900, 0xf980, 0xf980, 0xfa00,
	0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
	0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
	0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
};

/* volmap for HP Output Volumes */
static const SINT16	volmap_hp_es1[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdd00,
	0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500,
	0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00, 0xed00,
	0xee00, 0xef00, 0xf000, 0xf100, 0xf180, 0xf200, 0xf280, 0xf300,
	0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600, 0xf680, 0xf700,
	0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00, 0xfa80, 0xfb00,
	0xfb40, 0xfb80, 0xfbc0, 0xfc00, 0xfc40, 0xfc80, 0xfcc0, 0xfd00,
	0xfd40, 0xfd80, 0xfdc0, 0xfe00, 0xfe40, 0xfe80, 0xfec0, 0xff00,
	0xff40, 0xff80, 0xffc0, 0x0000, 0x0040, 0x0080, 0x00c0, 0x0100,
	0x0140, 0x0180, 0x01c0, 0x0200, 0x0240, 0x0280, 0x02c0, 0x0300,
	0x0340, 0x0380, 0x03c0, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400
};
static const SINT16	volmap_hp[]	= {
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000,
	0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xa000, 0xdc00,
	0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400,
	0xe500, 0xe600, 0xe700, 0xe800, 0xe900, 0xea00, 0xeb00, 0xec00,
	0xed00, 0xee00, 0xef00, 0xf000, 0xf080, 0xf100, 0xf180, 0xf200,
	0xf280, 0xf300, 0xf380, 0xf400, 0xf480, 0xf500, 0xf580, 0xf600,
	0xf680, 0xf700, 0xf780, 0xf800, 0xf880, 0xf900, 0xf980, 0xfa00,
	0xfa40, 0xfa80, 0xfac0, 0xfb00, 0xfb40, 0xfb80, 0xfbc0, 0xfc00,
	0xfc40, 0xfc80, 0xfcc0, 0xfd00, 0xfd40, 0xfd80, 0xfdc0, 0xfe00,
	0xfe40, 0xfe80, 0xfec0, 0xff00, 0xff40, 0xff80, 0xffc0, 0x0000,
	0x0040, 0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200,
	0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380, 0x03c0, 0x0400
};

/* volmap for Adif(ES) Volumes */
static const SINT16	volmap_adif[]	= {
	0xa000, 0xa300, 0xa400, 0xa500, 0xa600, 0xa700, 0xa800, 0xa900,
	0xaa00, 0xab00, 0xac00, 0xad00, 0xae00, 0xaf00, 0xb000, 0xb100,
	0xb200, 0xb300, 0xb400, 0xb500, 0xb600, 0xb700, 0xb800, 0xb900,
	0xba00, 0xbb00, 0xbc00, 0xbd00, 0xbe00, 0xbf00, 0xc000, 0xc100,
	0xc200, 0xc300, 0xc400, 0xc500, 0xc600, 0xc700, 0xc800, 0xc900,
	0xca00, 0xcb00, 0xcc00, 0xcd00, 0xce00, 0xcf00, 0xd000, 0xd100,
	0xd200, 0xd300, 0xd400, 0xd500, 0xd600, 0xd700, 0xd800, 0xd900,
	0xda00, 0xdb00, 0xdc00, 0xdd00, 0xde00, 0xdf00, 0xe000, 0xe100,
	0xe200, 0xe300, 0xe400, 0xe500, 0xe600, 0xe700, 0xe800, 0xe900,
	0xea00, 0xeb00, 0xec00, 0xed00, 0xee00, 0xef00, 0xf000, 0xf100,
	0xf200, 0xf300, 0xf400, 0xf500, 0xf600, 0xf700, 0xf800, 0xf900,
	0xfa00, 0xfb00, 0xfc00, 0xfd00, 0xfe00, 0xff00, 0x0000, 0x0100,
	0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700, 0x0800, 0x0900,
	0x0a00, 0x0b00, 0x0c00, 0x0d00, 0x0e00, 0x0f00, 0x1000, 0x1100,
	0x1200, 0x1200, 0x1200
};

struct mc_asoc_vreg_info {
	size_t	offset;
	const SINT16	*volmap;
	UINT8	channels;
};
static struct mc_asoc_vreg_info	vreg_map[MC_ASOC_N_VOL_REG]	= {
	{offsetof(struct MCDRV_VOL_INFO, aswD_MusicIn),
	 volmap_digital,
	  MUSICIN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_ExtIn),
	 volmap_digital,
	  EXTIN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_VoiceIn),
	 volmap_digital,
	  VOICEIN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_RefIn),
	 volmap_digital,
	  REFIN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Adif0In),
	 volmap_digital,
	  ADIF0IN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Adif1In),
	 volmap_digital,
	  ADIF1IN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Adif2In),
	 volmap_digital,
	  ADIF2IN_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_MusicOut),
	 volmap_digital,
	  MUSICOUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_ExtOut),
	 volmap_digital,
	  EXTOUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_VoiceOut),
	 volmap_digital,
	  VOICEOUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_RefOut),
	 volmap_digital,
	  REFOUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Dac0Out),
	 volmap_digital,
	  DAC0OUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_Dac1Out),
	 volmap_digital,
	  DAC1OUT_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_DpathDa),
	 volmap_digital,
	  DPATH_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswD_DpathAd),
	 volmap_digital,
	  DPATH_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_LineIn1),
	 volmap_ain,
	  LINEIN1_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Mic1),
	 volmap_ain,
	  MIC1_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Mic2),
	 volmap_ain,
	  MIC2_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Mic3),
	 volmap_ain,
	  MIC3_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Mic4),
	 volmap_ain,
	  MIC4_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Hp),
	 volmap_hp,
	  HP_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Sp),
	 volmap_sp[0],
	  SP_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_Rc),
	 volmap_aout,
	  RC_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_LineOut1),
	 volmap_lineout,
	  LINEOUT1_VOL_CHANNELS},
	{offsetof(struct MCDRV_VOL_INFO, aswA_LineOut2),
	 volmap_lineout2,
	  LINEOUT2_VOL_CHANNELS},

	{(size_t)-1,	NULL,		0},
};

static const DECLARE_TLV_DB_SCALE(mc_asoc_tlv_digital, -9600, 100, 1);
static unsigned int	mc_asoc_tlv_ain[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x02,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x03,	0x36,	TLV_DB_SCALE_ITEM(-3000, 100, 0),
	0x37,	0x3B,	TLV_DB_SCALE_ITEM(2150, 50, 0),
	0x3C,	0x3F,	TLV_DB_SCALE_ITEM(2400, 200, 0),
};
static unsigned int	mc_asoc_tlv_aout[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x2E,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x2F,	0x43,	TLV_DB_SCALE_ITEM(-3600, 100, 0),
	0x44,	0x57,	TLV_DB_SCALE_ITEM(-1550, 50, 0),
	0x58,	0x6F,	TLV_DB_SCALE_ITEM(-575, 25, 0),
};
static unsigned int	mc_asoc_tlv_sp[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x2E,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x2F,	0x43,	TLV_DB_SCALE_ITEM(-3600, 100, 0),
	0x44,	0x57,	TLV_DB_SCALE_ITEM(-1550, 50, 0),
	0x58,	0x6F,	TLV_DB_SCALE_ITEM(-575, 25, 0),
};
static unsigned int	mc_asoc_tlv_lout[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x2E,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x2F,	0x43,	TLV_DB_SCALE_ITEM(-3600, 100, 0),
	0x44,	0x57,	TLV_DB_SCALE_ITEM(-1550, 50, 0),
	0x58,	0x77,	TLV_DB_SCALE_ITEM(-575, 25, 0),
};
static unsigned int	mc_asoc_tlv_hp[]	= {
	TLV_DB_RANGE_HEAD(4),
	0x00,	0x2F,	TLV_DB_SCALE_ITEM(-9600, 0, 1),
	0x30,	0x43,	TLV_DB_SCALE_ITEM(-3500, 100, 0),
	0x44,	0x57,	TLV_DB_SCALE_ITEM(-1550, 50, 0),
	0x58,	0x7F,	TLV_DB_SCALE_ITEM(-575, 25, 0),
};

/* SP Gain */
static unsigned int	mc_asoc_tlv_sp_gain[]	= {
	TLV_DB_RANGE_HEAD(1),
	0x00,	0x04,	TLV_DB_SCALE_ITEM(1200, 100, 0)
};

/* Switch Clock */
static const char	* const switch_clock_param_text[] = {
	"CLKA", "CLKB"
};
static const struct soc_enum	switch_clock_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_SWITCH_CLOCK, 0,
		ARRAY_SIZE(switch_clock_param_text), switch_clock_param_text);

/* Adif0 Swap */
static const char	* const swap_param_text[] = {
	"Normal", "Swap", "Mute", "Center", "Mix", "MonoMix", "BothL", "BothR"
};
static const struct soc_enum	adif0_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_ADIF0_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Adif1 Swap */
static const struct soc_enum	adif1_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_ADIF1_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Adif2 Swap */
static const struct soc_enum	adif2_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_ADIF2_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Dac0 Swap */
static const struct soc_enum	dac0_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_DAC0_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Dac1 Swap */
static const struct soc_enum	dac1_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_DAC1_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Music Out0 Swap */
static const struct soc_enum	music_out0_swap_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_OUT0_SWAP, 0,
		ARRAY_SIZE(swap_param_text), swap_param_text);

/* Music In0 Swap */
static const char	* const swap2_param_text[] = {
	"Normal", "Both1", "Both0", "Swap"
};
static const struct soc_enum	music_in0_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_IN0_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Music In1 Swap */
static const struct soc_enum	music_in1_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_IN1_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Music In2 Swap */
static const struct soc_enum	music_in2_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_IN2_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Ext In Swap */
static const struct soc_enum	ext_in_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_IN_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Voice In Swap */
static const struct soc_enum	voice_in_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_IN_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Music Out1 Swap */
static const struct soc_enum	music_out1_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_OUT1_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Music Out2 Swap */
static const struct soc_enum	music_out2_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_MUSIC_OUT2_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Ext Out Swap */
static const struct soc_enum	ext_out_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_EXT_OUT_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

/* Voice Out Swap */
static const struct soc_enum	voice_out_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_VOICE_OUT_SWAP, 0,
		ARRAY_SIZE(swap2_param_text), swap2_param_text);

static const char	* const clear_dsp_prm_param_text[] = {
	"OFF", "ON"
};
static const struct soc_enum	clear_dsp_prm_param_enum =
	SOC_ENUM_SINGLE(MC_ASOC_CLEAR_DSP_PARAM, 0,
		ARRAY_SIZE(clear_dsp_prm_param_text), clear_dsp_prm_param_text);


static const struct snd_kcontrol_new	mc_asoc_snd_controls[]	= {
	SOC_DOUBLE_TLV("Music Input Volume",
		MC_ASOC_DVOL_MUSICIN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Music Input Switch",
		MC_ASOC_DVOL_MUSICIN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Ext Input Volume",
		MC_ASOC_DVOL_EXTIN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Ext Input Switch",
		MC_ASOC_DVOL_EXTIN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Voice Input Volume",
		MC_ASOC_DVOL_VOICEIN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Voice Input Switch",
		MC_ASOC_DVOL_VOICEIN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Ref Input Volume",
		MC_ASOC_DVOL_REFIN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Ref Input Switch",
		MC_ASOC_DVOL_REFIN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Adif0 Input Volume",
		MC_ASOC_DVOL_ADIF0IN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Adif0 Input Switch",
		MC_ASOC_DVOL_ADIF0IN, 7, 15, 1, 0),
	SOC_DOUBLE_TLV("Adif1 Input Volume",
		MC_ASOC_DVOL_ADIF1IN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Adif1 Input Switch",
		MC_ASOC_DVOL_ADIF1IN, 7, 15, 1, 0),
	SOC_DOUBLE_TLV("Adif2 Input Volume",
		MC_ASOC_DVOL_ADIF2IN, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Adif2 Input Switch",
		MC_ASOC_DVOL_ADIF2IN, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Music Output Volume",
		MC_ASOC_DVOL_MUSICOUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Music Output Switch",
		MC_ASOC_DVOL_MUSICOUT, 7, 15, 1, 0),
	SOC_DOUBLE_TLV("Ext Output Volume",
		MC_ASOC_DVOL_EXTOUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Ext Output Switch",
		MC_ASOC_DVOL_EXTOUT, 7, 15, 1, 0),
	SOC_DOUBLE_TLV("Voice Output Volume",
		MC_ASOC_DVOL_VOICEOUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Voice Output Switch",
		MC_ASOC_DVOL_VOICEOUT, 7, 15, 1, 0),
	SOC_DOUBLE_TLV("Ref Output Volume",
		MC_ASOC_DVOL_REFOUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Ref Output Switch",
		MC_ASOC_DVOL_REFOUT, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Dac0 Output Volume",
		MC_ASOC_DVOL_DAC0OUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Dac0 Output Switch",
		MC_ASOC_DVOL_DAC0OUT, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Dac1 Output Volume",
		MC_ASOC_DVOL_DAC1OUT, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Dac1 Output Switch",
		MC_ASOC_DVOL_DAC1OUT, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Dpath Da Volume",
		MC_ASOC_DVOL_DPATHDA, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Dpath Da Switch",
		MC_ASOC_DVOL_DPATHDA, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Dpath Ad Volume",
		MC_ASOC_DVOL_DPATHAD, 0, 8, 114, 0, mc_asoc_tlv_digital),
	SOC_DOUBLE("Dpath Ad Switch",
		MC_ASOC_DVOL_DPATHAD, 7, 15, 1, 0),


	SOC_DOUBLE_TLV("LineIn1 Volume",
		MC_ASOC_AVOL_LINEIN1, 0, 8, 63, 0, mc_asoc_tlv_ain),
	SOC_DOUBLE("LineIn1 Switch",
		MC_ASOC_AVOL_LINEIN1, 7, 15, 1, 0),

	SOC_SINGLE_TLV("Mic1 Volume",
		MC_ASOC_AVOL_MIC1, 0, 63, 0, mc_asoc_tlv_ain),
	SOC_SINGLE("Mic1 Switch",
		MC_ASOC_AVOL_MIC1, 7, 1, 0),

	SOC_SINGLE_TLV("Mic2 Volume",
		MC_ASOC_AVOL_MIC2, 0, 63, 0, mc_asoc_tlv_ain),
	SOC_SINGLE("Mic2 Switch",
		MC_ASOC_AVOL_MIC2, 7, 1, 0),

	SOC_SINGLE_TLV("Mic3 Volume",
		MC_ASOC_AVOL_MIC3, 0, 63, 0, mc_asoc_tlv_ain),
	SOC_SINGLE("Mic3 Switch",
		MC_ASOC_AVOL_MIC3, 7, 1, 0),

	SOC_SINGLE_TLV("Mic4 Volume",
		MC_ASOC_AVOL_MIC4, 0, 63, 0, mc_asoc_tlv_ain),
	SOC_SINGLE("Mic4 Switch",
		MC_ASOC_AVOL_MIC4, 7, 1, 0),

	SOC_DOUBLE_TLV("Headphone Volume",
		MC_ASOC_AVOL_HP, 0, 8, 127, 0, mc_asoc_tlv_hp),
	SOC_DOUBLE("Headphone Switch",
		MC_ASOC_AVOL_HP, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("Speaker Volume",
		MC_ASOC_AVOL_SP, 0, 8, 127, 0, mc_asoc_tlv_sp),
	SOC_DOUBLE("Speaker Switch",
		MC_ASOC_AVOL_SP, 7, 15, 1, 0),

	SOC_SINGLE_TLV("Receiver Volume",
		MC_ASOC_AVOL_RC, 0, 111, 0, mc_asoc_tlv_aout),
	SOC_SINGLE("Receiver Switch",
		MC_ASOC_AVOL_RC, 7, 1, 0),

	SOC_DOUBLE_TLV("LineOut1 Volume",
		MC_ASOC_AVOL_LINEOUT1, 0, 8, 119, 0, mc_asoc_tlv_lout),
	SOC_DOUBLE("LineOut1 Switch",
		MC_ASOC_AVOL_LINEOUT1, 7, 15, 1, 0),

	SOC_DOUBLE_TLV("LineOut2 Volume",
		MC_ASOC_AVOL_LINEOUT2, 0, 8, 119, 0, mc_asoc_tlv_lout),
	SOC_DOUBLE("LineOut2 Switch",
		MC_ASOC_AVOL_LINEOUT2, 7, 15, 1, 0),

	SOC_SINGLE_TLV("SP Gain",
		MC_ASOC_AVOL_SP_GAIN, 0, 4, 0, mc_asoc_tlv_sp_gain),

	SOC_ENUM("Switch Clock", switch_clock_param_enum),
	SOC_ENUM("Adif0 Swap", adif0_swap_param_enum),
	SOC_ENUM("Adif1 Swap", adif1_swap_param_enum),
	SOC_ENUM("Adif2 Swap", adif2_swap_param_enum),
	SOC_ENUM("Dac0 Swap", dac0_swap_param_enum),
	SOC_ENUM("Dac1 Swap", dac1_swap_param_enum),
	SOC_ENUM("Music Out0 Swap", music_out0_swap_param_enum),
	SOC_ENUM("Music In0 Swap", music_in0_param_enum),
	SOC_ENUM("Music In1 Swap", music_in1_param_enum),
	SOC_ENUM("Music In2 Swap", music_in2_param_enum),
	SOC_ENUM("Ext In Swap", ext_in_param_enum),
	SOC_ENUM("Voice In Swap", voice_in_param_enum),
	SOC_ENUM("Music Out1 Swap", music_out1_param_enum),
	SOC_ENUM("Music Out2 Swap", music_out2_param_enum),
	SOC_ENUM("Ext Out Swap", ext_out_param_enum),
	SOC_ENUM("Voice Out Swap", voice_out_param_enum),

	SOC_ENUM("Clear Dsp Parameter", clear_dsp_prm_param_enum),


};

struct snd_soc_component		*mc_asoc_codec;
static struct spi_device	*mc_asoc_spi;

struct device *mc_asoc_get_dev(void)
{
	return mc_asoc_spi ? &mc_asoc_spi->dev : NULL;
}

static UINT8	mc_asoc_ver_id		= 1;
static UINT8	mc_asoc_suspended;
static UINT8	mc_asoc_hpimpclass	= (UINT8)-1;
static UINT8	mc_asoc_jack_status;
static UINT8	mc_asoc_irq_func;


static UINT8	mc_asoc_MBSEL4		= 0x80;

static struct mutex	mc_asoc_mutex;
static struct mutex	hsdet_mutex;

static struct mc_asoc_data *mc_asoc_get_mc_asoc(
			const struct snd_soc_component *codec);
static int	map_drv_error(int err);
static int	set_bias_level(struct snd_soc_component *codec,
			       enum snd_soc_bias_level level);
/*
 * Function
 */
UINT8 mc_asoc_get_bus_select(void)
{
	return BUS_SELECT;
}

/*
 * Called by the core when it powers the PLL up or down, i.e. exactly when
 * the codec starts and stops needing its reference clock.
 */
void mc_asoc_enable_clock(int enable)
{
	struct mc_asoc_data	*mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);
	int	err;

	if (!mc_asoc || mc_asoc->mclk_enabled == !!enable)
		return;

	if (enable) {
		err = clk_prepare_enable(mc_asoc->mclk);
		if (err) {
			dev_err(mc_asoc_get_dev(), "failed to enable MCLK: %d\n",
				err);
			return;
		}
	} else {
		clk_disable_unprepare(mc_asoc->mclk);
	}
	mc_asoc->mclk_enabled	= !!enable;
}

static void mc_asoc_lock(char *fn)
{
	mutex_lock(&mc_asoc_mutex);
}

static void mc_asoc_unlock(char *fn)
{
	mutex_unlock(&mc_asoc_mutex);
}

static struct mc_asoc_data *mc_asoc_get_mc_asoc(
	const struct snd_soc_component *codec)
{
	struct mc_asoc_priv	*priv;

	if (codec == NULL)
		return NULL;

	priv	= snd_soc_component_get_drvdata((struct snd_soc_component *)codec);
	if (priv != NULL)
		return &priv->data;

	return NULL;
}

/* deliver i2c access to machdep */
static int map_drv_error(int err)
{
	switch (err) {
	case MCDRV_SUCCESS:
		return 0;
	case MCDRV_ERROR_ARGUMENT:
		return -EINVAL;
	case MCDRV_ERROR_STATE:
		return -EBUSY;
	case MCDRV_ERROR_TIMEOUT:
		return -EIO;
	default:
		/* internal error */
		return -EIO;
	}
}


static int mc_asoc_reg_cache[MC_ASOC_N_REG];

static int read_cache(
	struct snd_soc_component *codec,
	unsigned int reg)
{
	int *cache = mc_asoc_reg_cache;

	if (reg >= MC_ASOC_N_REG)
		return -EIO;

	return cache[reg];
}

static int write_cache(
	struct snd_soc_component *codec,
	unsigned int reg,
	unsigned int value)
{
	int *cache = mc_asoc_reg_cache;

	if (reg >= MC_ASOC_N_REG)
		return -EIO;
	cache[reg] = value;

	return 0;
}

#define	DSP_MEM_SIZE	(200000)
static int	dsp_mem_pt;
static UINT8	dsp_mem[DSP_MEM_SIZE];

static UINT8 *get_dsp_mem(int size)
{
	UINT8	*p	= NULL;
	if ((dsp_mem_pt+size) < DSP_MEM_SIZE) {
		p	= dsp_mem + dsp_mem_pt;
		dsp_mem_pt	+= size;
		dbg_info("dsp_mem_pt:%d\n", dsp_mem_pt);
	} else {
		dev_err_ratelimited(mc_asoc_get_dev(),
				    "out of DSP parameter memory\n");
	}
	return p;
}

#include <linux/firmware.h>
#include <linux/unaligned.h>

static int set_vol_info(struct snd_soc_component *codec,
			struct MCDRV_VOL_INFO *vol_info, int reg)
{
	SINT16 *vp, db, db_min, db_max;
	unsigned int v;
	int i, vol, cache;

	if (!codec || reg >= MC_ASOC_AVOL_SP_GAIN)
		return -EIO;

	if (vreg_map[reg].offset == (size_t)-1)
		return 0;

	vp = (void *)vol_info + vreg_map[reg].offset;
	cache = read_cache(codec, reg);
	if (cache < 0)
		return -EIO;

	for (i = 0; i < vreg_map[reg].channels; i++, vp++) {
		v = (cache >> (i * 8)) & 0xff;
		vol = (v & 0x80) ? (v & 0x7f) : 0;
		db = vreg_map[reg].volmap[vol];

		/* compensate for the measured headphone impedance */
		if (mc_asoc_hpimpclass != (UINT8)-1 &&
		    db > vreg_map[reg].volmap[0]) {
			if (reg == MC_ASOC_AVOL_HP) {
				db_min = vreg_map[reg].volmap[0];
				db_max = vreg_map[reg].volmap[ARRAY_SIZE(volmap_hp) - 1];
				db += aswHpVolImpTable[mc_asoc_hpimpclass] << 8;
				db = clamp(db, db_min, db_max);
			} else if (reg == MC_ASOC_DVOL_DAC0OUT) {
				db_min = volmap_digital[0];
				db_max = volmap_digital[ARRAY_SIZE(volmap_digital) - 1];
				db += aswDac0VolImpTable[mc_asoc_hpimpclass] << 8;
				db = clamp(db, db_min, db_max);
			}
		}

		*vp = db | MCDRV_VOL_UPDATE;
	}

	return 0;
}
static void set_BIAS(
	struct MCDRV_PATH_INFO	*path_info
)
{
	struct mc_asoc_data	*mc_asoc	= NULL;
	int	err;
	struct MCDRV_HSDET_INFO	stHSDetInfo;
	struct MCDRV_REG_INFO	reg_info;
	UINT8	mbsel4;

	if (mc_asoc_jack_status == SND_JACK_HEADPHONE) {
		err	= _McDrv_Ctrl(MCDRV_GET_HSDET,
					(void *)&stHSDetInfo, NULL, 0);
		if ((err == MCDRV_SUCCESS)
		&& (stHSDetInfo.bEnMicDet == MCDRV_MICDET_ENABLE)) {
			;
			path_info->asBias[3].dSrcOnOff	|=
						MCDRV_ASRC_MIC4_ON;
		}
	} else if (mc_asoc_jack_status == SND_JACK_HEADSET) {
		;
		path_info->asBias[3].dSrcOnOff	|= MCDRV_ASRC_MIC4_ON;
	}

	if (((path_info->asHp[0].dSrcOnOff & MCDRV_ASRC_DAC0_L_ON) != 0)
	|| ((path_info->asHp[1].dSrcOnOff & MCDRV_ASRC_DAC0_R_ON) != 0)
	|| ((path_info->asAdc0[0].dSrcOnOff & MCDRV_ASRC_MIC4_ON) != 0)
	|| ((path_info->asAdc0[1].dSrcOnOff & MCDRV_ASRC_MIC4_ON) != 0)
	|| ((path_info->asAdc1[0].dSrcOnOff & MCDRV_ASRC_MIC4_ON) != 0))
		mbsel4	= 0x80;
	else
		mbsel4	= mc_asoc_MBSEL4;

	mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);
	/* each access costs a round trip through the core, skip it if we can */
	if (mbsel4 != mc_asoc->mbsel4) {
		reg_info.bRegType = MCDRV_REGTYPE_ANA;
		reg_info.bAddress = 13;
		err	= _McDrv_Ctrl(MCDRV_READ_REG, (void *)&reg_info, NULL,
					0);
		if (err == MCDRV_SUCCESS) {
			reg_info.bData &= 0x3F;
			reg_info.bData |= mbsel4;
			err	= _McDrv_Ctrl(MCDRV_WRITE_REG,
						(void *)&reg_info, NULL, 0);
		}
		if (err != MCDRV_SUCCESS) {
			dbg_info("%d: Error in %s\n", err, __func__);
			mc_asoc->mbsel4	= -1;
		} else {
			mc_asoc->mbsel4	= mbsel4;
		}
	}

	if (mc_asoc->mic1_bias) {
		bool mic1_on;

		mic1_on = (path_info->asAdc0[0].dSrcOnOff & MCDRV_ASRC_MIC1_ON) ||
			  (path_info->asAdc0[1].dSrcOnOff & MCDRV_ASRC_MIC1_ON) ||
			  (path_info->asAdc1[0].dSrcOnOff & MCDRV_ASRC_MIC1_ON);
		if (mic1_on != mc_asoc->mic1_bias_enabled) {
			if (mic1_on)
				err = regulator_enable(mc_asoc->mic1_bias);
			else
				err = regulator_disable(mc_asoc->mic1_bias);
			if (err)
				dev_err(mc_asoc_get_dev(),
					"failed to switch MIC1 bias: %d\n", err);
			else
				mc_asoc->mic1_bias_enabled = mic1_on;
		}
	}
}

/*
 * DAI (PCM interface)
 */
#define YMU831_COM_UPDATE_FLAGS	(MCDRV_MUSIC_COM_UPDATE_FLAG |	\
				 MCDRV_EXT_COM_UPDATE_FLAG |	\
				 MCDRV_VOICE_COM_UPDATE_FLAG |	\
				 MCDRV_HIFI_COM_UPDATE_FLAG)
#define YMU831_DIR_UPDATE_FLAGS	(MCDRV_MUSIC_DIR_UPDATE_FLAG |	\
				 MCDRV_EXT_DIR_UPDATE_FLAG |	\
				 MCDRV_VOICE_DIR_UPDATE_FLAG |	\
				 MCDRV_HIFI_DIR_UPDATE_FLAG)
#define YMU831_DIT_UPDATE_FLAGS	(MCDRV_MUSIC_DIT_UPDATE_FLAG |	\
				 MCDRV_EXT_DIT_UPDATE_FLAG |	\
				 MCDRV_VOICE_DIT_UPDATE_FLAG |	\
				 MCDRV_HIFI_DIT_UPDATE_FLAG)

static int is_dio_modified(
	const struct MCDRV_DIO_PORT	*port,
	int	id,
	int	mode,
	UINT32	update
)
{
	int	err;
	struct MCDRV_DIO_INFO	cur_dio;

	err	= _McDrv_Ctrl(MCDRV_GET_DIGITALIO, &cur_dio, NULL, 0);
	if (err != MCDRV_SUCCESS)
		return map_drv_error(err);

	if (update & YMU831_COM_UPDATE_FLAGS) {
		if ((cur_dio.asPortInfo[id].sDioCommon.bMasterSlave
			!= port->sDioCommon.bMasterSlave)
		|| (cur_dio.asPortInfo[id].sDioCommon.bAutoFs
			!= port->sDioCommon.bAutoFs)
		|| (cur_dio.asPortInfo[id].sDioCommon.bFs
			!= port->sDioCommon.bFs)
		|| (cur_dio.asPortInfo[id].sDioCommon.bBckFs
			!= port->sDioCommon.bBckFs)
		|| (cur_dio.asPortInfo[id].sDioCommon.bInterface
			!= port->sDioCommon.bInterface)
		|| (cur_dio.asPortInfo[id].sDioCommon.bBckInvert
			!= port->sDioCommon.bBckInvert)
		|| (cur_dio.asPortInfo[id].sDioCommon.bSrcThru
			!= port->sDioCommon.bSrcThru))
			return 1;
		if (mode == MCDRV_DIO_PCM) {
			if ((cur_dio.asPortInfo[id].sDioCommon.bPcmHizTim
				!= port->sDioCommon.bPcmHizTim)
			|| (cur_dio.asPortInfo[id].sDioCommon.bPcmFrame
				!= port->sDioCommon.bPcmFrame)
			|| (cur_dio.asPortInfo[id].sDioCommon.bPcmHighPeriod
				!= port->sDioCommon.bPcmHighPeriod))
				return 1;
		}
	}

	if (update & YMU831_DIR_UPDATE_FLAGS) {
		if (mode == MCDRV_DIO_DA) {
			if ((cur_dio.asPortInfo[id].sDir.sDaFormat.bBitSel
				!= port->sDir.sDaFormat.bBitSel)
			|| (cur_dio.asPortInfo[id].sDir.sDaFormat.bMode
				!= port->sDir.sDaFormat.bMode))
				return 1;
		} else {
			if ((cur_dio.asPortInfo[id].sDir.sPcmFormat.bMono
				!= port->sDir.sPcmFormat.bMono)
			|| (cur_dio.asPortInfo[id].sDir.sPcmFormat.bOrder
				!= port->sDir.sPcmFormat.bOrder)
			|| (cur_dio.asPortInfo[id].sDir.sPcmFormat.bLaw
				!= port->sDir.sPcmFormat.bLaw)
			|| (cur_dio.asPortInfo[id].sDir.sPcmFormat.bBitSel
				!= port->sDir.sPcmFormat.bBitSel))
				return 1;
		}
	}

	if (update & YMU831_DIT_UPDATE_FLAGS) {
		if (mode == MCDRV_DIO_DA) {
			if ((cur_dio.asPortInfo[id].sDit.sDaFormat.bBitSel
				!= port->sDit.sDaFormat.bBitSel)
			|| (cur_dio.asPortInfo[id].sDit.sDaFormat.bMode
				!= port->sDit.sDaFormat.bMode))
				return 1;
		} else {
			if ((cur_dio.asPortInfo[id].sDit.sPcmFormat.bMono
				!= port->sDit.sPcmFormat.bMono)
			|| (cur_dio.asPortInfo[id].sDit.sPcmFormat.bOrder
				!= port->sDit.sPcmFormat.bOrder)
			|| (cur_dio.asPortInfo[id].sDit.sPcmFormat.bLaw
				!= port->sDit.sPcmFormat.bLaw)
			|| (cur_dio.asPortInfo[id].sDit.sPcmFormat.bBitSel
				!= port->sDit.sPcmFormat.bBitSel))
				return 1;
		}
	}
	return 0;
}

/* The core's digital ports, the HiFi port carries the music DAI at high rates */
enum {
	YMU831_PORT_MUSIC,
	YMU831_PORT_EXT,
	YMU831_PORT_VOICE,
	YMU831_PORT_HIFI,
};

static const struct {
	u32 com, dir, dit;
} ymu831_port_update[] = {
	[YMU831_PORT_MUSIC] = { MCDRV_MUSIC_COM_UPDATE_FLAG,
				MCDRV_MUSIC_DIR_UPDATE_FLAG,
				MCDRV_MUSIC_DIT_UPDATE_FLAG },
	[YMU831_PORT_EXT] = { MCDRV_EXT_COM_UPDATE_FLAG,
			      MCDRV_EXT_DIR_UPDATE_FLAG,
			      MCDRV_EXT_DIT_UPDATE_FLAG },
	[YMU831_PORT_VOICE] = { MCDRV_VOICE_COM_UPDATE_FLAG,
				MCDRV_VOICE_DIR_UPDATE_FLAG,
				MCDRV_VOICE_DIT_UPDATE_FLAG },
	[YMU831_PORT_HIFI] = { MCDRV_HIFI_COM_UPDATE_FLAG,
			       MCDRV_HIFI_DIR_UPDATE_FLAG,
			       MCDRV_HIFI_DIT_UPDATE_FLAG },
};

#define YMU831_SRC_ALL_OFF	0x00aaaaaa

/* the board's port configuration, indexed by DAI */
static const struct MCDRV_DIO_PORT * const mc_asoc_port_defaults[] = {
	[YMU831_PORT_MUSIC] = &stMusicPort_Default,
	[YMU831_PORT_EXT] = &stExtPort_Default,
	[YMU831_PORT_VOICE] = &stVoicePort_Default,
};

static void ymu831_init_ports(struct mc_asoc_data *mc_asoc)
{
	const struct MCDRV_DIO_PORT *def;
	struct mc_asoc_port_params *port;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mc_asoc->port); i++) {
		def = mc_asoc_port_defaults[i];
		port = &mc_asoc->port[i];

		port->master = def->sDioCommon.bMasterSlave;
		port->rate = def->sDioCommon.bFs;
		port->bckfs = def->sDioCommon.bBckFs;
		port->interface = def->sDioCommon.bInterface;
		port->inv = def->sDioCommon.bBckInvert;
		port->srcthru = def->sDioCommon.bSrcThru;
		if (port->interface == MCDRV_DIO_DA)
			port->format = def->sDir.sDaFormat.bMode;
		else
			port->format = def->sDioCommon.bPcmFrame;
	}
}

static void ymu831_mask_src(struct MCDRV_D1_CHANNEL *ch, int n, u32 on, u32 off)
{
	for (; n > 0; n--, ch++)
		ch->dSrcOnOff = (ch->dSrcOnOff & ~on) | off;
}

/*
 * Take the routes from and to a port down, the receive side when setting
 * up playback and the transmit side when setting up capture.
 */
static void ymu831_take_down_port(struct MCDRV_PATH_INFO *path, int id,
				  bool rx, bool tx)
{
	static const u32 d1_src[][2] = {
		[YMU831_PORT_MUSIC] = { MCDRV_D1SRC_MUSICIN_ON,
					MCDRV_D1SRC_MUSICIN_OFF },
		[YMU831_PORT_EXT] = { MCDRV_D1SRC_EXTIN_ON,
				      MCDRV_D1SRC_EXTIN_OFF },
		[YMU831_PORT_HIFI] = { MCDRV_D1SRC_HIFIIN_ON,
				       MCDRV_D1SRC_HIFIIN_OFF },
	};
	struct MCDRV_D1_CHANNEL *d1[] = {
		path->asMusicOut, path->asExtOut, path->asHifiOut,
		path->asVboxMixIn, path->asAe0, path->asAe1, path->asAe2,
		path->asAe3, path->asDac0, path->asDac1,
	};
	static const int d1_n[] = {
		MUSICOUT_PATH_CHANNELS, EXTOUT_PATH_CHANNELS,
		HIFIOUT_PATH_CHANNELS, VBOXMIXIN_PATH_CHANNELS,
		AE_PATH_CHANNELS, AE_PATH_CHANNELS, AE_PATH_CHANNELS,
		AE_PATH_CHANNELS, DAC0_PATH_CHANNELS, DAC1_PATH_CHANNELS,
	};
	struct MCDRV_D2_CHANNEL *d2[] = {
		path->asVoiceOut, path->asVboxIoIn, path->asVboxHostIn,
		path->asHostOut,
	};
	unsigned int i;

	if (rx) {
		if (id == YMU831_PORT_VOICE) {
			for (i = 0; i < ARRAY_SIZE(d2); i++)
				ymu831_mask_src((struct MCDRV_D1_CHANNEL *)d2[i],
						1, MCDRV_D2SRC_VOICEIN_ON,
						MCDRV_D2SRC_VOICEIN_OFF);
		} else {
			for (i = 0; i < ARRAY_SIZE(d1); i++)
				ymu831_mask_src(d1[i], d1_n[i], d1_src[id][0],
						d1_src[id][1]);
		}
	}

	if (tx) {
		switch (id) {
		case YMU831_PORT_MUSIC:
			ymu831_mask_src(path->asMusicOut, MUSICOUT_PATH_CHANNELS,
					~0, YMU831_SRC_ALL_OFF);
			break;
		case YMU831_PORT_EXT:
			ymu831_mask_src(path->asExtOut, EXTOUT_PATH_CHANNELS,
					~0, YMU831_SRC_ALL_OFF);
			break;
		case YMU831_PORT_VOICE:
			path->asVoiceOut[0].dSrcOnOff = YMU831_SRC_ALL_OFF;
			break;
		case YMU831_PORT_HIFI:
			ymu831_mask_src(path->asHifiOut, HIFIOUT_PATH_CHANNELS,
					~0, YMU831_SRC_ALL_OFF);
			break;
		}
	}
}

static int setup_dai(struct snd_soc_component *codec, int id,
		     struct mc_asoc_port_params *port_prm, int dir)
{
	struct MCDRV_DIO_INFO	dio;
	struct MCDRV_DIO_PORT	*port	= &dio.asPortInfo[id];
	struct MCDRV_PATH_INFO	path_info, tmp_path_info;
	int	mode	= port_prm->interface;
	UINT32	update	= 0;
	int	err, modify;

	err	= _McDrv_Ctrl(MCDRV_GET_PATH, &path_info, NULL, 0);
	if (err != MCDRV_SUCCESS)
		return map_drv_error(err);

	memset(&dio, 0, sizeof(struct MCDRV_DIO_INFO));

	if (port_prm->stream == 0) {
		port->sDioCommon.bMasterSlave	= port_prm->master;
		port->sDioCommon.bAutoFs	= MCDRV_AUTOFS_OFF;
		port->sDioCommon.bFs		= port_prm->rate;
		port->sDioCommon.bBckFs		= port_prm->bckfs;
		port->sDioCommon.bInterface	= mode;
		port->sDioCommon.bBckInvert	= port_prm->inv;
		port->sDioCommon.bSrcThru	= port_prm->srcthru;
		if (mode == MCDRV_DIO_PCM)
			port->sDioCommon.bPcmFrame	= port_prm->format;
		update |= ymu831_port_update[id].com;
	}

	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		if (mode == MCDRV_DIO_DA) {
			port->sDir.sDaFormat.bBitSel	= port_prm->bits[dir];
			port->sDir.sDaFormat.bMode	= port_prm->format;
		} else {
			port->sDir.sPcmFormat.bMono	= port_prm->pcm_mono[dir];
			port->sDir.sPcmFormat.bOrder	= port_prm->pcm_order[dir];
			port->sDir.sPcmFormat.bLaw	= port_prm->pcm_law[dir];
			port->sDir.sPcmFormat.bBitSel	= port_prm->bits[dir];
		}
		update |= ymu831_port_update[id].dir;
	} else {
		if (mode == MCDRV_DIO_DA) {
			port->sDit.sDaFormat.bBitSel	= port_prm->bits[dir];
			port->sDit.sDaFormat.bMode	= port_prm->format;
		} else {
			port->sDit.sPcmFormat.bMono	= port_prm->pcm_mono[dir];
			port->sDit.sPcmFormat.bOrder	= port_prm->pcm_order[dir];
			port->sDit.sPcmFormat.bLaw	= port_prm->pcm_law[dir];
			port->sDit.sPcmFormat.bBitSel	= port_prm->bits[dir];
		}
		update |= ymu831_port_update[id].dit;
	}

	modify	= is_dio_modified(port, id, mode, update);
	if (modify < 0)
		return -EIO;
	if (modify == 0)
		return 0;

	tmp_path_info = path_info;
	ymu831_take_down_port(&tmp_path_info, id,
			      dir == SNDRV_PCM_STREAM_PLAYBACK || !port_prm->stream,
			      dir == SNDRV_PCM_STREAM_CAPTURE || !port_prm->stream);

	if (!memcmp(&tmp_path_info, &path_info, sizeof(path_info))) {
		modify	= 0;
	} else {
		err	= _McDrv_Ctrl(MCDRV_SET_PATH, &tmp_path_info, NULL, 0);
		if (err != MCDRV_SUCCESS)
			return map_drv_error(err);
	}

	err	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &dio, NULL, update);
	if (err != MCDRV_SUCCESS)
		return map_drv_error(err);

	/* restore the routes taken down for the reconfiguration */
	if (modify != 0) {
		err	= _McDrv_Ctrl(MCDRV_SET_PATH, &path_info, NULL, 0);
		if (err != MCDRV_SUCCESS)
			return map_drv_error(err);
	}
	return 0;
}

static struct mc_asoc_port_params *mc_asoc_dai_port(struct snd_soc_dai *dai)
{
	struct mc_asoc_data	*mc_asoc	= mc_asoc_get_mc_asoc(dai->component);

	return &mc_asoc->port[dai->id];
}

static int mc_asoc_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mc_asoc_port_params *port = mc_asoc_dai_port(dai);
	UINT8 interface, format, master, inv;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		interface = MCDRV_DIO_DA;
		format = MCDRV_DAMODE_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		interface = MCDRV_DIO_DA;
		format = MCDRV_DAMODE_TAILALIGN;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		interface = MCDRV_DIO_DA;
		format = MCDRV_DAMODE_HEADALIGN;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		interface = MCDRV_DIO_PCM;
		format = MCDRV_PCM_SHORTFRAME;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		interface = MCDRV_DIO_PCM;
		format = MCDRV_PCM_LONGFRAME;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		master = MCDRV_DIO_MASTER;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		master = MCDRV_DIO_SLAVE;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		inv = MCDRV_BCLK_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		inv = MCDRV_BCLK_INVERT;
		break;
	default:
		return -EINVAL;
	}

	mc_asoc_lock("mc_asoc_set_fmt");
	port->interface = interface;
	port->format = format;
	port->master = master;
	port->inv = inv;
	mc_asoc_unlock("mc_asoc_set_fmt");

	return 0;
}

static int ymu831_bckfs(unsigned int ratio)
{
	switch (ratio) {
	case 16:
		return MCDRV_BCKFS_16;
	case 24:
		return MCDRV_BCKFS_24;
	case 32:
		return MCDRV_BCKFS_32;
	case 48:
		return MCDRV_BCKFS_48;
	case 64:
		return MCDRV_BCKFS_64;
	case 96:
		return MCDRV_BCKFS_96;
	case 128:
		return MCDRV_BCKFS_128;
	case 192:
		return MCDRV_BCKFS_192;
	case 256:
		return MCDRV_BCKFS_256;
	case 512:
		return MCDRV_BCKFS_512;
	default:
		return -EINVAL;
	}
}

static int mc_asoc_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct mc_asoc_port_params *port = mc_asoc_dai_port(dai);

	if (ratio && ymu831_bckfs(ratio) < 0)
		return -EINVAL;

	mc_asoc_lock("mc_asoc_set_bclk_ratio");
	port->bclk_ratio = ratio;
	mc_asoc_unlock("mc_asoc_set_bclk_ratio");

	return 0;
}


static int mc_asoc_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *codec = dai->component;
	struct mc_asoc_port_params *port = mc_asoc_dai_port(dai);
	struct MCDRV_DIOPATH_INFO sDioPathInfo;
	int dir = substream->stream;
	unsigned int channels = params_channels(params);
	unsigned int ratio;
	int id = dai->id;
	int rate, bckfs;
	int err = 0;

	switch (params_rate(params)) {
	case 8000:
		rate = MCDRV_FS_8000;
		break;
	case 11025:
		rate = MCDRV_FS_11025;
		break;
	case 16000:
		rate = MCDRV_FS_16000;
		break;
	case 22050:
		rate = MCDRV_FS_22050;
		break;
	case 32000:
		rate = MCDRV_FS_32000;
		break;
	case 44100:
		rate = MCDRV_FS_44100;
		break;
	case 48000:
		rate = MCDRV_FS_48000;
		break;
	case 96000:
		rate = MCDRV_FS_96000;
		break;
	case 192000:
		rate = MCDRV_FS_192000;
		break;
	default:
		return -EINVAL;
	}

	/* high rates are only available through the HiFi port */
	if (rate == MCDRV_FS_96000 || rate == MCDRV_FS_192000) {
		if (id != YMU831_PORT_MUSIC)
			return -EINVAL;
		id = YMU831_PORT_HIFI;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		port->bits[dir] = MCDRV_BITSEL_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		port->bits[dir] = MCDRV_BITSEL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		port->bits[dir] = MCDRV_BITSEL_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		port->bits[dir] = MCDRV_BITSEL_32;
		break;
	default:
		return -EINVAL;
	}

	port->pcm_mono[dir] = channels == 1 ? MCDRV_PCM_MONO : MCDRV_PCM_STEREO;
	switch (channels) {
	case 1:
	case 2:
		port->channels = MCDRV_MUSIC_2CH;
		break;
	case 4:
		port->channels = MCDRV_MUSIC_4CH;
		break;
	case 6:
		port->channels = MCDRV_MUSIC_6CH;
		break;
	default:
		return -EINVAL;
	}

	/* ALSA PCM samples are linear and sent MSB first */
	port->pcm_order[dir] = MCDRV_PCM_MSB_FIRST;
	port->pcm_law[dir] = MCDRV_PCM_LINEAR;

	/* bit clock as clock provider: two slots for I2S like formats */
	ratio = port->bclk_ratio;
	if (!ratio)
		ratio = params_physical_width(params) *
			(port->interface == MCDRV_DIO_DA ? 2 : channels);
	bckfs = ymu831_bckfs(ratio);
	if (bckfs < 0) {
		dev_err(codec->dev, "unsupported bit clock ratio %u\n", ratio);
		return bckfs;
	}

	mc_asoc_lock("mc_asoc_hw_params");

	if ((port->stream & ~(1 << dir)) &&
	    (rate != port->rate || bckfs != port->bckfs)) {
		err = -EBUSY;
		goto out;
	}

	port->rate = rate;
	port->bckfs = bckfs;

	if (id == YMU831_PORT_MUSIC) {
		sDioPathInfo.bMusicCh = port->channels;
		err = _McDrv_Ctrl(MCDRV_SET_DIGITALIO_PATH, &sDioPathInfo,
				  NULL, MCDRV_MUSICNUM_UPDATE_FLAG);
		if (err != MCDRV_SUCCESS) {
			dev_err(codec->dev, "failed to set channels: %d\n", err);
			err = map_drv_error(err);
			goto out;
		}
	}

	err = setup_dai(codec, id, port, dir);
	if (err) {
		dev_err(codec->dev, "failed to set up port %d: %d\n", id, err);
		goto out;
	}

	port->stream |= 1 << dir;

out:
	mc_asoc_unlock("mc_asoc_hw_params");
	return err;
}

static int mc_asoc_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct mc_asoc_port_params *port = mc_asoc_dai_port(dai);

	mc_asoc_lock("mc_asoc_hw_free");
	port->stream &= ~(1 << substream->stream);
	mc_asoc_unlock("mc_asoc_hw_free");

	return 0;
}

static const struct snd_soc_dai_ops mc_asoc_dai_ops = {
	.set_fmt	= mc_asoc_set_fmt,
	.set_bclk_ratio	= mc_asoc_set_bclk_ratio,
	.hw_params	= mc_asoc_hw_params,
	.hw_free	= mc_asoc_hw_free,
};

#define YMU831_PORT_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | \
			   SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
			   SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
			   SNDRV_PCM_RATE_48000)
#define YMU831_MUSIC_RATES (YMU831_PORT_RATES | SNDRV_PCM_RATE_96000 | \
			    SNDRV_PCM_RATE_192000)

static struct snd_soc_dai_driver mc_asoc_dai[] = {
	{
		.name = "ymu831-music",
		.id = YMU831_PORT_MUSIC,
		.playback = {
			.stream_name = "Music Playback",
			.channels_min = 1,
			.channels_max = 6,
			.rates = YMU831_MUSIC_RATES,
			.formats = MC_ASOC_FORMATS,
		},
		.capture = {
			.stream_name = "Music Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = YMU831_MUSIC_RATES,
			.formats = MC_ASOC_FORMATS,
		},
		.ops = &mc_asoc_dai_ops,
	}, {
		.name = "ymu831-ext",
		.id = YMU831_PORT_EXT,
		.playback = {
			.stream_name = "Ext Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = YMU831_PORT_RATES,
			.formats = MC_ASOC_FORMATS,
		},
		.capture = {
			.stream_name = "Ext Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = YMU831_PORT_RATES,
			.formats = MC_ASOC_FORMATS,
		},
		.ops = &mc_asoc_dai_ops,
	}, {
		.name = "ymu831-voice",
		.id = YMU831_PORT_VOICE,
		.playback = {
			.stream_name = "Voice Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = YMU831_PORT_RATES,
			.formats = MC_ASOC_FORMATS,
		},
		.capture = {
			.stream_name = "Voice Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = YMU831_PORT_RATES,
			.formats = MC_ASOC_FORMATS,
		},
		.ops = &mc_asoc_dai_ops,
	},
};

/*
 * Control interface
 */
/*
 * Virtual register
 *
 * 16bit software registers are implemented for volumes and mute
 * switches (as an exception, no mute switches for MIC and HP gain).
 * Register contents are stored in codec's register cache.
 *
 *	15	14			8	7			0
 *	+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *	|swR|	volume-R		|swL|	volume-L		|
 *	+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 */

static int write_reg_vol(
	struct snd_soc_component *codec,
	  unsigned int reg,
	  unsigned int value)
{
	struct MCDRV_VOL_INFO	vol_info;
	int	err;

	err	= write_cache(codec, reg, value);
	if (err != 0)
		dev_err(codec->dev, "Cache write to %x failed: %d\n", reg,
			err);

	memset(&vol_info, 0, sizeof(struct MCDRV_VOL_INFO));
	if (reg == MC_ASOC_AVOL_SP_GAIN) {
		/* the speaker gain selects the speaker volume curve */
		if (mc_asoc_ver_id != 0)
			return 0;
		vreg_map[MC_ASOC_AVOL_SP].volmap	= volmap_sp[value];
		reg	= MC_ASOC_AVOL_SP;
	}

	err	= set_vol_info(codec, &vol_info, reg);
	if (err < 0)
		return err;
	err	= _McDrv_Ctrl(MCDRV_SET_VOLUME, &vol_info, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev,
			"%d: Error in MCDRV_SET_VOLUME\n", err);
		return -EIO;
	}
	return 0;
}

static void auto_powerdown(
	struct snd_soc_component *codec
)
{
#if (AUTO_POWEROFF == AUTO_POWEROFF_ON)
	int	err	= 0;
	UINT8	bAEC[]	= {
		0x41, 0x45, 0x43,
		0x05,
		0, 0, 0, 60,
		0x00,
		253,
		0,
		0,
		/*	D7:	*/
		0x44, 0x37,
		0, 0, 0, 50,
		/*	AudioEngine:16	*/
		0x02, 0x00, 0x00, 0x00,
		0, 0, 0, 8,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		/*	V-BOX:23	*/
		0x03, 0x00, 0x00, 0x00,
		0, 0, 0, 15,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		/*	E-DSP:11	*/
		0x07, 0x00, 0x00, 0x00,
		0, 0, 0, 3,
		0,
		0,
		0,
	};

	err	= _McDrv_Ctrl(MCDRV_SET_DSP, bAEC, NULL, sizeof(bAEC));
	if (err != MCDRV_SUCCESS)
		dbg_info("%d: Error in MCDRV_SET_DSP\n", err);
#endif
}

static int add_dsp_prm(
	struct mc_asoc_data	*mc_asoc,
	int	i,
	int	j,
	UINT8	*param,
	UINT32	dSize
)
{
	struct mc_asoc_dsp_param	*dsp_prm	= NULL;

	dsp_prm	= &mc_asoc->param_store[i][j];
	if (dsp_prm->pabParam == NULL)
		dbg_info("param_store[%d][%d]->pabParam = %8p\n",
						i, j, dsp_prm->pabParam);
	else
		while (dsp_prm->pabParam != NULL) {
			dbg_info("pabParam = %8p\n", dsp_prm->pabParam);
			if (dsp_prm->next == NULL) {
				dsp_prm->next	= kzalloc(
					sizeof(struct mc_asoc_dsp_param),
					GFP_KERNEL);
				if (dsp_prm->next == NULL)
					return -ENOMEM;
				dsp_prm	= dsp_prm->next;
				dbg_info("next = %8p\n", dsp_prm);
				break;
			} else
				dsp_prm	= dsp_prm->next;
		}

	dbg_info("param = %8p\n", param);

	dsp_prm->pabParam	= param;
	dsp_prm->dSize		= dSize;
	return 0;
}

static void del_dsp_prm(
	struct mc_asoc_data	*mc_asoc
)
{
	int	i, j;
	struct mc_asoc_dsp_param	*dsp_prm	= NULL;
	struct mc_asoc_dsp_param	*next_prm	= NULL;

	for (i = 0; i <= DSP_PRM_VC_2MIC; i++) {
		for (j = 0; j <= DSP_PRM_USER; j++) {
			if (mc_asoc->param_store[i][j].pabParam
				!= NULL) {
				dbg_info(
			"free(param_store[%d][%d].pabParam:%8p)\n",
				i, j,
				mc_asoc->param_store[i][j].pabParam);
			}
			dsp_prm	= mc_asoc->param_store[i][j].next;
			while (dsp_prm != NULL) {
				dbg_info("free(pabParam:%8p)\n",
					dsp_prm->pabParam);
				next_prm	= dsp_prm->next;
				dbg_info("free(dsp_prm:%8p)\n",
					dsp_prm);
				kfree(dsp_prm);
				dsp_prm	= next_prm;
			}
			mc_asoc->param_store[i][j].pabParam
				= NULL;
			mc_asoc->param_store[i][j].dSize
				= 0;
			mc_asoc->param_store[i][j].next
				= NULL;
		}
	}
	dsp_mem_pt	= 0;
	dbg_info("dsp_mem_pt:%d\n", dsp_mem_pt);
}

static int set_switch_clock(
	struct snd_soc_component *codec,
	unsigned int reg,
	unsigned int value
)
{
	int	ret;
	struct	MCDRV_CLOCKSW_INFO	sInfo;

	TRACE_FUNC();

	sInfo.bClkSrc	= (UINT8)value;
	ret	= _McDrv_Ctrl(MCDRV_SET_CLOCKSW, &sInfo, NULL, 0);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_swap(
	struct snd_soc_component *codec,
	unsigned int reg,
	unsigned int value,
	size_t	offset,
	UINT32	dFlag
)
{
	int	ret;
	struct MCDRV_SWAP_INFO	sInfo;

	/* TRACE_FUNC(); */

	*(UINT8 *)((void *)&sInfo+offset)	= (UINT8)value;
	ret	= _McDrv_Ctrl(MCDRV_SET_SWAP, &sInfo, NULL, dFlag);
	if (ret != MCDRV_SUCCESS)
		return map_drv_error(ret);

	ret	= write_cache(codec, reg, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int set_dsp(
	struct snd_soc_component	*codec,
	UINT8	*param,
	unsigned long	size,
	unsigned long	option
)
{
	struct mc_asoc_data	*mc_asoc	= mc_asoc_get_mc_asoc(codec);
	UINT8	*param2;
	int	err;

	if (mc_asoc == NULL)
		return -EINVAL;

	dbg_info("option=%08lX\n", option);
	switch (option) {
	case	YMC_DSP_OUTPUT_BASE:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_OUTPUT,
				DSP_PRM_BASE, param, size);
		break;
	case	YMC_DSP_INPUT_BASE:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_INPUT,
				DSP_PRM_BASE, param, size);
		break;
	case	YMC_DSP_VOICECALL_BASE_COMMON:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
				DSP_PRM_BASE, param, size);
		if (err != 0)
			break;

		param2	= get_dsp_mem(size);
		if (param2 == NULL) {
			err	= -ENOMEM;
			break;
		}
		memcpy(param2, param, size);
		param	= param2;
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_BASE, param, size);
		break;
	case	YMC_DSP_VOICECALL_BASE_1MIC:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_1MIC,
				DSP_PRM_BASE, param, size);
		break;
	case	YMC_DSP_VOICECALL_BASE_2MIC:
		err	= add_dsp_prm(mc_asoc, DSP_PRM_VC_2MIC,
				DSP_PRM_BASE, param, size);
		break;
	default:
		err	= -EINVAL;
		break;
	}
	if (err != 0)
		return err;

	return map_drv_error(_McDrv_Ctrl(MCDRV_SET_DSP, param, NULL, size));
}

static int mc_asoc_write_reg(
	struct snd_soc_component *codec,
	unsigned int reg,
	unsigned int value)
{
	int	err	= 0;
	struct mc_asoc_data	*mc_asoc	= NULL;

	/* TRACE_FUNC(); */

	/* dbg_info("reg=%d, value=%04Xh\n", reg, value); */

	if (codec == NULL)
		return -EINVAL;

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	mc_asoc_lock("mc_asoc_write_reg");

	if (reg < MC_ASOC_N_VOL_REG) {
		switch (reg) {
		case	MC_ASOC_DVOL_MUSICIN:
		case	MC_ASOC_DVOL_EXTIN:
		case	MC_ASOC_DVOL_VOICEIN:
		case	MC_ASOC_DVOL_REFIN:
		case	MC_ASOC_DVOL_ADIF0IN:
		case	MC_ASOC_DVOL_ADIF1IN:
		case	MC_ASOC_DVOL_ADIF2IN:
		case	MC_ASOC_DVOL_MUSICOUT:
		case	MC_ASOC_DVOL_EXTOUT:
		case	MC_ASOC_DVOL_VOICEOUT:
		case	MC_ASOC_DVOL_REFOUT:
		case	MC_ASOC_DVOL_DAC0OUT:
		case	MC_ASOC_DVOL_DAC1OUT:
		case	MC_ASOC_DVOL_DPATHDA:
		case	MC_ASOC_DVOL_DPATHAD:
			if ((((value>>8)&0x7F) > 114)
			|| ((value&0x7F) > 114)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_LINEIN1:
		case	MC_ASOC_AVOL_MIC1:
		case	MC_ASOC_AVOL_MIC2:
		case	MC_ASOC_AVOL_MIC3:
		case	MC_ASOC_AVOL_MIC4:
			if ((((value>>8)&0x7F) > 63)
			|| ((value&0x7F) > 63)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_HP:
			if ((((value>>8)&0x7F) > 127)
			|| ((value&0x7F) > 127)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_SP:
			if ((((value>>8)&0x7F) > 127)
			|| ((value&0x7F) > 127)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_RC:
			if ((((value>>8)&0x7F) > 111)
			|| ((value&0x7F) > 111)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_LINEOUT1:
		case	MC_ASOC_AVOL_LINEOUT2:
			if ((((value>>8)&0x7F) > 119)
			|| ((value&0x7F) > 119)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		case	MC_ASOC_AVOL_SP_GAIN:
			if ((((value>>8)&0x7F) > 4)
			|| ((value&0x7F) > 4)) {
				dbg_info("reg=%d, value=%04Xh\n", reg, value);
				err	= -EINVAL;
			}
			break;
		}
		if (err == 0) {
			;
			err	= write_reg_vol(codec, reg, value);
		}
	} else {
		switch (reg) {
		case	MC_ASOC_SWITCH_CLOCK:
			err	= set_switch_clock(codec, reg, value);
			break;
		case	MC_ASOC_ADIF0_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bAdif0),
				MCDRV_SWAP_ADIF0_UPDATE_FLAG);
			break;
		case	MC_ASOC_ADIF1_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bAdif1),
				MCDRV_SWAP_ADIF1_UPDATE_FLAG);
			break;
		case	MC_ASOC_ADIF2_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bAdif2),
				MCDRV_SWAP_ADIF2_UPDATE_FLAG);
			break;
		case	MC_ASOC_DAC0_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bDac0),
				MCDRV_SWAP_DAC0_UPDATE_FLAG);
			break;
		case	MC_ASOC_DAC1_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bDac1),
				MCDRV_SWAP_DAC1_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_OUT0_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicOut0),
				MCDRV_SWAP_MUSICOUT0_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_IN0_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicIn0),
				MCDRV_SWAP_MUSICIN0_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_IN1_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicIn1),
				MCDRV_SWAP_MUSICIN1_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_IN2_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicIn2),
				MCDRV_SWAP_MUSICIN2_UPDATE_FLAG);
			break;
		case	MC_ASOC_EXT_IN_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bExtIn),
				MCDRV_SWAP_EXTIN_UPDATE_FLAG);
			break;
		case	MC_ASOC_VOICE_IN_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bVoiceIn),
				MCDRV_SWAP_VOICEIN_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_OUT1_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicOut1),
				MCDRV_SWAP_MUSICOUT1_UPDATE_FLAG);
			break;
		case	MC_ASOC_MUSIC_OUT2_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bMusicOut2),
				MCDRV_SWAP_MUSICOUT2_UPDATE_FLAG);
			break;
		case	MC_ASOC_EXT_OUT_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bExtOut),
				MCDRV_SWAP_EXTOUT_UPDATE_FLAG);
			break;
		case	MC_ASOC_VOICE_OUT_SWAP:
			err	= set_swap(codec, reg, value,
				offsetof(struct MCDRV_SWAP_INFO, bVoiceOut),
				MCDRV_SWAP_VOICEOUT_UPDATE_FLAG);
			break;
		case	MC_ASOC_CLEAR_DSP_PARAM:
			del_dsp_prm(mc_asoc);
			break;

		default:
			err	= -EINVAL;
			break;
		}
	}

	if (err < 0)
		dbg_info("err=%d\n", err);

	mc_asoc_unlock("mc_asoc_write_reg");
	return err;
}

static unsigned int mc_asoc_read_reg(
	struct snd_soc_component *codec,
	unsigned int reg)
{
	int	ret;
/*
	TRACE_FUNC();

	dbg_info("reg=%d\n", reg);
*/
	mc_asoc_lock("mc_asoc_read_reg");
	if (codec == NULL) {
		ret	= -EINVAL;
		goto exit;
	}
	ret	= read_cache(codec, reg);
	if (ret < 0)
		ret	= -EIO;
exit:
	mc_asoc_unlock("mc_asoc_read_reg");
	return (unsigned int)ret;
}

/*
 * Same as snd_soc_add_controls supported in alsa-driver 1.0.19 or later.
 * This function is implimented for compatibility with linux 2.6.29.
 */
static int mc_asoc_add_controls(
	struct snd_soc_component *codec,
	const struct snd_kcontrol_new *controls,
	int n)
{
	return snd_soc_add_component_controls(codec, controls, n);
}

static int mc_asoc_add_widgets(struct snd_soc_component *codec)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(codec);
	int	err;

	err	= snd_soc_dapm_new_controls(dapm, ymu831_dapm_widgets,
					    ymu831_num_dapm_widgets);
	if (err < 0)
		return err;

	return snd_soc_dapm_add_routes(dapm, ymu831_dapm_routes,
				       ymu831_num_dapm_routes);
}

/*
 * Hand the routes DAPM has enabled to the core. Called with the DAPM mutex
 * held, after every power sequence and on routing changes.
 */
static void ymu831_dapm_update_locked(struct snd_soc_component *codec)
{
	struct mc_asoc_data	*mc_asoc	= mc_asoc_get_mc_asoc(codec);
	struct ymu831_dapm	*ymu_dapm	= &mc_asoc->dapm;
	struct MCDRV_PATH_INFO	path_info, cur_info;
	int	active;
	int	err;

	active	= ymu831_dapm_build_path(codec, &mc_asoc->dapm, &path_info);
	if (active < 0) {
		dev_err(codec->dev, "failed to look up DAPM routes: %d\n",
			active);
		return;
	}

	mc_asoc_lock("ymu831_dapm_update");
	set_BIAS(&path_info);

	/*
	 * DAPM calls this after every step of a power sequence. Setting a path
	 * costs the core a full mute, power and volume cycle, so skip it when
	 * neither the routes nor the core's state changed since the last time.
	 */
	if (ymu_dapm->valid &&
	    !memcmp(&path_info, &ymu_dapm->requested, sizeof(path_info)) &&
	    _McDrv_Ctrl(MCDRV_GET_PATH, &cur_info, NULL, 0) == MCDRV_SUCCESS &&
	    !memcmp(&cur_info, &ymu_dapm->applied, sizeof(cur_info)))
		goto unlock;

	ymu_dapm->valid	= false;
	err	= _McDrv_Ctrl(MCDRV_SET_PATH, &path_info, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "failed to set path: %d\n", err);
		goto unlock;
	}
	if (!active)
		auto_powerdown(codec);

	ymu_dapm->requested	= path_info;
	ymu_dapm->valid	= _McDrv_Ctrl(MCDRV_GET_PATH, &ymu_dapm->applied,
				      NULL, 0) == MCDRV_SUCCESS;
unlock:
	mc_asoc_unlock("ymu831_dapm_update");
}

void ymu831_dapm_update(struct snd_soc_component *codec)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(codec);

	snd_soc_dapm_mutex_lock(dapm);
	ymu831_dapm_update_locked(codec);
	snd_soc_dapm_mutex_unlock(dapm);
}

static void ymu831_seq_notifier(struct snd_soc_component *codec,
				enum snd_soc_dapm_type type, int subseq)
{
	ymu831_dapm_update_locked(codec);
}

/*
 * DSP parameter and data controls
 *
 * The controls take standard TLV containers: a type word, the payload
 * length in bytes and the payload, as written by alsa-lib (e.g. UCM's
 * cset-tlv).
 */
struct mc_asoc_dsp_ctl {
	struct soc_bytes_ext	ext;
	unsigned long		option;
};

#define MC_ASOC_TLV_HDR_SIZE	(2 * sizeof(unsigned int))

static int mc_asoc_tlv_payload_len(const unsigned int __user *bytes,
				   unsigned int size, unsigned int *len)
{
	unsigned int hdr[2];

	if (size < MC_ASOC_TLV_HDR_SIZE)
		return -EINVAL;
	if (copy_from_user(hdr, bytes, sizeof(hdr)))
		return -EFAULT;
	if (!hdr[1] || hdr[1] > size - MC_ASOC_TLV_HDR_SIZE ||
	    hdr[1] > MAX_YMS_CTRL_PARAM_SIZE)
		return -EINVAL;

	*len = hdr[1];
	return 0;
}

/*
 * TLV payloads are made of 32 bit words, so a parameter blob may arrive
 * padded. The core wants exactly the length the blob's header declares.
 */
static unsigned int mc_asoc_dsp_blob_len(const UINT8 *param, unsigned int len)
{
	u32 declared;

	if (len < 8 || memcmp(param, "AEC", 3))
		return len;

	declared = get_unaligned_be32(param + 4);
	return declared <= len - 8 ? declared + 8 : len;
}

static int mc_asoc_dsp_param_put(struct snd_kcontrol *kcontrol,
				 const unsigned int __user *bytes,
				 unsigned int size)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct mc_asoc_dsp_ctl *ctl = container_of((void *)kcontrol->private_value,
						   struct mc_asoc_dsp_ctl, ext);
	unsigned int len;
	UINT8 *param;
	int err;

	err = mc_asoc_tlv_payload_len(bytes, size, &len);
	if (err)
		return err;

	mc_asoc_lock("mc_asoc_dsp_param_put");
	param = get_dsp_mem(len);
	if (!param)
		err = -ENOMEM;
	else if (copy_from_user(param, bytes + 2, len))
		err = -EFAULT;
	else
		err = set_dsp(codec, param, mc_asoc_dsp_blob_len(param, len),
			      ctl->option);
	mc_asoc_unlock("mc_asoc_dsp_param_put");

	return err;
}

static int mc_asoc_dsp_data_get(struct snd_kcontrol *kcontrol,
				unsigned int __user *bytes, unsigned int size)
{
	unsigned int hdr[2] = { 0, 0 };
	UINT8 *data;
	int ret;

	if (size <= MC_ASOC_TLV_HDR_SIZE)
		return -EINVAL;
	size = min_t(unsigned int, size - MC_ASOC_TLV_HDR_SIZE,
		     MAX_YMS_CTRL_PARAM_SIZE);

	data = kzalloc(size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mc_asoc_lock("mc_asoc_dsp_data_get");
	ret = _McDrv_Ctrl(MCDRV_GET_DSP_DATA, data, NULL, size);
	mc_asoc_unlock("mc_asoc_dsp_data_get");
	if (ret < MCDRV_SUCCESS) {
		ret = map_drv_error(ret);
		goto out;
	}

	hdr[1] = ret;
	ret = 0;
	if (copy_to_user(bytes, hdr, sizeof(hdr)) ||
	    copy_to_user(bytes + 2, data, hdr[1]))
		ret = -EFAULT;
out:
	kfree(data);
	return ret;
}

static int mc_asoc_dsp_data_put(struct snd_kcontrol *kcontrol,
				const unsigned int __user *bytes,
				unsigned int size)
{
	unsigned int len;
	void *data;
	int ret;

	ret = mc_asoc_tlv_payload_len(bytes, size, &len);
	if (ret)
		return ret;

	data = memdup_user(bytes + 2, len);
	if (IS_ERR(data))
		return PTR_ERR(data);

	mc_asoc_lock("mc_asoc_dsp_data_put");
	ret = _McDrv_Ctrl(MCDRV_SET_DSP_DATA, data, NULL, len);
	mc_asoc_unlock("mc_asoc_dsp_data_put");
	kfree(data);

	return ret < MCDRV_SUCCESS ? map_drv_error(ret) : 0;
}

#define MC_ASOC_TLV_MAX		(MAX_YMS_CTRL_PARAM_SIZE + MC_ASOC_TLV_HDR_SIZE)

#define MC_ASOC_DSP_PARAM(xoption)					\
	{								\
		.ext = { .max = MC_ASOC_TLV_MAX,			\
			 .put = mc_asoc_dsp_param_put },		\
		.option = (xoption),					\
	}

/*
 * The base parameter groups. The core also knows per-route overrides, but
 * those only exist to swap parameters behind userspace's back when the
 * route changes, which is up to the route configuration in userspace.
 */
static struct mc_asoc_dsp_ctl mc_asoc_dsp_ctls[] = {
	MC_ASOC_DSP_PARAM(YMC_DSP_OUTPUT_BASE),
	MC_ASOC_DSP_PARAM(YMC_DSP_INPUT_BASE),
	MC_ASOC_DSP_PARAM(YMC_DSP_VOICECALL_BASE_COMMON),
	MC_ASOC_DSP_PARAM(YMC_DSP_VOICECALL_BASE_1MIC),
	MC_ASOC_DSP_PARAM(YMC_DSP_VOICECALL_BASE_2MIC),
};

static struct soc_bytes_ext mc_asoc_dsp_data_ext = {
	.max = MC_ASOC_TLV_MAX,
	.get = mc_asoc_dsp_data_get,
	.put = mc_asoc_dsp_data_put,
};

#define MC_ASOC_TLV_BYTES(xname, xaccess, xext)				\
	{								\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,			\
		.name = xname,						\
		.access = (xaccess) |					\
			  SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK,		\
		.tlv.c = snd_soc_bytes_tlv_callback,			\
		.info = snd_soc_bytes_info_ext,				\
		.private_value = (unsigned long)(xext),			\
	}

static const struct snd_kcontrol_new mc_asoc_dsp_controls[] = {
	MC_ASOC_TLV_BYTES("DSP Output Parameters",
			  SNDRV_CTL_ELEM_ACCESS_TLV_WRITE,
			  &mc_asoc_dsp_ctls[0].ext),
	MC_ASOC_TLV_BYTES("DSP Input Parameters",
			  SNDRV_CTL_ELEM_ACCESS_TLV_WRITE,
			  &mc_asoc_dsp_ctls[1].ext),
	MC_ASOC_TLV_BYTES("Voice Call Parameters",
			  SNDRV_CTL_ELEM_ACCESS_TLV_WRITE,
			  &mc_asoc_dsp_ctls[2].ext),
	MC_ASOC_TLV_BYTES("Voice Call 1MIC Parameters",
			  SNDRV_CTL_ELEM_ACCESS_TLV_WRITE,
			  &mc_asoc_dsp_ctls[3].ext),
	MC_ASOC_TLV_BYTES("Voice Call 2MIC Parameters",
			  SNDRV_CTL_ELEM_ACCESS_TLV_WRITE,
			  &mc_asoc_dsp_ctls[4].ext),
	MC_ASOC_TLV_BYTES("C-DSP Data",
			  SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE,
			  &mc_asoc_dsp_data_ext),
};

static struct input_dev		*inp_dev;

/*
 * Headset detection is only armed while a jack is attached via set_jack(),
 * boards without a headset socket keep plug, mic and key detection off.
 */
static SINT32 mc_asoc_set_hsdet(void *info, void *info2, UINT32 update)
{
	struct mc_asoc_data *mc_asoc = mc_asoc_get_mc_asoc(mc_asoc_codec);
	struct MCDRV_HSDET_INFO hsdet = *(struct MCDRV_HSDET_INFO *)info;

	if (!mc_asoc || !mc_asoc->jack.hs_jack) {
		hsdet.bEnPlugDet	= MCDRV_PLUGDET_DISABLE;
		hsdet.bEnPlugDetDb	= MCDRV_PLUGDETDB_DISABLE;
		hsdet.bEnMicDet		= MCDRV_MICDET_DISABLE;
		hsdet.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
		hsdet.bEnDlyKeyOn	= MCDRV_KEYEN_D_D_D;
		hsdet.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
		hsdet.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
	}

	return _McDrv_Ctrl(MCDRV_SET_HSDET, &hsdet, info2, update);
}

static void mc_asoc_jack_report(int status, int mask)
{
	struct mc_asoc_data *mc_asoc = mc_asoc_get_mc_asoc(mc_asoc_codec);

	if (mc_asoc && mc_asoc->jack.hs_jack)
		snd_soc_jack_report(mc_asoc->jack.hs_jack, status, mask);
}

static void mc_asoc_report_key(unsigned int code, int value)
{
	if (!inp_dev)
		return;
	input_report_key(inp_dev, code, value);
	input_sync(inp_dev);
}

static struct workqueue_struct	*workq_mb4;
static struct delayed_work	delayed_work_mb4;

static void work_mb4(struct work_struct *work)
{
	TRACE_FUNC();

	mc_asoc_lock("work_mb4");
	mc_asoc_MBSEL4	= 0x00;
	mc_asoc_unlock("work_mb4");
}

static struct workqueue_struct	*workq_mkdeten;
static struct delayed_work	delayed_work_mkdeten;
static UINT8	mc_asoc_MICDET;

static void work_mkdeten(struct work_struct *work)
{
	int	err;
	UINT8	jack_status	= mc_asoc_jack_status;
	struct MCDRV_HSDET_INFO	stHSDetInfo;
	struct MCDRV_REG_INFO	reg_info;

	TRACE_FUNC();

	if (mc_asoc_jack_status == SND_JACK_HEADSET) {
		dbg_info("skip mkdeten\n");
		return;
	}

	mutex_lock(&hsdet_mutex);
	mc_asoc_lock("work_mkdeten");

	reg_info.bRegType	= MCDRV_REGTYPE_CD;
	reg_info.bAddress	= MCI_MICDET;
	err	= _McDrv_Ctrl(MCDRV_READ_REG, &reg_info, NULL, 0);
	if (err != MCDRV_SUCCESS)
		reg_info.bData	= 1;
	if (((reg_info.bData&0x47) == 0)
	&& ((reg_info.bData&0x47) == mc_asoc_MICDET)
	&& ((mc_asoc_hpimpclass < MC_ASOC_IMPCLASS_THRESHOLD)
		|| (mc_asoc_hpimpclass == 5)
		|| (mc_asoc_hpimpclass == 6))) {
		dbg_info("MICDET\n");
		mc_asoc_jack_status	= SND_JACK_HEADSET;
		dbg_info("queue_delayed_work_mb4\n");
		queue_delayed_work(workq_mb4, &delayed_work_mb4,
						msecs_to_jiffies(MSDETMB4OFF));
	} else {
		err	= _McDrv_Ctrl(MCDRV_GET_HSDET, (void *)&stHSDetInfo,
								NULL, 0);
		if (err == MCDRV_SUCCESS) {
			stHSDetInfo.bEnMicDet		= MCDRV_MICDET_DISABLE;
			stHSDetInfo.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnDlyKeyOn		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
			err	= mc_asoc_set_hsdet((void *)&stHSDetInfo, NULL,
						    0x7C);
		}
		if (err < MCDRV_SUCCESS) {
			;
			dbg_info("%d: Error in work_mkdeten\n", err);
		}
	}

	mc_asoc_unlock("work_mkdeten");
	if (jack_status != mc_asoc_jack_status) {
		if (mc_asoc_jack_status != 0) {
			;
			mc_asoc_jack_report(0, SND_JACK_HEADSET);
		}
		mc_asoc_jack_report(mc_asoc_jack_status, SND_JACK_HEADSET);
	}
	mutex_unlock(&hsdet_mutex);
}

static void hsdet_cb(UINT32 dFlags, struct MCDRV_HSDET_RES *psRes)
{
	struct MCDRV_HSDET_INFO	stHSDetInfo;
	int	err;
	UINT8	hpimpclass	= mc_asoc_hpimpclass;
	UINT8	bCurEnPlugDetDb;
	UINT8	bEnPlugDetDb;
	UINT8	bEnMicDet;
	UINT8	bEnDlyKeyOff;
	UINT8	bEnDlyKeyOn;
	UINT8	bEnKeyOff;
	UINT8	bEnKeyOn;
	UINT8	bKey0OnDlyTim;
	UINT8	bKey1OnDlyTim;
	UINT8	bKey2OnDlyTim;
	UINT8	bKey0OnDlyTim2;
	UINT8	bKey1OnDlyTim2;
	UINT8	bKey2OnDlyTim2;
	UINT8	bHsDetDbnc, bDbncNumPlug;
	UINT8	jack_status	= mc_asoc_jack_status;
	UINT32	jack_btn_on	= 0,
		jack_btn_off	= 0,
		jack_btn_stat	= 0;

	TRACE_FUNC();

	mutex_lock(&hsdet_mutex);
	mc_asoc_lock("hsdet_cb");

	dbg_info("dFlags=0x%08lX, bKeyCnt0=%d, bKeyCnt1=%d, bKeyCnt2=%d\n",
		dFlags,
		psRes->bKeyCnt0, psRes->bKeyCnt1,
		psRes->bKeyCnt2);

	if (mc_asoc_suspended == 0) {
		bEnPlugDetDb	= stHSDetInfo_Default.bEnPlugDetDb;
		bEnMicDet	= stHSDetInfo_Default.bEnMicDet;
		bEnDlyKeyOff	= stHSDetInfo_Default.bEnDlyKeyOff;
		bEnDlyKeyOn	= stHSDetInfo_Default.bEnDlyKeyOn;
		bEnKeyOff	= stHSDetInfo_Default.bEnKeyOff;
		bEnKeyOn	= stHSDetInfo_Default.bEnKeyOn;
		bKey0OnDlyTim	= stHSDetInfo_Default.bKey0OnDlyTim;
		bKey1OnDlyTim	= stHSDetInfo_Default.bKey1OnDlyTim;
		bKey2OnDlyTim	= stHSDetInfo_Default.bKey2OnDlyTim;
		bKey0OnDlyTim2	= stHSDetInfo_Default.bKey0OnDlyTim2;
		bKey1OnDlyTim2	= stHSDetInfo_Default.bKey1OnDlyTim2;
		bKey2OnDlyTim2	= stHSDetInfo_Default.bKey2OnDlyTim2;
		bHsDetDbnc	= stHSDetInfo_Default.bHsDetDbnc;
		bDbncNumPlug	= stHSDetInfo_Default.bDbncNumPlug;
	} else {
		bEnPlugDetDb	= stHSDetInfo_Suspend.bEnPlugDetDb;
		bEnMicDet	= stHSDetInfo_Suspend.bEnMicDet;
		bEnDlyKeyOff	= stHSDetInfo_Suspend.bEnDlyKeyOff;
		bEnDlyKeyOn	= stHSDetInfo_Suspend.bEnDlyKeyOn;
		bEnKeyOff	= stHSDetInfo_Suspend.bEnKeyOff;
		bEnKeyOn	= stHSDetInfo_Suspend.bEnKeyOn;
		bKey0OnDlyTim	= stHSDetInfo_Suspend.bKey0OnDlyTim;
		bKey1OnDlyTim	= stHSDetInfo_Suspend.bKey1OnDlyTim;
		bKey2OnDlyTim	= stHSDetInfo_Suspend.bKey2OnDlyTim;
		bKey0OnDlyTim2	= stHSDetInfo_Suspend.bKey0OnDlyTim2;
		bKey1OnDlyTim2	= stHSDetInfo_Suspend.bKey1OnDlyTim2;
		bKey2OnDlyTim2	= stHSDetInfo_Suspend.bKey2OnDlyTim2;
		bHsDetDbnc	= stHSDetInfo_Suspend.bHsDetDbnc;
		bDbncNumPlug	= stHSDetInfo_Suspend.bDbncNumPlug;
	}
	err	= _McDrv_Ctrl(MCDRV_GET_HSDET, (void *)&stHSDetInfo, NULL, 0);
	if (err < MCDRV_SUCCESS) {
		dbg_info("%d: Error in MCDRV_GET_HSDET\n", err);
		goto exit;
	}
	bCurEnPlugDetDb	= stHSDetInfo.bEnPlugDetDb;

	if (dFlags & MCDRV_HSDET_EVT_SENSEFIN_FLAG) {
		dbg_info("bPlugRev=%d, bHpImpClass=%d, wHpImp=%d\n",
			psRes->bPlugRev, psRes->bHpImpClass,
			psRes->wHpImp);
		mc_asoc_hpimpclass	= psRes->bHpImpClass;
	}

	if (dFlags & MCDRV_HSDET_EVT_PLUGUNDET_DB_FLAG) {
		if (bCurEnPlugDetDb & MCDRV_PLUGDETDB_UNDET_ENABLE) {
			dbg_info("PLUGUNDETDB\n");
			mc_asoc_jack_status	= 0;
			cancel_delayed_work(&delayed_work_mb4);
			dbg_info("cancel_delayed_work_mb4\n");
			cancel_delayed_work(&delayed_work_mkdeten);
			dbg_info("cancel_delayed_work_mkdeten\n");

			mc_asoc_MBSEL4	= 0x80;

			stHSDetInfo.bEnPlugDetDb	=
					bEnPlugDetDb & MCDRV_PLUGDETDB_DET_ENABLE;
			stHSDetInfo.bEnMicDet		= bEnMicDet;
			stHSDetInfo.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnDlyKeyOn		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bHsDetDbnc		= bHsDetDbnc;
			stHSDetInfo.bDbncNumPlug	= bDbncNumPlug;
			stHSDetInfo.cbfunc		= NULL;
			err	= mc_asoc_set_hsdet((void *)&stHSDetInfo, NULL,
						    0x410000FE);
			if (err < MCDRV_SUCCESS)
				dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);

			stHSDetInfo.cbfunc	= hsdet_cb;
			err	= mc_asoc_set_hsdet((void *)&stHSDetInfo, NULL,
						    0x40000000);
			if (err < MCDRV_SUCCESS)
				dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);
			mc_asoc_hpimpclass	= (UINT8)-1;
		} else {
		}
	}

	if (mc_asoc_jack_status == SND_JACK_HEADSET) {
		if (dFlags & MCDRV_HSDET_EVT_KEYON0_FLAG) {
			dbg_info("KEYON_0\n");
			if ((bEnKeyOn & MCDRV_KEYEN_D_D_E) != 0) {
				jack_btn_on	= SND_JACK_BTN_0;
				jack_btn_stat	= SND_JACK_BTN_0;
			}
		} else if (dFlags & MCDRV_HSDET_EVT_KEYON1_FLAG) {
			dbg_info("KEYON_1\n");
			if ((bEnKeyOn & MCDRV_KEYEN_D_E_D) != 0) {
				jack_btn_on	= SND_JACK_BTN_1;
				jack_btn_stat	= SND_JACK_BTN_1;
			}
		} else if (dFlags & MCDRV_HSDET_EVT_KEYON2_FLAG) {
			dbg_info("KEYON_2\n");
			if ((bEnKeyOn & MCDRV_KEYEN_E_D_D) != 0) {
				jack_btn_on	= SND_JACK_BTN_2;
				jack_btn_stat	= SND_JACK_BTN_2;
			}
		}

		if (dFlags & MCDRV_HSDET_EVT_KEYOFF0_FLAG) {
			dbg_info("KEYOFF_0\n");
			if ((bEnKeyOff & MCDRV_KEYEN_D_D_E) != 0) {
				;
				jack_btn_off	= SND_JACK_BTN_0;
			}
			if (((bEnDlyKeyOn & MCDRV_KEYEN_D_D_E) != 0)
			&& (mc_asoc_ver_id == 0)
			&& (bKey0OnDlyTim2 == 0)) {
				if ((stHSDetInfo.bEnKeyOff & 1) != 0) {
					stHSDetInfo.bEnKeyOff	&= ~1;
					stHSDetInfo.bKey0OnDlyTim	=
					    bKey0OnDlyTim;
					err	= mc_asoc_set_hsdet((void *)&stHSDetInfo,
								    NULL,
								    0x2020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		} else if (dFlags & MCDRV_HSDET_EVT_KEYOFF1_FLAG) {
			dbg_info("KEYOFF_1\n");
			if ((bEnKeyOff & MCDRV_KEYEN_D_E_D) != 0) {
				;
				jack_btn_off	= SND_JACK_BTN_1;
			}
			if (((bEnDlyKeyOn & MCDRV_KEYEN_D_E_D) != 0)
			&& (mc_asoc_ver_id == 0)
			&& (bKey1OnDlyTim2 == 0)) {
				if ((stHSDetInfo.bEnKeyOff & 2) != 0) {
					stHSDetInfo.bEnKeyOff	&= ~2;
					stHSDetInfo.bKey1OnDlyTim	=
					    bKey1OnDlyTim;
					err	= mc_asoc_set_hsdet((void *)&stHSDetInfo,
								    NULL,
								    0x4020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		} else if (dFlags & MCDRV_HSDET_EVT_KEYOFF2_FLAG) {
			dbg_info("KEYOFF_2\n");
			if ((bEnKeyOff & MCDRV_KEYEN_E_D_D) != 0) {
				;
				jack_btn_off	= SND_JACK_BTN_2;
			}
			if (((bEnDlyKeyOn & MCDRV_KEYEN_E_D_D) != 0)
			&& (mc_asoc_ver_id == 0)
			&& (bKey2OnDlyTim2 == 0)) {
				if ((stHSDetInfo.bEnKeyOff & 4) != 0) {
					stHSDetInfo.bEnKeyOff	&= ~4;
					stHSDetInfo.bKey2OnDlyTim	=
					    bKey2OnDlyTim;
					err	= mc_asoc_set_hsdet((void *)&stHSDetInfo,
								    NULL,
								    0x8020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		}

		if (dFlags & MCDRV_HSDET_EVT_DLYKEYON0_FLAG) {
			dbg_info("DLYKEYON_0\n");
			if ((bEnDlyKeyOn & MCDRV_KEYEN_D_D_E) != 0) {
				mc_asoc_report_key(MC_ASOC_EV_KEY_DELAYKEYON0, 1);
				mc_asoc_report_key(MC_ASOC_EV_KEY_DELAYKEYON0, 0);
				if ((mc_asoc_ver_id == 0)
				&& (bKey0OnDlyTim2 == 0)) {
					stHSDetInfo.bEnKeyOff	|= 1;
					stHSDetInfo.bKey0OnDlyTim	= 0;
					err	= mc_asoc_set_hsdet((void *)&stHSDetInfo,
								    NULL,
								    0x2020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		} else if (dFlags & MCDRV_HSDET_EVT_DLYKEYON1_FLAG) {
			dbg_info("DLYKEYON_1\n");
			if ((bEnDlyKeyOn & MCDRV_KEYEN_D_E_D) != 0) {
				mc_asoc_report_key(MC_ASOC_EV_KEY_DELAYKEYON1, 1);
				mc_asoc_report_key(MC_ASOC_EV_KEY_DELAYKEYON1, 0);

				if ((mc_asoc_ver_id == 0)
				&& (bKey1OnDlyTim2 == 0)) {
					stHSDetInfo.bEnKeyOff	|= 2;
					stHSDetInfo.bKey1OnDlyTim	= 0;
					err	= mc_asoc_set_hsdet((void *)&stHSDetInfo,
								    NULL,
								    0x4020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		} else if (dFlags & MCDRV_HSDET_EVT_DLYKEYON2_FLAG) {
			dbg_info("DLYKEYON_2\n");
			if ((bEnDlyKeyOn & MCDRV_KEYEN_E_D_D) != 0) {
				mc_asoc_report_key(MC_ASOC_EV_KEY_DELAYKEYON2, 1);
				mc_asoc_report_key(MC_ASOC_EV_KEY_DELAYKEYON2, 0);

				if ((mc_asoc_ver_id == 0)
				&& (bKey2OnDlyTim2 == 0)) {
					stHSDetInfo.bEnKeyOff	|= 4;
					stHSDetInfo.bKey2OnDlyTim	= 0;
					err	= mc_asoc_set_hsdet((void *)&stHSDetInfo,
								    NULL,
								    0x8020);
					if (err < MCDRV_SUCCESS)
						dbg_info(
					"%d: Error in MCDRV_SET_HSDET\n", err);
				}
			}
		}

		if (dFlags & MCDRV_HSDET_EVT_DLYKEYOFF0_FLAG) {
			dbg_info("DLYKEYOFF_0\n");
			if ((bEnDlyKeyOff & MCDRV_KEYEN_D_D_E) != 0) {
				mc_asoc_report_key(mc_asoc_ev_key_delaykeyoff0[psRes->bKeyCnt0], 1);
				mc_asoc_report_key(mc_asoc_ev_key_delaykeyoff0[psRes->bKeyCnt0], 0);
			}
		} else if (dFlags & MCDRV_HSDET_EVT_DLYKEYOFF1_FLAG) {
			dbg_info("DLYKEYOFF_1\n");
			if ((bEnDlyKeyOff & MCDRV_KEYEN_D_E_D) != 0) {
				mc_asoc_report_key(mc_asoc_ev_key_delaykeyoff1[psRes->bKeyCnt1], 1);
				mc_asoc_report_key(mc_asoc_ev_key_delaykeyoff1[psRes->bKeyCnt1], 0);
			}
		} else if (dFlags & MCDRV_HSDET_EVT_DLYKEYOFF2_FLAG) {
			dbg_info("DLYKEYOFF_2\n");
			if ((bEnDlyKeyOff & MCDRV_KEYEN_E_D_D) != 0) {
				mc_asoc_report_key(mc_asoc_ev_key_delaykeyoff2[psRes->bKeyCnt2], 1);
				mc_asoc_report_key(mc_asoc_ev_key_delaykeyoff2[psRes->bKeyCnt2], 0);
			}
		}
	}

	if ((dFlags & MCDRV_HSDET_EVT_PLUGDET_DB_FLAG)
	&& (bCurEnPlugDetDb & MCDRV_PLUGDETDB_DET_ENABLE)) {
		dbg_info("PLUGDETDB\n");
		if ((dFlags & MCDRV_HSDET_EVT_MICDET_FLAG)
		&& (bEnMicDet & MCDRV_MICDET_ENABLE)) {
			if ((mc_asoc_hpimpclass >= MC_ASOC_IMPCLASS_THRESHOLD)
			&& (mc_asoc_hpimpclass != 5)
			&& (mc_asoc_hpimpclass != 6)) {
				mc_asoc_jack_status	= SND_JACK_HEADPHONE;
				bEnMicDet	= MCDRV_MICDET_DISABLE;
				stHSDetInfo.bEnMicDet	= bEnMicDet;
				bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
				bEnDlyKeyOn	= MCDRV_KEYEN_D_D_D;
				bEnKeyOff	= MCDRV_KEYEN_D_D_D;
				bEnKeyOn	= MCDRV_KEYEN_D_D_D;
			} else {
				dbg_info("MICDET\n");
				mc_asoc_jack_status	= SND_JACK_HEADSET;
				dbg_info("queue_delayed_work_mb4\n");
				queue_delayed_work(workq_mb4, &delayed_work_mb4,
						msecs_to_jiffies(MSDETMB4OFF));
			}
		} else {
			struct MCDRV_REG_INFO	reg_info;
			reg_info.bRegType	= MCDRV_REGTYPE_CD;
			reg_info.bAddress	= MCI_MICDET;
			err	= _McDrv_Ctrl(MCDRV_READ_REG, &reg_info, NULL,
									0);
			if (err != MCDRV_SUCCESS)
				reg_info.bData	= 1;
			mc_asoc_MICDET	= reg_info.bData & 0x47;
			mc_asoc_jack_status	= SND_JACK_HEADPHONE;
			dbg_info("queue_delayed_work_mkdeten\n");
			queue_delayed_work(workq_mkdeten,
						&delayed_work_mkdeten,
						msecs_to_jiffies(MSMKDETENOFF));
		}
		stHSDetInfo.bEnPlugDetDb	=
				bEnPlugDetDb & MCDRV_PLUGDETDB_UNDET_ENABLE;
		stHSDetInfo.bEnDlyKeyOff	= bEnDlyKeyOff;
		stHSDetInfo.bEnDlyKeyOn		= bEnDlyKeyOn;
		stHSDetInfo.bEnKeyOff		= bEnKeyOff;
		stHSDetInfo.bEnKeyOn		= bEnKeyOn;
		stHSDetInfo.bHsDetDbnc		= HSUNDETDBNC;
		stHSDetInfo.bDbncNumPlug	= HSUNDETDBNCNUM;
		stHSDetInfo.cbfunc		= NULL;
		err	= mc_asoc_set_hsdet((void *)&stHSDetInfo, NULL,
					    0x410000FE);
		if (err < MCDRV_SUCCESS)
			dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);
		stHSDetInfo.cbfunc	= hsdet_cb;
		err	= mc_asoc_set_hsdet((void *)&stHSDetInfo, NULL,
					    0x40000000);
		if (err < MCDRV_SUCCESS)
			dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);
		if (stHSDetInfo.bSgnlNum == 0xFF)
			mc_asoc_hpimpclass	= MC_ASOC_IMP_TBL_NUM - 1;
		mc_asoc_MBSEL4	= 0x80;
	}
	if ((mc_asoc_jack_status == SND_JACK_HEADPHONE)
	&& (dFlags & MCDRV_HSDET_EVT_MICDET_FLAG)
	&& (bEnMicDet & MCDRV_MICDET_ENABLE)) {
		if ((mc_asoc_hpimpclass >= MC_ASOC_IMPCLASS_THRESHOLD)
		&& (mc_asoc_hpimpclass != 5)
		&& (mc_asoc_hpimpclass != 6)) {
			cancel_delayed_work(&delayed_work_mkdeten);

			stHSDetInfo.bEnMicDet		= MCDRV_MICDET_DISABLE;
			stHSDetInfo.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnDlyKeyOn		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
			stHSDetInfo.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
			err	= mc_asoc_set_hsdet((void *)&stHSDetInfo, NULL,
						    0x7C);
			if (err < MCDRV_SUCCESS)
				dbg_info("%d: Error in MCDRV_SET_HSDET\n", err);
		} else {
			dbg_info("MICDET\n");
			mc_asoc_jack_status	= SND_JACK_HEADSET;
			cancel_delayed_work(&delayed_work_mkdeten);
			dbg_info("cancel_delayed_work_mkdeten\n");
			dbg_info("queue_delayed_work_mb4\n");
			queue_delayed_work(workq_mb4, &delayed_work_mb4,
							msecs_to_jiffies(MSDETMB4OFF));
		}
	}

	if (hpimpclass != mc_asoc_hpimpclass) {
		if ((mc_asoc_hpimpclass == (UINT8)-1)
		|| ((mc_asoc_hpimpclass >= MC_ASOC_IMPCLASS_THRESHOLD)
		    && (mc_asoc_hpimpclass != 5)
		    && (mc_asoc_hpimpclass != 6))) {
		} else {
			/* reapply the impedance compensated volumes */
			struct MCDRV_VOL_INFO	vol_info = { };

			if (!set_vol_info(mc_asoc_codec, &vol_info,
					  MC_ASOC_AVOL_HP) &&
			    !set_vol_info(mc_asoc_codec, &vol_info,
					  MC_ASOC_DVOL_DAC0OUT))
				_McDrv_Ctrl(MCDRV_SET_VOLUME, &vol_info, NULL, 0);
		}
	}

exit:
	;
	mc_asoc_unlock("hsdet_cb");
	if (jack_status != mc_asoc_jack_status) {
		dbg_info("mc_asoc_jack_status=%d\n", mc_asoc_jack_status);
		if (mc_asoc_jack_status != 0) {
			;
			mc_asoc_jack_report(0, SND_JACK_HEADSET);
		}
		mc_asoc_jack_report(mc_asoc_jack_status, SND_JACK_HEADSET);
	}
	if (jack_btn_on != 0) {
		mc_asoc_jack_report(jack_btn_stat, jack_btn_on);
	}
	if (jack_btn_off != 0) {
		mc_asoc_jack_report(0, jack_btn_off);
	}
	mutex_unlock(&hsdet_mutex);
}

static irqreturn_t mc_asoc_irq_thread(int irq, void *data)
{
	int	err;

	TRACE_FUNC();

	if (mc_asoc_suspended == 1) {
		/* handled on resume */
		mc_asoc_irq_func	= 1;
		return IRQ_HANDLED;
	}

	err	= _McDrv_Ctrl(MCDRV_IRQ, NULL, NULL, 0);
	if (err < 0)
		dev_err(mc_asoc_get_dev(), "failed to handle IRQ: %d\n",
			map_drv_error(err));

	return IRQ_HANDLED;
}

static int init_irq(struct snd_soc_component *codec)
{
	struct mc_asoc_data	*mc_asoc	= mc_asoc_get_mc_asoc(codec);
	int	err;

	if (!mc_asoc->irq) {
		dev_dbg(codec->dev, "no IRQ, headset and DSP events unavailable\n");
		return 0;
	}

	err	= request_threaded_irq(mc_asoc->irq, NULL, mc_asoc_irq_thread,
				       IRQF_ONESHOT, "ymu831", mc_asoc);
	if (err < 0)
		dev_err(codec->dev, "failed to request IRQ: %d\n", err);

	return err;
}

static void term_irq(void)
{
	struct mc_asoc_data	*mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);

	if (mc_asoc->irq)
		free_irq(mc_asoc->irq, mc_asoc);
}


/*
 * Codec device
 */
static int mc_asoc_probe(
	struct snd_soc_component *codec
)
{
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct device		*dev		= NULL;
	struct MCDRV_DIO_INFO	sDioInfo;
	struct MCDRV_DIOPATH_INFO	sDioPathInfo;
	int	err, err2;
	UINT32	update	= 0;
	struct MCDRV_REG_INFO	reg_info;

	TRACE_FUNC();


	mc_asoc_codec		= codec;
	mc_asoc_suspended	= 0;
	mc_asoc_hpimpclass	= (UINT8)-1;
	mc_asoc_jack_status	= 0;
	mc_asoc_irq_func	= 0;
	dsp_mem_pt		= 0;

	workq_mb4	= create_workqueue("mb4");
	if (workq_mb4 == NULL) {
		err	= -ENOMEM;
		goto error_codec_data;
	}
	INIT_DELAYED_WORK(&delayed_work_mb4, work_mb4);

	workq_mkdeten	= create_workqueue("mkdeten");
	if (workq_mkdeten == NULL) {
		err	= -ENOMEM;
		goto error_codec_data;
	}
	INIT_DELAYED_WORK(&delayed_work_mkdeten, work_mkdeten);


	if (codec == NULL) {
		err	= -ENODEV;
		goto error_codec_data;
	}

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	dev	= codec->dev;
	if (mc_asoc == NULL || dev == NULL) {
		err	= -ENODEV;
		goto error_codec_data;
	}

	/* init hardware */
	mc_asoc->setup	= mc_asoc_cfg_setup;
	ymu831_init_ports(mc_asoc);
	/* external supply for the digital core while its LDO is off */
	mc_asoc->setup.init2.bOption[19]	= mc_asoc->dvdd ? 0 : 1;
	err	= _McDrv_Ctrl(MCDRV_INIT, &mc_asoc->setup.init,
						&mc_asoc->setup.init2, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_INIT\n", err);
		err	= -EIO;
		goto error_init_hw;
	}
	mc_asoc->mbsel4		= -1;
	mc_asoc->dapm.valid	= false;

	reg_info.bRegType	= MCDRV_REGTYPE_ANA;
	reg_info.bAddress	= 0;
	err	= _McDrv_Ctrl(MCDRV_READ_REG, &reg_info, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_READ_REG\n", err);
		err	= -EIO;
		goto error_init_hw;
	}
	mc_asoc_ver_id	= reg_info.bData&0x07;

	if (mc_asoc_ver_id < 2) {
		err	= _McDrv_Ctrl(MCDRV_TERM, NULL, NULL, 0);
		if (err != MCDRV_SUCCESS) {
			dev_err(dev, "%d: Error in MCDRV_TERM\n", err);
			err	= -EIO;
			goto error_init_hw;
		}
		mc_asoc->setup.init.bMbSel1	= MCDRV_MBSEL_20;
		mc_asoc->setup.init.bMbSel2	= MCDRV_MBSEL_20;
		mc_asoc->setup.init.bMbSel3	= MCDRV_MBSEL_20;
		mc_asoc->setup.init.bMbSel4	= MCDRV_MBSEL_20;
		err	= _McDrv_Ctrl(MCDRV_INIT, &mc_asoc->setup.init,
						&mc_asoc->setup.init2, 0);
		if (err != MCDRV_SUCCESS) {
			dev_err(dev, "%d: Error in MCDRV_INIT\n", err);
			err	= -EIO;
			goto error_init_hw;
		}
	}

	if (mc_asoc_ver_id == 0) {
		vreg_map[MC_ASOC_AVOL_HP].volmap	= volmap_hp_es1;
		vreg_map[MC_ASOC_AVOL_LINEOUT2].volmap	= volmap_lineout;
		vreg_map[MC_ASOC_DVOL_ADIF0IN].volmap	= volmap_adif;
		vreg_map[MC_ASOC_DVOL_ADIF1IN].volmap	= volmap_adif;
	} else {
		vreg_map[MC_ASOC_AVOL_SP].volmap	= volmap_sp[4];
	}

	/* controls */
	err	= mc_asoc_add_controls(codec, mc_asoc_snd_controls,
				ARRAY_SIZE(mc_asoc_snd_controls));
	if (err < 0) {
		dev_err(dev, "%d: Error in mc_asoc_add_controls\n", err);
		goto error_add_ctl;
	}

	err	= mc_asoc_add_widgets(codec);
	if (err < 0) {
		dev_err(dev, "%d: Error in mc_asoc_add_widgets\n", err);
		goto error_add_ctl;
	}

	err	= snd_soc_add_component_controls(codec, mc_asoc_dsp_controls,
						 ARRAY_SIZE(mc_asoc_dsp_controls));
	if (err < 0) {
		dev_err(dev, "%d: Error adding DSP controls\n", err);
		goto error_add_ctl;
	}



	sDioInfo.asPortInfo[0]	= stMusicPort_Default;
	sDioInfo.asPortInfo[1]	= stExtPort_Default;
	sDioInfo.asPortInfo[2]	= stVoicePort_Default;
	sDioInfo.asPortInfo[3]	= stHifiPort_Default;

	update	= MCDRV_MUSIC_COM_UPDATE_FLAG
		| MCDRV_MUSIC_DIR_UPDATE_FLAG
		| MCDRV_MUSIC_DIT_UPDATE_FLAG
		| MCDRV_EXT_COM_UPDATE_FLAG
		| MCDRV_EXT_DIR_UPDATE_FLAG
		| MCDRV_EXT_DIT_UPDATE_FLAG
		| MCDRV_VOICE_COM_UPDATE_FLAG
		| MCDRV_VOICE_DIR_UPDATE_FLAG
		| MCDRV_VOICE_DIT_UPDATE_FLAG
		| MCDRV_HIFI_COM_UPDATE_FLAG
		| MCDRV_HIFI_DIR_UPDATE_FLAG
		| MCDRV_HIFI_DIT_UPDATE_FLAG;
	err	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO, &sDioInfo, NULL, update);
	if (err != MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_SET_DIGITALIO\n", err);
		goto error_set_mode;
	}

	update	= MCDRV_PHYS0_UPDATE_FLAG
		| MCDRV_PHYS1_UPDATE_FLAG
		| MCDRV_PHYS2_UPDATE_FLAG
		| MCDRV_PHYS3_UPDATE_FLAG
		| MCDRV_DIR0SLOT_UPDATE_FLAG
		| MCDRV_DIR1SLOT_UPDATE_FLAG
		| MCDRV_DIR2SLOT_UPDATE_FLAG
		| MCDRV_DIT0SLOT_UPDATE_FLAG
		| MCDRV_DIT1SLOT_UPDATE_FLAG
		| MCDRV_DIT2SLOT_UPDATE_FLAG;
	memcpy(sDioPathInfo.abPhysPort, mc_asoc->dio_ports,
	       sizeof(sDioPathInfo.abPhysPort));
	sDioPathInfo.abMusicRSlot[0]	= mc_asoc_cfg_setup.rslot[0];
	sDioPathInfo.abMusicRSlot[1]	= mc_asoc_cfg_setup.rslot[1];
	sDioPathInfo.abMusicRSlot[2]	= mc_asoc_cfg_setup.rslot[2];
	sDioPathInfo.abMusicTSlot[0]	= mc_asoc_cfg_setup.tslot[0];
	sDioPathInfo.abMusicTSlot[1]	= mc_asoc_cfg_setup.tslot[1];
	sDioPathInfo.abMusicTSlot[2]	= mc_asoc_cfg_setup.tslot[2];
	err	= _McDrv_Ctrl(MCDRV_SET_DIGITALIO_PATH, &sDioPathInfo, NULL,
			update);
	if (err != MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_SET_DIGITALIO_PATH\n", err);
		goto error_set_mode;
	}

	mc_asoc->hsdet_store	= stHSDetInfo_Default;
	mc_asoc->hsdet_store.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
	mc_asoc->hsdet_store.bEnDlyKeyOn	= MCDRV_KEYEN_D_D_D;
	mc_asoc->hsdet_store.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
	mc_asoc->hsdet_store.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
	mc_asoc->hsdet_store.cbfunc		= hsdet_cb;
	if (mc_asoc_ver_id == 0) {
		;
		mc_asoc->hsdet_store.bIrqType	= MCDRV_IRQTYPE_NORMAL;
	}
	err	= mc_asoc_set_hsdet((void *)&mc_asoc->hsdet_store,
				    (void *)&stHSDet2Info_Default, 0x7fffffff);
	if (err < MCDRV_SUCCESS) {
		dev_err(dev, "%d: Error in MCDRV_SET_HSDET\n", err);
		goto error_set_mode;
	}

	err	= _McDrv_Ctrl(MCDRV_IRQ, NULL, NULL, 0);
	if (err < 0) {
		dev_err(dev, "%d: Error in MCDRV_IRQ\n", err);
		goto error_set_mode;
	}

	/* IRQ Initialize */
	err = init_irq(codec);
	if (err < 0) {
		dev_err(dev, "%d: Error in init_irq\n", err);
		goto error_set_mode;
	}

	set_bias_level(codec, SND_SOC_BIAS_OFF);
	goto exit;

error_set_mode:
error_add_ctl:
	err2	= _McDrv_Ctrl(MCDRV_TERM, NULL, NULL, 0);
	if (err2 < 0) {
		;
		dev_err(dev, "%d: Error in MCDRV_TERM\n", err2);
	}
error_init_hw:
error_codec_data:
	if (workq_mb4)
		destroy_workqueue(workq_mb4);
	if (workq_mkdeten)
		destroy_workqueue(workq_mkdeten);
	workq_mb4	= NULL;
	workq_mkdeten	= NULL;
exit:
	return err;
}

static void mc_asoc_remove(struct snd_soc_component *codec)
{
	int err	= 0;
	struct mc_asoc_data	*mc_asoc	= NULL;

	/*TRACE_FUNC();*/

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return;

	/* IRQ terminate */
	term_irq();

	if (inp_dev) {
		input_unregister_device(inp_dev);
		inp_dev = NULL;
	}

	del_dsp_prm(mc_asoc);
	ymu831_dapm_reset(&mc_asoc->dapm);

	set_bias_level(codec, SND_SOC_BIAS_OFF);
	if (codec) {
		err	= _McDrv_Ctrl(MCDRV_TERM, NULL, NULL, 0);
		if (err != MCDRV_SUCCESS) {
			dev_err(codec->dev, "%d: Error in MCDRV_TERM\n", err);
			err	= -EIO;
		}
	}
	mc_asoc_enable_clock(0);
	mc_asoc_set_codec_ldod(0);
	if (mc_asoc->mic1_bias_enabled) {
		regulator_disable(mc_asoc->mic1_bias);
		mc_asoc->mic1_bias_enabled	= false;
	}

	destroy_workqueue(workq_mb4);
	destroy_workqueue(workq_mkdeten);
	workq_mb4	= NULL;
	workq_mkdeten	= NULL;
}

static int mc_asoc_suspend(
	struct snd_soc_component *codec
)
{
	int	err;
	struct mc_asoc_data	*mc_asoc	= NULL;
	struct MCDRV_HSDET_INFO	stHSDetInfo;

	TRACE_FUNC();

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;


	mc_asoc_lock("mc_asoc_suspend");

	err	= _McDrv_Ctrl(MCDRV_GET_HSDET,
				(void *)&mc_asoc->hsdet_store, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_suspend\n", err);
		err	= -EIO;
		goto error;
	}
	mc_asoc->hsdet_store.bDlyIrqStop	=
					stHSDetInfo_Default.bDlyIrqStop;

	if (device_may_wakeup(codec->dev))
		enable_irq_wake(mc_asoc->irq);

	stHSDetInfo	= stHSDetInfo_Suspend;
	if (mc_asoc_ver_id == 0) {
		;
		stHSDetInfo.bIrqType	= MCDRV_IRQTYPE_NORMAL;
	}
	if (mc_asoc_jack_status != SND_JACK_HEADSET) {
		stHSDetInfo.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
		stHSDetInfo.bEnDlyKeyOn		= MCDRV_KEYEN_D_D_D;
		stHSDetInfo.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
		stHSDetInfo.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
	}
	stHSDetInfo.bEnPlugDetDb	&= mc_asoc->hsdet_store.bEnPlugDetDb;
	stHSDetInfo.bEnMicDet		&= mc_asoc->hsdet_store.bEnMicDet;
	err	= mc_asoc_set_hsdet((void *)&stHSDetInfo, NULL, 0x7fffffff);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_suspend\n", err);
		err	= -EIO;
		goto error;
	}
	stHSDetInfo.cbfunc	= hsdet_cb;
	err	= mc_asoc_set_hsdet((void *)&stHSDetInfo, NULL, 0x40000000);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_suspend\n", err);
		err	= -EIO;
		goto error;
	}

	mc_asoc_suspended	= 1;

error:
	mc_asoc_unlock("mc_asoc_suspend");

	return err;
}

static int mc_asoc_resume(
	struct snd_soc_component *codec
)
{
	struct mc_asoc_data	*mc_asoc	= NULL;
	int	err;
	struct MCDRV_HSDET_INFO	stHSDetInfo;

	TRACE_FUNC();

	if (mc_asoc_suspended != 1)
		return 0;

	mc_asoc	= mc_asoc_get_mc_asoc(codec);
	if (mc_asoc == NULL)
		return -EINVAL;

	mc_asoc_suspended	= 0;

	if (mc_asoc_irq_func != 0) {
		err	= map_drv_error(_McDrv_Ctrl(MCDRV_IRQ, NULL, NULL, 0));
		mc_asoc_irq_func	= 0;
	}

	mc_asoc_lock("mc_asoc_resume");



	err	= _McDrv_Ctrl(MCDRV_GET_HSDET, (void *)&stHSDetInfo, NULL, 0);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_resume\n", err);
		err	= -EIO;
		goto error;
	}

	mc_asoc->hsdet_store.bEnPlugDetDb	=
		stHSDetInfo_Default.bEnPlugDetDb & stHSDetInfo.bEnPlugDetDb;
	mc_asoc->hsdet_store.bEnMicDet		= stHSDetInfo.bEnMicDet;
	if (mc_asoc_jack_status != SND_JACK_HEADSET) {
		mc_asoc->hsdet_store.bEnDlyKeyOff	= MCDRV_KEYEN_D_D_D;
		mc_asoc->hsdet_store.bEnDlyKeyOn	= MCDRV_KEYEN_D_D_D;
		mc_asoc->hsdet_store.bEnKeyOff		= MCDRV_KEYEN_D_D_D;
		mc_asoc->hsdet_store.bEnKeyOn		= MCDRV_KEYEN_D_D_D;
	} else {
		mc_asoc->hsdet_store.bEnDlyKeyOff	=
					stHSDetInfo_Default.bEnDlyKeyOff;
		mc_asoc->hsdet_store.bEnDlyKeyOn	=
					stHSDetInfo_Default.bEnDlyKeyOn;
		mc_asoc->hsdet_store.bEnKeyOff		=
					stHSDetInfo_Default.bEnKeyOff;
		mc_asoc->hsdet_store.bEnKeyOn		=
					stHSDetInfo_Default.bEnKeyOn;
	}
	mc_asoc->hsdet_store.cbfunc	= NULL;
	err	= mc_asoc_set_hsdet((void *)&mc_asoc->hsdet_store, NULL,
				    0x7fffffff);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_resume\n", err);
		err	= -EIO;
		goto error;
	}
	mc_asoc->hsdet_store.cbfunc	= hsdet_cb;
	err	= mc_asoc_set_hsdet((void *)&mc_asoc->hsdet_store, NULL,
				    0x40000000);
	if (err != MCDRV_SUCCESS) {
		dev_err(codec->dev, "%d: Error in mc_asoc_resume\n", err);
		err	= -EIO;
		goto error;
	}
	if (device_may_wakeup(codec->dev)) {
		if (!mc_asoc->irq)
			dev_err(codec->dev, "%s : No irq supported\n", __func__);
		else
			disable_irq_wake(mc_asoc->irq);
	}


error:
	mc_asoc_unlock("mc_asoc_resume");
	return err;
}

static int set_bias_level(
	struct snd_soc_component	*codec,
	enum snd_soc_bias_level	level)
{
	dev_dbg(codec->dev, "%s: level %d\n", __func__, level);
	return 0;
}
static int mc_asoc_set_bias_level(
	struct snd_soc_component	*codec,
	enum snd_soc_bias_level	level)
{
	mc_asoc_lock("mc_asoc_set_bias_level");
	set_bias_level(codec, level);
	mc_asoc_unlock("mc_asoc_set_bias_level");
	return 0;
}

static int mc_asoc_register_keys(struct device *dev)
{
	int	i, err;

	inp_dev	= input_allocate_device();
	if (!inp_dev)
		return -ENOMEM;

	inp_dev->name = "Headset keys";
	input_set_capability(inp_dev, EV_KEY, MC_ASOC_EV_KEY_DELAYKEYON0);
	input_set_capability(inp_dev, EV_KEY, MC_ASOC_EV_KEY_DELAYKEYON1);
	input_set_capability(inp_dev, EV_KEY, MC_ASOC_EV_KEY_DELAYKEYON2);
	for (i = 0; i < 8; i++) {
		input_set_capability(inp_dev, EV_KEY,
				     mc_asoc_ev_key_delaykeyoff0[i]);
		input_set_capability(inp_dev, EV_KEY,
				     mc_asoc_ev_key_delaykeyoff1[i]);
		input_set_capability(inp_dev, EV_KEY,
				     mc_asoc_ev_key_delaykeyoff2[i]);
	}
	err	= input_register_device(inp_dev);
	if (err != 0) {
		dev_err(dev, "%d: Error in input_register_device\n", err);
		input_free_device(inp_dev);
		inp_dev	= NULL;
	}
	return err;
}

/*
 * Headset detection is opt-in: a machine driver for a board with a headset
 * socket passes its jack here, which arms plug, mic and button detection.
 */
static int mc_asoc_set_jack(struct snd_soc_component *codec,
			    struct snd_soc_jack *jack, void *data)
{
	struct mc_asoc_data	*mc_asoc	= mc_asoc_get_mc_asoc(codec);
	int	err;

	if (!mc_asoc)
		return -EINVAL;

	if (jack) {
		snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_MEDIA);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
		if (!inp_dev) {
			err	= mc_asoc_register_keys(codec->dev);
			if (err)
				return err;
		}
	}

	mc_asoc_lock("mc_asoc_set_jack");
	mc_asoc->jack.hs_jack	= jack;
	err	= mc_asoc_set_hsdet((void *)&mc_asoc->hsdet_store,
				    (void *)&stHSDet2Info_Default, 0x7fffffff);
	mc_asoc_unlock("mc_asoc_set_jack");
	if (err < MCDRV_SUCCESS)
		dev_err(codec->dev, "%d: Error in MCDRV_SET_HSDET\n", err);

	device_init_wakeup(codec->dev, !!jack);

	if (!jack && inp_dev) {
		input_unregister_device(inp_dev);
		inp_dev	= NULL;
	}

	return map_drv_error(err);
}

static const struct snd_soc_component_driver mc_asoc_codec_dev = {
	.probe		= mc_asoc_probe,
	.remove		= mc_asoc_remove,
	.suspend	= mc_asoc_suspend,
	.resume		= mc_asoc_resume,
	.read		= mc_asoc_read_reg,
	.write		= mc_asoc_write_reg,
	.endianness	= 1,
	.idle_bias_on	= 0,
	.set_bias_level	= mc_asoc_set_bias_level,
	.set_jack	= mc_asoc_set_jack,
	.seq_notifier	= ymu831_seq_notifier,
};

/*
 * SPI framing: the command byte carries the register address in bits 6:1,
 * bit 7 selects a read and bit 0 a burst, in which all following bytes go
 * to the same register until chip select is released.
 */
#define YMU831_SPI_READ		0x80
#define YMU831_SPI_BURST	0x01

static int ymu831_spi_reg_write(void *context, unsigned int reg,
				unsigned int val)
{
	u8 buf[2] = { reg << 1, val };

	return spi_write_then_read(context, buf, sizeof(buf), NULL, 0);
}

static int ymu831_spi_reg_read(void *context, unsigned int reg,
			       unsigned int *val)
{
	u8 cmd = (reg << 1) | YMU831_SPI_READ;
	u8 data;
	int ret;

	ret = spi_write_then_read(context, &cmd, 1, &data, 1);
	if (ret)
		return ret;

	*val = data;
	return 0;
}

static int ymu831_spi_reg_noinc_write(void *context, unsigned int reg,
				      const void *val, size_t val_count)
{
	u8 *buf;
	int ret;

	buf = kmalloc(val_count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = (reg << 1) | YMU831_SPI_BURST;
	memcpy(buf + 1, val, val_count);
	ret = spi_write(context, buf, val_count + 1);
	kfree(buf);

	return ret;
}

static int ymu831_spi_reg_noinc_read(void *context, unsigned int reg,
				     void *val, size_t val_count)
{
	u8 cmd = (reg << 1) | YMU831_SPI_READ | YMU831_SPI_BURST;

	return spi_write_then_read(context, &cmd, 1, val, val_count);
}

static const struct regmap_bus ymu831_spi_bus = {
	.reg_write = ymu831_spi_reg_write,
	.reg_read = ymu831_spi_reg_read,
	.reg_noinc_write = ymu831_spi_reg_noinc_write,
	.reg_noinc_read = ymu831_spi_reg_noinc_read,
};

/* Data ports of the register windows and FIFOs: reading them has effects */
static bool ymu831_precious_reg(struct device *dev, unsigned int reg)
{
	/* the DSP banks */
	switch (reg >> 8) {
	case MCDRV_REGTYPE_B:
	case MCDRV_REGTYPE_C:
	case MCDRV_REGTYPE_E:
	case MCDRV_REGTYPE_F:
		return true;
	}

	switch (reg) {
	case MCI_A_REG_D:
	case MCI_ANA_REG_D:
	case MCI_CD_REG_D:
	case MCI_MA_REG_D:
	case MCI_MB_REG_D:
	case MCI_B_REG_D:
	case MCI_BMAD:
	case MCI_E_REG_D:
	case MCI_EDSP_FW_D:
	case MCI_C_REG_D:
	case MCI_DEC_FIFO:
	case MCI_FSQ_FFIFO:
	case MCI_F_REG_D:
	case MCI_FMAD:
		return true;
	default:
		return false;
	}
}

/*
 * The register banks are reached through an address and a data register
 * on the interface. They are mapped at YMU831_REG(bank, address), with
 * the bank numbers of the core's MCDRV_REGTYPE_*.
 */
#define YMU831_REG(bank, addr)	(((bank) << 8) | (addr))

#define YMU831_BANK(_bank, _size, _sel, _mask, _data)			\
	{								\
		.name = #_bank,						\
		.range_min = YMU831_REG(MCDRV_REGTYPE_##_bank, 0),	\
		.range_max = YMU831_REG(MCDRV_REGTYPE_##_bank, (_size) - 1), \
		.selector_reg = (_sel),					\
		.selector_mask = (_mask),				\
		.window_start = (_data),				\
		.window_len = 1,					\
	}

static const struct regmap_range_cfg ymu831_banks[] = {
	YMU831_BANK(A, MCDRV_REG_NUM_A, MCI_A_REG_A, 0x7f, MCI_A_REG_D),
	YMU831_BANK(MA, MCDRV_REG_NUM_MA, MCI_MA_REG_A, 0x7f, MCI_MA_REG_D),
	YMU831_BANK(MB, MCDRV_REG_NUM_MB, MCI_MB_REG_A, 0x7f, MCI_MB_REG_D),
	YMU831_BANK(B, MCDRV_REG_NUM_B, MCI_B_REG_A, 0x7f, MCI_B_REG_D),
	YMU831_BANK(E, MCDRV_REG_NUM_E, MCI_E_REG_A, 0x7f, MCI_E_REG_D),
	YMU831_BANK(C, MCDRV_REG_NUM_C, MCI_C_REG_A, 0xff, MCI_C_REG_D),
	YMU831_BANK(F, MCDRV_REG_NUM_F, MCI_F_REG_A, 0x7f, MCI_F_REG_D),
	YMU831_BANK(ANA, MCDRV_REG_NUM_ANA, MCI_ANA_REG_A, 0x7f,
		    MCI_ANA_REG_D),
	YMU831_BANK(CD, MCDRV_REG_NUM_CD, MCI_CD_REG_A, 0x7f, MCI_CD_REG_D),
};

static bool ymu831_readable_reg(struct device *dev, unsigned int reg)
{
	unsigned int addr = reg & 0xff;

	switch (reg >> 8) {
	case MCDRV_REGTYPE_IF:
		return addr < MCDRV_REG_NUM_IF;
	case MCDRV_REGTYPE_A:
		return addr < MCDRV_REG_NUM_A;
	case MCDRV_REGTYPE_MA:
		return addr < MCDRV_REG_NUM_MA;
	case MCDRV_REGTYPE_MB:
		return addr < MCDRV_REG_NUM_MB;
	case MCDRV_REGTYPE_B:
		return addr < MCDRV_REG_NUM_B;
	case MCDRV_REGTYPE_E:
		return addr < MCDRV_REG_NUM_E;
	case MCDRV_REGTYPE_C:
		return addr < MCDRV_REG_NUM_C;
	case MCDRV_REGTYPE_F:
		return addr < MCDRV_REG_NUM_F;
	case MCDRV_REGTYPE_ANA:
		return addr < MCDRV_REG_NUM_ANA;
	case MCDRV_REGTYPE_CD:
		return addr < MCDRV_REG_NUM_CD;
	default:
		return false;
	}
}

/*
 * The core keeps its own shadow of the registers, and it moves the bank
 * address registers behind regmap's back for burst transfers, so no cache.
 */
static const struct regmap_config ymu831_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = YMU831_REG(MCDRV_REGTYPE_CD, MCDRV_REG_NUM_CD - 1),
	.readable_reg = ymu831_readable_reg,
	.writeable_reg = ymu831_readable_reg,
	.precious_reg = ymu831_precious_reg,
	.ranges = ymu831_banks,
	.num_ranges = ARRAY_SIZE(ymu831_banks),
	.cache_type = REGCACHE_NONE,
};

static struct regmap *mc_asoc_get_regmap(void)
{
	struct mc_asoc_data	*mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);

	return mc_asoc ? mc_asoc->regmap : NULL;
}

void mc_asoc_reg_write(UINT8 bRegType, UINT16 wAddr, UINT8 bData)
{
	int	err;

	err = regmap_write(mc_asoc_get_regmap(), YMU831_REG(bRegType, wAddr),
			   bData);
	if (err)
		dev_err(mc_asoc_get_dev(), "failed to write %u:%u: %d\n",
			bRegType, wAddr, err);
}

void mc_asoc_reg_write_burst(UINT8 bRegType, UINT16 wAddr, const UINT8 *pbData,
			     UINT32 dCount)
{
	int	err;

	err = regmap_noinc_write(mc_asoc_get_regmap(),
				 YMU831_REG(bRegType, wAddr), pbData, dCount);
	if (err)
		dev_err(mc_asoc_get_dev(), "failed to write %u:%u: %d\n",
			bRegType, wAddr, err);
}

/* Execute a packet of command bytes as built by the core */
void mc_asoc_write_data(UINT8 bSlaveAdr, const UINT8 *pbData, UINT32 dSize)
{
	struct regmap	*regmap	= mc_asoc_get_regmap();
	UINT32	i;
	int	err	= 0;

	for (i = 0; i + 1 < dSize && !err; i += 2) {
		unsigned int reg = pbData[i] >> 1;

		if (pbData[i] & YMU831_SPI_BURST) {
			/* a burst extends to the end of the packet */
			err = regmap_noinc_write(regmap, reg, &pbData[i + 1],
						 dSize - i - 1);
			break;
		}
		err = regmap_write(regmap, reg, pbData[i + 1]);
	}

	if (err)
		dev_err(mc_asoc_get_dev(), "register write failed: %d\n", err);
}

void mc_asoc_read_data(UINT8 bSlaveAdr, UINT32 dAddress, UINT8 *pbData,
		       UINT32 dSize)
{
	struct regmap	*regmap	= mc_asoc_get_regmap();
	unsigned int	val;
	int	err;

	if (dSize > 1) {
		err = regmap_noinc_read(regmap, dAddress, pbData, dSize);
	} else {
		err = regmap_read(regmap, dAddress, &val);
		pbData[0] = val;
	}

	if (err)
		dev_err(mc_asoc_get_dev(), "register read failed: %d\n", err);
}

/*
 * Called by the core to supply the digital core externally while the
 * codec's own digital LDO is off.
 */
void mc_asoc_set_codec_ldod(int status)
{
	struct mc_asoc_data	*mc_asoc;
	int	err;

	mc_asoc	= mc_asoc_get_mc_asoc(mc_asoc_codec);
	if (!mc_asoc || !mc_asoc->dvdd || mc_asoc->dvdd_enabled == !!status)
		return;

	if (status) {
		err = regulator_enable(mc_asoc->dvdd);
		if (err) {
			dev_err(mc_asoc_get_dev(), "failed to enable DVDD: %d\n",
				err);
			return;
		}
	} else {
		regulator_disable(mc_asoc->dvdd);
	}
	mc_asoc->dvdd_enabled	= !!status;
}

static void ymu831_release_instance(void *data)
{
	mc_asoc_spi	= NULL;
}

static int ymu831_parse_dio_ports(struct device *dev,
				  struct mc_asoc_data *mc_asoc)
{
	static const UINT8 phys_port[] = {
		MCDRV_PHYSPORT_DIO0, MCDRV_PHYSPORT_DIO1, MCDRV_PHYSPORT_DIO2,
	};
	u32 dio[ARRAY_SIZE(mc_asoc->dio_ports)] = { 0, 2, 1, 0 };
	unsigned int i;
	int err;

	err = device_property_read_u32_array(dev, "yamaha,dio-ports", dio,
					     ARRAY_SIZE(dio));
	if (err && err != -EINVAL)
		return dev_err_probe(dev, err, "invalid yamaha,dio-ports\n");

	for (i = 0; i < ARRAY_SIZE(dio); i++) {
		if (dio[i] >= ARRAY_SIZE(phys_port))
			return dev_err_probe(dev, -EINVAL,
					     "invalid DIO port %u\n", dio[i]);
		mc_asoc->dio_ports[i] = phys_port[dio[i]];
	}

	return 0;
}

static int mc_asoc_spi_probe(
	struct spi_device *spi)
{
	struct mc_asoc_priv	*mc_asoc_priv;
	struct mc_asoc_data	*mc_asoc;
	int	err;

	/* the core keeps its state in globals */
	if (mc_asoc_spi)
		return dev_err_probe(&spi->dev, -EBUSY,
				     "only one instance is supported\n");

	mc_asoc_spi	= spi;
	err = devm_add_action_or_reset(&spi->dev, ymu831_release_instance,
				       NULL);
	if (err)
		return err;

	TRACE_FUNC();

	mc_asoc_priv	= devm_kzalloc(&spi->dev, sizeof(struct mc_asoc_priv),
				       GFP_KERNEL);
	if (!mc_asoc_priv) {
		err	= -ENOMEM;
		goto err_alloc_priv;
	}
	mc_asoc	= &mc_asoc_priv->data;
	mc_asoc->irq	= spi->irq;

	mc_asoc->dvdd = devm_regulator_get_optional(&spi->dev, "dvdd");
	if (IS_ERR(mc_asoc->dvdd)) {
		if (PTR_ERR(mc_asoc->dvdd) != -ENODEV)
			return dev_err_probe(&spi->dev, PTR_ERR(mc_asoc->dvdd),
					     "failed to get DVDD supply\n");
		mc_asoc->dvdd = NULL;
	}

	err = ymu831_parse_dio_ports(&spi->dev, mc_asoc);
	if (err)
		return err;

	mc_asoc->mic1_bias = devm_regulator_get_optional(&spi->dev, "mic1-bias");
	if (IS_ERR(mc_asoc->mic1_bias)) {
		if (PTR_ERR(mc_asoc->mic1_bias) != -ENODEV)
			return dev_err_probe(&spi->dev,
					     PTR_ERR(mc_asoc->mic1_bias),
					     "failed to get MIC1 bias supply\n");
		mc_asoc->mic1_bias = NULL;
	}

	/* gated by the core, see mc_asoc_enable_clock() */
	mc_asoc->mclk = devm_clk_get_optional(&spi->dev, "mclk");
	if (IS_ERR(mc_asoc->mclk))
		return dev_err_probe(&spi->dev, PTR_ERR(mc_asoc->mclk),
				     "failed to get MCLK\n");

	mc_asoc->regmap = devm_regmap_init(&spi->dev, &ymu831_spi_bus, spi,
					   &ymu831_regmap_config);
	if (IS_ERR(mc_asoc->regmap))
		return dev_err_probe(&spi->dev, PTR_ERR(mc_asoc->regmap),
				     "failed to initialise regmap\n");

	err = devm_mutex_init(&spi->dev, &mc_asoc->mutex);
	if (err)
		return err;

	dev_set_drvdata(&spi->dev, mc_asoc_priv);

	err	= devm_snd_soc_register_component(&spi->dev, &mc_asoc_codec_dev,
						  mc_asoc_dai,
						  ARRAY_SIZE(mc_asoc_dai));
	if (err < 0)
		goto err_reg_codec;

	goto exit;

err_reg_codec:
err_alloc_priv:
	dev_err(&spi->dev, "err=%d: failed to probe MC_ASOC\n", err);

exit:
	return err;
}

static const struct of_device_id mc_asoc_of_match[] = {
	{ .compatible = "yamaha,ymu831" },
	{ }
};
MODULE_DEVICE_TABLE(of, mc_asoc_of_match);

static const struct spi_device_id mc_asoc_spi_ids[] = {
	{ "ymu831" },
	{ }
};
MODULE_DEVICE_TABLE(spi, mc_asoc_spi_ids);

static struct spi_driver	mc_asoc_spi_driver = {
	.driver = {
		.name	= MC_ASOC_HWDEP_ID,
		.of_match_table = mc_asoc_of_match,
	},
	.id_table = mc_asoc_spi_ids,
	.probe	= mc_asoc_spi_probe,
};
/*
 * Module init and exit
 */
static int __init ymu831_init(void)
{
	int	err	= 0;

	mutex_init(&mc_asoc_mutex);
	mutex_init(&hsdet_mutex);

	err	= spi_register_driver(&mc_asoc_spi_driver);

	return err;
}
module_init(ymu831_init);

static void __exit ymu831_exit(void)
{
	spi_unregister_driver(&mc_asoc_spi_driver);
	mutex_destroy(&mc_asoc_mutex);
	mutex_destroy(&hsdet_mutex);
}
module_exit(ymu831_exit);

MODULE_AUTHOR("Yamaha Corporation");
MODULE_DESCRIPTION("Yamaha YMU831 ALSA SoC codec driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(MC_ASOC_DRIVER_VERSION);
