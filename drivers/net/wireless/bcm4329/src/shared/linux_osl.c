/*
 * Linux OS Independent Layer
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
 * $Id: linux_osl.c,v 1.125.12.3.8.7 2010/05/04 21:10:04 Exp $
 */


#define LINUX_OSL
#if defined(CHROMIUMOS_COMPAT_WIRELESS)
#include <linux/sched.h>
#endif
#include <typedefs.h>
#include <bcmendian.h>
#include <linuxver.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <linux/delay.h>
#include <pcicfg.h>

#ifdef DHD_DEBUG
#define OSL_MSG_ERROR(x) printk x
#define OSL_MSG_INFO(x)
#else
#define OSL_MSG_ERROR(x)
#define OSL_MSG_INFO(x)
#endif

#define PCI_CFG_RETRY 		10

#define OS_HANDLE_MAGIC		0x1234abcd	
#define BCM_MEM_FILENAME_LEN 	24		

#ifdef DHD_USE_STATIC_BUF
#define DHD_SKB_HDRSIZE 		336
#define DHD_SKB_1PAGE_BUFSIZE	((PAGE_SIZE*1)-DHD_SKB_HDRSIZE)
#define DHD_SKB_2PAGE_BUFSIZE	((PAGE_SIZE*2)-DHD_SKB_HDRSIZE)
#define DHD_SKB_4PAGE_BUFSIZE	((PAGE_SIZE*4)-DHD_SKB_HDRSIZE)

#define MAX_STATIC_BUF_NUM 16
#define STATIC_BUF_SIZE	(PAGE_SIZE*2)
#define STATIC_BUF_TOTAL_LEN (MAX_STATIC_BUF_NUM*STATIC_BUF_SIZE)
typedef struct bcm_static_buf {
	struct semaphore static_sem;
	unsigned char *buf_ptr;
	unsigned char buf_use[MAX_STATIC_BUF_NUM];
} bcm_static_buf_t;

static bcm_static_buf_t *bcm_static_buf = 0;

#define MAX_STATIC_PKT_NUM 8
typedef struct bcm_static_pkt {
	struct sk_buff *skb_4k[MAX_STATIC_PKT_NUM];
	struct sk_buff *skb_8k[MAX_STATIC_PKT_NUM];
	struct sk_buff *skb_16k;
	struct semaphore osl_pkt_sem;
	unsigned char pkt_use[MAX_STATIC_PKT_NUM*2+1];
} bcm_static_pkt_t;
static bcm_static_pkt_t *bcm_static_skb = 0;

#endif 
typedef struct bcm_mem_link {
	struct bcm_mem_link *prev;
	struct bcm_mem_link *next;
	uint	size;
	int	line;
	char	file[BCM_MEM_FILENAME_LEN];
} bcm_mem_link_t;

struct osl_info {
	osl_pubinfo_t pub;
	uint magic;
	void *pdev;
	uint malloced;
	uint failed;
	uint bustype;
	bcm_mem_link_t *dbgmem_list;
};

static int16 linuxbcmerrormap[] =
{	0, 			
	-EINVAL,		
	-EINVAL,		
	-EINVAL,		
	-EINVAL,		
	-EINVAL,		
	-EINVAL,		
	-EINVAL,		
	-EINVAL,		
	-EINVAL,		
	-EINVAL,		
	-EINVAL, 		
	-EINVAL, 		
	-EINVAL, 		
	-E2BIG,			
	-E2BIG,			
	-EBUSY, 		
	-EINVAL, 		
	-EINVAL, 		
	-EINVAL, 		
	-EINVAL, 		
	-EFAULT, 		
	-ENOMEM, 		
	-EOPNOTSUPP,		
	-EMSGSIZE,		
	-EINVAL,		
	-EPERM,			
	-ENOMEM, 		
	-EINVAL, 		
	-ERANGE, 		
	-EINVAL, 		
	-EINVAL, 		
	-EINVAL, 		
	-EINVAL, 		
	-EINVAL,		
	-EIO,			
	-ENODEV,		
	-EINVAL,		
	-EIO,			
	-EIO,			
	-EINVAL,		
	-EINVAL,		



#if BCME_LAST != -41
#error "You need to add a OS error translation in the linuxbcmerrormap \
	for new error code defined in bcmutils.h"
#endif 
};


int
osl_error(int bcmerror)
{
	if (bcmerror > 0)
		bcmerror = 0;
	else if (bcmerror < BCME_LAST)
		bcmerror = BCME_ERROR;

	
	return linuxbcmerrormap[-bcmerror];
}

void * dhd_os_prealloc(int section, unsigned long size);

#define WLAN_SKB_BUF_NUM	17
extern struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];


osl_t *
osl_attach(void *pdev, uint bustype, bool pkttag)
{
	osl_t *osh;

	osh = kmalloc(sizeof(osl_t), GFP_KERNEL);
	ASSERT(osh);

	bzero(osh, sizeof(osl_t));

	
	ASSERT(ABS(BCME_LAST) == (ARRAYSIZE(linuxbcmerrormap) - 1));

	osh->magic = OS_HANDLE_MAGIC;
	osh->malloced = 0;
	osh->failed = 0;
	osh->dbgmem_list = NULL;
	osh->pdev = pdev;
	osh->pub.pkttag = pkttag;
	osh->bustype = bustype;

	switch (bustype) {
		case PCI_BUS:
		case SI_BUS:
		case PCMCIA_BUS:
			osh->pub.mmbus = TRUE;
			break;
		case JTAG_BUS:
		case SDIO_BUS:
		case USB_BUS:
		case SPI_BUS:
			osh->pub.mmbus = FALSE;
			break;
		default:
			ASSERT(FALSE);
			break;
	}

#ifdef DHD_USE_STATIC_BUF


	if (!bcm_static_buf) {
		if (!(bcm_static_buf = (bcm_static_buf_t *)dhd_os_prealloc(3, STATIC_BUF_SIZE+
			STATIC_BUF_TOTAL_LEN))) {
			OSL_MSG_ERROR(("osl_attach: can not alloc static buf!\n"));
		}
		else
			OSL_MSG_INFO(("osl_attach: alloc static buf at %x!\n", (unsigned int)bcm_static_buf));

		
		init_MUTEX(&bcm_static_buf->static_sem);

		
		bcm_static_buf->buf_ptr = (unsigned char *)bcm_static_buf + STATIC_BUF_SIZE;

	}
	
	if (!bcm_static_skb)
	{
		int i;
#ifndef CUSTOMER_HW_SAMSUNG
		void *skb_buff_ptr = 0;
#endif
		bcm_static_skb = (bcm_static_pkt_t *)((char *)bcm_static_buf + 2048);
#ifdef CUSTOMER_HW_SAMSUNG

/* 
		for (i = 0; i < MAX_STATIC_PKT_NUM; i++) {
			bcm_static_skb->skb_4k[i] = dev_alloc_skb(DHD_SKB_1PAGE_BUFSIZE);
			if (bcm_static_skb->skb_4k[i] == NULL) {
				OSL_MSG_ERROR(("osl_attach: 4K memory allocation failure. idx=%d\n", i));
				goto err;
			}
		}
			
		for (i = 0; i < MAX_STATIC_PKT_NUM; i++) {
			bcm_static_skb->skb_8k[i] = dev_alloc_skb_kernel(DHD_SKB_2PAGE_BUFSIZE);
			if (bcm_static_skb->skb_8k[i] == NULL) {
				OSL_MSG_ERROR(("osl_attach: 8K memory allocation failure. idx=%d\n", i));
				goto err;
			}
		}

		bcm_static_skb->skb_16k = dev_alloc_skb_kernel(DHD_SKB_4PAGE_BUFSIZE);
		if (bcm_static_skb->skb_16k == NULL) {
			OSL_MSG_ERROR(("osl_attach: 16K memory allocation failure. idx=%d\n", i));
			goto err;
		}
*/
		printk("use staic skb\n");
		for (i = 0; i < MAX_STATIC_PKT_NUM; i++) {
			bcm_static_skb->skb_4k[i] = wlan_static_skb[i];
			if (bcm_static_skb->skb_4k[i] == NULL) {
				OSL_MSG_ERROR(("osl_attach: 4K memory allocation failure. idx=%d\n", i));
				goto err;
			}
		}
			
		for (i = 0; i < MAX_STATIC_PKT_NUM; i++) {
			bcm_static_skb->skb_8k[i] =wlan_static_skb[i+MAX_STATIC_PKT_NUM];
			if (bcm_static_skb->skb_8k[i] == NULL) {
				OSL_MSG_ERROR(("osl_attach: 8K memory allocation failure. idx=%d\n", i));
				goto err;
			}
		}

		bcm_static_skb->skb_16k = wlan_static_skb[2*MAX_STATIC_PKT_NUM];
		if (bcm_static_skb->skb_16k == NULL) {
			OSL_MSG_ERROR(("osl_attach: 16K memory allocation failure. idx=%d\n", i));
			goto err;
		}
#else
		skb_buff_ptr = dhd_os_prealloc(4, 0);

		bcopy(skb_buff_ptr, bcm_static_skb, sizeof(struct sk_buff *)*16);
#endif /* CUSTOMER_HW_SAMSUNG */
		for (i = 0; i < MAX_STATIC_PKT_NUM*2+1; i++)
			bcm_static_skb->pkt_use[i] = 0;

		init_MUTEX(&bcm_static_skb->osl_pkt_sem);
	}
#endif 
	return osh;
err:

	kfree(osh);
	return 0;
}

void
osl_detach(osl_t *osh)
{
	if (osh == NULL)
		return;

#ifdef DHD_USE_STATIC_BUF
	if (bcm_static_buf) {
		bcm_static_buf = 0;
	}
	if (bcm_static_skb) {
		int i;
		down(&bcm_static_skb->osl_pkt_sem);
		/* 2011.01.21 static wifi skb 
		for(i=0; i<MAX_STATIC_PKT_NUM*2+1; i++) {
			dev_kfree_skb(bcm_static_skb->skb_4k[i]);
		}
		*///--> 2011.01.21 static wifi skb 
		up(&bcm_static_skb->osl_pkt_sem);
		bcm_static_skb = 0;
	}
#endif 
	ASSERT(osh->magic == OS_HANDLE_MAGIC);
	kfree(osh);
}


void*
osl_pktget(osl_t *osh, uint len)
{
	struct sk_buff *skb;

	if ((skb = dev_alloc_skb(len))) {
		skb_put(skb, len);
		skb->priority = 0;


		osh->pub.pktalloced++;
	}

	return ((void*) skb);
}
void*
osl_pktget_kernel(osl_t *osh, uint len)
{
	struct sk_buff *skb;

	if ((skb = dev_alloc_skb_kernel(len))) {
		skb_put(skb, len);
		skb->priority = 0;


		osh->pub.pktalloced++;
	}

	return ((void*) skb);
}


void
osl_pktfree(osl_t *osh, void *p, bool send)
{
	struct sk_buff *skb, *nskb;

	skb = (struct sk_buff*) p;

	if (send && osh->pub.tx_fn)
		osh->pub.tx_fn(osh->pub.tx_ctx, p, 0);

	
	while (skb) {
		nskb = skb->next;
		skb->next = NULL;


		if (skb->destructor) {
			
			dev_kfree_skb_any(skb);
		} else {
			
			dev_kfree_skb(skb);
		}

		osh->pub.pktalloced--;

		skb = nskb;
	}
}

#ifdef DHD_USE_STATIC_BUF
void*
osl_pktget_static(osl_t *osh, uint len)
{
	int i = 0;
	struct sk_buff *skb;

	
	if (len > DHD_SKB_4PAGE_BUFSIZE)
	{
		OSL_MSG_ERROR(("osl_pktget_static: Do we really need this big skb?? len=%d\n", len));
		return osl_pktget_kernel(osh, len);
	}

	
	down(&bcm_static_skb->osl_pkt_sem);
	if (len <= DHD_SKB_1PAGE_BUFSIZE)
	{
		
		for (i = 0; i < MAX_STATIC_PKT_NUM; i++)
		{
			if (bcm_static_skb->pkt_use[i] == 0)
				break;
		}

		if (i != MAX_STATIC_PKT_NUM)
		{
			bcm_static_skb->pkt_use[i] = 1;
			up(&bcm_static_skb->osl_pkt_sem);

			skb = bcm_static_skb->skb_4k[i];
			skb->tail = skb->data + len;
			skb->len = len;
			
			return skb;
		}
	}

	if (len <= DHD_SKB_2PAGE_BUFSIZE) 
	{
		for (i = 0; i < MAX_STATIC_PKT_NUM; i++)
		{
			if (bcm_static_skb->pkt_use[i+MAX_STATIC_PKT_NUM] == 0)
				break;
		}

		if (i != MAX_STATIC_PKT_NUM)
		{
			bcm_static_skb->pkt_use[i+MAX_STATIC_PKT_NUM] = 1;
			up(&bcm_static_skb->osl_pkt_sem);
			skb = bcm_static_skb->skb_8k[i];
			skb->tail = skb->data + len;
			skb->len = len;
			
			return skb;
		}
	}

	if (bcm_static_skb->pkt_use[MAX_STATIC_PKT_NUM*2] == 0) 
	{
		bcm_static_skb->pkt_use[MAX_STATIC_PKT_NUM*2] = 1;
		up(&bcm_static_skb->osl_pkt_sem);

		skb = bcm_static_skb->skb_16k;
		skb->tail = skb->data + len;
		skb->len = len;

		return skb;
	}
	
	up(&bcm_static_skb->osl_pkt_sem);
	OSL_MSG_ERROR(("osl_pktget_static: all static pkt in use!\n"));
	return osl_pktget(osh, len);
}


void
osl_pktfree_static(osl_t *osh, void *p, bool send)
{
	int i;
	
	for (i = 0; i < MAX_STATIC_PKT_NUM*2+1; i++)
	{
		if (p == bcm_static_skb->skb_4k[i])
		{
			down(&bcm_static_skb->osl_pkt_sem);
			bcm_static_skb->pkt_use[i] = 0;
			up(&bcm_static_skb->osl_pkt_sem);

			
			return;
		}
	}
	return osl_pktfree(osh, p, send);
}
#endif 
uint32
osl_pci_read_config(osl_t *osh, uint offset, uint size)
{
	uint val = 0;
	uint retry = PCI_CFG_RETRY;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	
	ASSERT(size == 4);

	do {
		pci_read_config_dword(osh->pdev, offset, &val);
		if (val != 0xffffffff)
			break;
	} while (retry--);


	return (val);
}

void
osl_pci_write_config(osl_t *osh, uint offset, uint size, uint val)
{
	uint retry = PCI_CFG_RETRY;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	
	ASSERT(size == 4);

	do {
		pci_write_config_dword(osh->pdev, offset, val);
		if (offset != PCI_BAR0_WIN)
			break;
		if (osl_pci_read_config(osh, offset, size) == val)
			break;
	} while (retry--);

}


uint
osl_pci_bus(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

	return ((struct pci_dev *)osh->pdev)->bus->number;
}


uint
osl_pci_slot(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

	return PCI_SLOT(((struct pci_dev *)osh->pdev)->devfn);
}

static void
osl_pcmcia_attr(osl_t *osh, uint offset, char *buf, int size, bool write)
{
}

void
osl_pcmcia_read_attr(osl_t *osh, uint offset, void *buf, int size)
{
	osl_pcmcia_attr(osh, offset, (char *) buf, size, FALSE);
}

void
osl_pcmcia_write_attr(osl_t *osh, uint offset, void *buf, int size)
{
	osl_pcmcia_attr(osh, offset, (char *) buf, size, TRUE);
}



void*
osl_malloc(osl_t *osh, uint size)
{
	void *addr;

	
	if (osh)
		ASSERT(osh->magic == OS_HANDLE_MAGIC);

#ifdef DHD_USE_STATIC_BUF
	if (bcm_static_buf)
	{
		int i = 0;
		if ((size >= PAGE_SIZE)&&(size <= STATIC_BUF_SIZE))
		{
			down(&bcm_static_buf->static_sem);
			
			for (i = 0; i < MAX_STATIC_BUF_NUM; i++)
			{
				if (bcm_static_buf->buf_use[i] == 0)
					break;
			}
			
			if (i == MAX_STATIC_BUF_NUM)
			{
				up(&bcm_static_buf->static_sem);
				OSL_MSG_INFO(("osl_malloc: all static buff in use!\n"));
				goto original;
			}
			
			bcm_static_buf->buf_use[i] = 1;
			up(&bcm_static_buf->static_sem);

			bzero(bcm_static_buf->buf_ptr+STATIC_BUF_SIZE*i, size);
			if (osh)
				osh->malloced += size;

			return ((void *)(bcm_static_buf->buf_ptr+STATIC_BUF_SIZE*i));
		}
	}
original:
#endif 

	if ((addr = kmalloc(size, GFP_ATOMIC)) == NULL) {
		OSL_MSG_ERROR(("osl_malloc: GFP_ATOMIC failed, trying GFP_KERNEL\n"));
        if ((addr = kmalloc(size, GFP_KERNEL)) == NULL) {
			OSL_MSG_ERROR(("osl_malloc: GFP_KERNEL failed also\n"));
            if (osh)
                osh->failed++;
            return (NULL);
        }
	}
	if (osh)
		osh->malloced += size;

	return (addr);
}

void
osl_mfree(osl_t *osh, void *addr, uint size)
{
#ifdef DHD_USE_STATIC_BUF
	if (bcm_static_buf)
	{
		if ((addr > (void *)bcm_static_buf) && ((unsigned char *)addr
			<= ((unsigned char *)bcm_static_buf + STATIC_BUF_TOTAL_LEN)))
		{
			int buf_idx = 0;
			
			buf_idx = ((unsigned char *)addr - bcm_static_buf->buf_ptr)/STATIC_BUF_SIZE;
			
			down(&bcm_static_buf->static_sem);
			bcm_static_buf->buf_use[buf_idx] = 0;
			up(&bcm_static_buf->static_sem);

			if (osh) {
				ASSERT(osh->magic == OS_HANDLE_MAGIC);
				osh->malloced -= size;
			}
			return;
		}
	}
#endif 
	if (osh) {
		ASSERT(osh->magic == OS_HANDLE_MAGIC);
		osh->malloced -= size;
	}
	kfree(addr);
}

uint
osl_malloced(osl_t *osh)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	return (osh->malloced);
}

uint
osl_malloc_failed(osl_t *osh)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	return (osh->failed);
}

void*
osl_dma_alloc_consistent(osl_t *osh, uint size, ulong *pap)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	return (pci_alloc_consistent(osh->pdev, size, (dma_addr_t*)pap));
}

void
osl_dma_free_consistent(osl_t *osh, void *va, uint size, ulong pa)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	pci_free_consistent(osh->pdev, size, va, (dma_addr_t)pa);
}

uint
osl_dma_map(osl_t *osh, void *va, uint size, int direction)
{
	int dir;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	dir = (direction == DMA_TX)? PCI_DMA_TODEVICE: PCI_DMA_FROMDEVICE;
	return (pci_map_single(osh->pdev, va, size, dir));
}

void
osl_dma_unmap(osl_t *osh, uint pa, uint size, int direction)
{
	int dir;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	dir = (direction == DMA_TX)? PCI_DMA_TODEVICE: PCI_DMA_FROMDEVICE;
	pci_unmap_single(osh->pdev, (uint32)pa, size, dir);
}


void
osl_delay(uint usec)
{
	uint d;

	while (usec > 0) {
		d = MIN(usec, 1000);
		udelay(d);
		usec -= d;
	}
}



void *
osl_pktdup(osl_t *osh, void *skb)
{
	void * p;

	if ((p = skb_clone((struct sk_buff*)skb, GFP_ATOMIC)) == NULL)
		return NULL;

	
	if (osh->pub.pkttag)
		bzero((void*)((struct sk_buff *)p)->cb, OSL_PKTTAG_SZ);

	
	osh->pub.pktalloced++;
	return (p);
}
