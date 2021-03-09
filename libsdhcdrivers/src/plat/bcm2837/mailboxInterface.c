/**
 * Copyright (C) 2019, Hensoldt Cyber GmbH
 */

#include <string.h>

#include <sdhc/plat/mailbox.h>
#include <sdhc/plat/mailboxInterface.h>
#include <sdhc/plat/environment.h>
#include "../../services.h"

/* Defines ----------------------------------------------------------------*/
/*
	See: https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
*/
#define VALUE_LENGTH_RESPONSE	(1 << 31)

//------------------------------------------------------------------------------
// bcm2835.h
#define GPU_CACHED_BASE		0x40000000
#define GPU_UNCACHED_BASE	0xC0000000

#define GPU_MEM_BASE	GPU_UNCACHED_BASE

// Convert ARM address to GPU bus address (does also work for aliases)
#define BUS_ADDRESS(addr)	(((addr) & ~0xC0000000) | GPU_MEM_BASE)
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// synchronize.h
#define DataMemBarrier() 	__asm volatile ("dmb" ::: "memory")
//------------------------------------------------------------------------------



//	the actual mailbox base address is 0x3F00B880 but when mapping the
//	base address has to be PAGE_SIZE alligned
#define MAILBOX_BASE(addr)      ((unsigned long)addr + 0x880)
#define MAILBOX0_READ           0x00
#define MAILBOX0_STATUS         0x18
#define MAILBOX1_WRITE          0x20
#define MAILBOX1_STATUS         0x38

#define MAILBOX_STATUS_EMPTY    0x40000000
#define MAILBOX_STATUS_FULL     0x80000000

#define DMA_PAGE_SIZE	4096
#define DMA_ALIGNEMENT 	4096

/* Private function prototypes ----------------------------------------------------------------*/
static uint32_t read32(uint32_t nAddress);
static void write32(uint32_t nAddress, uint32_t nValue);
void MailboxFlush(mailbox_dev_t mbox);
unsigned MailboxRead(mailbox_dev_t mbox, unsigned channel);
void MailboxWrite(mailbox_dev_t mbox, unsigned channel, unsigned nData);
unsigned MailboxWriteRead(mailbox_dev_t mbox, unsigned channel, unsigned nData);

/* Public functions ----------------------------------------------------------------*/
bool MailboxInterface_getTag(mailbox_dev_t mbox, uint32_t nTagId, void *pTag, unsigned nTagSize, unsigned nRequestParmSize)
{
	unsigned nBufferSize = sizeof (MailboxInterface_PropertyBuffer) + nTagSize + sizeof (uint32_t);

	// MailboxInterface_PropertyBuffer *pBuffer = (MailboxInterface_PropertyBuffer *)mbox->dalloc->dma_alloc_fn(mbox->dalloc->cookie, DMA_PAGE_SIZE, DMA_ALIGNEMENT,0,PS_MEM_NORMAL);
	MailboxInterface_PropertyBuffer *pBuffer = (MailboxInterface_PropertyBuffer *)dma_alloc(DMA_PAGE_SIZE, DMA_ALIGNEMENT);
	if(pBuffer == NULL)
	{
		ZF_LOGE("DMA allocation failed.");
		return false;
	}
	pBuffer->nBufferSize	= nBufferSize;
	pBuffer->nCode 			= CODE_REQUEST;

	memcpy (pBuffer->Tags, pTag, nTagSize);

	MailboxInterface_PropertyTag *pHeader = (MailboxInterface_PropertyTag *) pBuffer->Tags;

	pHeader->nTagId 		= nTagId;
	pHeader->nValueBufSize 	= nTagSize - sizeof (MailboxInterface_PropertyTag);
	pHeader->nValueLength 	= nRequestParmSize & ~VALUE_LENGTH_RESPONSE;

	uint32_t *pEndTag 	= (uint32_t *) (pBuffer->Tags + nTagSize);
	*pEndTag 			= PROPTAG_END;
	uintptr_t physAddr 		= dma_getPhysicalAddr(pBuffer);

	uint32_t nBufferAddress = BUS_ADDRESS ((uint32_t) physAddr);

	if (MailboxWriteRead (mbox, MAILBOX_CHANNEL, nBufferAddress) != nBufferAddress)
	{
		// mbox->dalloc->dma_free_fn(NULL,pBuffer,nBufferSize);
        dma_free(pBuffer, DMA_ALIGNEMENT);
		return false;
	}

	DataMemBarrier ();

	if (pBuffer->nCode != CODE_RESPONSE_SUCCESS)
	{
		// mbox->dalloc->dma_free_fn(NULL,pBuffer,nBufferSize);
        dma_free(pBuffer, DMA_ALIGNEMENT);
		return false;
	}

	if (!(pHeader->nValueLength & VALUE_LENGTH_RESPONSE))
	{
		// mbox->dalloc->dma_free_fn(NULL,pBuffer,nBufferSize);
        dma_free(pBuffer, DMA_ALIGNEMENT);
		return false;
	}

	pHeader->nValueLength &= ~VALUE_LENGTH_RESPONSE;
	if (pHeader->nValueLength == 0)
	{
		// mbox->dalloc->dma_free_fn(NULL,pBuffer,nBufferSize);
        dma_free(pBuffer, DMA_ALIGNEMENT);
		return false;
	}

	memcpy (pTag, pBuffer->Tags, nTagSize);

	// mbox->dalloc->dma_free_fn(NULL,pBuffer,nBufferSize);
	dma_free(pBuffer, DMA_ALIGNEMENT);

	return true;
}

/* Private functions ----------------------------------------------------------------*/
unsigned MailboxRead(mailbox_dev_t mbox, unsigned channel)
{
	unsigned nResult;
	do
	{
		while (read32 (MAILBOX_BASE(mbox->base) + MAILBOX0_STATUS) & MAILBOX_STATUS_EMPTY)
		{
			// do nothing
		}
		nResult = read32 (MAILBOX_BASE(mbox->base) + MAILBOX0_READ);
	}
	while ((nResult & 0xF) != MAILBOX_CHANNEL);		// channel number is in the lower 4 bits
	return nResult & ~0xF;
}

void MailboxWrite(mailbox_dev_t mbox, unsigned channel, unsigned nData)
{
	while (read32 (MAILBOX_BASE(mbox->base) + MAILBOX1_STATUS) & MAILBOX_STATUS_FULL)
	{
		// do nothing
	}
	write32 (MAILBOX_BASE(mbox->base) + MAILBOX1_WRITE, channel | nData);	// channel number is in the lower 4 bits
}

unsigned MailboxWriteRead(mailbox_dev_t mbox, unsigned channel, unsigned nData)
{
	DataMemBarrier();

	MailboxFlush(mbox);

	MailboxWrite(mbox, channel, nData);

	unsigned nResult = MailboxRead(mbox, channel);

	DataMemBarrier();

	return nResult;
}

void MailboxFlush(mailbox_dev_t mbox)
{
	while (!(read32 (MAILBOX_BASE(mbox->base) + MAILBOX0_STATUS) & MAILBOX_STATUS_EMPTY))
	{
		read32 (MAILBOX_BASE(mbox->base) + MAILBOX0_READ);

		udelay(20000);
	}
}

static uint32_t read32(uint32_t nAddress)
{
	return *(volatile uint32_t *) nAddress;
}

static void write32(uint32_t nAddress, uint32_t nValue)
{
	*(volatile uint32_t *) nAddress = nValue;
}