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
#include <linux/platform_device.h>


#define MODULE_NAME 	"morse-dt"
#define NPAGES 			8
#define NPAGES_ORDER 	3

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

// Treiber-Variablen für jede Instanz
struct morse_dt_data
{
	struct device 			*dev;
	dev_t 					dev_t;
	struct gpio_desc 		*gpiod_outpin;
	struct gpio_desc 		*gpiod_inpin;
	struct cdev				morse_cdev;
	struct class 			*morse_class;
	struct device 			*morse_device;	
	struct device_attribute dev_attr_dot_unit;
	unsigned int 			morse_dot_unit;
	char *                  morse_mem;
};

static void morse_lang(struct morse_dt_data* data)
{
	gpiod_set_value(data->gpiod_outpin, 1);
	msleep(3 * data->morse_dot_unit);
	gpiod_set_value(data->gpiod_outpin, 0);
	msleep(data->morse_dot_unit);
}

static void morse_kurz(struct morse_dt_data* data)
{
	gpiod_set_value(data->gpiod_outpin, 1);
	msleep(data->morse_dot_unit);
	gpiod_set_value(data->gpiod_outpin, 0);
	msleep(data->morse_dot_unit);
}


static void morse_letter_space(struct morse_dt_data* data)
{
	// Pause: 3 Dot-Spaces
	// 1 Dot-Space von kurz-/lang-Morse-Zeichen vorhanden
	msleep(2 * data->morse_dot_unit);
}

static void morse_word_space(struct morse_dt_data* data)
{
	// Pause: 7 Dot-Spaces
	// 1 Dot-Space von kurz-/lang-Morse-Zeichen vorhanden
	// 2 Dot-Spaces von Letter-Space vorhanden
	msleep(4 * data->morse_dot_unit);
}


static void morse_char(struct morse_dt_data* data, char ch)
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
					morse_kurz(data);
					break;
				case '-':
					morse_lang(data);
					break;
			}
			j++;
		}
		morse_letter_space(data);
	}
	else
	{
		// Vereinfachung: 
		// eigentlich für Space, jedoch auch bei allen anderen nicht gefundenen Zeichen
		morse_word_space(data);
	}
}


int morse_fkt (struct morse_dt_data* _data, void* data)
{
	int i;
	struct morse_string* ms = (struct morse_string*)data;

	printk (KERN_INFO "morse_fkt(START): (%d) %s\n", ms->len, ms->s);

	for (i = 0; i < ms->len; i++)
	{
		morse_char(_data, ms->s[i]);
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

	struct morse_dt_data *data = (struct morse_dt_data*) file->private_data;

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

	iButton = gpiod_get_value(data->gpiod_inpin);
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

	struct morse_dt_data *data = (struct morse_dt_data*) file->private_data;

	ms = kmalloc (sizeof(struct morse_string), GFP_KERNEL | __GFP_ZERO);

	nLen = sizeof(ms->s);
	if (count < nLen)
		nLen = count;

	if (!copy_from_user(ms->s, buf, count) )
	{
		ms->len = count;
		printk (KERN_INFO "Morse: (%d) %s\n", ms->len, ms->s);

		morse_fkt (data, (void*)ms);

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
	// Trick um die Adresse von unserer Device Instanz zu setzen
	struct morse_dt_data *data;

	printk (KERN_INFO "morse_open\n");
	
	data = container_of(node->i_cdev, struct morse_dt_data, morse_cdev);

	// damit später darauf zugegriffen werden kann
	file->private_data = data; /* for other methods */

	return 0;
}

static int morse_close(struct inode *node, struct file *file)
{
	printk (KERN_INFO "morse_close\n");

	return 0;
}

static int morse_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	long length = vma->vm_end - vma->vm_start;
	char Test[] = "Test";

	struct morse_dt_data *data = (struct morse_dt_data*)file->private_data;

	// prüfe Länge
	if(length > NPAGES * PAGE_SIZE)
	{
		return -EIO;
	}

	if((ret = remap_pfn_range(vma,
						      vma->vm_start,
							  virt_to_phys((void*)data->morse_mem) >> PAGE_SHIFT,
							  length,
							  vma->vm_page_prot)) < 0 )
							  {
								  return ret;
							  }

	memcpy(data->morse_mem, &Test, 4);

	return 0;
}

static struct file_operations morse_fops = {
	.owner   = THIS_MODULE,
	.open    = morse_open,
	.release = morse_close,
	.write   = morse_write,
	.read    = morse_read,
	.mmap    = morse_mmap,
};

// implementiert compatible-Strings die unterstützt werden sollen
static const struct of_device_id of_morse_dt_match[] =
{
	{.compatible = "morse-dt", } ,
	{},
};

// erstellt Tabelle mit allen im Modul implementierten Device-Treibern
MODULE_DEVICE_TABLE(of, of_morse_dt_match);

ssize_t dot_unit_store (struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	struct morse_dt_data *data = container_of(attr, struct morse_dt_data,	dev_attr_dot_unit);

	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	data->morse_dot_unit = val;

	return count;
}

ssize_t dot_unit_show (struct device* dev, struct device_attribute* attr, char* buf)
{
	struct morse_dt_data *data = container_of(attr, struct morse_dt_data, dev_attr_dot_unit);

	sprintf (buf, "%u\n", data->morse_dot_unit);
	return strlen(buf);
}

// Probe Funktion
static int morse_dt_probe(struct platform_device *pdev)
{
	int ret = 0;

	struct device 			*dev = &pdev->dev;
	struct morse_dt_data 	*data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);

	dev_info(dev, "morse_dt_probe()\n");

	// Informationen vom Device Tree einlesen, welche Pins verwendet werden sollen
	data->gpiod_outpin = devm_gpiod_get(dev, "output", GPIOD_OUT_HIGH);
	if(IS_ERR(data->gpiod_outpin))
	{
		dev_err(dev, "output-gpios: err=%ld\n", PTR_ERR(data->gpiod_outpin));
		return PTR_ERR(data->gpiod_outpin);
	}

	data->gpiod_inpin = devm_gpiod_get(dev, "input", GPIOD_IN);
	if(IS_ERR(data->gpiod_inpin))
	{
		dev_err(dev, "input-gpios: err=%ld\n", PTR_ERR(data->gpiod_inpin));
		return PTR_ERR(data->gpiod_inpin);
	}

	platform_set_drvdata(pdev, data);

	// 32bit device nummer erstellen
	data->dev_t = MKDEV(240, 0);
	// major/minor Nummerbereich registrieren
	if ((ret = register_chrdev_region(data->dev_t, 1, MODULE_NAME)) < 0) 
	{
		printk(KERN_ERR "morse_init(): register_chrdev_region(): ret=%d\n", ret);
		ret = -1;
		goto out;
	}
	// Character device initialisieren
	cdev_init(&data->morse_cdev, &morse_fops);
	// beim virtuellen Filesystem anmelden
	if ((ret = cdev_add(&data->morse_cdev, data->dev_t, 1)) < 0) 
	{
		printk(KERN_ERR "morse_init(): cdev_add(): ret=%d\n", ret);
		ret = -2;
		goto out_unalloc_region;
	}

	// Eintrag fuer uevent in /sys anlegen
	// /sys/class/morse
	data->morse_class = class_create (THIS_MODULE, MODULE_NAME);
	if (IS_ERR(data->morse_class))
	{
		printk (KERN_ERR "morse_init(): class_create(): err=%ld\n", PTR_ERR(data->morse_class));
		ret = -5;
		goto out_cdev_del;
	}

	// /sys/class/morse/morse0/
	data->morse_device = device_create (data->morse_class, NULL, data->dev_t, NULL, "morse%d", MINOR(data->dev_t));
	if (IS_ERR(data->morse_device))
	{
		printk (KERN_ERR "morse_init(): device_create(): err=%ld\n", PTR_ERR(data->morse_device));
		ret = -6;
		goto out_class_destroy;
	}

	// default-Wert
	data->morse_dot_unit = 500;

	// Datei mit Attributen für /sys/class/morse/morse0/dot_unit alegen
	sysfs_attr_init(&data->dev_attr_dot_unit);
	data->dev_attr_dot_unit.attr.name = "dot_unit";
	data->dev_attr_dot_unit.attr.mode = 0664;		// Zugriffsrechte
	data->dev_attr_dot_unit.show = dot_unit_show;
	data->dev_attr_dot_unit.store = dot_unit_store;	

	ret = device_create_file (data->morse_device, &data->dev_attr_dot_unit);
	if (ret)
	{
		printk(KERN_ERR "morse_init(): device_create_file(dot_unit): ret=%d\n", ret);
		ret = -7;
		goto out_device_destroy;
	}

	if((data->morse_mem = (char*)__get_free_pages (GFP_KERNEL, NPAGES_ORDER)) == NULL)
	{
		printk(KERN_ERR "morse_init(): __get_free_pages\n");
		ret = -ENOMEM;
		goto out_device_destroy;
	}

	// alles OK
	return ret;

out_device_destroy:
	device_destroy (data->morse_class, data->dev_t);
out_class_destroy:
	class_destroy (data->morse_class);
out_cdev_del:
	cdev_del(&data->morse_cdev);
out_unalloc_region:
	unregister_chrdev_region(data->dev_t, 1);
out:
	return ret;
}

static int morse_dt_remove(struct platform_device *pdev)
{
	struct morse_dt_data *data = platform_get_drvdata(pdev);

	device_remove_file (data->morse_device, &data->dev_attr_dot_unit);

	device_destroy (data->morse_class, data->dev_t);

	class_destroy (data->morse_class);

	cdev_del(&data->morse_cdev);
	
	unregister_chrdev_region(data->dev_t, 1);

	free_pages((long)data->morse_mem, NPAGES_ORDER);

	printk (KERN_INFO "morse_dt_remove()\n");	

	return 0;
}

// definert Einsprungadressen für Platform-Device-Treiber
static struct platform_driver morse_dt_driver = 
{ 	
	.probe = morse_dt_probe,
	.remove = morse_dt_remove,
	.driver =
	{
		.name = "morse-dt",
		.of_match_table = of_morse_dt_match,	
	},
};

module_platform_driver(morse_dt_driver);

MODULE_DESCRIPTION("Morsezeichen mittels GPIO");
MODULE_AUTHOR("aheberer");
MODULE_LICENSE("GPL");


