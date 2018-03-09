// hrt.c
//
// Mechanismen:
//   - hrtimer-Callback-Funktion
//   - konstante Anzahl an Abtastungen pro Zeiteinheit
//   - Aufzeichnung vom Jitter der Callback-Funktion
//   - Trace-Events im ftrace
//

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/hrtimer.h>

#define CREATE_TRACE_POINTS
#include "jitter.h"



#define HRT_PERIOD_MS   1000000UL

static unsigned long nPeriodMS = 200UL;

static unsigned long nSum;
static unsigned long nSample;
static unsigned long nMin;
static unsigned long nMax;

static struct hrtimer hrt_timer_sft;


static dev_t hrt_dev;
static struct cdev hrt_cdev;


static enum hrtimer_restart hrt_timer_fn (struct hrtimer *hrtimer)
{
	struct timespec ts1;
	struct timespec ts0;
	struct timespec tsDiff;
	ktime_t diff;
	ktime_t ktime1;

	ktime1 = ktime_get();

	diff = ktime_sub (ktime1, hrtimer_get_expires(hrtimer));
	tsDiff = ktime_to_timespec (diff);

	nSum += tsDiff.tv_nsec;
	if (tsDiff.tv_nsec < nMin)
		nMin = tsDiff.tv_nsec;
	if (tsDiff.tv_nsec > nMax)
		nMax = tsDiff.tv_nsec;
	nSample++;

	ts1 = ktime_to_timespec (ktime1);
	ts0 = ktime_to_timespec (hrtimer_get_expires(hrtimer));

	trace_jitter ("evt", 
			(unsigned long int)(nMax - nMin), 
			(unsigned long int)tsDiff.tv_nsec, 
			(unsigned long int)(nSum / nSample), 
			nMin, nMax);

	hrtimer_forward(hrtimer, hrtimer_get_expires(hrtimer), ns_to_ktime(HRT_PERIOD_MS * nPeriodMS));

	return HRTIMER_RESTART;
}


static int hrt_start(void)
{
	nSum = 0;
	nSample = 0;
	nMin = 999999999;
	nMax = 0;

	hrt_timer_sft.function = hrt_timer_fn;
	// hrt_timer_sft.cb_mode = HRTIMER_CB_SOFTIRQ;
	hrt_timer_sft.irqsafe = 1;
	hrtimer_start(&hrt_timer_sft, 
			ns_to_ktime(HRT_PERIOD_MS * nPeriodMS), HRTIMER_MODE_REL);

	return 0;
}


static int hrt_stop(void)
{
	hrtimer_cancel (&hrt_timer_sft);

	return 0;
}


// Generierung von ioctl-Nummern nicht korrekt
// siehe  asm/ioctl.h
//
static ssize_t hrt_ioctl (struct inode *node, struct file *file, uint cmd, ulong arg) 
{
	switch (cmd)
	{
		case 21:

			hrt_start();
			break;

		case 22:

			hrt_stop();
			break;

		case 29:

			nPeriodMS = arg;
			break;

		default:
			return -EINVAL;
	}

	return 0;
}


static struct file_operations hrt_fops = {
	.owner   = THIS_MODULE,
	.ioctl   = hrt_ioctl,
};



static int __init hrt_init(void)
{
	int ret = 0;

	printk (KERN_INFO "hrt_init()\n");

	hrt_dev = MKDEV(241, 0);
	if ((ret = register_chrdev_region(hrt_dev, 1, "hrt")) < 0) 
	{
		printk(KERN_ERR "hrt_init(): register_chrdev_region(): ret=%d\n", ret);
		goto out;
	}

	cdev_init(&hrt_cdev, &hrt_fops);
	if ((ret = cdev_add(&hrt_cdev, hrt_dev, 1)) < 0) 
	{
		printk(KERN_ERR "hrt_init() - cdev_init(): could not allocate chrdev; ret=%d\n", ret);
		goto out_unalloc_region;
	}

	hrtimer_init(&hrt_timer_sft, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	return ret;

out_unalloc_region:
	unregister_chrdev_region(hrt_dev, 1);
out:
	return ret;
}


static void __exit hrt_exit(void)
{
	hrt_stop();

	cdev_del(&hrt_cdev);
	unregister_chrdev_region(hrt_dev, 1);

	printk (KERN_INFO "hrt_exit()\n");
}


module_init(hrt_init);
module_exit(hrt_exit);


MODULE_DESCRIPTION("hrtimer mit jitter-Berechnung und Ausgabe im ftrace");
MODULE_LICENSE("Dual BSD/GPL");



