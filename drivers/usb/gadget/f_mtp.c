/*
 * drivers/usb/gadget/f_mtp.c
 *
 * Function Driver for USB MTP,
 * mtpg.c -- MTP Driver, for MTP development,
 *
 * Copyright (C) 2009 by Samsung Electronics,
 * Author: Deepak and Madhukar.
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
 */

/*
 * f_mtp.c file is the driver for MTP device. Totally three
 * EndPoints will be configured in which 2 Bulk End Points
 * and 1 Interrupt End point. This driver will also register as
 * misc driver and exposes file operation funtions to user space.
 */

/* Includes */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>

#include <linux/sched.h>
#include <asm-generic/siginfo.h>

#include "f_mtp.h"
#include "gadget_chips.h"

/*-------------------------------------------------------------------------*/
/*Only for Debug*/
#define DEBUG_MTP 0
//#define EP_MTP_REALLOC
#define EP_INT_MTP_REALLOC

#if DEBUG_MTP
#define DEBUG_MTP_SETUP
#define DEBUG_MTP_READ
#define DEBUG_MTP_WRITE

#else
#undef DEBUG_MTP_SETUP
#undef DEBUG_MTP_READ
#undef DEBUG_MTP_WRITE
#endif


//#define DEBUG_MTP_SETUP
//#define DEBUG_MTP_READ
//#define DEBUG_MTP_WRITE


#ifdef DEBUG_MTP_SETUP
#define DEBUG_MTPB(fmt,args...) printk(fmt, ##args)
#else
#define DEBUG_MTPB(fmt,args...) do {} while(0)
#endif

#ifdef DEBUG_MTP_READ
#define DEBUG_MTPR(fmt,args...) printk(fmt, ##args)
#else
#define DEBUG_MTPR(fmt,args...) do {} while(0)
#endif

#ifdef DEBUG_MTP_WRITE
#define DEBUG_MTPW(fmt,args...) printk(fmt, ##args)
#else
#define DEBUG_MTPW(fmt,args...) do {} while(0)
#endif
/*-------------------------------------------------------------------------*/

#define BULK_BUFFER_SIZE	 4096

/* number of rx and tx requests to allocate */
#define RX_REQ_MAX		 4
#define TX_REQ_MAX		 4

#define DRIVER_NAME		 "usb_mtp_gadget"

static const char longname[] = 	"Gadget_MTP";
static const char shortname[] = DRIVER_NAME;
static int mtp_pid; 
typedef enum {
mtp_disable_desc = 0, //0
mtp_enable_desc	  //1
};

/* MTP Device Structure*/
struct mtpg_dev {
	struct usb_function function;
	struct usb_composite_dev *cdev;
	struct usb_gadget *gadget;

	spinlock_t		lock;

	u8			config;
	int			online;
	int 			error;
	struct list_head 	tx_idle;
	struct list_head 	rx_idle;
	struct list_head 	rx_done;
	wait_queue_head_t 	read_wq;
	wait_queue_head_t 	write_wq;

	struct usb_request 	*read_req;
	unsigned char 		*read_buf;
	unsigned 		read_count;

	struct usb_ep		*bulk_in;
	struct usb_ep		*bulk_out;
	struct usb_ep           *int_in;
	struct usb_request	*notify_req;

	atomic_t 		read_excl;
	atomic_t 		write_excl;
	atomic_t 		ioctl_excl;
	atomic_t 		open_excl;
	atomic_t 		wintfd_excl;
	char cancel_io_buf[USB_PTPREQUEST_CANCELIO_SIZE+1]; 

};

/* Global mtpg_dev Structure
* the_mtpg variable be used between mtpg_open() and mtpg_function_bind() */
static struct mtpg_dev    *the_mtpg; 

/* Three full-speed and high-speed endpoint descriptors: bulk-in, bulk-out,
 * and interrupt-in. */

#define INT_MAX_PACKET_SIZE 10

//static struct usb_interface_descriptor mtpg_interface_desc = {
struct usb_interface_descriptor mtpg_interface_desc = {
	.bLength =		sizeof mtpg_interface_desc,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	3,
	.bInterfaceClass =	USB_CLASS_STILL_IMAGE,
	.bInterfaceSubClass =	01,
	.bInterfaceProtocol =	01,
};

static struct usb_endpoint_descriptor fs_mtpg_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

static struct usb_endpoint_descriptor fs_mtpg_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

static struct usb_endpoint_descriptor int_fs_notify_desc = {
        .bLength =              USB_DT_ENDPOINT_SIZE,
        .bDescriptorType =      USB_DT_ENDPOINT,
        .bEndpointAddress =     USB_DIR_IN,
        .bmAttributes =         USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize =       __constant_cpu_to_le16(64),
        .bInterval =            0x04,
};

static struct usb_descriptor_header *fs_mtpg_desc[] = {
	(struct usb_descriptor_header *) &mtpg_interface_desc,
	(struct usb_descriptor_header *) &fs_mtpg_in_desc,
	(struct usb_descriptor_header *) &fs_mtpg_out_desc,
	(struct usb_descriptor_header *) &int_fs_notify_desc,
	NULL,
};

static struct usb_endpoint_descriptor hs_mtpg_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	/* bEndpointAddress copied from fs_mtpg_in_desc during mtpg_function_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_mtpg_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	/* bEndpointAddress copied from fs_mtpg_out_desc during mtpg_function_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
	.bInterval =		1,	/* NAK every 1 uframe */
};

static struct usb_endpoint_descriptor int_hs_notify_desc = {
        .bLength =              USB_DT_ENDPOINT_SIZE,
        .bDescriptorType =      USB_DT_ENDPOINT,
        .bEndpointAddress =     USB_DIR_IN,
        .bmAttributes =         USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize =       __constant_cpu_to_le16(64),
        .bInterval =            INT_MAX_PACKET_SIZE + 4,
};

static struct usb_descriptor_header *hs_mtpg_desc[] = {
	(struct usb_descriptor_header *) &mtpg_interface_desc,
	(struct usb_descriptor_header *) &hs_mtpg_in_desc,
	(struct usb_descriptor_header *) &hs_mtpg_out_desc,
	(struct usb_descriptor_header *) &int_hs_notify_desc,
	NULL
};

/* string IDs are assigned dynamically */
#define F_MTP_IDX			0
#define STRING_PRODUCT_IDX		1
#define STRING_SERIAL_IDX		2

/* default serial number takes at least two packets */
static char serial[] = "0123456789.0123456789.0123456789";

static struct usb_string strings_dev_mtp[] = {
	[F_MTP_IDX].s = "Android MTP",
	[STRING_PRODUCT_IDX].s = longname,
	[STRING_SERIAL_IDX].s = serial,
	{  },			/* end of list */
};

static struct usb_gadget_strings stringtab_mtp = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev_mtp,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_mtp,
	NULL,
};

/* used when mtp function is disabled */
static struct usb_descriptor_header *null_mtpg_descs[] = {
	NULL,
};

/* -------------------------------------------------------------------------
 *	Main Functionalities Start!
 * ------------------------------------------------------------------------- */

static inline struct mtpg_dev *func_to_dev(struct usb_function *f)
{
	return container_of(f, struct mtpg_dev, function);
}

static inline int _lock(atomic_t *excl)
{

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);

	if (atomic_inc_return(excl) == 1) {
		return 0;
	} else {
		atomic_dec(excl);
		return -1;
	}
}

static inline void _unlock(atomic_t *excl)
{
	atomic_dec(excl);
}

/* add a request to the tail of a list */
static void inline req_put(struct mtpg_dev *dev, struct list_head *head,
		struct usb_request *req)
{
	unsigned long flags;

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);

	spin_lock_irqsave(&dev->lock, flags);
	list_add_tail(&req->list, head);
	spin_unlock_irqrestore(&dev->lock, flags);
}

/* remove a request from the head of a list */
static struct usb_request *req_get(struct mtpg_dev *dev, struct list_head *head)
{
	unsigned long flags;
	struct usb_request *req;

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);

	spin_lock_irqsave(&dev->lock, flags);
	if (list_empty(head)) {
		req = 0;
	} 
	else {
		req = list_first_entry(head, struct usb_request, list);
		list_del(&req->list);
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	return req;
}

static int mtp_send_signal(int value)
{
	int ret;	
	struct siginfo info;
	struct task_struct *t;
	memset(&info, 0, sizeof(struct siginfo));
	info.si_signo = SIG_SETUP;
	info.si_code = SI_QUEUE;
	info.si_int = value;
	rcu_read_lock();
	t = find_task_by_vpid(mtp_pid);
	if(t == NULL){
		printk("no such pid\n");
		rcu_read_unlock();
		return -ENODEV;
	}

	rcu_read_unlock();
	ret = send_sig_info(SIG_SETUP, &info, t);    //send the signal
	if (ret < 0) {
		printk("error sending signal !!!!!!!!\n");
		return ret;
	}
	return 0;

}

static int mtpg_open(struct inode *ip, struct file *fp)
{
	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);

	if (_lock(&the_mtpg->open_excl)){
		printk("mtpg_open fn -- mtpg device busy\n");
		return -EBUSY;
	}

	fp->private_data = the_mtpg;

	/* clear the error latch */

	DEBUG_MTPB("[%s] mtpg_open and clearing the error = 0 \n", __func__);

	the_mtpg->error = 0;

	return 0;
}

static ssize_t mtpg_read(struct file *fp, char __user *buf,
				size_t count, loff_t *pos)
{
	struct mtpg_dev *dev = fp->private_data;
	struct usb_request *req;
	int r = count, xfer;
	int ret;

	DEBUG_MTPR("*******[%s] and count = (%d)\n",__func__, count);

	if (_lock(&dev->read_excl)){
		printk("mtpg_read fn -- mtpg device busy\n");
		return -EBUSY;
	}

	while (!(dev->online || dev->error)) {
		DEBUG_MTPR("******[%s] and line is = %d \n", __FUNCTION__,__LINE__);
		ret = wait_event_interruptible(dev->read_wq,
				(dev->online || dev->error));
		if (ret < 0) {
			_unlock(&dev->read_excl);
			printk("-------line is = %d,mtp_read ret<0 \n",__LINE__);
			return ret;
		}
	}

	while (count > 0) {
		DEBUG_MTPR("*********[%s] and line is = %d \n", __FUNCTION__,__LINE__);

		if (dev->error) {
			r = -EIO;
			printk("*******[%s]\t%d: dev->error so break r=%d\n",__FUNCTION__,__LINE__,r);
			break;
		}

		/* if we have idle read requests, get them queued */
		DEBUG_MTPR("*********[%s]\t%d: get request \n", __FUNCTION__,__LINE__);
		while ((req = req_get(dev, &dev->rx_idle))) {
requeue_req:
			req->length = BULK_BUFFER_SIZE;
			DEBUG_MTPR("[%s]\t%d: ---------- usb-ep-queue \n", __FUNCTION__,__LINE__);
			ret = usb_ep_queue(dev->bulk_out, req, GFP_ATOMIC);

			DEBUG_MTPR("*********** [%s]\t%d: Endpoint : %s \n",__func__,__LINE__, dev->bulk_out->name);

			if (ret < 0) {
				r = -EIO;
				dev->error = 1;
				req_put(dev, &dev->rx_idle, req);
				printk("*****[%s] \t line %d, RETURN ERROR r = %d !!!!!!!!! \n", __FUNCTION__,__LINE__,r);
				goto fail;
			} else {
				DEBUG_MTPR("********* [%s] rx req queue %p\n",__FUNCTION__, req);
			}
		}
		
		DEBUG_MTPR("*******[%s]\t%d: read_count = %d\n", __FUNCTION__,__LINE__, dev->read_count);

		/* if we have data pending, give it to userspace */
		if (dev->read_count > 0) {
			DEBUG_MTPR("*******[%s]\t%d: read_count = %d\n", __FUNCTION__,__LINE__, dev->read_count);
			if (dev->read_count < count) {
				xfer = dev->read_count;
			}
			else {
				xfer = count;
			}

			DEBUG_MTPR("*****[%s] copy_to_user : 0x%X bytes on endpoint %X\n",__FUNCTION__, dev->read_count, dev->bulk_out);

			if (copy_to_user(buf, dev->read_buf, xfer)) {
				r = -EFAULT;
				DEBUG_MTPR("*****[%s]\t%d: copy-to-user failed so RET r = %d!!!!!!!\n",__FUNCTION__,__LINE__,r);
				break;
			}

			dev->read_buf += xfer;
			dev->read_count -= xfer;
			buf += xfer;
			count -= xfer;

			/* if we've emptied the buffer, release the request */
			if (dev->read_count == 0) {
				DEBUG_MTPR("******[%s] and line is = %d \n", __FUNCTION__,__LINE__);
				req_put(dev, &dev->rx_idle, dev->read_req);
				dev->read_req = 0;
			}

			/* Updating the buffer size and returnung from mtpg_read */
			r = xfer;
			DEBUG_MTPR("***** [%s] \t %d: returning lenght %d\n", __FUNCTION__,__LINE__,r);
			goto fail;
		}

		/* wait for a request to complete */
		req = 0;
		DEBUG_MTPR("*******[%s] and line is = %d \n", __FUNCTION__,__LINE__);

		ret = wait_event_interruptible(dev->read_wq, ((req = req_get(dev, &dev->rx_done)) || dev->error));

		DEBUG_MTPR("*******[%s]\t%d: dev->error %d and req = %p \n", __FUNCTION__,__LINE__,dev->error, req);

		if (req != 0) {
			/* if we got a 0-len one we need to put it back into
			** service.  if we made it the current read req we'd
			** be stuck forever
			*/
			if (req->actual == 0)
				goto requeue_req;

			dev->read_req = req;
			dev->read_count = req->actual;
			dev->read_buf = req->buf;

			DEBUG_MTPR("******[%s]\t%d: rx_req=%p req->actual=%d \n",__FUNCTION__,__LINE__, req, req->actual);
		}

		if (ret < 0) {
			r = ret;
			DEBUG_MTPR("***** [%s]\t%d after ret=%d so break return = %d\n",__FUNCTION__,__LINE__, ret, r);
			break;
		}
	}

fail:
	_unlock(&dev->read_excl);

	DEBUG_MTPR("******[%s]\t%d: RETURNING Bact to USpace r=%d + + + +  + + + %d\n",__FUNCTION__,__LINE__,r);
	return r;

}

static ssize_t mtpg_write(struct file *fp, const char __user *buf,
				 size_t count, loff_t *pos)
{
	struct mtpg_dev *dev = fp->private_data;
	struct usb_request *req = 0;
	int r = count, xfer;
	int ret;


	DEBUG_MTPW("[%s] \t%d ep bulk_out name = %s\n", __FUNCTION__,__LINE__ ,dev->bulk_out->name);

	if (_lock(&dev->write_excl)) {
		DEBUG_MTPW("USB/Driver/f_mtp.c mtpg_write dev->write_exel _lock\n");
		return -EBUSY;
		}

	while (count > 0) {
		if (dev->error) {
			r = -EIO;
			printk("***** [%s]\t%d count > 0 but dev->error so break !!!!!!\n",__FUNCTION__, __LINE__);
			break;
		}

		/* get an idle tx request to use */
		req = 0;

		ret = wait_event_interruptible(dev->write_wq, ((req = req_get(dev, &dev->tx_idle)) || dev->error));

		DEBUG_MTPW("[%s]\t%d: count : %d, dev->error = %d\n",__FUNCTION__, __LINE__, count, dev->error);

		if (ret < 0) {
			r = ret;
			printk("***** [%s]\t%d ret = %d !!!!!!\n",__FUNCTION__, __LINE__,r);
			break;
		}

		if (req != 0) {
			if (count > BULK_BUFFER_SIZE) {
				xfer = BULK_BUFFER_SIZE;
			}
			else{
				xfer = count;
			}

			DEBUG_MTPW("***** [%s]\t%d copy_from_user length %d \n",__FUNCTION__, __LINE__,xfer);

			if (copy_from_user(req->buf, buf, xfer)) {
				r = -EFAULT;
				break;
			}

			req->length = xfer;
			ret = usb_ep_queue(dev->bulk_in, req, GFP_ATOMIC);
			if (ret < 0) {
				DEBUG_MTPW("********** mtpg_write after ep_queue ret < 0 brk\n");
				dev->error = 1;
				r = -EIO;
				DEBUG_MTPW("***** [%s]\t%d after ep_queue ret=%d so break return = %d\n",__FUNCTION__,__LINE__, ret, r);
				break;
			}

			buf += xfer;
			count -= xfer;

			/* zero this so we don't try to free it on error exit */
			req = 0;
			}
	}

	if (req){
		DEBUG_MTPW("[%s] \t%d  req_put \n", __FUNCTION__,__LINE__ );
		req_put(dev, &dev->tx_idle, req);
	}

	_unlock(&dev->write_excl);
	
	DEBUG_MTPW("[%s]\t%d  RETURN back to USpace r=%d + + + + + + + + + + \n", __FUNCTION__,__LINE__,r );
	return r;
}

/*Fixme for Interrupt Transfer*/
static void interrupt_complete(struct usb_ep *ep, struct usb_request *req )
{
	printk("******** Finished Writing Interrupt Data \n");
}

static ssize_t interrupt_write(struct file *fd, const char __user *buf, size_t count)
{
	struct mtpg_dev *dev = fd->private_data;
	struct usb_request *req = 0;
	int  ret;

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);
	req = dev->notify_req;

	if( !req )
		printk("Alloc has failed \n");

	if(_lock(&dev->wintfd_excl)) {
		printk("write failed on interrupt endpoint \n");
		return -EBUSY;
	}

	if(copy_from_user(req->buf, buf, count)) {
		printk("copy from user has failed\n");
		return -EIO;
	}
	
	req->length = count;
	req->complete = interrupt_complete;

	ret = usb_ep_queue(dev->int_in, req, GFP_ATOMIC);

	if( ret < 0 )
		return -EIO;

	_unlock(&dev->wintfd_excl);
	return ret;
}

/*Fixme for enabling and disabling the MTP*/
static long  mtpg_ioctl(struct file *fd, unsigned int code, unsigned long arg)
{

	struct mtpg_dev		*dev = fd->private_data;
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request	*req = cdev->req;
	int status = 0; 
	int size = 0;
	int ret_value = 0;
	int max_pkt = 0;

	char *buf_ptr = NULL;
	char buf[USB_PTPREQUEST_GETSTATUS_SIZE+1] = {0};

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);

	switch (code) {
		case MTP_ACM_ENABLE:
			DEBUG_MTPB("[%s]\tline[%d] MTP_ACM_ENABLE \n", __func__,__LINE__);
//			mtp_enable();
			break;

		case MTP_ONLY_ENABLE:
			printk("[%s:%d] calling mtp_function_enable with %d \n",__func__,__LINE__,mtp_enable_desc);
			mtp_function_enable(mtp_enable_desc);

			DEBUG_MTPB("[%s] \tline [%d] MTP_ONLY_ENABLE \n", __func__,__LINE__);

			if (dev->cdev && dev->cdev->gadget )
			{			
				printk("[%s] B4 disconnecting gadget\tline = [%d] \n", __func__,__LINE__);
				usb_gadget_disconnect(dev->cdev->gadget);
				printk("[%s] \tline = [%d] calling usb_gadget_connect after msleep of 5 \n", __func__,__LINE__);
				msleep(5);
				usb_gadget_connect(dev->cdev->gadget);
			}
			status = 10;
			printk("[%s]  [%d] MTP_ONLY_ENABLE ioctl and clearing the error = 0 \n", __func__,__LINE__);
			the_mtpg->error = 0;
			break;

		case MTP_DISABLE:
			DEBUG_MTPB("[%s] \tline [%d] MTP_DISABLE \n", __func__,__LINE__);
			//mtp_function_enable(mtp_disable_desc);

			if (dev->cdev && dev->cdev->gadget )
			{			
				usb_gadget_disconnect(dev->cdev->gadget);
				msleep(5);
				usb_gadget_connect(dev->cdev->gadget);
			}

			break;

		case MTP_CLEAR_HALT:
			status = usb_ep_clear_halt (dev->bulk_in);
			status = usb_ep_clear_halt (dev->bulk_out);
			break;

		case MTP_WRITE_INT_DATA:
			DEBUG_MTPB("[%s] \t %d MTP intrpt_Write \n",__func__,__LINE__);
			ret_value = interrupt_write(fd, (const char *)arg, MTP_MAX_PACKET_LEN_FROM_APP );
			if(ret_value < 0){
				printk("[%s] \t %d interrupt-fd failed \n", __func__,__LINE__);
				status = -EIO;
			}
			else {
				printk("[%s] \t %d interrupt fd success \n", __func__,__LINE__);
				status = MTP_MAX_PACKET_LEN_FROM_APP;
			}
			break;
			
		case SET_MTP_USER_PID:
			mtp_pid = arg;
			DEBUG_MTPB("[%s] SET_MTP_USER_PID; pid = %d \tline = [%d] \n", __func__,mtp_pid,__LINE__); 
			break;
		case GET_SETUP_DATA:
			buf_ptr = (char *)arg;
			if (copy_to_user(buf_ptr, dev->cancel_io_buf, USB_PTPREQUEST_CANCELIO_SIZE)) {
                status = -EFAULT;
                printk("[%s:%d] Error in copy_to_user\n", __func__, __LINE__);
            }
			DEBUG_MTPB("[%s] GET_SETUP_DATA\tline = [%d] \n", __func__,__LINE__); 
			break;
		case SET_SETUP_DATA:
			buf_ptr = (char *)arg;
			if (copy_from_user(buf, buf_ptr, USB_PTPREQUEST_GETSTATUS_SIZE)) {
                status = -EFAULT;
                printk("[%s:%d] Error in copy_from_user\n", __func__, __LINE__);
            }
            else {
                size = buf[0];
                DEBUG_MTPB("[%s] SET_SETUP_DATA; data size = %d\tline = [%d] \n", __func__,size,__LINE__); 
                memcpy(req->buf, buf, size);
                req->zero = 0;
                req->length = size;
                status = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
                if (status < 0)
					DEBUG_MTPB("[%s] Error at usb_ep_queue\tline = [%d] \n", __func__,__LINE__); 
            }
			break;
		case SET_ZLP_DATA:
			req->zero = 1;
			req->length = 0;
			printk("[%s] SEND_ZLP_DATA and usb_ep_queu 0 data size = %d\tline = [%d] \n", __func__,size,__LINE__);
			status = usb_ep_queue(dev->bulk_in, req, GFP_ATOMIC);
			if (status < 0) {
				printk("[%s] Error at usb_ep_queue\tline = [%d] \n", __func__,__LINE__); 
			} else {
				printk("[%s] usb_ep_queue passed and status = %d\tline = [%d] \n", __func__,__LINE__,status); 
				status =20;
			}
			break;
		case GET_HIGH_FULL_SPEED:
			printk("[%s] GET_HIGH_FULL_SPEED and \tline = [%d] \n", __func__,__LINE__);
			max_pkt = dev->bulk_in->maxpacket;
			printk("[%s]  line = %d max_pkt = [%d] \n", __func__,__LINE__, max_pkt); 
			if(max_pkt == 64)
				status = 64;
			else
				status =512;
			break;
			
		default:
			status = -ENOTTY;
	}

	DEBUG_MTPB("[%s] \tline = [%d] ioctl code %d\n", __func__,__LINE__,code);

	return status;
}

static int mtpg_release_device(struct inode *ip, struct file *fp)
{
	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);

	if(the_mtpg != NULL)
		_unlock(&the_mtpg->open_excl);

	return 0;
}

/* file operations for MTP device /dev/usb_mtp_gadget */
static struct file_operations mtpg_fops = {
	.owner   = THIS_MODULE,
	.read    = mtpg_read,
	.write   = mtpg_write,
	.open    = mtpg_open,
	.unlocked_ioctl = mtpg_ioctl,
	.release = mtpg_release_device,
};

static struct miscdevice mtpg_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = shortname,
	.fops = &mtpg_fops,
};

struct usb_request *alloc_ep_req(struct usb_ep *ep, unsigned len, gfp_t kmalloc_flags) 
{
	struct usb_request	*req;

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);

	req = usb_ep_alloc_request(ep, GFP_ATOMIC);
	if (req) {
		req->length = len;
		req->buf = kmalloc(len, GFP_ATOMIC);
		if (!req->buf) {
			usb_ep_free_request(ep, req);
			req = NULL;
		}
	}
	return req;
}

static void mtpg_request_free(struct usb_request *req, struct usb_ep *ep)
{

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);
 
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

static struct usb_request *mtpg_request_new(struct usb_ep *ep, int buffer_size)
{
	
	struct usb_request *req = usb_ep_alloc_request(ep, GFP_KERNEL);

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__); 

	if (!req) {
		printk("******* %s \t line %d ERROR !!! \n",__FUNCTION__,__LINE__);
		return NULL;
	}

	/* now allocate buffers for the requests */
	req->buf = kmalloc(buffer_size, GFP_KERNEL);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	return req;
}

static void mtpg_complete_in(struct usb_ep *ep, struct usb_request *req)
{
	struct mtpg_dev *dev = the_mtpg;

	DEBUG_MTPB("[%s] \t %d req->status is = %d\n", __FUNCTION__,__LINE__, req->status);

	if (req->status != 0)
		dev->error = 1;

	req_put(dev, &dev->tx_idle, req);
	wake_up(&dev->write_wq);
}

static void mtpg_complete_out(struct usb_ep *ep, struct usb_request *req)
{
	struct mtpg_dev *dev = the_mtpg;

	DEBUG_MTPB("[%s] \tline = [%d]req->status is = %d \n", __func__,__LINE__, req->status); 
	if (req->status != 0) {
		dev->error = 1;

		DEBUG_MTPB(" [%s] \t %d dev->error is = %d for rx_idle\n", __FUNCTION__,__LINE__, dev->error);
		req_put(dev, &dev->rx_idle, req);
	} 
	else {
		DEBUG_MTPB("[%s] \t %d for rx_done \n", __FUNCTION__,__LINE__);
		req_put(dev, &dev->rx_done, req);
	}
	wake_up(&dev->read_wq);
}

static void mtpg_function_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct mtpg_dev	*dev = func_to_dev(f);
	struct usb_request *req;

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__); 

	spin_lock_irq(&dev->lock);
	while ((req = req_get(dev, &dev->rx_idle))) {
		mtpg_request_free(req, dev->bulk_out);
		}
	while ((req = req_get(dev, &dev->tx_idle))) {
		mtpg_request_free(req, dev->bulk_in);
		}
	dev->online = 0;
	dev->error = 1;
	spin_unlock_irq(&dev->lock);

	misc_deregister(&mtpg_device);
	kfree(the_mtpg);
	the_mtpg = NULL;
}

static int __init mtpg_function_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct mtpg_dev	*mtpg 	= func_to_dev(f);
	struct usb_request 	*req;
	struct usb_ep		*ep;
	int		rc = 0;
    int     i, id;

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__);
	printk("[%s] \tline = [%d] \n", __func__,__LINE__);
 
	id = usb_interface_id(c, f);
	if (id < 0) {
		printk("Error in usb_string_id Failed !!! \n");
		return id;
	}

	mtpg_interface_desc.bInterfaceNumber = id;
#ifdef EP_MTP_REALLOC	
	printk("[%s] Skip EP Alloc at Booting  \n", __func__);
#else
	ep = usb_ep_autoconfig(cdev->gadget, &fs_mtpg_in_desc);
	if (!ep){
		printk("Error in usb_ep_autoconfig for fs IN DESC Failed !!!!!!!!!! \n");
		goto autoconf_fail;
	}
	ep->driver_data = mtpg;		/* claim the endpoint */
	mtpg->bulk_in = ep;
	the_mtpg->bulk_in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &fs_mtpg_out_desc);
	if (!ep){
		printk("Error in usb_ep_autoconfig for fs OUT DESC Failed !!!!!!!!!! \n");
		goto autoconf_fail;
	}
	ep->driver_data = mtpg;		/* claim the endpoint */
	mtpg->bulk_out = ep;
	the_mtpg->bulk_out = ep;
	printk("[%s]  in =0x%x , out =0x%x \n", __func__,mtpg->bulk_in ,mtpg->bulk_out);

#ifdef EP_INT_MTP_REALLOC   // alloc interrupt endpoint at specific EP alloc functoin for sharing EP_INT (ep-6)
	/* Interrupt Support for MTP */
	ep = usb_specific_ep_config(cdev->gadget, &int_fs_notify_desc);
	if (!ep){
		printk("Error in usb_ep_autoconfig for fs INT IN DESC Failed !!!!!!!!!! \n");
		goto autoconf_fail;
	}
	ep->driver_data = mtpg;
	mtpg->int_in = ep;
	the_mtpg->int_in = ep;
	printk("[%s]  Specific Ep Alloc  int=0x%x \n", __func__,mtpg->int_in);
#else // alloc interrupt endpoint nomal EP alloc functoin
	/* Interrupt Support for MTP */
	ep = usb_ep_autoconfig(cdev->gadget, &int_fs_notify_desc);
	if (!ep){
		printk("Error in usb_ep_autoconfig for fs INT IN DESC Failed !!!!!!!!!! \n");
		goto autoconf_fail;
	}
	ep->driver_data = mtpg;
	mtpg->int_in = ep;
	the_mtpg->int_in = ep;
	printk("[%s]  in =0x%x , out =0x%x, int =0x%x \n", __func__,mtpg->bulk_in ,mtpg->bulk_out ,mtpg->int_in);
#endif

	mtpg->notify_req = alloc_ep_req(ep,
			sizeof(struct usb_mtp_ctrlrequest) + 2,
			GFP_ATOMIC);
	if (!mtpg->notify_req)
		goto out;

	for (i = 0; i < RX_REQ_MAX; i++) {
		req = mtpg_request_new(mtpg->bulk_out, BULK_BUFFER_SIZE);
		if (!req){
			goto out;
		}
		req->complete = mtpg_complete_out;
		req_put(mtpg, &mtpg->rx_idle, req);
	}

	for (i = 0; i < TX_REQ_MAX; i++) {
		req = mtpg_request_new(mtpg->bulk_in, BULK_BUFFER_SIZE);
		if (!req){
			goto out;
		}
		req->complete = mtpg_complete_in;
		req_put(mtpg, &mtpg->tx_idle, req);
	}

	rc = -ENOMEM;

	if (gadget_is_dualspeed(cdev->gadget)) {

		DEBUG_MTPB("[%s] \tdual speed line = [%d] \n", __func__,__LINE__); 
		printk("[%s] \tdual speed line = [%d] \n", __func__,__LINE__); 

		/* Assume endpoint addresses are the same for both speeds */
		hs_mtpg_in_desc.bEndpointAddress =
				fs_mtpg_in_desc.bEndpointAddress;
		hs_mtpg_out_desc.bEndpointAddress =
				fs_mtpg_out_desc.bEndpointAddress;
		int_hs_notify_desc.bEndpointAddress = 
				int_fs_notify_desc.bEndpointAddress;
		
		f->hs_descriptors = hs_mtpg_desc;
	}
#endif

	mtpg->cdev = cdev; 
	the_mtpg->cdev = cdev;

	/*This is required for intializing Descriptors to NULL*/
	printk("***** [%s:%d] calling mtp_function_enable with %d \n",__func__,__LINE__,mtp_disable_desc);
	mtp_function_enable(mtp_disable_desc);

	return 0;

autoconf_fail:
	printk("mtpg unable to autoconfigure all endpoints\n");
	rc = -ENOTSUPP;
out:
	mtpg_function_unbind(c, f);
	return rc;
}

static int mtpg_function_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct mtpg_dev	*dev = func_to_dev(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret;

	if (dev->int_in->driver_data){
		usb_ep_disable(dev->int_in);
	}

	ret = usb_ep_enable(dev->int_in, ep_choose(cdev->gadget, &int_hs_notify_desc, &int_fs_notify_desc ));
	if(ret) {
		usb_ep_disable(dev->int_in);
		dev->int_in->driver_data = NULL;
		printk("USB/Driver/f_mtp.c Error in enabling the interrupt endpoint \n");
		return ret;
	}
	dev->int_in->driver_data = dev;

	if(dev->bulk_in->driver_data){
		usb_ep_disable(dev->bulk_in);
	}

	ret = usb_ep_enable(dev->bulk_in, ep_choose(cdev->gadget, &hs_mtpg_in_desc, &fs_mtpg_in_desc));
	if (ret) {
		usb_ep_disable(dev->bulk_in);
		dev->bulk_in->driver_data = NULL;
		 printk("[%s] Enable Bulk-Out EP error!!! %d\n", __FUNCTION__, __LINE__);
		 return ret;
	}
	dev->bulk_in->driver_data = dev;

	if(dev->bulk_out->driver_data){
		usb_ep_disable(dev->bulk_out);
	}
	ret = usb_ep_enable(dev->bulk_out, ep_choose(cdev->gadget, &hs_mtpg_out_desc, &fs_mtpg_out_desc));
	if (ret) {
		usb_ep_disable(dev->bulk_out);
		dev->bulk_out->driver_data = NULL;
		printk("[%s] Enable Bulk-In EP error!!! %d\n", __FUNCTION__, __LINE__);
		return ret;
	}
	dev->bulk_out->driver_data = dev;

	dev->online = 1;

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);

	return 0;
}

static void mtpg_function_disable(struct usb_function *f)
{

	struct mtpg_dev	*dev = func_to_dev(f);

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__); 

	dev->online = 0;
	dev->error = 1;
	
	usb_ep_disable(dev->int_in);
	dev->int_in->driver_data = NULL;

	usb_ep_disable(dev->bulk_in);
	dev->bulk_in->driver_data = NULL;
	
	usb_ep_disable(dev->bulk_out);
	dev->bulk_out->driver_data = NULL;

	wake_up(&dev->read_wq);
}


/*PIMA15740-2000 spec: Class specific setup request for MTP*/
static void mtp_complete_cancel_io(struct usb_ep *ep,
		struct usb_request *req)
{
	int i;
	struct mtpg_dev	*dev = ep->driver_data;
	struct usb_composite_dev *cdev = dev->cdev;
	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__); 
	if (req->status != 0) 
	{
		DEBUG_MTPB("[%s] req->status !=0 \tline = [%d] \n", __func__,__LINE__); 
		return;
	}

	if(req->actual != USB_PTPREQUEST_CANCELIO_SIZE)
	{
		DEBUG_MTPB("[%s] USB_PTPREQUEST_CANCELIO_SIZE\tline = [%d] \n", __func__,__LINE__); 
		usb_ep_set_halt(ep);

	}
	else
	{
		memset(dev->cancel_io_buf, 0, USB_PTPREQUEST_CANCELIO_SIZE+1);
		memcpy(dev->cancel_io_buf, req->buf, USB_PTPREQUEST_CANCELIO_SIZE);
		/*Debugging*/
		for(i=0;i<USB_PTPREQUEST_CANCELIO_SIZE; i++)
			DEBUG_MTPB("[%s] cancel_io_buf[%d] = %x \tline = [%d] \n", __func__,i,dev->cancel_io_buf[i],__LINE__); 
		mtp_send_signal(USB_PTPREQUEST_CANCELIO);
	}
	

}
static int mtpg_function_setup(struct usb_function *f,
					const struct usb_ctrlrequest *ctrl)
{
	struct mtpg_dev	*dev = func_to_dev(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int signal_request = 0;
	int value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__); 
	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {

		case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_PTPREQUEST_CANCELIO:
			DEBUG_MTPB("[%s] USB_PTPREQUEST_CANCELIO \tline = [%d] \n", __func__,__LINE__); 
			DEBUG_MTPB("[%s] \tline = [%d]  w_value = %x,w_index = %x, w_length = %x\n", w_value, w_index, w_length);
			if (w_value == 0x00 && w_index == mtpg_interface_desc.bInterfaceNumber && w_length == 0x06)
			{
				DEBUG_MTPB("[%s] read USB_PTPREQUEST_CANCELIO data \tline = [%d] \n", __func__,__LINE__); 
				value = w_length;
				cdev->gadget->ep0->driver_data = dev;
				req->complete = mtp_complete_cancel_io;
				req->zero = 0;
				req->length = value;
				value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
				if (value < 0)
					printk("[%s] \tline = [%d] Error at usb_ep_queue !!!!!!!\n");
			}
			return value;
			break;
		case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_PTPREQUEST_RESET:
			DEBUG_MTPB("[%s] USB_PTPREQUEST_RESET \tline = [%d] \n", __func__,__LINE__); 
			signal_request = USB_PTPREQUEST_RESET;
			break;

		case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_PTPREQUEST_GETSTATUS:
			DEBUG_MTPB("[%s] USB_PTPREQUEST_GETSTATUS \tline = [%d] \n", __func__,__LINE__); 
			signal_request = USB_PTPREQUEST_GETSTATUS;
			break;

		case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_PTPREQUEST_GETEVENT:
			DEBUG_MTPB("[%s] USB_PTPREQUEST_GETEVENT \tline = [%d] \n", __func__,__LINE__); 
			signal_request = USB_PTPREQUEST_GETEVENT;
			break;
		default:
			DEBUG_MTPB("[%s] INVALID REQUEST \tline = [%d] \n", __func__,__LINE__); 

		}
	value = mtp_send_signal(signal_request);
	return value;
}

int __init mtp_function_add(struct usb_configuration *c)
{
	struct mtpg_dev	*mtpg;
	int		status;
	int		rc = 0;

	DEBUG_MTPB("[%s] \tline = [%d] \n", __func__,__LINE__); 

	mtpg = (struct mtpg_dev*) kzalloc(sizeof(struct mtpg_dev), GFP_KERNEL);
	if (!mtpg) {
		printk("[%s] \t mtpg_dev_alloc memory  failed !!!\n", __func__);
		return -ENOMEM;
	}

	if (strings_dev_mtp[F_MTP_IDX].id == 0) {
		status = usb_string_id(c->cdev);
		if (status < 0) {
            printk("[%s] Error in get usb_string_id\n");
            goto err_misc_register;
        }
		strings_dev_mtp[F_MTP_IDX].id = status;
		mtpg_interface_desc.iInterface = status;
	}

	spin_lock_init(&mtpg->lock);
	init_waitqueue_head(&mtpg->read_wq);
	init_waitqueue_head(&mtpg->write_wq);

	atomic_set(&mtpg->open_excl, 0);
	atomic_set(&mtpg->read_excl, 0);
	atomic_set(&mtpg->write_excl, 0);
	atomic_set(&mtpg->wintfd_excl, 0);

	INIT_LIST_HEAD(&mtpg->rx_idle);
	INIT_LIST_HEAD(&mtpg->rx_done);
	INIT_LIST_HEAD(&mtpg->tx_idle);

	mtpg->function.name = longname;	
	mtpg->function.strings = dev_strings;
//Test the switch
#if 0
	mtpg->function.descriptors = fs_mtpg_desc;
	mtpg->function.hs_descriptors = hs_mtpg_desc;
#else
	mtpg->function.descriptors = null_mtpg_descs;
	mtpg->function.hs_descriptors = null_mtpg_descs;
#endif
	mtpg->function.bind = mtpg_function_bind;
	mtpg->function.unbind = mtpg_function_unbind;
	mtpg->function.setup = mtpg_function_setup;
	mtpg->function.set_alt = mtpg_function_set_alt;
	mtpg->function.disable = mtpg_function_disable;

	/* the_mtpg must be set before calling usb_gadget_register_driver */
	the_mtpg = mtpg;

	rc = misc_register(&mtpg_device);
	if (rc != 0){
		printk("Error in misc_register of mtpg_device Failed !!!\n");
		goto err_misc_register;
	}
	
	rc = usb_add_function(c, &mtpg->function);
	if (rc != 0){
		printk("Error in usb_add_function Failed !!!\n");
		goto err_usb_add_function;
	}

	return 0;

err_usb_add_function:
	misc_deregister(&mtpg_device);

err_misc_register:
	kfree(mtpg);
	printk("mtp gadget driver failed to initialize !!! \n");
	return rc;
}

int mtp_function_config_changed(struct usb_composite_dev *cdev,	struct usb_configuration *c)
{
	struct mtpg_dev *mtpg = the_mtpg;
	int 	status;
//#ifdef EP_MTP_REALLOC
	struct usb_ep		*ep;
	int 		rc = 0;
	struct usb_request 	*req;
	int			 i;
//#endif
	printk(KERN_INFO "mtp_function_config_changed\n");

	mtpg->function.bind = NULL;

	status = usb_add_function(c, &mtpg->function);
	if (status)
		printk("usb_add_function failed\n");
	
	status = usb_interface_id(c, &mtpg->function);
	if (status < 0) {
		printk("Error in usb_string_id Failed !!! \n");
		return status;
	}

	mtpg_interface_desc.bInterfaceNumber = status;

#ifdef EP_MTP_REALLOC
	ep = usb_specific_ep_realloc(cdev->gadget, &fs_mtpg_in_desc);
	if (!ep){
		printk("Error in usb_ep_autoconfig for fs IN DESC Failed !!!!!!!!!! \n");
		goto autoconf_fail2;
	}
	ep->driver_data = mtpg; 	/* claim the endpoint */
	mtpg->bulk_in = ep;
	the_mtpg->bulk_in = ep;

	ep = usb_specific_ep_realloc(cdev->gadget, &fs_mtpg_out_desc);
	if (!ep){
		printk("Error in usb_ep_autoconfig for fs OUT DESC Failed !!!!!!!!!! \n");
		goto autoconf_fail2;
	}
	ep->driver_data = mtpg; 	/* claim the endpoint */
	mtpg->bulk_out = ep;
	the_mtpg->bulk_out = ep;

	/* Interrupt Support for MTP */
	ep = usb_specific_ep_realloc(cdev->gadget, &int_fs_notify_desc);
	if (!ep){
		printk("Error in usb_ep_autoconfig for fs INT IN DESC Failed !!!!!!!!!! \n");
		goto autoconf_fail2;
	}
	ep->driver_data = mtpg;
	mtpg->int_in = ep;
	the_mtpg->int_in = ep;
	
	printk("[%s] EP Realloc  in =0x%x , out =0x%x, int =0x%x \n", __func__,mtpg->bulk_in ,mtpg->bulk_out ,mtpg->int_in);
	mtpg->notify_req = alloc_ep_req(ep,
			sizeof(struct usb_mtp_ctrlrequest) + 2,
			GFP_ATOMIC);
	if (!mtpg->notify_req)
		goto out2;

	for (i = 0; i < RX_REQ_MAX; i++) {
		req = mtpg_request_new(mtpg->bulk_out, BULK_BUFFER_SIZE);
		if (!req){
			goto out2;
		}
		req->complete = mtpg_complete_out;
		req_put(mtpg, &mtpg->rx_idle, req);
	}

	for (i = 0; i < TX_REQ_MAX; i++) {
		req = mtpg_request_new(mtpg->bulk_in, BULK_BUFFER_SIZE);
		if (!req){
			goto out2;
		}
		req->complete = mtpg_complete_in;
		req_put(mtpg, &mtpg->tx_idle, req);
	}

	rc = -ENOMEM;

	if (gadget_is_dualspeed(cdev->gadget)) {

		DEBUG_MTPB("[%s] \tdual speed line = [%d] \n", __func__,__LINE__); 
		printk("[%s] \tdual speed line = [%d] \n", __func__,__LINE__); 

		/* Assume endpoint addresses are the same for both speeds */
		hs_mtpg_in_desc.bEndpointAddress =
				fs_mtpg_in_desc.bEndpointAddress;
		hs_mtpg_out_desc.bEndpointAddress =
				fs_mtpg_out_desc.bEndpointAddress;
		int_hs_notify_desc.bEndpointAddress = 
				int_fs_notify_desc.bEndpointAddress;
		
		mtpg->function.hs_descriptors = hs_mtpg_desc;
	}
		mtpg->cdev = cdev; 
		the_mtpg->cdev = cdev;
#else
#ifdef EP_INT_MTP_REALLOC
	printk("[%s] Skip EP Realloc in =0x%x , out =0x%x \n", __func__,mtpg->bulk_in ,mtpg->bulk_out);

	/* Interrupt Support for MTP */
	ep = usb_specific_ep_realloc(cdev->gadget, &int_fs_notify_desc);
	if (!ep){
		printk("Error in usb_ep_autoconfig for fs INT IN DESC Failed !!!!!!!!!! \n");
		goto autoconf_fail2;
	}
	ep->driver_data = mtpg;
	mtpg->int_in = ep;
	the_mtpg->int_in = ep;	
	printk("[%s] Specific EP Realloc int =0x%x \n", __func__ ,mtpg->int_in);
	
	mtpg->notify_req = alloc_ep_req(ep,
			sizeof(struct usb_mtp_ctrlrequest) + 2,
			GFP_ATOMIC);
	if (!mtpg->notify_req)
		goto out2;

	int_hs_notify_desc.bEndpointAddress = 
			int_fs_notify_desc.bEndpointAddress;

	mtpg->cdev = cdev; 
	the_mtpg->cdev = cdev;
#else
	printk("[%s] Skip EP Realloc  in =0x%x , out =0x%x, int =0x%x \n", __func__,mtpg->bulk_in ,mtpg->bulk_out ,mtpg->int_in);
#endif

#endif	
	return 0;
//#ifdef EP_MTP_REALLOC
autoconf_fail2:
	printk("mtpg unable to autoconfigure all endpoints\n");
	rc = -ENOTSUPP;

out2:
	mtpg_function_unbind(c, &mtpg->function);
	return rc;
//#endif


}

void mtp_function_enable(int enable)
{
        struct mtpg_dev *dev = the_mtpg;

        if (dev) {
                printk("[%s] mtp_function => (%s)\n", __func__,
                        enable ? "enabled" : "disabled");

                if (enable) {
                        printk("****** %s and line %d fs and hs desc \n",__FUNCTION__,__LINE__);
                        dev->function.descriptors = fs_mtpg_desc;
                        dev->function.hs_descriptors = hs_mtpg_desc;
                } else {
                        printk("****** %s and line %d null_desc \n",__FUNCTION__,__LINE__);
                        dev->function.descriptors = null_mtpg_descs;
                        dev->function.hs_descriptors = null_mtpg_descs;
                }
        }
        else {
                printk("[%s] dev does not exist\n", __func__);
        }
}

MODULE_AUTHOR("Deepak And Madhukar");
MODULE_LICENSE("GPL");
