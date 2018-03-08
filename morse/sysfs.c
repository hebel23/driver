
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

DEVICE_ATTR (dot_unit, 0664, dot_unit_show, dot_unit_store);



	// Eintrag fuer uevent in /sys
	morse_class = class_create (THIS_MODULE, MODULE_NAME);
	if (IS_ERR(morse_class))
	{
		printk (KERN_ERR "morse_init(): class_create(): err=%ld\n", PTR_ERR(morse_class));
		ret = -3;
		goto out_cdev_del;
	}

	morse_device = device_create (morse_class, NULL, morse_dev, NULL, "morse%d", MINOR(morse_dev));
	if (IS_ERR(morse_device))
	{
		printk (KERN_ERR "morse_init(): device_create(): err=%ld\n", PTR_ERR(morse_device));
		ret = -4;
		goto out_class_destroy;
	}

	ret = device_create_file (morse_device, &dev_attr_dot_unit);
	if (ret)
	{
		printk(KERN_ERR "morse_init(): device_create_file(dot_unit): ret=%d\n", ret);
		ret = -5;
		goto out_device_destroy;
	}




	device_remove_file (morse_device, &dev_attr_dot_unit);
	device_destroy (morse_class, morse_dev);
	class_destroy (morse_class);

