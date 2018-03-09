/*
 * SRF04-CHAR: ultrasonic sensor for distance measuring by using GPIOs
 *
 * Copyright (c) 2017 Andreas Klinger <ak@it-klinger.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For details about the device see:
 * http://www.robot-electronics.co.uk/htm/srf04tech.htm
 *
 * the measurement cycle as timing diagram looks like:
 *
 *          +---+
 * GPIO     |   |
 * trig:  --+   +------------------------------------------------------
 *          ^   ^
 *          |<->|
 *         udelay(10)
 *
 * ultra           +-+ +-+ +-+
 * sonic           | | | | | |
 * burst: ---------+ +-+ +-+ +-----------------------------------------
 *                           .
 * ultra                     .              +-+ +-+ +-+
 * sonic                     .              | | | | | |
 * echo:  ----------------------------------+ +-+ +-+ +----------------
 *                           .                        .
 *                           +------------------------+
 * GPIO                      |                        |
 * echo:  -------------------+                        +---------------
 *                           ^                        ^
 *                           interrupt                interrupt
 *                           (ts_rising)              (ts_falling)
 *                           |<---------------------->|
 *                              pulse time measured
 *                              --> one round trip of ultra sonic waves
 */
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>
//#include <linux/iio/iio.h>
//#include <linux/iio/sysfs.h>

struct srf04_data {
	struct device		*dev;
	struct gpio_desc	*gpiod_trig;
	struct gpio_desc	*gpiod_echo;
	struct mutex		lock;
	int			irqnr;
	ktime_t			ts_rising;
	ktime_t			ts_falling;
	struct completion	rising;
	struct completion	falling;
	dev_t			devt;
	struct cdev 		cdev;
};

static irqreturn_t srf04_handle_irq(int irq, void *dev_id)
{
	struct srf04_data *data = (struct srf04_data*)dev_id;

	if (gpiod_get_value(data->gpiod_echo)) {
		data->ts_rising = ktime_get();
		complete(&data->rising);
	} else {
		data->ts_falling = ktime_get();
		complete(&data->falling);
	}

	return IRQ_HANDLED;
}

static int srf04_read_distance(struct srf04_data *data)
{
	int ret;
	ktime_t ktime_dt;
	u64 dt_ns;
	u32 time_ns, distance_mm;

	/*
	 * just one read-echo-cycle can take place at a time
	 * ==> lock against concurrent reading calls
	 */
	mutex_lock(&data->lock);

	reinit_completion(&data->rising);
	reinit_completion(&data->falling);

	gpiod_set_value(data->gpiod_trig, 1);
	udelay(10);
	gpiod_set_value(data->gpiod_trig, 0);

	/* it cannot take more than 20 ms */
	ret = wait_for_completion_killable_timeout(&data->rising, HZ/50);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return ret;
	} else if (ret == 0) {
		mutex_unlock(&data->lock);
		return -ETIMEDOUT;
	}

	ret = wait_for_completion_killable_timeout(&data->falling, HZ/50);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return ret;
	} else if (ret == 0) {
		mutex_unlock(&data->lock);
		return -ETIMEDOUT;
	}

	ktime_dt = ktime_sub(data->ts_falling, data->ts_rising);

	mutex_unlock(&data->lock);

	dt_ns = ktime_to_ns(ktime_dt);
	/*
	 * measuring more than 3 meters is beyond the capabilities of
	 * the sensor
	 * ==> filter out invalid results for not measuring echos of
	 *     another us sensor
	 *
	 * formula:
	 *         distance       3 m
	 * time = ---------- = --------- = 9404389 ns
	 *          speed       319 m/s
	 *
	 * using a minimum speed at -20 °C of 319 m/s
	 */
	if (dt_ns > 9404389)
		return -EIO;

	time_ns = dt_ns;

	/*
	 * the speed as function of the temperature is approximately:
	 *
	 * speed = 331,5 + 0,6 * Temp
	 *   with Temp in °C
	 *   and speed in m/s
	 *
	 * use 343 m/s as ultrasonic speed at 20 °C here in absence of the
	 * temperature
	 *
	 * therefore:
	 *             time     343
	 * distance = ------ * -----
	 *             10^6       2
	 *   with time in ns
	 *   and distance in mm (one way)
	 *
	 * because we limit to 3 meters the multiplication with 343 just
	 * fits into 32 bit
	 */
	distance_mm = time_ns * 343 / 2000000;

	return distance_mm;
}

static int srf04_open(struct inode *node, struct file *file)
{
	struct srf04_data *data = container_of(node->i_cdev, struct srf04_data, cdev);

	file->private_data = data;

	return 0;
}

static ssize_t srf04_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	struct srf04_data *data = file->private_data;
	int ret;
	char val[20];
	int len;

	if ((*offset) > 0)
		return 0;

	ret = srf04_read_distance(data);
	if (ret < 0)
		return ret;

	memset(val, 0, sizeof(val));
	sprintf(val, "%d\n", ret);
	
	len = strlen(val);
	if ( !copy_to_user(buf, val, len) ) {
		(*offset) += len;
		return len;
	}
	else
		return -ENOMEM;

	return 0;
}

static struct file_operations srf04_fops = {
	.owner		= THIS_MODULE,
	.open		= srf04_open,
	.read		= srf04_read,
};

static int srf04_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct srf04_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(struct srf04_data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "failed to allocate device\n");
		return -ENOMEM;
	}

	data->dev = dev;

	mutex_init(&data->lock);
	init_completion(&data->rising);
	init_completion(&data->falling);

	data->gpiod_trig = devm_gpiod_get(dev, "trig", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_trig)) {
		dev_err(dev, "failed to get trig-gpios: err=%ld\n",
					PTR_ERR(data->gpiod_trig));
		return PTR_ERR(data->gpiod_trig);
	}

	data->gpiod_echo = devm_gpiod_get(dev, "echo", GPIOD_IN);
	if (IS_ERR(data->gpiod_echo)) {
		dev_err(dev, "failed to get echo-gpios: err=%ld\n",
					PTR_ERR(data->gpiod_echo));
		return PTR_ERR(data->gpiod_echo);
	}

	if (gpiod_cansleep(data->gpiod_echo)) {
		dev_err(data->dev, "cansleep-GPIOs not supported\n");
		return -ENODEV;
	}

	data->irqnr = gpiod_to_irq(data->gpiod_echo);
	if (data->irqnr < 0) {
		dev_err(data->dev, "gpiod_to_irq: %d\n", data->irqnr);
		return data->irqnr;
	}

	ret = devm_request_irq(dev, data->irqnr, srf04_handle_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			pdev->name, data);
	if (ret < 0) {
		dev_err(data->dev, "request_irq: %d\n", ret);
		return ret;
	}

	data->devt = MKDEV(240, 0);
	if ((ret = register_chrdev_region(data->devt, 1, "srf")) < 0) 
	{
		dev_err(data->dev, "register_chrdev_region: %d\n", ret);
		return ret;
	}

	cdev_init(&data->cdev, &srf04_fops);
	if ((ret = cdev_add(&data->cdev, data->devt, 1)) < 0) 
	{
		dev_err(data->dev, "cdev_add: %d\n", ret);
		unregister_chrdev_region(data->devt, 1);
		return ret;
	}

	platform_set_drvdata(pdev, data);

	return 0;
}

static int srf04_remove(struct platform_device *pdev)
{
	struct srf04_data *data = platform_get_drvdata(pdev);

	cdev_del(&data->cdev);
	unregister_chrdev_region(data->devt, 1);

	return 0;
}

static const struct of_device_id of_srf04_match[] = {
	{ .compatible = "devantech,srf04", },
	{},
};

MODULE_DEVICE_TABLE(of, of_srf04_match);

static struct platform_driver srf04_driver = {
	.probe		= srf04_probe,
	.remove		= srf04_remove,
	.driver		= {
		.name		= "srf04-char",
		.of_match_table	= of_srf04_match,
	},
};

module_platform_driver(srf04_driver);

MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_DESCRIPTION("SRF04 ultrasonic sensor for distance measuring as character device using GPIOs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:srf04");
