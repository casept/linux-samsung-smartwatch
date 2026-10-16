// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IR transmitter built from a Lattice iCE40 FPGA running Samsung's IR
 * bitstream, as found in the Samsung Gear 2 (rinato) and other Samsung
 * devices of that era.
 *
 * The FPGA is configured through its SPI slave port, whose clock and data
 * pins double as the SCL and SDA lines of the bit-banged I2C bus that the
 * bitstream then implements. A frame is a single I2C write:
 *
 *	0x00, length (16 bit), carrier in Hz (24 bit), durations (16 bit each)
 *
 * where the length counts the bytes from the length field on and the
 * durations alternate between pulse and space, counted in carrier cycles.
 * The bitstream plays one duration past the end of the frame, so a zero
 * duration that is not included in the length follows the frame. The
 * interrupt line is pulled low while a frame is transmitted.
 *
 * Copyright (C) 2013 Samsung Electronics
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <media/rc-core.h>

#define DRIVER_NAME	"ice40-ir-tx"
#define DEVICE_NAME	"Samsung iCE40 IR Transmitter"
#define FIRMWARE_NAME	"samsung/ice40_irda.fw"

/* The bitstream needs extra clocks after the image to start up */
#define ICE40_DUMMY_CLOCKS	49

#define ICE40_MIN_CARRIER	25000
#define ICE40_MAX_CARRIER	60000
#define ICE40_DEFAULT_CARRIER	38000

/* The frame buffer holds 500 durations, including the trailing zero */
#define ICE40_MAX_DURATIONS	499

#define ICE40_HDR_LEN		6
#define ICE40_TX_TIMEOUT_MS	50

struct ice40_ir {
	struct i2c_client *client;
	struct regulator *supply;
	struct gpio_desc *reset;
	struct gpio_desc *ss;
	struct gpio_desc *cdone;
	struct completion tx_done;
	u32 carrier;
	u8 *buf;

	/* Statistics exposed through debugfs */
	u64 tx_frames;
	u64 tx_errors;
	u64 tx_timeouts;
	u64 irqs;
	unsigned int last_count;
	u32 last_carrier;
	s64 last_tx_us;
};

static void ice40_ir_spi_bit(struct i2c_algo_bit_data *bit, int val)
{
	bit->setscl(bit->data, 0);
	bit->setsda(bit->data, val);
	udelay(1);
	bit->setscl(bit->data, 1);
	udelay(1);
}

static int ice40_ir_configure(struct ice40_ir *ir, const struct firmware *fw)
{
	struct i2c_adapter *adap = ir->client->adapter;
	struct i2c_algo_bit_data *bit = adap->algo_data;
	size_t i;
	int b;

	i2c_lock_bus(adap, I2C_LOCK_ROOT_ADAPTER);

	/* Select SPI slave mode: SS_B low while CRESET_B is released */
	gpiod_set_value_cansleep(ir->ss, 1);
	bit->setscl(bit->data, 1);
	bit->setsda(bit->data, 0);
	gpiod_set_value_cansleep(ir->reset, 1);
	usleep_range(30, 40);
	gpiod_set_value_cansleep(ir->reset, 0);
	usleep_range(1000, 1100);

	for (i = 0; i < fw->size; i++)
		for (b = 7; b >= 0; b--)
			ice40_ir_spi_bit(bit, (fw->data[i] >> b) & 1);

	for (i = 0; i < ICE40_DUMMY_CLOCKS; i++)
		ice40_ir_spi_bit(bit, 0);

	/* SS_B is the active-low reset of the design once configured */
	gpiod_set_value_cansleep(ir->ss, 0);

	/* Release the bus: SDA rising while SCL is high is a STOP */
	bit->setsda(bit->data, 1);
	i2c_unlock_bus(adap, I2C_LOCK_ROOT_ADAPTER);

	if (!gpiod_get_value_cansleep(ir->cdone))
		return -EIO;

	return 0;
}

static int ice40_ir_set_carrier(struct rc_dev *rc, u32 carrier)
{
	struct ice40_ir *ir = rc->priv;

	if (carrier < ICE40_MIN_CARRIER || carrier > ICE40_MAX_CARRIER)
		return -EINVAL;

	ir->carrier = carrier;

	return 0;
}

/*
 * Send the frame in ir->buf and wait for the busy line to signal the end of
 * the transmission.
 */
static int ice40_ir_send_frame(struct ice40_ir *ir, unsigned int len,
			       unsigned int total_us)
{
	unsigned long timeout;
	int ret;

	reinit_completion(&ir->tx_done);

	ret = i2c_master_send(ir->client, ir->buf, len);
	if (ret != len)
		return ret < 0 ? ret : -EIO;

	timeout = usecs_to_jiffies(total_us) +
		  msecs_to_jiffies(ICE40_TX_TIMEOUT_MS);
	if (!wait_for_completion_timeout(&ir->tx_done, timeout))
		return -ETIMEDOUT;

	return 0;
}

static int ice40_ir_tx(struct rc_dev *rc, unsigned int *txbuf,
		       unsigned int count)
{
	struct ice40_ir *ir = rc->priv;
	struct device *dev = &ir->client->dev;
	unsigned int len = ICE40_HDR_LEN + 2 * count;
	unsigned int buf_len = len + 2;
	unsigned int total_us = 0;
	ktime_t start;
	unsigned int i;
	u8 *p = ir->buf;
	int ret;

	if (count > ICE40_MAX_DURATIONS)
		return -EINVAL;

	*p++ = 0;
	*p++ = (len - 1) >> 8;
	*p++ = len - 1;
	*p++ = ir->carrier >> 16;
	*p++ = ir->carrier >> 8;
	*p++ = ir->carrier;

	for (i = 0; i < count; i++) {
		u64 cycles = DIV_ROUND_CLOSEST_ULL((u64)txbuf[i] * ir->carrier,
						   USEC_PER_SEC);

		if (!cycles || cycles > U16_MAX)
			return -EINVAL;

		*p++ = cycles >> 8;
		*p++ = cycles;
		total_us += txbuf[i];
	}

	/* Terminating zero duration, see above */
	*p++ = 0;
	*p++ = 0;

	ret = regulator_enable(ir->supply);
	if (ret)
		return ret;

	usleep_range(2500, 3000);
	start = ktime_get();

	ret = ice40_ir_send_frame(ir, buf_len, total_us);
	if (ret) {
		if (ret == -ETIMEDOUT) {
			ir->tx_timeouts++;
			ret = -EIO;
		}
		dev_err(dev, "failed to transmit frame: %d\n", ret);
		goto out;
	}

	ir->last_tx_us = ktime_us_delta(ktime_get(), start);
	ir->last_count = count;
	ir->last_carrier = ir->carrier;
	ir->tx_frames++;
	ret = count;
out:
	if (ret < 0)
		ir->tx_errors++;
	regulator_disable(ir->supply);

	return ret;
}

static irqreturn_t ice40_ir_irq(int irq, void *data)
{
	struct ice40_ir *ir = data;

	ir->irqs++;
	complete(&ir->tx_done);

	return IRQ_HANDLED;
}

static int ice40_ir_stats_show(struct seq_file *s, void *unused)
{
	struct ice40_ir *ir = s->private;

	seq_printf(s, "frames:       %llu\n", ir->tx_frames);
	seq_printf(s, "errors:       %llu\n", ir->tx_errors);
	seq_printf(s, "timeouts:     %llu\n", ir->tx_timeouts);
	seq_printf(s, "irqs:         %llu\n", ir->irqs);
	seq_printf(s, "carrier:      %u\n", ir->carrier);
	seq_printf(s, "last_count:   %u\n", ir->last_count);
	seq_printf(s, "last_carrier: %u\n", ir->last_carrier);
	seq_printf(s, "last_tx_us:   %lld\n", ir->last_tx_us);
	seq_printf(s, "cdone:        %d\n",
		   gpiod_get_value_cansleep(ir->cdone));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ice40_ir_stats);

static int ice40_ir_device_info_show(struct seq_file *s, void *unused)
{
	struct ice40_ir *ir = s->private;
	u8 info[8];
	int ret;

	ret = i2c_master_recv(ir->client, info, sizeof(info));
	if (ret < 0)
		return ret;

	seq_printf(s, "%*ph\n", (int)sizeof(info), info);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ice40_ir_device_info);

static void ice40_ir_debugfs_remove(void *dir)
{
	debugfs_remove_recursive(dir);
}

static void ice40_ir_debugfs_init(struct ice40_ir *ir)
{
	struct device *dev = &ir->client->dev;
	struct dentry *dir;
	char *name;

	name = devm_kasprintf(dev, GFP_KERNEL, DRIVER_NAME "-%s", dev_name(dev));
	if (!name)
		return;

	dir = debugfs_create_dir(name, NULL);
	debugfs_create_file("stats", 0444, dir, ir, &ice40_ir_stats_fops);
	debugfs_create_file("device_info", 0400, dir, ir,
			    &ice40_ir_device_info_fops);

	devm_add_action_or_reset(dev, ice40_ir_debugfs_remove, dir);
}

static int ice40_ir_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	const char *fw_name = FIRMWARE_NAME;
	const struct firmware *fw;
	struct ice40_ir *ir;
	struct rc_dev *rc;
	struct clk *clk;
	int ret;

	if (client->adapter->algo != &i2c_bit_algo)
		return dev_err_probe(dev, -EINVAL,
				     "must sit on a bit-banged I2C bus\n");

	ir = devm_kzalloc(dev, sizeof(*ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	ir->buf = devm_kmalloc(dev, ICE40_HDR_LEN + 2 * (ICE40_MAX_DURATIONS + 1),
			       GFP_KERNEL);
	if (!ir->buf)
		return -ENOMEM;

	ir->client = client;
	ir->carrier = ICE40_DEFAULT_CARRIER;
	init_completion(&ir->tx_done);

	ir->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(ir->supply))
		return dev_err_probe(dev, PTR_ERR(ir->supply),
				     "failed to get power supply\n");

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk),
				     "failed to enable clock\n");

	ir->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ir->reset))
		return dev_err_probe(dev, PTR_ERR(ir->reset),
				     "failed to get reset GPIO\n");

	ir->ss = devm_gpiod_get(dev, "ss", GPIOD_OUT_HIGH);
	if (IS_ERR(ir->ss))
		return dev_err_probe(dev, PTR_ERR(ir->ss),
				     "failed to get SS GPIO\n");

	ir->cdone = devm_gpiod_get(dev, "cdone", GPIOD_IN);
	if (IS_ERR(ir->cdone))
		return dev_err_probe(dev, PTR_ERR(ir->cdone),
				     "failed to get CDONE GPIO\n");

	ret = devm_request_irq(dev, client->irq, ice40_ir_irq, 0,
			       dev_name(dev), ir);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ\n");

	of_property_read_string(dev->of_node, "firmware-name", &fw_name);
	ret = request_firmware(&fw, fw_name, dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to load %s\n", fw_name);

	ret = ice40_ir_configure(ir, fw);
	release_firmware(fw);
	if (ret)
		return dev_err_probe(dev, ret, "FPGA configuration failed\n");

	rc = devm_rc_allocate_device(dev, RC_DRIVER_IR_RAW_TX);
	if (!rc)
		return -ENOMEM;

	rc->priv = ir;
	rc->driver_name = DRIVER_NAME;
	rc->device_name = DEVICE_NAME;
	rc->tx_ir = ice40_ir_tx;
	rc->s_tx_carrier = ice40_ir_set_carrier;

	i2c_set_clientdata(client, ir);
	ice40_ir_debugfs_init(ir);

	return devm_rc_register_device(dev, rc);
}

static int ice40_ir_suspend(struct device *dev)
{
	struct ice40_ir *ir = dev_get_drvdata(dev);

	gpiod_set_value_cansleep(ir->ss, 1);

	return 0;
}

static int ice40_ir_resume(struct device *dev)
{
	struct ice40_ir *ir = dev_get_drvdata(dev);

	gpiod_set_value_cansleep(ir->ss, 0);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ice40_ir_pm_ops, ice40_ir_suspend,
				ice40_ir_resume);

static const struct of_device_id ice40_ir_of_match[] = {
	{ .compatible = "samsung,ice40-ir-tx" },
	{ }
};
MODULE_DEVICE_TABLE(of, ice40_ir_of_match);

static struct i2c_driver ice40_ir_driver = {
	.probe = ice40_ir_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ice40_ir_of_match,
		.pm = pm_sleep_ptr(&ice40_ir_pm_ops),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_i2c_driver(ice40_ir_driver);

MODULE_DESCRIPTION("Samsung iCE40 FPGA IR transmitter");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE_NAME);
