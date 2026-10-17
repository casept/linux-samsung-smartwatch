/* SPDX-License-Identifier: Zlib */
/*
 * YMU831 ASoC codec driver
 *
 * Copyright (c) 2012 Yamaha Corporation
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

#ifndef YMU831_H
#define YMU831_H

#include <sound/pcm.h>
#include <sound/soc.h>
#include "mcdriver.h"

#define MC_ASOC_NAME			"ymu831"

/* DSP parameter groups */
#define YMC_DSP_OUTPUT_BASE		(0x00000000)
#define YMC_DSP_INPUT_BASE		(0x00000010)
#define YMC_DSP_VOICECALL_BASE_1MIC	(0x00000100)
#define YMC_DSP_VOICECALL_BASE_2MIC	(0x00000200)
#define YMC_DSP_VOICECALL_BASE_COMMON	(0x00000F00)

#endif
