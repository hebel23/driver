// list_rtmutex.c: konkurrierende Listenoperationen - RT-Mutex
//
// Mechanismen:
//  * hrtimer-Interrupt beschreibt die Liste
//  * /sys-FS messwert: zum Schreiben in die Liste
//  * /sys-FS zaehler: zum Ausgeben der Liste und Löschen von Einträgen
//  * /sys-FS cycle: zum Einstellen der Cycle-Zeit von hrtimer
//

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/rtmutex.h>


struct rcul_messwert
{
	struct list_head list;
	long zaehler;
	ktime_t zeit;
	long wert;
};

static struct rcul_messwert *rcul_mw;
static struct rt_mutex rcul_list_lock;

static LIST_HEAD (rcul_list_head);

static struct hrtimer rcul_timer;

static unsigned int rcul_zaehler = 0;

static dev_t rcul_dev;

// Referenzen fuer /sys-FS
static struct class* rcul_class;
static struct device* rcul_device;

static unsigned long long rcul_cycle = 1000ULL;


static void rcul_add_messwert (long wert)
{
	struct rcul_messwert *mw;

	rcul_zaehler++;

	// besser: Slab-Allocator für Listenelemente
	// To-Do: Pruefung auf NULL-Pointer
	mw = (struct rcul_messwert*) kmalloc (sizeof(*rcul_mw), GFP_ATOMIC);
	mw->zaehler = rcul_zaehler;
	mw->zeit = ktime_get();
	mw->wert = wert;
	INIT_LIST_HEAD (&mw->list);

	list_add_tail (&mw->list, &rcul_list_head);

}


static ssize_t cycle_show (struct device* dev, struct device_attribute* attr, char* buf)
{
	sprintf (buf, "%llu\n", rcul_cycle);
	printk (KERN_INFO __FILE__ ": cycle: %s\n", buf);
	return strlen(buf);
}

ssize_t cycle_store (struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	rcul_cycle = simple_strtol (buf, NULL, 10);
	printk (KERN_INFO __FILE__ ": cycle: %llu\n", rcul_cycle);
	return count;
}


static ssize_t messwert_show (struct device* dev, struct device_attribute* attr, char* buf)
{
	struct rcul_messwert *mw;
	struct timespec ts;
	char *pos;
	struct rcul_messwert* n;
	unsigned long flags;

	rt_mutex_lock (&rcul_list_lock);
	local_irq_save (flags);
	pos = buf;
	list_for_each_entry_safe (mw, n, &rcul_list_head, list)
	{
		ts = ktime_to_timespec (mw->zeit);
		sprintf (pos, "%6ld %6lu.%09lu %10ld\n", mw->zaehler, ts.tv_sec, ts.tv_nsec, mw->wert);
		if ((pos - buf) > (PAGE_SIZE - 80))
		{
			local_irq_restore (flags);
			rt_mutex_unlock (&rcul_list_lock);
			return strlen(buf);
		}
		pos = buf + strlen(buf);
	}

	local_irq_restore (flags);
	rt_mutex_unlock (&rcul_list_lock);

	return strlen(buf);
}


static ssize_t messwert_store (struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	int wert;

	wert = simple_strtol (buf, NULL, 10);

	rcul_add_messwert (wert);

	return count;
}


static ssize_t delete_store (struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	int anzahl;
	int i;
	struct rcul_messwert *mw;
	unsigned long flags;

	anzahl = simple_strtol (buf, NULL, 10);

	rt_mutex_lock (&rcul_list_lock);
	local_irq_save (flags);

	for (i = 0; i < anzahl; i++)
	{
		if (list_empty(&rcul_list_head))
		{
			local_irq_restore (flags);
			rt_mutex_unlock (&rcul_list_lock);
			return count;
		}
		mw = list_first_entry (&rcul_list_head, struct rcul_messwert, list);
		list_del (&mw->list);
		kfree (mw);
	}

	local_irq_restore (flags);
	rt_mutex_unlock (&rcul_list_lock);

	return count;
}

static DEVICE_ATTR (cycle_ms, 0666, cycle_show, cycle_store);
static DEVICE_ATTR (delete, 0222, NULL, delete_store);
static DEVICE_ATTR (messwert, 0666, messwert_show, messwert_store);


static enum hrtimer_restart rcul_timer_fn (struct hrtimer *hrtimer)
{
	// Check, ob Callback-Fkt wirklich im HW-ISR aufgerufen wird
	printk_once (KERN_INFO "irq: %lu softirq: %lu nmi: %lu preempt: %08X\n", in_irq(), in_softirq(), in_nmi(), preempt_count());

	rcul_add_messwert (1000000000 + rcul_zaehler);

	hrtimer_forward(hrtimer, hrtimer_get_expires(hrtimer), ns_to_ktime(rcul_cycle * 1000000ULL));

	return HRTIMER_RESTART;
}


static int rcul_timer_start(void)
{
	hrtimer_init(&rcul_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	rcul_timer.function = rcul_timer_fn;
#ifdef CONFIG_PREEMPT_RT
	rcul_timer.irqsafe = 1;
#endif

	hrtimer_start(&rcul_timer, ns_to_ktime(rcul_cycle * 1000000ULL), HRTIMER_MODE_REL);

	return 0;
}


static int __init rcul_init(void)
{
	int ret = 0;

	printk (KERN_INFO "rcul_init()\n");

	rt_mutex_init (&rcul_list_lock);

	rcul_dev = MKDEV(240, 1);

	// Eintrag fuer uevent in /sys
	rcul_class = class_create (THIS_MODULE, "rcul");
	if (IS_ERR(rcul_class))
	{
		printk (KERN_ERR "rcul_init(): class_create(): rcul_class=%li\n", PTR_ERR(rcul_class));
		ret = -3;
		goto out_unalloc_region;
	}

	rcul_device = device_create (rcul_class, NULL, rcul_dev, NULL, "rcul%d", MINOR(rcul_dev));
	if (IS_ERR(rcul_device))
	{
		printk (KERN_ERR "rcul_init(): device_create(): rcul_device=%li\n", PTR_ERR(rcul_device));
		ret = -3;
		goto out_class;
	}

	// To-Do: anständige Fehlerbehandlung
	ret = device_create_file (rcul_device, &dev_attr_messwert);
	if (ret)
		printk(KERN_ERR "rcul_init(): device_create_file(messwert): ret=%d\n", ret);

	ret = device_create_file (rcul_device, &dev_attr_cycle_ms);
	if (ret)
		printk(KERN_ERR "rcul_init(): device_create_file(cycle_ms): ret=%d\n", ret);

	ret = device_create_file (rcul_device, &dev_attr_delete);
	if (ret)
		printk(KERN_ERR "rcul_init(): device_create_file(delete): ret=%d\n", ret);

	rcul_timer_start ();

	return ret;

out_class: 
	class_destroy (rcul_class);

out_unalloc_region:
	unregister_chrdev_region(rcul_dev, 1);

	return ret;
}


static void __exit rcul_exit(void)
{
	hrtimer_cancel (&rcul_timer);

	device_remove_file (rcul_device, &dev_attr_delete);
	device_remove_file (rcul_device, &dev_attr_cycle_ms);
	device_remove_file (rcul_device, &dev_attr_messwert);

	device_destroy (rcul_class, rcul_dev);
	class_destroy (rcul_class);

	printk (KERN_INFO "rcul_exit()\n");
}


module_init(rcul_init);
module_exit(rcul_exit);


MODULE_DESCRIPTION("list_rtmutex --- konkurrierende Listenoperationen mit RT-Mutex gesichert");
MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_LICENSE("Dual BSD/GPL");


