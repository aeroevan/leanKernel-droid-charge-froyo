/* linux/drivers/media/video/samsung/tv20/hpd.h
 *
 * hpd interface header file for Samsung TVOut driver
 *
 * Copyright (c) 2009 Samsung Electronics
 * 	http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define VERSION         "1.2" /* Driver version number */
#define HPD_MINOR       243 /* Major 10, Minor 243, /dev/hpd */

#define HPD_LO          0
#define HPD_HI          1

#define HDMI_ON 	1
#define HDMI_OFF 	0

struct hpd_struct {
	spinlock_t lock;
	wait_queue_head_t waitq;
	atomic_t state;
};

extern int s5p_hpd_set_eint(void);
extern int s5p_hpd_set_hdmiint(void);
extern bool hpd_sleep_state;
