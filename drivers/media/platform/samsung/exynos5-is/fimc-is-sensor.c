/*
 * Samsung EXYNOS5250 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Arun Kumar K <arun.kk@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "fimc-is-sensor.h"

static const struct sensor_drv_data s5k6a3_drvdata = {
	.id		= FIMC_IS_SENSOR_ID_S5K6A3,
	.open_timeout	= S5K6A3_OPEN_TIMEOUT,
	.setfile_name	= "exynos5_s5k6a3_setfile.bin",
};

static const struct sensor_drv_data s5k4e5_drvdata = {
	.id		= FIMC_IS_SENSOR_ID_S5K4E5,
	.open_timeout	= S5K4E5_OPEN_TIMEOUT,
	.setfile_name	= "exynos5_s5k4e5_setfile.bin",
};

static const struct of_device_id fimc_is_sensor_of_ids[] = {
	{
		.compatible	= "samsung,s5k6a3",
		.data		= &s5k6a3_drvdata,
	},
	{
		.compatible	= "samsung,s5k4e5",
		.data		= &s5k4e5_drvdata,
	},
	{  }
};

const struct sensor_drv_data *exynos5_is_sensor_get_drvdata(
			struct device_node *node)
{
	const struct of_device_id *of_id;

	of_id = of_match_node(fimc_is_sensor_of_ids, node);
	return of_id ? of_id->data : NULL;
}
