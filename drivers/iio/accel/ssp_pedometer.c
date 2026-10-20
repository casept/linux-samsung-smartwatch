// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pedometer running as a library on the Samsung sensorhub
 *
 * The MCU counts steps from the accelerometer and estimates distance, speed
 * and calories from them and the user's height, weight and gender.
 */

#include <linux/completion.h>
#include <linux/iio/common/ssp_sensors.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/jiffies.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/unaligned.h>

#define SSP_PEDOMETER_NAME		"ssp-pedometer"

/*
 * The MCU reports what changed since its previous report, while the AP is
 * awake whenever the steps changed. The top bits of the first byte select one
 * of three record layouts. Distance is in cm, speed in 0.1 km/h, calories in
 * 0.01 kcal, except in long records with 0.1 kcal.
 */
#define SSP_PEDOMETER_KIND(b)		((b) >> 6)
#define SSP_PEDOMETER_KIND_LONG		2
#define SSP_PEDOMETER_KIND_BATCH	3

#define SSP_PEDOMETER_SHORT_LEN		15
#define SSP_PEDOMETER_SHORT_STEPS	3	/* be16 */
#define SSP_PEDOMETER_SHORT_DISTANCE	5	/* be16 */
#define SSP_PEDOMETER_SHORT_SPEED	7	/* u8 */
#define SSP_PEDOMETER_SHORT_STATUS	8	/* s8 */
#define SSP_PEDOMETER_SHORT_ENERGY	9	/* u8 */

#define SSP_PEDOMETER_LONG_LEN		33
#define SSP_PEDOMETER_LONG_STEPS	10	/* be24 */
#define SSP_PEDOMETER_LONG_DISTANCE	13	/* be24 */
#define SSP_PEDOMETER_LONG_SPEED	16	/* u8 */
#define SSP_PEDOMETER_LONG_STATUS	17	/* s8 */
#define SSP_PEDOMETER_LONG_ENERGY	18	/* be16, 0.1 kcal */

/* Batched while the AP sleeps, one entry per minute, without a status */
#define SSP_PEDOMETER_BATCH_HDR_LEN	2	/* kind, entry count */
#define SSP_PEDOMETER_BATCH_ENTRY_LEN	20
#define SSP_PEDOMETER_BATCH_DISTANCE	4	/* be16 */
#define SSP_PEDOMETER_BATCH_ENERGY	6	/* be16 */
#define SSP_PEDOMETER_BATCH_SPEED	17	/* u8 */
#define SSP_PEDOMETER_BATCH_STEPS	18	/* be16 */

/* Step status of the MCU, the others need a barometer */
#define SSP_PEDOMETER_STATUS_STOP	0
#define SSP_PEDOMETER_STATUS_WALK	3
#define SSP_PEDOMETER_STATUS_RUN	4

/* The MCU forgets speed and activity this long after the last step */
#define SSP_PEDOMETER_IDLE_MS		3000

/* How long a read waits for the report it requested */
#define SSP_PEDOMETER_REPORT_MS		100

struct ssp_pedometer_report {
	u32 steps;
	u32 distance_cm;
	u32 energy_ckcal;
	u8 speed_dkmh;
	s8 status;
};

struct ssp_pedometer {
	struct ssp_library lib;
	struct iio_dev *indio_dev;
	/* Serializes starting, stopping and configuring the library */
	struct mutex lock;
	bool counter_en;
	bool event_en;

	/* Totals since probe */
	u32 steps;
	u32 distance_cm;
	u32 energy_ckcal;
	/* State as of the last step */
	u8 speed_dkmh;
	s8 status;
	unsigned long last_step;

	/* Steps per change event, and steps since the last one */
	u32 event_steps;
	atomic_t pending_steps;

	/* Any report arrived since a read requested one */
	struct completion report;
};

static int ssp_pedometer_parse(const u8 *buf, unsigned int len,
			       struct ssp_pedometer_report *rep)
{
	const u8 *entry;
	unsigned int i, n, rec_len;

	if (!len)
		return -EPROTO;

	switch (SSP_PEDOMETER_KIND(buf[0])) {
	case SSP_PEDOMETER_KIND_LONG:
		rec_len = SSP_PEDOMETER_LONG_LEN;
		if (len < rec_len)
			return -EPROTO;

		rep->steps = get_unaligned_be24(&buf[SSP_PEDOMETER_LONG_STEPS]);
		rep->distance_cm = get_unaligned_be24(&buf[SSP_PEDOMETER_LONG_DISTANCE]);
		rep->energy_ckcal = get_unaligned_be16(&buf[SSP_PEDOMETER_LONG_ENERGY]) * 10;
		rep->speed_dkmh = buf[SSP_PEDOMETER_LONG_SPEED];
		rep->status = buf[SSP_PEDOMETER_LONG_STATUS];
		break;
	case SSP_PEDOMETER_KIND_BATCH:
		if (len < SSP_PEDOMETER_BATCH_HDR_LEN)
			return -EPROTO;

		n = buf[1];
		rec_len = SSP_PEDOMETER_BATCH_HDR_LEN +
			  n * SSP_PEDOMETER_BATCH_ENTRY_LEN;
		if (len < rec_len)
			return -EPROTO;

		*rep = (struct ssp_pedometer_report){ .status = -1 };
		for (i = 0; i < n; i++) {
			entry = &buf[SSP_PEDOMETER_BATCH_HDR_LEN +
				     i * SSP_PEDOMETER_BATCH_ENTRY_LEN];
			rep->steps +=
				get_unaligned_be16(&entry[SSP_PEDOMETER_BATCH_STEPS]);
			rep->distance_cm +=
				get_unaligned_be16(&entry[SSP_PEDOMETER_BATCH_DISTANCE]);
			rep->energy_ckcal +=
				get_unaligned_be16(&entry[SSP_PEDOMETER_BATCH_ENERGY]);
			rep->speed_dkmh = entry[SSP_PEDOMETER_BATCH_SPEED];
		}
		break;
	default:
		/* Kinds 0 and 1 */
		rec_len = SSP_PEDOMETER_SHORT_LEN;
		if (len < rec_len)
			return -EPROTO;

		rep->steps = get_unaligned_be16(&buf[SSP_PEDOMETER_SHORT_STEPS]);
		rep->distance_cm = get_unaligned_be16(&buf[SSP_PEDOMETER_SHORT_DISTANCE]);
		rep->energy_ckcal = buf[SSP_PEDOMETER_SHORT_ENERGY];
		rep->speed_dkmh = buf[SSP_PEDOMETER_SHORT_SPEED];
		rep->status = buf[SSP_PEDOMETER_SHORT_STATUS];
		break;
	}

	return rec_len;
}

static void ssp_pedometer_account(struct ssp_pedometer *ped,
				  const struct ssp_pedometer_report *rep)
{
	u32 pending, event_steps;

	WRITE_ONCE(ped->distance_cm, ped->distance_cm + rep->distance_cm);
	WRITE_ONCE(ped->energy_ckcal, ped->energy_ckcal + rep->energy_ckcal);
	if (rep->status >= 0)
		WRITE_ONCE(ped->status, rep->status);

	if (!rep->steps)
		return;

	/* Batched minutes are history, not the current activity */
	if (rep->status >= 0) {
		WRITE_ONCE(ped->speed_dkmh, rep->speed_dkmh);
		WRITE_ONCE(ped->last_step, jiffies);
	}
	WRITE_ONCE(ped->steps, ped->steps + rep->steps);

	if (!READ_ONCE(ped->event_en))
		return;

	/*
	 * Steps beyond a multiple of event_steps count toward the next event,
	 * one event stands for any number of crossings
	 */
	pending = atomic_add_return(rep->steps, &ped->pending_steps);
	event_steps = READ_ONCE(ped->event_steps);
	if (pending >= event_steps) {
		atomic_sub(pending - pending % event_steps,
			   &ped->pending_steps);
		iio_push_event(ped->indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_STEPS, 0, IIO_NO_MOD,
						  IIO_EV_TYPE_CHANGE,
						  IIO_EV_DIR_NONE),
			       iio_get_time_ns(ped->indio_dev));
	}
}

static int ssp_pedometer_process_data(struct ssp_library *lib, const u8 *buf,
				      unsigned int len)
{
	struct ssp_pedometer *ped = container_of(lib, struct ssp_pedometer, lib);
	struct ssp_pedometer_report rep;
	int ret;

	ret = ssp_pedometer_parse(buf, len, &rep);
	if (ret < 0)
		return ret;

	ssp_pedometer_account(ped, &rep);
	complete(&ped->report);

	return ret;
}

/* The MCU forgot the steps it held back and the activity in its reset */
static void ssp_pedometer_readded(struct ssp_library *lib)
{
	struct ssp_pedometer *ped = container_of(lib, struct ssp_pedometer, lib);

	WRITE_ONCE(ped->last_step,
		   jiffies - msecs_to_jiffies(SSP_PEDOMETER_IDLE_MS));
}

/*
 * The MCU reports only when the step count changed, and holds back steps for
 * a while. Ask it for the changes since its previous report, which it sends
 * even if there are none.
 */
static void ssp_pedometer_refresh(struct ssp_pedometer *ped)
{
	static const u8 args[] = { 1, 0 };	/* One report, right now */

	guard(mutex)(&ped->lock);

	if (!ped->counter_en && !ped->event_en)
		return;

	reinit_completion(&ped->report);
	if (ssp_library_request(&ped->lib, args, sizeof(args)) < 0)
		return;

	/* The values of the previous report are still good otherwise */
	wait_for_completion_timeout(&ped->report,
				    msecs_to_jiffies(SSP_PEDOMETER_REPORT_MS));
}

/* The MCU doesn't report becoming idle, as no steps change then */
static bool ssp_pedometer_idle(struct ssp_pedometer *ped)
{
	return time_after(jiffies, READ_ONCE(ped->last_step) +
			  msecs_to_jiffies(SSP_PEDOMETER_IDLE_MS));
}

static enum iio_modifier ssp_pedometer_activity(struct ssp_pedometer *ped)
{
	if (ssp_pedometer_idle(ped))
		return IIO_MOD_STILL;

	switch (READ_ONCE(ped->status)) {
	case SSP_PEDOMETER_STATUS_WALK:
		return IIO_MOD_WALKING;
	case SSP_PEDOMETER_STATUS_RUN:
		return IIO_MOD_RUNNING;
	default:
		return IIO_MOD_STILL;
	}
}

static int ssp_pedometer_start(struct ssp_pedometer *ped)
{
	const u8 args[] = {
		ssp_library_get_profile(&ped->lib, SSP_PROFILE_HEIGHT),
		ssp_library_get_profile(&ped->lib, SSP_PROFILE_WEIGHT),
		ssp_library_get_profile(&ped->lib, SSP_PROFILE_GENDER),
	};

	return ssp_library_add(&ped->lib, args, sizeof(args));
}

/* The library runs while either the counter or the detector is enabled */
static int ssp_pedometer_set_enable(struct ssp_pedometer *ped, bool counter_en,
				    bool event_en)
{
	bool running = ped->counter_en || ped->event_en;
	int ret = 0;

	lockdep_assert_held(&ped->lock);

	if ((counter_en || event_en) && !running) {
		ret = ssp_pedometer_start(ped);
		if (ret < 0)
			return ret;
	} else if (!counter_en && !event_en && running) {
		/* It counts as removed even if the MCU could not be told */
		ret = ssp_library_remove(&ped->lib);
	}

	if (event_en && !ped->event_en)
		atomic_set(&ped->pending_steps, 0);

	ped->counter_en = counter_en;
	WRITE_ONCE(ped->event_en, event_en);

	return ret;
}

static int ssp_pedometer_read_raw(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan, int *val,
				  int *val2, long mask)
{
	struct ssp_pedometer *ped = iio_priv(indio_dev);
	u32 tmp;

	if (mask == IIO_CHAN_INFO_PROCESSED || mask == IIO_CHAN_INFO_RAW)
		ssp_pedometer_refresh(ped);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_STEPS:
			*val = READ_ONCE(ped->steps);
			return IIO_VAL_INT;
		case IIO_DISTANCE:
			/* cm to m */
			tmp = READ_ONCE(ped->distance_cm);
			*val = tmp / 100;
			*val2 = tmp % 100 * 10000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACTIVITY:
			*val = chan->channel2 == ssp_pedometer_activity(ped) ?
			       100 : 0;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_VELOCITY:
			*val = ssp_pedometer_idle(ped) ? 0 :
			       READ_ONCE(ped->speed_dkmh);
			return IIO_VAL_INT;
		case IIO_ENERGY:
			*val = READ_ONCE(ped->energy_ckcal);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VELOCITY:
			/* 0.1 km/h to m/s */
			*val = 1;
			*val2 = 36;
			return IIO_VAL_FRACTIONAL;
		case IIO_ENERGY:
			/* 0.01 kcal to J */
			*val = 41;
			*val2 = 840000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_ENABLE: {
		guard(mutex)(&ped->lock);
		*val = ped->counter_en;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_CALIBHEIGHT:
		/* cm to m */
		tmp = ssp_library_get_profile(&ped->lib, SSP_PROFILE_HEIGHT);
		*val = tmp / 100;
		*val2 = tmp % 100 * 10000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBWEIGHT:
		*val = ssp_library_get_profile(&ped->lib, SSP_PROFILE_WEIGHT);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ssp_pedometer_write_raw(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan, int val,
				   int val2, long mask)
{
	struct ssp_pedometer *ped = iio_priv(indio_dev);
	int tmp;

	switch (mask) {
	case IIO_CHAN_INFO_ENABLE: {
		guard(mutex)(&ped->lock);
		return ssp_pedometer_set_enable(ped, val, ped->event_en);
	}
	case IIO_CHAN_INFO_CALIBHEIGHT: {
		/* m to cm, the MCU takes 1 byte */
		if (val < 0 || val > U8_MAX / 100 || val2 < 0)
			return -EINVAL;

		tmp = val * 100 + val2 / 10000;
		if (tmp < 1 || tmp > U8_MAX)
			return -EINVAL;

		return ssp_library_set_profile(&ped->lib, SSP_PROFILE_HEIGHT,
					       tmp);
	}
	case IIO_CHAN_INFO_CALIBWEIGHT:
		if (val < 1 || val > U8_MAX)
			return -EINVAL;

		return ssp_library_set_profile(&ped->lib, SSP_PROFILE_WEIGHT,
					       val);
	default:
		return -EINVAL;
	}
}

static int ssp_pedometer_read_event_config(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir)
{
	struct ssp_pedometer *ped = iio_priv(indio_dev);

	guard(mutex)(&ped->lock);

	return ped->event_en;
}

static int ssp_pedometer_write_event_config(struct iio_dev *indio_dev,
					    const struct iio_chan_spec *chan,
					    enum iio_event_type type,
					    enum iio_event_direction dir,
					    bool state)
{
	struct ssp_pedometer *ped = iio_priv(indio_dev);

	guard(mutex)(&ped->lock);

	return ssp_pedometer_set_enable(ped, ped->counter_en, state);
}

static int ssp_pedometer_read_event_value(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir,
					  enum iio_event_info info,
					  int *val, int *val2)
{
	struct ssp_pedometer *ped = iio_priv(indio_dev);

	*val = READ_ONCE(ped->event_steps);

	return IIO_VAL_INT;
}

static int ssp_pedometer_write_event_value(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir,
					   enum iio_event_info info,
					   int val, int val2)
{
	struct ssp_pedometer *ped = iio_priv(indio_dev);

	if (val < 1)
		return -EINVAL;

	WRITE_ONCE(ped->event_steps, val);

	return 0;
}

static const struct iio_info ssp_pedometer_info = {
	.read_raw = ssp_pedometer_read_raw,
	.write_raw = ssp_pedometer_write_raw,
	.read_event_config = ssp_pedometer_read_event_config,
	.write_event_config = ssp_pedometer_write_event_config,
	.read_event_value = ssp_pedometer_read_event_value,
	.write_event_value = ssp_pedometer_write_event_value,
};

static const char * const ssp_pedometer_genders[] = { "male", "female" };

static int ssp_pedometer_get_gender(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan)
{
	struct ssp_pedometer *ped = iio_priv(indio_dev);

	return ssp_library_get_profile(&ped->lib, SSP_PROFILE_GENDER) ==
	       SSP_PROFILE_FEMALE;
}

static int ssp_pedometer_set_gender(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    unsigned int mode)
{
	struct ssp_pedometer *ped = iio_priv(indio_dev);

	return ssp_library_set_profile(&ped->lib, SSP_PROFILE_GENDER,
				       mode ? SSP_PROFILE_FEMALE :
					      SSP_PROFILE_MALE);
}

static const struct iio_enum ssp_pedometer_gender_enum = {
	.items = ssp_pedometer_genders,
	.num_items = ARRAY_SIZE(ssp_pedometer_genders),
	.get = ssp_pedometer_get_gender,
	.set = ssp_pedometer_set_gender,
};

static const struct iio_chan_spec_ext_info ssp_pedometer_ext_info[] = {
	IIO_ENUM("calibgender", IIO_SHARED_BY_TYPE, &ssp_pedometer_gender_enum),
	IIO_ENUM_AVAILABLE("calibgender", IIO_SHARED_BY_TYPE,
			   &ssp_pedometer_gender_enum),
	{ }
};

static const struct iio_event_spec ssp_pedometer_step_detect_event = {
	.type = IIO_EV_TYPE_CHANGE,
	.dir = IIO_EV_DIR_NONE,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE) | BIT(IIO_EV_INFO_VALUE),
};

#define SSP_PEDOMETER_ACTIVITY_CHANNEL(_mod) {			\
	.type = IIO_ACTIVITY,					\
	.modified = 1,						\
	.channel2 = _mod,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBHEIGHT), \
	.scan_index = -1,					\
	.ext_info = ssp_pedometer_ext_info,			\
}

static const struct iio_chan_spec ssp_pedometer_channels[] = {
	{
		.type = IIO_STEPS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_ENABLE),
		.scan_index = -1, /* No buffer support */
		.event_spec = &ssp_pedometer_step_detect_event,
		.num_event_specs = 1,
	},
	{
		.type = IIO_DISTANCE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_CALIBHEIGHT),
		.scan_index = -1,
		.ext_info = ssp_pedometer_ext_info,
	},
	{
		.type = IIO_VELOCITY,
		.modified = 1,
		.channel2 = IIO_MOD_ROOT_SUM_SQUARED_X_Y_Z,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_CALIBHEIGHT),
		.scan_index = -1,
		.ext_info = ssp_pedometer_ext_info,
	},
	{
		.type = IIO_ENERGY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_CALIBHEIGHT) |
				      BIT(IIO_CHAN_INFO_CALIBWEIGHT),
		.scan_index = -1,
		.ext_info = ssp_pedometer_ext_info,
	},
	SSP_PEDOMETER_ACTIVITY_CHANNEL(IIO_MOD_STILL),
	SSP_PEDOMETER_ACTIVITY_CHANNEL(IIO_MOD_WALKING),
	SSP_PEDOMETER_ACTIVITY_CHANNEL(IIO_MOD_RUNNING),
};

static int ssp_pedometer_probe(struct platform_device *pdev)
{
	struct ssp_pedometer *ped;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*ped));
	if (!indio_dev)
		return -ENOMEM;

	ped = iio_priv(indio_dev);
	ped->indio_dev = indio_dev;
	ped->lib.id = SSP_LIBRARY_PEDOMETER;
	ped->lib.process_data = ssp_pedometer_process_data;
	ped->lib.readded = ssp_pedometer_readded;
	ped->event_steps = 1;
	ped->last_step = jiffies - msecs_to_jiffies(SSP_PEDOMETER_IDLE_MS);

	init_completion(&ped->report);

	ret = devm_mutex_init(&pdev->dev, &ped->lock);
	if (ret)
		return ret;

	indio_dev->name = SSP_PEDOMETER_NAME;
	indio_dev->info = &ssp_pedometer_info;
	indio_dev->channels = ssp_pedometer_channels;
	indio_dev->num_channels = ARRAY_SIZE(ssp_pedometer_channels);

	ssp_library_init(&pdev->dev, &ped->lib);

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret)
		return ret;

	/* Stops, and with it the events, before the IIO device goes away */
	return ssp_register_library(&pdev->dev, &ped->lib);
}

static const struct platform_device_id ssp_pedometer_id_table[] = {
	{ SSP_PEDOMETER_NAME },
	{ }
};
MODULE_DEVICE_TABLE(platform, ssp_pedometer_id_table);

static struct platform_driver ssp_pedometer_driver = {
	.driver = {
		.name = SSP_PEDOMETER_NAME,
	},
	.probe = ssp_pedometer_probe,
	.id_table = ssp_pedometer_id_table,
};

module_platform_driver(ssp_pedometer_driver);

MODULE_AUTHOR("Davids Paskevics <davids.paskevics@gmail.com>");
MODULE_DESCRIPTION("Samsung sensorhub pedometer driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_SSP_SENSORS");
