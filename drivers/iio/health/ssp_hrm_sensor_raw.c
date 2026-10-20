// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Davids Paskevics <davids.paskevics@gmail.com>.
 *
 * Raw light intensities of the heart rate monitor behind the Samsung
 * sensorhub, measured in the two time slots of its optical front end
 */

#include <linux/iio/buffer.h>
#include <linux/iio/common/ssp_sensors.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "../common/ssp_sensors/ssp_iio_sensor.h"

#define SSP_HRM_RAW_NAME "ssp-hrm-raw"

enum ssp_hrm_raw_channel {
	SSP_HRM_RAW_CHANNEL_A,
	SSP_HRM_RAW_CHANNEL_B,
	SSP_HRM_RAW_CHANNEL_TIME,
};

static int ssp_hrm_raw_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, int *val,
				int *val2, long mask)
{
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ssp_convert_to_freq(ssp_get_sensor_delay(data, SSP_BIO_HRM_RAW),
				    val, val2);
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int ssp_hrm_raw_write_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan, int val,
				 int val2, long mask)
{
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ssp_change_delay(data, SSP_BIO_HRM_RAW,
					ssp_convert_to_time(val, val2));
	default:
		return -EINVAL;
	}
}

static const struct iio_info ssp_hrm_raw_iio_info = {
	.read_raw = ssp_hrm_raw_read_raw,
	.write_raw = ssp_hrm_raw_write_raw,
};

/* The MCU always sends both */
static const unsigned long ssp_hrm_raw_scan_mask[] = { 0x3, 0 };

#define SSP_HRM_RAW_CHANNEL(_index) {					\
	.type = IIO_INTENSITY,						\
	.indexed = 1,							\
	.channel = _index,						\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = _index,						\
	.scan_type = {							\
		.sign = 'u',						\
		.realbits = 32,						\
		.storagebits = 32,					\
		.endianness = IIO_LE,					\
	},								\
}

static const struct iio_chan_spec ssp_hrm_raw_channels[] = {
	SSP_HRM_RAW_CHANNEL(SSP_HRM_RAW_CHANNEL_A),
	SSP_HRM_RAW_CHANNEL(SSP_HRM_RAW_CHANNEL_B),
	SSP_CHAN_TIMESTAMP(SSP_HRM_RAW_CHANNEL_TIME),
};

static int ssp_process_hrm_raw_data(struct iio_dev *indio_dev, void *buf,
				    int64_t timestamp)
{
	return ssp_common_process_data(indio_dev, buf, SSP_BIO_HRM_RAW_SIZE,
				       timestamp);
}

static const struct iio_buffer_setup_ops ssp_hrm_raw_buffer_ops = {
	.postenable = ssp_common_buffer_postenable,
	.predisable = ssp_common_buffer_predisable,
	.postdisable = ssp_common_buffer_postdisable,
};

static int ssp_hrm_raw_probe(struct platform_device *pdev)
{
	struct ssp_sensor_data *spd;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*spd));
	if (!indio_dev)
		return -ENOMEM;

	spd = iio_priv(indio_dev);
	spd->process_data = ssp_process_hrm_raw_data;
	spd->type = SSP_BIO_HRM_RAW;

	indio_dev->name = SSP_HRM_RAW_NAME;
	indio_dev->info = &ssp_hrm_raw_iio_info;
	indio_dev->channels = ssp_hrm_raw_channels;
	indio_dev->num_channels = ARRAY_SIZE(ssp_hrm_raw_channels);
	indio_dev->available_scan_masks = ssp_hrm_raw_scan_mask;

	ret = devm_iio_kfifo_buffer_setup(&pdev->dev, indio_dev,
					  &ssp_hrm_raw_buffer_ops);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, indio_dev);

	/* Unregistered only after the buffer got disabled on removal */
	ret = ssp_register_consumer(indio_dev, SSP_BIO_HRM_RAW);
	if (ret)
		return ret;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct platform_device_id ssp_hrm_raw_id_table[] = {
	{ SSP_HRM_RAW_NAME },
	{ }
};
MODULE_DEVICE_TABLE(platform, ssp_hrm_raw_id_table);

static struct platform_driver ssp_hrm_raw_driver = {
	.driver = {
		.name = SSP_HRM_RAW_NAME,
	},
	.probe = ssp_hrm_raw_probe,
	.id_table = ssp_hrm_raw_id_table,
};
module_platform_driver(ssp_hrm_raw_driver);

MODULE_AUTHOR("Davids Paskevics <davids.paskevics@gmail.com>");
MODULE_DESCRIPTION("Samsung sensorhub heart rate monitor driver (raw values)");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_SSP_SENSORS");
