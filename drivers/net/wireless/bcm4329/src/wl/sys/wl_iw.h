/*
 * Linux Wireless Extensions support
 *
 * Copyright (C) 1999-2010, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: wl_iw.h,v 1.5.34.1.6.16 2010/04/19 21:32:10 Exp $
 */


#ifndef _wl_iw_h_
#define _wl_iw_h_

#include <linux/wireless.h>

#include <typedefs.h>
#include <proto/ethernet.h>
#include <wlioctl.h>

#define SOFTAP 1


#define FEATURE_HOTSPOT_EVENT
#define	WL_IW_RSSI_MINVAL		-200	
#define	WL_IW_RSSI_NO_SIGNAL	-91	
#define	WL_IW_RSSI_VERY_LOW	-80	
#define	WL_IW_RSSI_LOW		-70	
#define	WL_IW_RSSI_GOOD		-68	
#define	WL_IW_RSSI_VERY_GOOD	-58	
#define	WL_IW_RSSI_EXCELLENT	-57	
#define	WL_IW_RSSI_INVALID	 0	
#define MAX_WX_STRING 80
#define isprint(c) bcm_isprint(c)
#define WL_IW_SET_ACTIVE_SCAN	(SIOCIWFIRSTPRIV+1)
#define WL_IW_GET_RSSI			(SIOCIWFIRSTPRIV+3)
#define WL_IW_SET_PASSIVE_SCAN	(SIOCIWFIRSTPRIV+5)
#define WL_IW_GET_LINK_SPEED	(SIOCIWFIRSTPRIV+7)
#define WL_IW_GET_CURR_MACADDR	(SIOCIWFIRSTPRIV+9)
#define WL_IW_SET_STOP				(SIOCIWFIRSTPRIV+11)
#define WL_IW_SET_START			(SIOCIWFIRSTPRIV+13)


#define WL_SET_AP_CFG           (SIOCIWFIRSTPRIV+15)
#define WL_AP_STA_LIST          (SIOCIWFIRSTPRIV+17)
#define WL_AP_MAC_FLTR	        (SIOCIWFIRSTPRIV+19)
#define WL_AP_BSS_START         (SIOCIWFIRSTPRIV+21)
#define WL_FW_DISASSOC_STA      (SIOCIWFIRSTPRIV+23)
#define WL_AP_STOP              (SIOCIWFIRSTPRIV+25)
#define WL_FW_RELOAD            (SIOCIWFIRSTPRIV+27)
#ifdef FEATURE_HOTSPOT_EVENT
//  SecFeature CHG START STEALTH_V by jaekwan.jeon
#define WL_AP_MAX_ASSOC         (SIOCIWFIRSTPRIV+29)
// #define GET_QUEUE		(SIOCIWFIRSTPRIV+29)
// #define GET_QUEUE_SIZE  (SIOCIWFIRSTPRIV+31)
//  SecFeature CHG END STEALTH_V by jaekwan.jeon
#endif
#define 		G_SCAN_RESULTS 8*1024
#define 		WE_ADD_EVENT_FIX	0x80
#define          G_WLAN_SET_ON	0
#define          G_WLAN_SET_OFF	1

#define CHECK_EXTRA_FOR_NULL(extra) \
if (!extra) { \
	WL_ERROR(("%s: error : extra is null pointer\n", __FUNCTION__)); \
	return -EINVAL; \
}

typedef struct wl_iw {
	char nickname[IW_ESSID_MAX_SIZE];

	struct iw_statistics wstats;

	int spy_num;
	uint32 pwsec;			
	uint32 gwsec;			
	bool privacy_invoked; 		

	struct ether_addr spy_addr[IW_MAX_SPY];
	struct iw_quality spy_qual[IW_MAX_SPY];
	void  *wlinfo;
	dhd_pub_t * pub;
} wl_iw_t;

int	 wl_control_wl_start(struct net_device *dev);
//#define WLC_IW_SS_CACHE_MAXLEN				512
#define WLC_IW_SS_CACHE_MAXLEN				1024
#define WLC_IW_SS_CACHE_CTRL_FIELD_MAXLEN	32
#define WLC_IW_BSS_INFO_MAXLEN 				\
	(WLC_IW_SS_CACHE_MAXLEN - WLC_IW_SS_CACHE_CTRL_FIELD_MAXLEN)

typedef struct wl_iw_ss_cache{
	uint32 buflen;
	uint32 version;
	uint32 count;
	wl_bss_info_t bss_info[1];
	char dummy[WLC_IW_BSS_INFO_MAXLEN - sizeof(wl_bss_info_t)];
	int dirty;
	struct wl_iw_ss_cache *next;
} wl_iw_ss_cache_t;

typedef struct wl_iw_ss_cache_ctrl {
	wl_iw_ss_cache_t *m_cache_head;	
	int m_link_down;		
	int m_timer_expired;		
	char m_active_bssid[ETHER_ADDR_LEN];	
	uint m_prev_scan_mode;	
	uint m_cons_br_scan_cnt;	
	struct timer_list *m_timer;	
} wl_iw_ss_cache_ctrl_t;
typedef enum broadcast_first_scan {
	BROADCAST_SCAN_FIRST_IDLE = 0,
	BROADCAST_SCAN_FIRST_STARTED,
	BROADCAST_SCAN_FIRST_RESULT_READY,
	BROADCAST_SCAN_FIRST_RESULT_CONSUMED
} broadcast_first_scan_t;
#ifdef SOFTAP

#define NEW_AP_INTERFACE

#define SSID_LEN	33
#define SEC_LEN		16
#define KEY_LEN		65
#define PROFILE_OFFSET	32

#define MACLIST_MODE_DISABLED	0
#define MACLIST_MODE_ENABLED	1
#define MACLIST_MODE_ALLOW		2
struct mflist {
	uint count;
	struct ether_addr ea[16];
};
struct mac_list_set {
	uint32	mode;
	struct mflist white_list;
	struct mflist black_list;
};

struct ap_profile {
	uint8	ssid[SSID_LEN];
	uint8	sec[SEC_LEN];
	uint8	key[KEY_LEN];
	uint32	channel; 
	uint32	preamble;
	uint32	max_scb;	
#ifdef NEW_AP_INTERFACE
	uint32	hidden_ssid;
	uint32	op_mode;
	uint32	key_index;
	int     is_wep;
	struct mac_list_set mac_filter;
#endif
};
#endif   

#if WIRELESS_EXT > 12
#include <net/iw_handler.h>
extern const struct iw_handler_def wl_iw_handler_def;
#endif 

extern int wl_iw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
extern void wl_iw_event(struct net_device *dev, wl_event_msg_t *e, void* data);
extern int wl_iw_get_wireless_stats(struct net_device *dev, struct iw_statistics *wstats);
int wl_iw_attach(struct net_device *dev, void * dhdp);
void wl_iw_detach(void);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#define IWE_STREAM_ADD_EVENT(info, stream, ends, iwe, extra) \
	iwe_stream_add_event(info, stream, ends, iwe, extra)
#define IWE_STREAM_ADD_VALUE(info, event, value, ends, iwe, event_len) \
	iwe_stream_add_value(info, event, value, ends, iwe, event_len)
#define IWE_STREAM_ADD_POINT(info, stream, ends, iwe, extra) \
	iwe_stream_add_point(info, stream, ends, iwe, extra)
#else
#define IWE_STREAM_ADD_EVENT(info, stream, ends, iwe, extra) \
	iwe_stream_add_event(stream, ends, iwe, extra)
#define IWE_STREAM_ADD_VALUE(info, event, value, ends, iwe, event_len) \
	iwe_stream_add_value(event, value, ends, iwe, event_len)
#define IWE_STREAM_ADD_POINT(info, stream, ends, iwe, extra) \
	iwe_stream_add_point(stream, ends, iwe, extra)
#endif

#endif 
