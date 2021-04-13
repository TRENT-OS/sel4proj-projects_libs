/**
 * Copyright (C) 2019, Hensoldt Cyber GmbH
 */

#pragma once

/* Includes -------------------------------------------------------------*/
#include <sdhc/plat/mailboxInterface.h>
#include <sdhc/plat/mailbox.h>

/* Defines -------------------------------------------------------------*/
#define POWER_STATE_OFF		(0 << 0)
#define POWER_STATE_ON		(1 << 0)
#define POWER_STATE_WAIT	(1 << 1)
#define POWER_STATE_NO_DEVICE	(1 << 1)	// in response
#define DEVICE_ID_SD_CARD	0
#define DEVICE_ID_USB_HCD	3		// for SetPowerStateOn()
/* Type declarations -------------------------------------------------------------*/
typedef struct PropertyTagPowerState
{
	MailboxInterface_PropertyTag	Tag;
	uint32_t		nDeviceId;
	uint32_t		nState;
}
PropertyTagPowerState;

typedef struct PropertyTagClockState
{
	MailboxInterface_PropertyTag	Tag;
	uint32_t		nClockId;
	uint32_t		nState;
}
PropertyTagClockState;

typedef struct PropertyTagMACAddress
{
	MailboxInterface_PropertyTag	Tag;
	uint8_t		Address[6];
	uint8_t		Padding[2];
}
PropertyTagMACAddress;

typedef struct PropertyTagClockRate
{
	MailboxInterface_PropertyTag	Tag;
	uint32_t nClockId;
	#define CLOCK_ID_EMMC		1
	#define CLOCK_ID_UART		2
	#define CLOCK_ID_ARM		3
	#define CLOCK_ID_CORE		4
	#define CLOCK_ID_EMMC2		12
	uint32_t nRate;			// Hz
	uint32_t skip_setting_turbo;
}
PropertyTagClockRate;

typedef struct PropertyTagBoardModel
{
	MailboxInterface_PropertyTag	Tag;
	uint32_t nBoardModel;
}
PropertyTagBoardModel;

typedef enum TMachineModel
{
	MachineModelA,
	MachineModelBRelease1MB256,
	MachineModelBRelease2MB256,
	MachineModelBRelease2MB512,
	MachineModelAPlus,
	MachineModelBPlus,
	MachineModelZero,
	MachineModelZeroW,
	MachineModel2B,
	MachineModel3B,
	MachineModel3APlus,
	MachineModel3BPlus,
	MachineModelCM,
	MachineModelCM3,
	MachineModelCM3Plus,
	MachineModel4B,
	MachineModelUnknown
} TMachineModel;

void* dma_alloc (unsigned nSize, unsigned alignement);

void dma_free (void* pBlock, unsigned alignement);

uintptr_t dma_getPhysicalAddr(void* ptr);

int SetPowerStateOn (mailbox_dev_t mbox, unsigned nDeviceId);

// Get the current base clock rate in Hz
int GetBaseClock (mailbox_dev_t mbox);

unsigned GetClockRate (mailbox_dev_t mbox, uint32_t nClockId);

unsigned SetClockRate (mailbox_dev_t mbox, uint32_t nClockId, uint32_t rate);

int SetSDHostClock (mailbox_dev_t mbox, uint32_t *msg, size_t length);

int GetMachineModel (mailbox_dev_t mbox);

uint32_t GetClockDivider (uint32_t base_clock, uint32_t target_rate);

unsigned GetClockState(mailbox_dev_t mbox, uint32_t nClockId);

unsigned SetClockState(mailbox_dev_t mbox, uint32_t nClockId, uint32_t state);

unsigned GetMaxClockRate (mailbox_dev_t mbox, uint32_t nClockId);

unsigned GetMinClockRate (mailbox_dev_t mbox, uint32_t nClockId);