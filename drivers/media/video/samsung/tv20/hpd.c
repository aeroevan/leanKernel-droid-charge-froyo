/* linux/drivers/media/video/samsung/tv20/hpd.c
 *
 * hpd interface ftn file for Samsung TVOut driver
 *
 * Copyright (c) 2009 Samsung Electronics
 * 	http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/irq.h>
#include <linux/kobject.h>
#include <asm/io.h>
#include <linux/timer.h>
#include <linux/wakelock.h>
#include <mach/gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio-bank.h>
#include <mach/regs-gpio.h>
#include <mach/gpio.h>
#include "s5p_tv.h"
#include "hpd.h"


#ifdef HPDDEBUG
#define HPDIFPRINTK(fmt, args...) \
	printk(KERN_INFO "[HPD_IF] %s: " fmt, __func__ , ## args)
#else
#define HPDIFPRINTK(fmt, args...)
#endif

static struct hpd_struct hpd_struct;
struct wake_lock hpd_wake_lock;
static int last_hpd_state;
atomic_t hdmi_status;
atomic_t poll_state;

bool hpd_sleep_state;

static DECLARE_WORK(hpd_work, (void *)s5p_handle_cable);

enum {
	EVENT_NONE,
	EVENT_RISING,
	EVENT_FALLING,
};
static int irq_event =  EVENT_NONE;

int s5p_hpd_get_state(void)
{
	if (gpio_get_value(S5PV210_GPH1(5))) {
		atomic_set(&hpd_struct.state, 1);
		last_hpd_state = HPD_HI;
		irq_event = EVENT_RISING;
	}
	else {
		atomic_set(&hpd_struct.state, 0);
		last_hpd_state = HPD_LO;
		irq_event = EVENT_FALLING;
	}

	return atomic_read(&hpd_struct.state);
}

int s5p_hpd_set_state(int state)
{
	return atomic_set(&hpd_struct.state,state);
}


int s5p_hpd_open(struct inode *inode, struct file *file)
{
	atomic_set(&poll_state, 1);

	return 0;
}

int s5p_hpd_release(struct inode *inode, struct file *file)
{
	return 0;
}

ssize_t s5p_hpd_read(struct file *file, char __user *buffer, size_t count,
		loff_t *ppos)
{
	ssize_t retval;

	spin_lock_irq(&hpd_struct.lock);

	retval = put_user(atomic_read(&hpd_struct.state),
			(unsigned int __user *) buffer);

	atomic_set(&poll_state, -1);

	spin_unlock_irq(&hpd_struct.lock);

	return retval;
}

unsigned int s5p_hpd_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &hpd_struct.waitq, wait);

	if (atomic_read(&poll_state) != -1)
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations hpd_fops = {
	.owner   = THIS_MODULE,
	.open    = s5p_hpd_open,
	.release = s5p_hpd_release,
	.read    = s5p_hpd_read,
	.poll    = s5p_hpd_poll,
};

static struct miscdevice hpd_misc_device = {
	HPD_MINOR,
	"HPD",
	&hpd_fops,
};

int s5p_hpd_set_hdmiint(void)
{
	/* EINT -> HDMI */

	set_irq_type(IRQ_EINT13, IRQ_TYPE_NONE);

	if (last_hpd_state){
		s5p_hdmi_disable_interrupts(HDMI_IRQ_HPD_UNPLUG);
	}
	else{
		s5p_hdmi_disable_interrupts(HDMI_IRQ_HPD_PLUG);
	}
	atomic_set(&hdmi_status, HDMI_ON);

	s3c_gpio_cfgpin(S5PV210_GPH1(5), S5PV210_GPH1_5_HDMI_HPD);
	s3c_gpio_setpull(S5PV210_GPH1(5), S3C_GPIO_PULL_DOWN);
	writel(readl(S5PV210_GPH1DRV)|0x3<<10, S5PV210_GPH1DRV);

	s5p_hdmi_hpd_gen();

	if (s5p_hdmi_get_hpd_status()){
		s5p_hdmi_enable_interrupts(HDMI_IRQ_HPD_UNPLUG);
	}
	else{
		s5p_hdmi_enable_interrupts(HDMI_IRQ_HPD_PLUG);
		schedule_work(&hpd_work);
		printk("\n++ %d",__LINE__);
	}
	return 0;
}
EXPORT_SYMBOL(s5p_hpd_set_hdmiint);

int s5p_hpd_set_eint(void)
{
	/* HDMI -> EINT */
	atomic_set(&hdmi_status, HDMI_OFF);

	s5p_hdmi_clear_pending(HDMI_IRQ_HPD_PLUG);
	s5p_hdmi_clear_pending(HDMI_IRQ_HPD_UNPLUG);

	s5p_hdmi_disable_interrupts(HDMI_IRQ_HPD_PLUG);
	s5p_hdmi_disable_interrupts(HDMI_IRQ_HPD_UNPLUG);

	s3c_gpio_cfgpin(S5PV210_GPH1(5), S5PV210_GPH1_5_EXT_INT31_5);
	s3c_gpio_setpull(S5PV210_GPH1(5), S3C_GPIO_PULL_DOWN);
	if (s5p_hpd_get_state())
		set_irq_type(IRQ_EINT13, IRQ_TYPE_EDGE_FALLING);
	else
		set_irq_type(IRQ_EINT13, IRQ_TYPE_EDGE_RISING);

	writel(readl(S5PV210_GPH1DRV)|0x3<<10, S5PV210_GPH1DRV);

	printk("\n++ s5p_hpd_set_eint\n"); 
	return 0;
}
EXPORT_SYMBOL(s5p_hpd_set_eint);

int irq_eint(int irq)
{

	int hpd_check;

	//  if(atomic_read(&hpd_struct.state) == HPD_SLEEP)
	if( hpd_sleep_state ==1)
	{
		hpd_check=1;
		wake_lock_timeout(&hpd_wake_lock,3*HZ);
	}
	else
		hpd_check=gpio_get_value(S5PV210_GPH1(5));


	if(hpd_check){

		atomic_set(&hpd_struct.state, HPD_HI);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_HI;
		wake_up_interruptible(&hpd_struct.waitq);
	} else {
		atomic_set(&hpd_struct.state, HPD_LO);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_LO;
		wake_up_interruptible(&hpd_struct.waitq);
	}

	if (atomic_read(&hpd_struct.state))
		set_irq_type(IRQ_EINT13, IRQ_TYPE_EDGE_FALLING);
	else
		set_irq_type(IRQ_EINT13, IRQ_TYPE_EDGE_RISING);

	schedule_work(&hpd_work);

	HPDIFPRINTK("%s\n", atomic_read(&hpd_struct.state) == HPD_HI ?
			"HPD HI" : "HPD LO");

	return IRQ_HANDLED;

}

int irq_hdmi(int irq)
{
	u8 flag;
	int ret = IRQ_HANDLED;

	/* read flag register */
	flag = s5p_hdmi_get_interrupts();

	if (s5p_hdmi_get_hpd_status())
		s5p_hdmi_disable_interrupts(HDMI_IRQ_HPD_UNPLUG);
	else
		s5p_hdmi_disable_interrupts(HDMI_IRQ_HPD_PLUG);

	s5p_hdmi_clear_pending(HDMI_IRQ_HPD_PLUG);
	s5p_hdmi_clear_pending(HDMI_IRQ_HPD_UNPLUG);


	/* is this our interrupt? */

	if (!(flag & (1 << HDMI_IRQ_HPD_PLUG | 1 << HDMI_IRQ_HPD_UNPLUG))) {
		ret = IRQ_NONE;
		goto out;
	}

	if (flag == (1 << HDMI_IRQ_HPD_PLUG | 1 << HDMI_IRQ_HPD_UNPLUG)) {

		HPDIFPRINTK("HPD_HI && HPD_LO\n");

		if (last_hpd_state == HPD_HI && s5p_hdmi_get_hpd_status())
			flag = 1 << HDMI_IRQ_HPD_UNPLUG;
		else
			flag = 1 << HDMI_IRQ_HPD_PLUG;
	}

	if (flag & (1 << HDMI_IRQ_HPD_PLUG)) {

		s5p_hdmi_enable_interrupts(HDMI_IRQ_HPD_UNPLUG);

		atomic_set(&hpd_struct.state, HPD_HI);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_HI;
		wake_up_interruptible(&hpd_struct.waitq);

		s5p_hdcp_encrypt_stop(true);

		HPDIFPRINTK("HPD_HI\n");

	} else if (flag & (1 << HDMI_IRQ_HPD_UNPLUG)) {

		s5p_hdcp_encrypt_stop(false);

		s5p_hdmi_enable_interrupts(HDMI_IRQ_HPD_PLUG);

		atomic_set(&hpd_struct.state, HPD_LO);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_LO;
		wake_up_interruptible(&hpd_struct.waitq);

		HPDIFPRINTK("HPD_LO\n");
	}

	schedule_work(&hpd_work);

out:
	return IRQ_HANDLED;
}

/*
 * HPD interrupt handler
 *
 * Handles interrupt requests from HPD hardware.
 * Handler changes value of internal variable and notifies waiting thread.
 */
/*
   enum {
   EVENT_NONE,
   EVENT_RISING,
   EVENT_FALLING,
   };
 */
//static int irq_event =  EVENT_NONE;

static void hpd_irq_check_timer_func(unsigned long dummy);

static DEFINE_TIMER(hpd_irq_check_timer, hpd_irq_check_timer_func, 0, 0);

irqreturn_t s5p_hpd_irq_handler(int irq)
{
	int ret = IRQ_HANDLED;
	unsigned long flags;
	int hpd_check;

	spin_lock_irqsave(&hpd_struct.lock, flags);

	//    if(atomic_read(&hpd_struct.state) == HPD_SLEEP)
	if( hpd_sleep_state ==1)
	{
		hpd_check=1;
		if (irq_event == EVENT_FALLING) {
			mod_timer(&hpd_irq_check_timer, jiffies + HZ/20);
		}
		irq_event = EVENT_RISING;
		if (atomic_read(&hdmi_status))
			ret = irq_hdmi(irq);
		else
			ret = irq_eint(irq);
		spin_unlock_irqrestore(&hpd_struct.lock, flags);
		return ret;
	}
	else
	{
		hpd_check=gpio_get_value(S5PV210_GPH1(5));
	}

	if(hpd_check){
		if (irq_event == EVENT_FALLING) {
			mod_timer(&hpd_irq_check_timer, jiffies + HZ/20);
		}
		irq_event = EVENT_RISING;
	} else {
		irq_event =  EVENT_FALLING;
		del_timer(&hpd_irq_check_timer);
	}

	/* check HDMI status */
	if (atomic_read(&hdmi_status)) {
		/* HDMI on */
		ret = irq_hdmi(irq);
		HPDIFPRINTK("HDMI HPD interrupt\n");
	}
#if 0 
	else {
		/* HDMI off */
		ret = irq_eint(irq);
		HPDIFPRINTK("EINT HPD interrupt\n");
	}
#endif
	spin_unlock_irqrestore(&hpd_struct.lock, flags);

	return ret;
}

static void hpd_irq_check_timer_func(unsigned long dummy)
{
	unsigned long flags;

	//printk("[TVOUT][%s:called]\n",__func__);
	int hpd_check;
	//  if(atomic_read(&hpd_struct.state) == HPD_SLEEP)

	if( hpd_sleep_state ==1)
	{
		hpd_check=1;
	} else {

		hpd_check=gpio_get_value(S5PV210_GPH1(5));

		if(hpd_check){

			atomic_set(&hpd_struct.state, HPD_HI);
			atomic_set(&poll_state, 1);

			last_hpd_state = HPD_HI;
			wake_up_interruptible(&hpd_struct.waitq);
		} else {
			atomic_set(&hpd_struct.state, HPD_LO);
			atomic_set(&poll_state, 1);

			last_hpd_state = HPD_LO;
			wake_up_interruptible(&hpd_struct.waitq);
		}

		if (atomic_read(&hpd_struct.state))
			set_irq_type(IRQ_EINT13, IRQ_TYPE_EDGE_FALLING);
		else
			set_irq_type(IRQ_EINT13, IRQ_TYPE_EDGE_RISING);

		schedule_work(&hpd_work);

		HPDIFPRINTK("%s\n", atomic_read(&hpd_struct.state) == HPD_HI ?
				"HPD HI" : "HPD LO");
	}

	if (irq_event ==EVENT_RISING && !hpd_check/*gpio_get_value(S5PV210_GPH1(5))*/) {
		printk("[TVOUT][%s:re-call irq handler]\n",__func__);
		//local_irq_save(flags);
		s5p_hpd_irq_handler(IRQ_EINT13);
		//local_irq_restore(flags);
	}
}

static int __init s5p_hpd_probe(struct platform_device *pdev)
{
	if (misc_register(&hpd_misc_device)) {
		printk(KERN_WARNING " Couldn't register device 10, %d.\n",
				HPD_MINOR);
		return -EBUSY;
	}

	init_waitqueue_head(&hpd_struct.waitq);

	spin_lock_init(&hpd_struct.lock);

	atomic_set(&hpd_struct.state, -1);

	atomic_set(&hdmi_status, HDMI_OFF);

	s3c_gpio_cfgpin(S5PV210_GPH1(5), S5PV210_GPH1_5_EXT_INT31_5);
	s3c_gpio_setpull(S5PV210_GPH1(5), S3C_GPIO_PULL_DOWN);
	writel(readl(S5PV210_GPH1DRV)|0x3<<10, S5PV210_GPH1DRV);

	if (gpio_get_value(S5PV210_GPH1(5))) {
		atomic_set(&hpd_struct.state, HPD_HI);
		last_hpd_state = HPD_HI;
		irq_event = EVENT_RISING;
	} else {
		atomic_set(&hpd_struct.state, HPD_LO);
		last_hpd_state = HPD_LO;
		irq_event = EVENT_FALLING;
	}

	set_irq_type(IRQ_EINT13, IRQ_TYPE_EDGE_BOTH);

	if (request_irq(IRQ_EINT13, s5p_hpd_irq_handler, IRQF_DISABLED,
				"hpd", s5p_hpd_irq_handler)) {
		printk(KERN_ERR  "failed to install %s irq\n", "hpd");
		misc_deregister(&hpd_misc_device);
		return -EIO;
	}
	hpd_sleep_state = 0;
	s5p_hdmi_register_isr(s5p_hpd_irq_handler, (u8)HDMI_IRQ_HPD_PLUG);
	s5p_hdmi_register_isr(s5p_hpd_irq_handler, (u8)HDMI_IRQ_HPD_UNPLUG);

	return 0;
}

/*
 *  Remove
 */
static int s5p_hpd_remove(struct platform_device *pdev)
{
	return 0;
}


#ifdef CONFIG_PM
/*
 *  Suspend
 */
int s5p_hpd_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

/*
 *  Resume
 */
int s5p_hpd_resume(struct platform_device *dev)
{
	return 0;
}
#else
#define s5p_hpd_suspend NULL
#define s5p_hpd_resume NULL
#endif

static struct platform_driver s5p_hpd_driver = {
	.probe		= s5p_hpd_probe,
	.remove		= s5p_hpd_remove,
	.suspend	= s5p_hpd_suspend,
	.resume		= s5p_hpd_resume,
	.driver		= {
		.name	= "s5p-hpd",
		.owner	= THIS_MODULE,
	},
};

static char banner[] __initdata =
"S5PC11X HPD Driver, (c) 2009 Samsung Electronics\n";

int __init s5p_hpd_init(void)
{
	int ret;
#if defined (CONFIG_TARGET_LOCALE_EUR) || defined (CONFIG_TARGET_LOCALE_HKTW) || defined (CONFIG_TARGET_LOCALE_HKTW_FET) || defined (CONFIG_TARGET_LOCALE_VZW) || defined (CONFIG_TARGET_LOCALE_USAGSM)
	if(HWREV < 0x8)
		return -1;
#endif	
	wake_lock_init(&hpd_wake_lock,WAKE_LOCK_SUSPEND,"HPD-STATUS");
	printk(banner);

	ret = platform_driver_register(&s5p_hpd_driver);

	if (ret) {
		printk(KERN_ERR "Platform Device Register Failed %d\n", ret);
		return -1;
	}

	return 0;
}

static void __exit s5p_hpd_exit(void)
{
	wake_lock_destroy(&hpd_wake_lock);
	misc_deregister(&hpd_misc_device);
}

module_init(s5p_hpd_init);
module_exit(s5p_hpd_exit);


