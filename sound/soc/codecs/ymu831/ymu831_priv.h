/* SPDX-License-Identifier: Zlib */
/*
 * YMU831 ASoC codec driver - private header
 *
 * Copyright (c) 2012-2013 Yamaha Corporation
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef YMU831_PRIV_H
#define YMU831_PRIV_H

#include <sound/soc.h>
#include "mcdriver.h"
#include "ymu831_dapm.h"

#undef	MC_ASOC_TEST

/*
 * Virtual registers
 */
enum {
	MC_ASOC_DVOL_MUSICIN,
	MC_ASOC_DVOL_EXTIN,
	MC_ASOC_DVOL_VOICEIN,
	MC_ASOC_DVOL_REFIN,
	MC_ASOC_DVOL_ADIF0IN,
	MC_ASOC_DVOL_ADIF1IN,
	MC_ASOC_DVOL_ADIF2IN,
	MC_ASOC_DVOL_MUSICOUT,
	MC_ASOC_DVOL_EXTOUT,
	MC_ASOC_DVOL_VOICEOUT,
	MC_ASOC_DVOL_REFOUT,
	MC_ASOC_DVOL_DAC0OUT,
	MC_ASOC_DVOL_DAC1OUT,
	MC_ASOC_DVOL_DPATHDA,
	MC_ASOC_DVOL_DPATHAD,
	MC_ASOC_AVOL_LINEIN1,
	MC_ASOC_AVOL_MIC1,
	MC_ASOC_AVOL_MIC2,
	MC_ASOC_AVOL_MIC3,
	MC_ASOC_AVOL_MIC4,
	MC_ASOC_AVOL_HP,
	MC_ASOC_AVOL_SP,
	MC_ASOC_AVOL_RC,
	MC_ASOC_AVOL_LINEOUT1,
	MC_ASOC_AVOL_LINEOUT2,
	MC_ASOC_AVOL_SP_GAIN,

	MC_ASOC_SWITCH_CLOCK,

	MC_ASOC_ADIF0_SWAP,
	MC_ASOC_ADIF1_SWAP,
	MC_ASOC_ADIF2_SWAP,

	MC_ASOC_DAC0_SWAP,
	MC_ASOC_DAC1_SWAP,

	MC_ASOC_MUSIC_OUT0_SWAP,

	MC_ASOC_MUSIC_IN0_SWAP,
	MC_ASOC_MUSIC_IN1_SWAP,
	MC_ASOC_MUSIC_IN2_SWAP,

	MC_ASOC_EXT_IN_SWAP,

	MC_ASOC_VOICE_IN_SWAP,

	MC_ASOC_MUSIC_OUT1_SWAP,
	MC_ASOC_MUSIC_OUT2_SWAP,

	MC_ASOC_EXT_OUT_SWAP,

	MC_ASOC_VOICE_OUT_SWAP,

	MC_ASOC_CLEAR_DSP_PARAM,

	MC_ASOC_N_REG
};

#define MC_ASOC_N_VOL_REG			(MC_ASOC_AVOL_SP_GAIN + 1)

/*
 * Driver private data structure
 */
struct mc_asoc_setup {
	struct MCDRV_INIT_INFO	init;
	struct MCDRV_INIT2_INFO	init2;
	unsigned char	rslot[3];
	unsigned char	tslot[3];
};

struct mc_asoc_port_params {
	UINT8	rate;
	UINT8	bits[SNDRV_PCM_STREAM_LAST+1];
	UINT8	pcm_mono[SNDRV_PCM_STREAM_LAST+1];
	UINT8	pcm_order[SNDRV_PCM_STREAM_LAST+1];
	UINT8	pcm_law[SNDRV_PCM_STREAM_LAST+1];
	UINT8	master;
	UINT8	inv;
	UINT8	srcthru;
	UINT8	interface;
	UINT8	format;
	UINT8	bckfs;
	unsigned int	bclk_ratio;
	UINT8	channels;
	UINT8	stream;	/* bit0: Playback, bit1: Capture */
	UINT8	hold[SNDRV_PCM_STREAM_LAST + 1];	/* core port hold bit */
};

struct mc_asoc_dsp_param {
	UINT8	*pabParam;
	UINT32	dSize;
	struct mc_asoc_dsp_param	*next;
};

struct mc_asoc_jack {
	struct snd_soc_jack *hs_jack;
};

struct mc_asoc_data {
	struct mutex			mutex;
	struct mc_asoc_setup		setup;
	struct mc_asoc_port_params	port[3];
	UINT8				dio_ports[4];
	struct MCDRV_CLOCKSW_INFO	clocksw_store;
	struct MCDRV_PATH_INFO		path_store;
	struct MCDRV_VOL_INFO		vol_store;
	struct MCDRV_DIO_INFO		dio_store;
	struct MCDRV_DIOPATH_INFO	diopath_store;
	struct MCDRV_SWAP_INFO		swap_store;
	struct MCDRV_HSDET_INFO		hsdet_store;
	struct MCDRV_HSDET2_INFO	hsdet2_store;
	struct mc_asoc_dsp_param	param_store[4][2];
	struct mc_asoc_jack		jack;
	struct ymu831_dapm		dapm;
	int				mbsel4;
	UINT8				port_hold;
	struct regmap			*regmap;
	struct clk			*mclk;
	bool				mclk_enabled;
	struct regulator		*dvdd;
	bool				dvdd_enabled;
	struct regulator		*mic1_bias;
	bool				mic1_bias_enabled;
	int				irq;
};

struct mc_asoc_priv {
	struct mc_asoc_data	data;
};

void	mc_asoc_reg_write(UINT8 bRegType, UINT16 wAddr, UINT8 bData);
void	mc_asoc_reg_write_burst(UINT8 bRegType, UINT16 wAddr,
				const UINT8 *pbData, UINT32 dCount);
extern void	mc_asoc_write_data(UINT8 bSlaveAdr,
				const UINT8 *pbData,
				UINT32 dSize);
extern void	mc_asoc_read_data(UINT8	bSlaveAdr,
				UINT32 dAddress,
				UINT8 *pbData,
				UINT32 dSize);
extern	void	mc_asoc_set_codec_ldod(int status);
extern	UINT8	mc_asoc_get_bus_select(void);
extern	void	mc_asoc_enable_clock(int enable);

/*
 * Debugging: the core command dumper and the traces are dynamic debug
 * messages, register traffic is visible through the SPI tracepoints.
 */
#define dbg_info(format, arg...)	dev_dbg(mc_asoc_get_dev(), format, ## arg)
#define TRACE_FUNC()	dev_dbg(mc_asoc_get_dev(), "%s\n", __func__)

struct device *mc_asoc_get_dev(void);
#define _McDrv_Ctrl	McDrv_Ctrl_dbg

extern SINT32	McDrv_Ctrl_dbg(
			UINT32 dCmd, void *pvPrm1, void *pvPrm2, UINT32 dPrm);

#endif /* YMU831_PRIV_H */
