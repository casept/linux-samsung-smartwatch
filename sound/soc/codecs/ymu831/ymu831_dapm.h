/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef YMU831_DAPM_H
#define YMU831_DAPM_H

#include <sound/soc.h>

#include "mcdriver.h"

struct ymu831_dapm {
	/* sink and source widget of every route bit */
	struct snd_soc_dapm_widget	**widgets;
	struct snd_soc_dapm_widget	*micbias[BIAS_PATH_CHANNELS];
};

extern const struct snd_soc_dapm_widget ymu831_dapm_widgets[];
extern const unsigned int ymu831_num_dapm_widgets;
extern const struct snd_soc_dapm_route ymu831_dapm_routes[];
extern const unsigned int ymu831_num_dapm_routes;

void ymu831_dapm_reset(struct ymu831_dapm *ymu_dapm);
int ymu831_dapm_build_path(struct snd_soc_component *component,
			   struct ymu831_dapm *ymu_dapm,
			   struct MCDRV_PATH_INFO *path);

/* in ymu831.c: push the DAPM routing to the CODEC */
void ymu831_dapm_update(struct snd_soc_component *component);

#endif
