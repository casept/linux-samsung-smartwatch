// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2024, Davids Paskevics <davids.paskevics@gmail.com>.
 */

#include <linux/iio/common/ssp_sensors.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include "../common/ssp_sensors/ssp_iio_sensor.h"

#define SSP_HRM_NAME "ssp-hrm"
static const char ssp_hrm_device_name[] = SSP_HRM_NAME;

/* TODO: Need to enable regulator for LEDs */

/* TODO: Remove, only here for reference */
struct ssp_hrm_lib {
	s16 hr;
	s16 rri;
	s32 snr;
};

enum ssp_hrm_channel_lib {
	SSP_CHANNEL_HRM_HR,
	SSP_CHANNEL_HRM_RRI,
	SSP_CHANNEL_HRM_SNR,
	SSP_CHANNEL_SCAN_INDEX_TIME_LIB,
};

static int ssp_hrm_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	u32 t;
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		t = ssp_get_sensor_delay(data, SSP_BIO_HRM_LIB);
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
		ret = ssp_change_delay(data, SSP_BIO_HRM_LIB, ret);
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
	/* All 3 or none, don't allow selecting */
	0x7,
	0,
};

static struct iio_chan_spec ssp_hrm_channels[] = {
	SSP_CHANNEL_AG(IIO_COUNT, IIO_NO_MOD, SSP_CHANNEL_HRM_HR),
	SSP_CHANNEL_AG(IIO_COUNT, IIO_NO_MOD, SSP_CHANNEL_HRM_RRI),
	SSP_CHANNEL_AG(IIO_COUNT, IIO_NO_MOD, SSP_CHANNEL_HRM_SNR),
	SSP_CHAN_TIMESTAMP(SSP_CHANNEL_SCAN_INDEX_TIME_LIB),
};

static int ssp_process_hrm_data(struct iio_dev *indio_dev, void *buf,
				int64_t timestamp)
{
	return ssp_common_process_data(indio_dev, buf, SSP_BIO_HRM_LIB_SIZE,
				       timestamp);
}

static const struct iio_buffer_setup_ops ssp_hrm_buffer_ops = {
	.postenable = &ssp_common_buffer_postenable,
	.postdisable = &ssp_common_buffer_postdisable,
};

static int ssp_hrm_probe(struct platform_device *pdev)
{
	int ret;
	struct iio_dev *indio_dev;
	struct ssp_sensor_data *spd;

	/*
	 * Post-process channels defined by macro to make them indexed and avoid naming conflict
	 * TODO: This should be done with a custom macro in the common header instead
	*/
	for (int i = 0; i < ARRAY_SIZE(ssp_hrm_channels); i++) {
		ssp_hrm_channels[i].indexed = 1;
		ssp_hrm_channels[i].channel = i;
	}

	dev_info(&pdev->dev, "Probing SSP HRM driver\n");

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*spd));
	if (!indio_dev)
		return -ENOMEM;

	spd = iio_priv(indio_dev);

	spd->process_data = ssp_process_hrm_data;
	spd->type = SSP_BIO_HRM_LIB;

	indio_dev->name = ssp_hrm_device_name;
	indio_dev->info = &ssp_hrm_iio_info;
	indio_dev->channels = ssp_hrm_channels;
	indio_dev->num_channels = ARRAY_SIZE(ssp_hrm_channels);
	indio_dev->available_scan_masks = ssp_hrm_scan_mask;

	ret = devm_iio_kfifo_buffer_setup(&pdev->dev, indio_dev,
					  &ssp_hrm_buffer_ops);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, indio_dev);

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register IIO device!\n");
		return ret;
	}

	/* ssp registering should be done after all iio setup */
	ssp_register_consumer(indio_dev, SSP_BIO_HRM_LIB);

	dev_info(&pdev->dev, "Probe OK!\n");
	return 0;
}

static struct platform_driver ssp_hrm_driver = {
	.driver = {
		.name = SSP_HRM_NAME,
	},
	.probe = ssp_hrm_probe,
};

module_platform_driver(ssp_hrm_driver);

MODULE_AUTHOR("Davids Paskevics <davids.paskevics@gmail.com>");
MODULE_DESCRIPTION("Samsung sensorhub heart rate monitor driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_SSP_SENSORS);
