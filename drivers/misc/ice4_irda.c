/*
 * driver/ice4_fpga IRDA fpga driver
 *
 * Copyright (C) 2013 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm-generic/errno-base.h>
#include <linux/device/class.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/sysfs.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>

#define IRDA_NAME "ice4-irda"
/* TODO: Define as standard kernel IR transmitter, but this should suffice for testing */
#define DEVICE_CLASS "samsung_irda"
#define DEVICE_NAME "ice4_irda"

#define IRDA_TEST_CODE_SIZE 141
#define IRDA_TEST_CODE_ADDR 0x00
#define MAX_SIZE 2048
#define READ_LENGTH 8

#define DUMMY_BIT_COUNT 49

struct ice4_fpga_data {
	struct i2c_client *client;
	struct device *dev;
	struct device *sys_dev;
	struct regulator *ir_regulator;
	struct mutex mutex;
	struct clk *mclk;
	struct cdev char_dev;
	struct class *dev_class;
	struct {
		unsigned char addr;
		unsigned char data[MAX_SIZE];
	} i2c_block_transfer;
	int count;
	int dev_id;
	int ir_freq;
	int ir_sum;

	int gpio_irda_irq;
	int gpio_fpga_rst_n;
};

static int ack_number;
static int count_number;

static struct class *ice4_irda_class;

/* sysfs node ir_send */
static void ir_remocon_work(struct ice4_fpga_data *data, int count)
{
	struct i2c_client *client = data->client;

	int buf_size = count + 2;
	int ret;
	int emission_time;
	int ack_pin_onoff;
	int retry_count = 0;

	data->i2c_block_transfer.addr = 0x00;

	data->i2c_block_transfer.data[0] = count >> 8;
	data->i2c_block_transfer.data[1] = count & 0xff;

	if (count_number >= 100)
		count_number = 0;

	count_number++;

	dev_info(data->dev, "total buf_size: %d\n", buf_size);

	mutex_lock(&data->mutex);

	buf_size++;
	ret = i2c_master_send(
		client, (unsigned char *)&(data->i2c_block_transfer), buf_size);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err1 %d\n", __func__, ret);
		ret = i2c_master_send(
			client, (unsigned char *)&(data->i2c_block_transfer),
			buf_size);
		if (ret < 0) {
			dev_err(&client->dev, "%s: err2 %d\n", __func__, ret);
		}
	}

	mdelay(10);

	ack_pin_onoff = 0;

	if (gpio_get_value(data->gpio_irda_irq)) {
		dev_info(data->dev, "%d Checksum NG!\n", count_number);
		ack_pin_onoff = 1;
	} else {
		dev_info(data->dev, "%d Checksum OK!\n", count_number);
		ack_pin_onoff = 2;
	}
	ack_number = ack_pin_onoff;

	mutex_unlock(&data->mutex);

	data->count = 2;

	emission_time = (1000 * (data->ir_sum) / (data->ir_freq));
	if (emission_time > 0)
		msleep(emission_time);

	dev_info(data->dev, "emission_time = %d\n", emission_time);

	while (!gpio_get_value(data->gpio_irda_irq)) {
		mdelay(10);
		dev_info(data->dev, "%d try to check IRDA_IRQ\n", retry_count);
		retry_count++;

		if (retry_count > 5)
			break;
	}

	if (gpio_get_value(data->gpio_irda_irq)) {
		dev_info(data->dev, "%d Sending IR OK!\n", count_number);
		ack_pin_onoff = 4;
	} else {
		dev_info(data->dev, "%d Sending IR NG!\n", count_number);
		ack_pin_onoff = 2;
	}

	ack_number += ack_pin_onoff;

	data->ir_freq = 0;
	data->ir_sum = 0;
}

static ssize_t remocon_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t size)
{
	dev_info(dev, "remocon_store called!\n");
	struct ice4_fpga_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int count, i, ret;

	if (data->ir_regulator) {
		ret = regulator_enable(data->ir_regulator);
		if (ret)
			dev_err(data->dev, "Cannot enable regulator\n");
	}

	usleep_range(2500, 3000);

	dev_info(data->dev, "ir_send called\n");

	for (i = 0; i < MAX_SIZE; i++) {
		if (sscanf(buf++, "%u", &value) == 1) {
			if (value == 0 || *buf == '\0')
				break;

			if (data->count == 2) {
				data->ir_freq = value;
				data->i2c_block_transfer.data[2] = value >> 16;
				data->i2c_block_transfer.data[3] =
					(value >> 8) & 0xFF;
				data->i2c_block_transfer.data[4] = value & 0xFF;

				data->count += 3;
			} else {
				data->ir_sum += value;
				count = data->count;
				data->i2c_block_transfer.data[count] = value >>
								       8;
				data->i2c_block_transfer.data[count + 1] =
					value & 0xFF;
				data->count += 2;
			}

			while (value > 0) {
				buf++;
				value /= 10;
			}
		} else {
			break;
		}
	}

	ir_remocon_work(data, data->count);

	if (data->ir_regulator) {
		ret = regulator_disable(data->ir_regulator);
		if (ret)
			dev_err(data->dev, "Cannot disable regulator\n");
	}

	return size;
}

static ssize_t remocon_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	dev_info(dev, "remocon_show called!\n");
	struct ice4_fpga_data *data = dev_get_drvdata(dev);
	int i;
	char *bufp = buf;

	for (i = 5; i < MAX_SIZE - 1; i++) {
		if (data->i2c_block_transfer.data[i] == 0 &&
		    data->i2c_block_transfer.data[i + 1] == 0)
			break;
		else
			bufp += sprintf(bufp, "%u,",
					data->i2c_block_transfer.data[i]);
	}
	return strlen(buf);
}

static DEVICE_ATTR(ir_send, 0664, remocon_show, remocon_store);

/* sysfs node ir_send_result */
static ssize_t remocon_ack(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ice4_fpga_data *data = dev_get_drvdata(dev);

	dev_info(data->dev, "ack_number = %d\n", ack_number);

	if (ack_number == 6)
		return sprintf(buf, "1\n");

	return sprintf(buf, "0\n");
}

static DEVICE_ATTR(ir_send_result, 0664, remocon_ack, NULL);

static int irda_read_device_info(struct ice4_fpga_data *data)
{
	struct i2c_client *client = data->client;
	u8 buf_ir_test[8];
	int ret;

	dev_info(data->dev, "%s: called\n", __func__);

	ret = i2c_master_recv(client, buf_ir_test, READ_LENGTH);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	dev_info(data->dev, "buf_ir dev_id: 0x%02x, 0x%02x\n", buf_ir_test[2],
		 buf_ir_test[3]);
	ret = data->dev_id = (buf_ir_test[2] << 8 | buf_ir_test[3]);

	return ret;
}

/* sysfs node check_ir */
static ssize_t check_ir_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct ice4_fpga_data *data = dev_get_drvdata(dev);
	int ret;

	ret = irda_read_device_info(data);

	return snprintf(buf, 4, "%d\n", ret);
}

static DEVICE_ATTR(check_ir, 0664, check_ir_show, NULL);

static ssize_t toggle_rst_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t size)
{
	struct ice4_fpga_data *data = dev_get_drvdata(dev);
	static int high;

	dev_info(data->dev, "GPIO_FPGA_RST_N(%d) will be %d\n",
		 data->gpio_fpga_rst_n, high);
	gpio_set_value(data->gpio_fpga_rst_n, high);

	high = !high;

	return size;
}

static DEVICE_ATTR(toggle_rst, 0664, NULL, toggle_rst_store);

/* sysfs node irda_test */
static ssize_t irda_test_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t size)
{
	int ret, i;
	struct ice4_fpga_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct {
		unsigned char addr;
		unsigned char data[IRDA_TEST_CODE_SIZE];
	} i2c_block_transfer;
	unsigned char BSR_data[IRDA_TEST_CODE_SIZE] = {
		0x00, 0x8D, 0x00, 0x96, 0x00, 0x01, 0x50, 0x00, 0xA8, 0x00,
		0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00,
		0x3F, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00,
		0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00,
		0x15, 0x00, 0x15, 0x00, 0x3F, 0x00, 0x15, 0x00, 0x3F, 0x00,
		0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x3F, 0x00, 0x15, 0x00,
		0x3F, 0x00, 0x15, 0x00, 0x3F, 0x00, 0x15, 0x00, 0x3F, 0x00,
		0x15, 0x00, 0x3F, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00,
		0x3F, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00,
		0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00,
		0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x3F, 0x00,
		0x15, 0x00, 0x15, 0x00, 0x15, 0x00, 0x3F, 0x00, 0x15, 0x00,
		0x3F, 0x00, 0x15, 0x00, 0x3F, 0x00, 0x15, 0x00, 0x3F, 0x00,
		0x15, 0x00, 0x3F, 0x00, 0x15, 0x00, 0x3F, 0x00, 0x15, 0x00,
		0x3F
	};

	dev_info(data->dev, "IRDA test code start\n");

	/* make data for sending */
	for (i = 0; i < IRDA_TEST_CODE_SIZE; i++)
		i2c_block_transfer.data[i] = BSR_data[i];

	/* sending data by I2C */
	i2c_block_transfer.addr = IRDA_TEST_CODE_ADDR;
	ret = i2c_master_send(client, (unsigned char *)&i2c_block_transfer,
			      IRDA_TEST_CODE_SIZE);
	if (ret < 0) {
		dev_err(data->dev, "%s: err1 %d\n", __func__, ret);
		ret = i2c_master_send(client,
				      (unsigned char *)&i2c_block_transfer,
				      IRDA_TEST_CODE_SIZE);
		if (ret < 0)
			dev_err(data->dev, "%s: err2 %d\n", __func__, ret);
	}

	return size;
}

static ssize_t irda_test_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return strlen(buf);
}

static DEVICE_ATTR(irda_test, 0664, irda_test_show, irda_test_store);

static struct attribute *sec_ir_attributes[] = {
	&dev_attr_ir_send.attr,	   &dev_attr_ir_send_result.attr,
	&dev_attr_check_ir.attr,   &dev_attr_irda_test.attr,
	&dev_attr_toggle_rst.attr, NULL,
};

static struct attribute_group sec_ir_attr_group = {
	.attrs = sec_ir_attributes,
};

static int ice4_irda_parse_dt(struct device *dev)
{
	struct ice4_fpga_data *data = dev_get_drvdata(dev);
	struct device_node *node = dev->of_node;
	int ret;

	data->gpio_irda_irq = of_get_named_gpio(node, "irda-gpio", 0);
	if (!gpio_is_valid(data->gpio_irda_irq)) {
		dev_err(dev, "Cannot get irda-gpio\n");
		return -ENODEV;
	}

	data->gpio_fpga_rst_n = of_get_named_gpio(node, "fpga-reset-gpio", 0);
	if (!gpio_is_valid(data->gpio_fpga_rst_n)) {
		dev_err(dev, "Cannot get fpga-reset-gpio\n");
		return -ENODEV;
	}

	data->ir_regulator = devm_regulator_get(dev, "ir-supply");
	if (IS_ERR(data->ir_regulator)) {
		dev_err(dev, "Cannot get ir regulator\n");
		data->ir_regulator = NULL;
	}

	data->mclk = devm_clk_get(data->dev, "out");
	if (IS_ERR(data->mclk)) {
		dev_err(dev, "Cannot get out clk\n");
		return PTR_ERR(data->mclk);
	}

	ret = clk_prepare_enable(data->mclk);
	if (ret) {
		dev_err(dev, "Cannot clk enalbe : out");
		return ret;
	}

	return 0;
}

static int ice4_irda_gpio_configuration(struct device *dev)
{
	struct ice4_fpga_data *data = dev_get_drvdata(dev);
	int ret;

	ret = devm_gpio_request_one(dev, data->gpio_irda_irq, GPIOF_IN,
				    "irda-gpio");
	if (ret) {
		dev_err(dev, "Cannot request irda-gpio\n");
		goto err_gpio_request;
	}

	ret = devm_gpio_request_one(dev, data->gpio_fpga_rst_n,
				    GPIOF_OUT_INIT_LOW, "fpga-reset-gpio");
	if (ret) {
		dev_err(dev, "Cannot request fpga-reset-gpio\n");
		goto err_gpio_request;
	}

	return 0;

err_gpio_request:
	return ret;
}

static int ice4_irda_probe(struct i2c_client *client)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct ice4_fpga_data *data;
	int ret;

	dev_info(&client->dev, "probe start!\n");

	/* TODO: Wait until FPGA region has been initialized (fpga-region property)*/

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,
			"Failed to i2c functionality check err\n");
		return -EIO;
	}

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (NULL == data) {
		dev_err(&client->dev, "Failed to allocate data\n");
		return -ENOMEM;
	}

	data->dev = &client->dev;
	data->client = client;
	mutex_init(&data->mutex);
	data->count = 2;
	i2c_set_clientdata(client, data);

	ret = ice4_irda_parse_dt(&client->dev);
	if (ret) {
		dev_err(&client->dev, "Failed to parse dt\n");
		return ret;
	}
	dev_info(&client->dev, "Parsed device tree!\n");

	ret = ice4_irda_gpio_configuration(&client->dev);
	if (ret) {
		dev_err(&client->dev, "Failed to set GPIO configuration!\n");
		return ret;
	}
	dev_info(&client->dev, "Set GPIO configuration!\n");

	data->dev_class = ice4_irda_class;

	// For some reason, the class is registered even after unloading the module.
	// Work around by checking and not registering if it exists.
	if (!class_is_registered(data->dev_class)) {
		// TODO: register owner as THIS_MODULE
		ret = class_register(data->dev_class);
		if (ret < 0) {
			dev_err(&client->dev,
				"Failed to register device class!\n");
			goto err_class_register;
		}
		dev_info(&client->dev, "Registered device class!\n");
	} else {
		dev_info(&client->dev,
			 "Device class already registered, skipping!\nn");
	}

	data->sys_dev = device_create(data->dev_class, data->dev,
				      data->char_dev.dev, data, DEVICE_NAME);
	if (IS_ERR(data->sys_dev)) {
		dev_err(&client->dev,
			"Failed to create ice4_irda_dev device!\n");
		return PTR_ERR(data->sys_dev);
	}
	dev_info(&client->dev, "Created device!\n");

	ret = sysfs_create_group(&data->sys_dev->kobj, &sec_ir_attr_group);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to create sysfs group!\n");
		goto err_sysfs_create;
	}
	dev_info(&client->dev, "Created sysfs group!\n");

	dev_info(&client->dev, "Probe complete!\n");
	return 0;

err_sysfs_create:
	device_destroy(data->dev_class, data->sys_dev->devt);
err_class_register:

	return ret;
}

static void ice4_irda_remove(struct i2c_client *client)
{
	struct ice4_fpga_data *data = i2c_get_clientdata(client);

	sysfs_remove_group(&data->sys_dev->kobj, &sec_ir_attr_group);
	clk_disable_unprepare(data->mclk);
	device_destroy(data->dev_class, data->sys_dev->devt);

	dev_info(&client->dev, "Cleanup complete!\n");
}

#ifdef CONFIG_PM
static int ice4_irda_suspend(struct device *dev)
{
	struct ice4_fpga_data *data = dev_get_drvdata(dev);

	gpio_set_value(data->gpio_fpga_rst_n, 0);

	return 0;
}

static int ice4_irda_resume(struct device *dev)
{
	struct ice4_fpga_data *data = dev_get_drvdata(dev);

	gpio_set_value(data->gpio_fpga_rst_n, 1);

	return 0;
}

static const struct dev_pm_ops ice4_fpga_pm_ops = {
	.suspend = ice4_irda_suspend,
	.resume = ice4_irda_resume,
};
#endif

static struct of_device_id ice4_irda_of_match[] = {
	{
		.compatible = "samsung,ice4-irda",
	},
	{},
};

static const struct i2c_device_id ice4_irda_id[] = { { IRDA_NAME, 0 }, {} };
MODULE_DEVICE_TABLE(i2c, ice4_irda_id);

static struct i2c_driver ice4_irda_i2c_driver = {
	.probe = ice4_irda_probe,
	.remove = ice4_irda_remove,
	.driver = {
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ice4_irda_of_match),
		.name	= IRDA_NAME,
#ifdef CONFIG_PM
		.pm	= &ice4_fpga_pm_ops,
#endif
	},
	.id_table = ice4_irda_id,
};

static int __init ice4_irda_module_init(void)
{
	int ret = 0;

	ice4_irda_class = class_create(DEVICE_CLASS);
	if (IS_ERR(ice4_irda_class)) {
		pr_err("failed to create class: %d\n", ret);
		return PTR_ERR(ice4_irda_class);
	}

	return i2c_register_driver(THIS_MODULE, &ice4_irda_i2c_driver);
}

static void __exit ice4_irda_module_exit(void)
{
	class_destroy(ice4_irda_class);
	i2c_del_driver(&ice4_irda_i2c_driver);
};

module_init(ice4_irda_module_init);
module_exit(ice4_irda_module_exit);
// module_i2c_driver(ice4_irda_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SEC IRDA driver using ice4 fpga");
