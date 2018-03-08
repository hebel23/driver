// morse.c: Verwendung von Dig-Output zum Morsen
//
// Mechanismen:
//  * Dateischnittstelle: open(), close(), write()
//  * GPIO's
//  * dynamisch oder statisch vergebene Major-Number
//  * Generierung eines /sys-FS-Eintrages inkl. UEVENT für udevd bzw. mdev
//

#include <linux/version.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ctype.h>


#define MODULE_NAME "morse"

struct morse_zeichen
{
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


struct morse_string
{
	int len;
	char s[100];
};

static const int morse_led_yellow = 105;	// phyBoard-WEGA-AM-335x
static const int morse_led_orange = 83;		// phyBoard-WEGA-AM-335x
static const int morse_led_red = 82;		// phyBoard-WEGA-AM-335x
static const int morse_btn1 = 20;		// phyBoard-WEGA-AM-335x
static const int morse_btn2 = 7;		// phyBoard-WEGA-AM-335x
static const int morse_btn3 = 106;		// phyBoard-WEGA-AM-335x

static unsigned int morse_dot_unit   = 500;  // [ms]

static dev_t morse_dev;
static struct cdev morse_cdev;
static struct class * morse_class;
static struct device * morse_device;

static void morse_lang(void)
{
	gpio_set_value (morse_led_yellow, 1);
	msleep(3 * morse_dot_unit);
	gpio_set_value (morse_led_yellow, 0);
	msleep(morse_dot_unit);
}

static void morse_kurz(void)
{
	gpio_set_value (morse_led_yellow, 1);
	msleep(morse_dot_unit);
	gpio_set_value (morse_led_yellow, 0);
	msleep(morse_dot_unit);
}


static void morse_letter_space(void)
{
	// Pause: 3 Dot-Spaces
	// 1 Dot-Space von kurz-/lang-Morse-Zeichen vorhanden
	msleep(2 * morse_dot_unit);
}

static void morse_word_space(void)
{
	// Pause: 7 Dot-Spaces
	// 1 Dot-Space von kurz-/lang-Morse-Zeichen vorhanden
	// 2 Dot-Spaces von Letter-Space vorhanden
	msleep(4 * morse_dot_unit);
}


static void morse_char(char ch)
{
	int i = 0;

	while ((morse_table[i].c) && (morse_table[i].c != tolower(ch)))
		i++;

	if (morse_table[i].c)
	{
		int j = 0;
		while (morse_table[i].z[j] != 'S')
		{
			switch (morse_table[i].z[j])
			{
				case '.':
					morse_kurz();
					break;
				case '-':
					morse_lang();
					break;
			}
			j++;
		}
		morse_letter_space();
	}
	else
	{
		// Vereinfachung: 
		// eigentlich für Space, jedoch auch bei allen anderen nicht gefundenen Zeichen
		morse_word_space();
	}
}


int morse_fkt (void* data)
{
	int i;
	struct morse_string* ms = (struct morse_string*)data;

	printk (KERN_INFO "morse_fkt(START): (%d) %s\n", ms->len, ms->s);

	for (i = 0; i < ms->len; i++)
	{
		morse_char(ms->s[i]);
	}

	printk (KERN_INFO "morse_fkt(ENDE): (%d) %s\n", ms->len, ms->s);

	kfree (data);

	return 0;
}

static ssize_t morse_read(struct file *file, char __user *buf, size_t cnt, loff_t *off)
{
	int iRetVal = 0;
	int iButton;
	char *cTempString;

	if(*off >= 2)
	{
		// Abbruch-Kriterium
		return 0;
	}

	cTempString = kmalloc(10, GFP_KERNEL | __GFP_ZERO);
	if(cTempString == 0)
	{
		printk (KERN_ERR "alloc error\n");
		return ENOMEM;
	}

	iButton = gpio_get_value (morse_btn1);
	snprintf(cTempString, 10, "%i\n", iButton);

	if(!copy_to_user(buf, cTempString, strlen(cTempString)))
	{
		*off = strlen(cTempString);
		iRetVal = strlen(cTempString);
	}
	else
	{
		printk (KERN_ERR "copy_to_user error\n");
		iRetVal = -EFAULT;
	}

	kfree (cTempString);

	return iRetVal;
}

static ssize_t morse_write(struct file *file, const char *buf, size_t count, loff_t *offset)
{
	int nLen;
	struct morse_string* ms = NULL;

	ms = kmalloc (sizeof(struct morse_string), GFP_KERNEL | __GFP_ZERO);

	nLen = sizeof(ms->s);
	if (count < nLen)
		nLen = count;

	if ( !copy_from_user(ms->s, buf, count) )
	{
		ms->len = count;
		printk (KERN_INFO "Morse: (%d) %s\n", ms->len, ms->s);

		morse_fkt ((void*)ms);

		return (count);
	}
	else
	{
		kfree (ms);
		return (-ENOMEM);
	}
}


static int morse_open(struct inode *node, struct file *file)
{
	printk (KERN_INFO "morse_open\n");
	return 0;
}


static int morse_close(struct inode *node, struct file *file)
{
	printk (KERN_INFO "morse_close\n");
	return 0;
}


static struct file_operations morse_fops = {
	.owner   = THIS_MODULE,
	.open    = morse_open,
	.release = morse_close,
	.write   = morse_write,
	.read    = morse_read,
};

ssize_t dot_unit_store (struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;
	morse_dot_unit = val;

	return count;
}

ssize_t dot_unit_show (struct device* dev, struct device_attribute* attr, char* buf)
{
	sprintf (buf, "%u\n", morse_dot_unit);
	return strlen(buf);
}

// Datei mit Attributen für /sys/class/morse/morse0/dot_unit
DEVICE_ATTR (dot_unit, 0664, dot_unit_show, dot_unit_store);

static int __init morse_init(void)
{
	int ret = 0;

	printk (KERN_INFO "morse_init()\n");

	morse_dev = MKDEV(240, 0);
	if ((ret = register_chrdev_region(morse_dev, 1, MODULE_NAME)) < 0) 
	{
		printk(KERN_ERR "morse_init(): register_chrdev_region(): ret=%d\n", ret);
		ret = -1;
		goto out;
	}

	cdev_init(&morse_cdev, &morse_fops);
	if ((ret = cdev_add(&morse_cdev, morse_dev, 1)) < 0) 
	{
		printk(KERN_ERR "morse_init(): cdev_add(): ret=%d\n", ret);
		ret = -2;
		goto out_unalloc_region;
	}

	// LED
	ret = gpio_request (morse_led_yellow, MODULE_NAME);
	if (ret)
	{
		printk(KERN_ERR "morse_init(): gpio_request: ret=%d\n", ret);
		ret = -3;
		goto out_cdev_del;
	}

	// Button
	ret = gpio_request(morse_btn1, MODULE_NAME);
	if(ret)
	{
		printk(KERN_ERR "morse_init(): gpio_request: ret=%d\n", ret);
		ret = -3;
		goto out_cdev_del;
	}
	// LED
	ret = gpio_direction_output (morse_led_yellow, 1);
	if (ret)
	{
		printk(KERN_ERR "morse_init(): gpio_direction_output: ret=%d\n", ret);
		ret = -4;
		goto out_gpio_free;
	}

	// Button
	ret = gpio_direction_input(morse_btn1);
	if (ret)
	{
		printk(KERN_ERR "morse_init(): gpio_direction_input: ret=%d\n", ret);
		ret = -4;
		goto out_gpio_free;
	}

	// Eintrag fuer uevent in /sys
	// /sys/class/morse
	morse_class = class_create (THIS_MODULE, MODULE_NAME);
	if (IS_ERR(morse_class))
	{
		printk (KERN_ERR "morse_init(): class_create(): err=%ld\n", PTR_ERR(morse_class));
		ret = -5;
		goto out_cdev_del;
	}

	// /sys/class/morse/morse0/
	morse_device = device_create (morse_class, NULL, morse_dev, NULL, "morse%d", MINOR(morse_dev));
	if (IS_ERR(morse_device))
	{
		printk (KERN_ERR "morse_init(): device_create(): err=%ld\n", PTR_ERR(morse_device));
		ret = -6;
		goto out_class_destroy;
	}

	ret = device_create_file (morse_device, &dev_attr_dot_unit);
	if (ret)
	{
		printk(KERN_ERR "morse_init(): device_create_file(dot_unit): ret=%d\n", ret);
		ret = -7;
		goto out_device_destroy;
	}

	// alles OK
	return ret;

out_device_destroy:
	device_destroy (morse_class, morse_dev);
out_class_destroy:
	class_destroy (morse_class);
out_gpio_free:
	gpio_free(morse_led_yellow);
	gpio_free(morse_btn1);
out_cdev_del:
	cdev_del(&morse_cdev);
out_unalloc_region:
	unregister_chrdev_region(morse_dev, 1);
out:
	return ret;
}

static void __exit morse_exit(void)
{
	device_remove_file (morse_device, &dev_attr_dot_unit);

	device_destroy (morse_class, morse_dev);

	class_destroy (morse_class);

	gpio_free(morse_led_yellow);
	gpio_free(morse_btn1);

	cdev_del(&morse_cdev);
	unregister_chrdev_region(morse_dev, 1);

	printk (KERN_INFO "morse_exit()\n");
}


module_init(morse_init);
module_exit(morse_exit);


MODULE_DESCRIPTION("Morsezeichen mittels GPIO");
MODULE_AUTHOR("aheberer");
MODULE_LICENSE("GPL");


