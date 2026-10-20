// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2024, Davids Paskevics <davids.paskevics@gmail.com>.
 */

#include <linux/iio/common/ssp_sensors.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include "../common/ssp_sensors/ssp_iio_sensor.h"

#define SSP_HRM_RAW_FAC_NAME "ssp-hrm-raw-fac"
static const char ssp_hrm_raw_fac_device_name[] = SSP_HRM_RAW_FAC_NAME;

/* TODO: Remove, only here for reference */
struct ssp_hrm_raw_fac {
	u32 ch_a;
	u32 ch_b;
	u32 frequency;
	u32 noise_value;
	u32 dc_value;
	u32 ac_value;
	u32 perfusion_rate;
	u32 snrac;
	u32 snrdc;
};

enum ssp_hrm_channel_raw {
	SSP_CHANNEL_HRM_CH_A,
	SSP_CHANNEL_HRM_CH_B,
	SSP_CHANNEL_HRM_FREQUENCY,
	SSP_CHANNEL_HRM_NOISE,
	SSP_CHANNEL_HRM_DC,
	SSP_CHANNEL_HRM_AC,
	SSP_CHANNEL_HRM_PERFUSION_RATE,
	SSP_CHANNEL_HRM_SNR_AC,
	SSP_CHANNEL_HRM_SNR_DC,
	SSP_CHANNEL_SCAN_INDEX_TIME_RAW,
};

static int ssp_hrm_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	u32 t;
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		t = ssp_get_sensor_delay(data, SSP_BIO_HRM_RAW_FAC);
		ssp_convert_to_freq(t, val, val2);
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		break;
	}

	return -EINVAL;
}

static int ssp_hrm_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	int ret;
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = ssp_convert_to_time(val, val2);
		ret = ssp_change_delay(data, SSP_BIO_HRM_RAW_FAC, ret);
		if (ret < 0)
			dev_err(&indio_dev->dev,
				"Failed to enable HRM sensor!\n");

		return ret;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info ssp_hrm_iio_info = {
	.read_raw = &ssp_hrm_read_raw,
	.write_raw = &ssp_hrm_write_raw,
};

static const unsigned long ssp_hrm_scan_mask[] = {
	/* All 9 or none, don't allow selecting */
	0x1FF,
	0,
};

static const struct iio_chan_spec ssp_hrm_channels_raw_fac[] = {
	/* COUNT is not correct, but for some of these a better type doesn't exist */
	/* Taken from downstream code's struct */
	SSP_COUNT_CHANNEL(SSP_CHANNEL_HRM_CH_A, 'u', 32),
	SSP_COUNT_CHANNEL(SSP_CHANNEL_HRM_CH_B, 'u', 32),
	SSP_COUNT_CHANNEL(SSP_CHANNEL_HRM_FREQUENCY, 'u', 32),
	SSP_COUNT_CHANNEL(SSP_CHANNEL_HRM_NOISE, 'u', 32),
	SSP_COUNT_CHANNEL(SSP_CHANNEL_HRM_DC, 'u', 32),
	SSP_COUNT_CHANNEL(SSP_CHANNEL_HRM_AC, 'u', 32),
	SSP_COUNT_CHANNEL(SSP_CHANNEL_HRM_PERFUSION_RATE, 'u', 32),
	SSP_COUNT_CHANNEL(SSP_CHANNEL_HRM_SNR_AC, 'u', 32),
	SSP_COUNT_CHANNEL(SSP_CHANNEL_HRM_SNR_DC, 'u', 32),
	SSP_CHAN_TIMESTAMP(SSP_CHANNEL_SCAN_INDEX_TIME_RAW),
};

static int ssp_process_hrm_raw_fac_data(struct iio_dev *indio_dev, void *buf,
					int64_t timestamp)
{
	return ssp_common_process_data(indio_dev, buf, SSP_BIO_HRM_RAW_FAC_SIZE,
				       timestamp);
}

static const struct iio_buffer_setup_ops ssp_hrm_raw_fac_buffer_ops = {
	.postenable = &ssp_common_buffer_postenable,
	.predisable = &ssp_common_buffer_predisable,
	.postdisable = &ssp_common_buffer_postdisable,
};

static int ssp_hrm_raw_fac_probe(struct platform_device *pdev)
{
	int ret;
	struct iio_dev *indio_dev;
	struct ssp_sensor_data *spd;

	dev_info(&pdev->dev, "Probing SSP HRM factory raw driver\n");

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*spd));
	if (!indio_dev)
		return -ENOMEM;

	spd = iio_priv(indio_dev);

	spd->process_data = ssp_process_hrm_raw_fac_data;
	spd->type = SSP_BIO_HRM_RAW_FAC;

	indio_dev->name = ssp_hrm_raw_fac_device_name;
	indio_dev->info = &ssp_hrm_iio_info;
	indio_dev->channels = ssp_hrm_channels_raw_fac;
	indio_dev->num_channels = ARRAY_SIZE(ssp_hrm_channels_raw_fac);
	indio_dev->available_scan_masks = ssp_hrm_scan_mask;

	ret = devm_iio_kfifo_buffer_setup(&pdev->dev, indio_dev,
					  &ssp_hrm_raw_fac_buffer_ops);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, indio_dev);

	/* Unregistered only after the buffer got disabled on removal */
	ret = ssp_register_consumer(indio_dev, SSP_BIO_HRM_RAW_FAC);
	if (ret)
		return ret;

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register IIO device!\n");
		return ret;
	}

	dev_info(&pdev->dev, "Probe OK!\n");
	return 0;
}

static const struct platform_device_id ssp_hrm_raw_fac_id_table[] = {
	{ SSP_HRM_RAW_FAC_NAME },
	{ }
};
MODULE_DEVICE_TABLE(platform, ssp_hrm_raw_fac_id_table);

static struct platform_driver ssp_hrm_raw_fac_driver = {
	.driver = {
		.name = SSP_HRM_RAW_FAC_NAME,
	},
	.probe = ssp_hrm_raw_fac_probe,
	.id_table = ssp_hrm_raw_fac_id_table,
};

module_platform_driver(ssp_hrm_raw_fac_driver);

MODULE_AUTHOR("Davids Paskevics <davids.paskevics@gmail.com>");
MODULE_DESCRIPTION("Samsung sensorhub heart rate monitor driver (factory test values)");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_SSP_SENSORS");
