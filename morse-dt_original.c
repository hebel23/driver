/*
 * morse-dt.c: Verwendung von Dig-Output zum Morsen mit Device-Tree
 * 
 * Copyright (c) 2016 Andreas Klinger <ak@it-klinger.de>
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
 * Mechanismen:
 * - Dateischnittstelle: open(), close(), write()
 * - GPIO's
 * - dynamisch oder statisch vergebene Major-Number
 * - Generierung eines /sys-FS-Eintrages inkl. UEVENT für udevd bzw. mdev
 */

/*
 * Device-Tree-Beispiel
 *
 * morse@0 {
 * 	compatible = "seminar,morse";
 * 	mors-gpios = <&gpio3 9 GPIO_ACTIVE_HIGH>;
 * };      
 */

#include <linux/version.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>

#define MODULE_NAME "morse"

#define MORSE_DOT_UNIT_DEFAULT	500

struct morse_data {
	struct device		*dev;
	struct gpio_desc	*gpiod_mors;
	struct cdev		cdev;
	dev_t			devt;
	struct class		*morse_class;
	struct device		*morse_device;
	struct device_attribute dev_attr_dot_unit;
	unsigned int		dot_unit;
};

struct morse_zeichen {
	char c;
	char* z;
};

static struct morse_zeichen morse_table[] = {
	{'a', ".-S"},
	{'b', "-...S"},
	{'c', "-.-.S"},
	{'d', "-..S"},
	{'e', ".S"},
	{'f', "..-.S"},
	{'g', "--.S"},
	{'h', "....S"},
	{'i', "..S"},
	{'j', ".---S"},
	{'k', "-.-S"},
	{'l', ".-..S"},
	{'m', "--S"},
	{'n', "-.S"},
	{'o', "---S"},
	{'p', ".--.S"},
	{'q', "--.-S"},
	{'r', ".-.S"},
	{'s', "...S"},
	{'t', "-S"},
	{'u', "..-S"},
	{'v', "...-S"},
	{'w', ".--S"},
	{'x', "-..-S"},
	{'y', "-.--S"},
	{'z', "--..S"},
	{'1', ".----S"},
	{'2', "..---S"},
	{'3', "...--S"},
	{'4', "....-S"},
	{'5', ".....S"},
	{'6', "-....S"},
	{'7', "--...S"},
	{'8', "---..S"},
	{'9', "----.S"},
	{'0', "-----S"},
	{0, NULL},
};

struct morse_string {
	int len;
	char s[100];
};

static void morse_lang(struct morse_data *data)
{
	gpiod_set_value (data->gpiod_mors, 1);
	msleep(3 * data->dot_unit);
	gpiod_set_value (data->gpiod_mors, 0);
	msleep(data->dot_unit);
}

static void morse_kurz(struct morse_data *data)
{
	gpiod_set_value (data->gpiod_mors, 1);
	msleep(data->dot_unit);
	gpiod_set_value (data->gpiod_mors, 0);
	msleep(data->dot_unit);
}

static void morse_letter_space(struct morse_data *data)
{
	/*
	 * Pause: 3 Dot-Spaces
	 * 1 Dot-Space von kurz-/lang-Morse-Zeichen vorhanden
	 */
	msleep(2 * data->dot_unit);
}

static void morse_word_space(struct morse_data *data)
{
	/*
	 * Pause: 7 Dot-Spaces
	 * 1 Dot-Space von kurz-/lang-Morse-Zeichen vorhanden
	 * 2 Dot-Spaces von Letter-Space vorhanden
	 */
	msleep(4 * data->dot_unit);
}

static void morse_char(char ch, struct morse_data *data)
{
	int i = 0;

	while ((morse_table[i].c) && (morse_table[i].c != tolower(ch)))
		i++;

	if (morse_table[i].c) {
		int j = 0;
		while (morse_table[i].z[j] != 'S') {
			switch (morse_table[i].z[j]) {
				case '.':
					morse_kurz(data);
					break;
				case '-':
					morse_lang(data);
					break;
			}
			j++;
		}
		morse_letter_space(data);
	} else {
		/*
		 * Vereinfachung: 
		 * eigentlich für Space, jedoch auch bei allen anderen
		 * nicht gefundenen Zeichen
		 */
		morse_word_space(data);
	}
}

static int morse_fkt (struct morse_string *ms, struct morse_data *data)
{
	int i;

	dev_info (data->dev, "morse_fkt(START): len=%d, s=%s\n", ms->len,
			ms->s);

	for (i = 0; i < ms->len; i++) {
		morse_char(ms->s[i], data);
	}

	dev_info (data->dev, "morse_fkt(ENDE): len=%d, s=%s\n", ms->len,
			ms->s);

	kfree (ms);

	return 0;
}

static ssize_t dot_unit_store (struct device* dev, struct device_attribute*
		attr, const char* buf, size_t count)
{
	struct morse_data *data = container_of(attr, struct morse_data,
			dev_attr_dot_unit);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;
	data->dot_unit = val;

	return count;
}

static ssize_t dot_unit_show (struct device* dev, struct device_attribute*
		attr, char* buf)
{
	struct morse_data *data = container_of(attr, struct morse_data,
			dev_attr_dot_unit);

	sprintf (buf, "%u\n", data->dot_unit);
	return strlen(buf);
}

static ssize_t morse_write(struct file *file, const char *buf, size_t
		count, loff_t *offset)
{
	struct morse_data *data = file->private_data;
	int nLen;
	struct morse_string* ms = NULL;

	ms = kmalloc (sizeof(struct morse_string), 
						GFP_KERNEL | __GFP_ZERO);

	nLen = sizeof(ms->s);
	if (count < nLen)
		nLen = count;

	if ( !copy_from_user( ms->s, buf, nLen) ) {
		ms->len = nLen;
		dev_info (data->dev, "Morse: (%d) %s\n", ms->len, ms->s);

		morse_fkt (ms, data);

		return (count);
	} else {
		kfree (ms);
		return (-ENOMEM);
	}
}

static int morse_open(struct inode *node, struct file *file)
{
	struct morse_data *data = container_of(node->i_cdev,
					struct morse_data, cdev);

	file->private_data = data;

	return 0;
}

static struct file_operations morse_fops = {
	.owner   = THIS_MODULE,
	.open    = morse_open,
	.write   = morse_write,
};

static int morse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct morse_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(struct morse_data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "failed to allocate device\n");
		return -ENOMEM;
	}

	data->dev = dev;

	data->devt = MKDEV(240, 0);
	ret = register_chrdev_region(data->devt, 1, MODULE_NAME);
	if (ret < 0) {
		dev_err(dev, "register_chrdev_region(): ret=%d\n", ret);
		goto out;
	}

	cdev_init(&data->cdev, &morse_fops);
	if ((ret = cdev_add(&data->cdev, data->devt, 1)) < 0) {
		dev_err(data->dev, "cdev_add: ret=%d\n", ret);
		goto out_unalloc_region;
	}

	data->morse_class = class_create (THIS_MODULE, MODULE_NAME);
	if (IS_ERR(data->morse_class)) {
		dev_err(data->dev, "class_create: err=%ld\n",
				PTR_ERR(data->morse_class));
		ret = PTR_ERR(data->morse_class);
		goto out_cdev_del;
	}

	data->morse_device = device_create (data->morse_class, NULL,
			data->devt, NULL, "morse%d", MINOR(data->devt));
	if (IS_ERR(data->morse_device)) {
		dev_err(data->dev, "device_create: err=%ld\n",
				PTR_ERR(data->morse_device));
		ret = PTR_ERR(data->morse_device);
		goto out_class_destroy;
	}

	data->dot_unit = MORSE_DOT_UNIT_DEFAULT;

	sysfs_attr_init(&data->dev_attr_dot_unit);
	data->dev_attr_dot_unit.attr.name = "dot_unit";
	data->dev_attr_dot_unit.attr.mode = 0664;
	data->dev_attr_dot_unit.show = dot_unit_show;
	data->dev_attr_dot_unit.store = dot_unit_store;

	ret = device_create_file (data->morse_device,
			&data->dev_attr_dot_unit);
	if (ret) {
		dev_err(data->dev, "device_create_file: ret=%d\n", ret);
		goto out_device_destroy;
	}

	data->gpiod_mors = devm_gpiod_get(dev, "mors", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_mors)) {
		dev_err(dev, "failed to get mors-gpios: err=%ld\n",
				PTR_ERR(data->gpiod_mors));
		ret = PTR_ERR(data->gpiod_mors);
		goto out_device_remove_file;
	}

	platform_set_drvdata(pdev, data);

	return ret;

out_device_remove_file:
	device_remove_file (data->morse_device, &data->dev_attr_dot_unit);
out_device_destroy:
	device_destroy (data->morse_class, data->devt);
out_class_destroy:
	class_destroy (data->morse_class);
out_cdev_del:
	cdev_del(&data->cdev);
out_unalloc_region:
	unregister_chrdev_region(data->devt, 1);
out:
	return ret;
}

static int morse_remove(struct platform_device *pdev)
{
	struct morse_data *data = platform_get_drvdata(pdev);

	device_remove_file (data->morse_device, &data->dev_attr_dot_unit);
	device_destroy (data->morse_class, data->devt);
	class_destroy (data->morse_class);

	cdev_del(&data->cdev);
	unregister_chrdev_region(data->devt, 1);

	return 0;
}

static const struct of_device_id of_morse_match[] = {
	{ .compatible = "seminar,morse", },
	{},
};

MODULE_DEVICE_TABLE(of, of_morse_match);

static struct platform_driver morse_driver = {
	.probe		= morse_probe,
	.remove		= morse_remove,
	.driver		= {
		.name		= "morse",
		.of_match_table	= of_morse_match,
	},
};

module_platform_driver(morse_driver);

MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_DESCRIPTION("Morsezeichen mittels GPIO senden");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:driver");

