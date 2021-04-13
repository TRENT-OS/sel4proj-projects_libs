/**
 * Copyright (C) 2019, Hensoldt Cyber GmbH
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <sdhc/plat/sdio.h>
#include <sdhc/plat/environment.h>
#include <sdhc/plat/mailboxInterface.h>
#include <sdhc/plat/mailbox.h>

// #include <camkes.h>
#include <camkes/dma.h>

/* Environment functions -------------------------------------------------------------*/
void* dma_alloc (unsigned nSize, unsigned alignement)
{
    // we are setting cached to false to allocate non-cached DMA memory for the
    // NIC driver
    return camkes_dma_alloc(nSize, alignement, false);
}

void dma_free (void* pBlock, unsigned alignement)
{
    camkes_dma_free(pBlock, alignement);
}

uintptr_t dma_getPhysicalAddr(void* ptr)
{
    return camkes_dma_get_paddr(ptr);
}

int SetPowerStateOn (mailbox_dev_t mbox, unsigned nDeviceId)
{
    PropertyTagPowerState PowerState;
    PowerState.nDeviceId = nDeviceId;
    PowerState.nState = POWER_STATE_ON | POWER_STATE_WAIT;

    if (!MailboxInterface_getTag (mbox, PROPTAG_SET_POWER_STATE, &PowerState,
                                  sizeof PowerState, 8)
        || (PowerState.nState & POWER_STATE_NO_DEVICE)
        || !(PowerState.nState & POWER_STATE_ON))
    {
        ZF_LOGE("Failed to set power state on!");
        return 0;
    }

    return 1;
}

unsigned GetClockState(mailbox_dev_t mbox, uint32_t nClockId)
{
	PropertyTagClockState TagClockState;
	TagClockState.nClockId = nClockId;
	if (MailboxInterface_getTag (mbox, PROPTAG_GET_CLOCK_STATE, &TagClockState, sizeof TagClockState, 4))
	{
		return TagClockState.nState;
	}

	return 0;
}

unsigned SetClockState(mailbox_dev_t mbox, uint32_t nClockId, uint32_t state)
{
	PropertyTagClockState TagClockState;
	TagClockState.nClockId = nClockId;
	TagClockState.nState   = state;
	if (MailboxInterface_getTag (mbox, PROPTAG_SET_CLOCK_STATE, &TagClockState, sizeof TagClockState, 8))
	{
		return TagClockState.nState;
	}

	return 0;
}

// Get the current base clock rate in Hz
int GetBaseClock (mailbox_dev_t mbox)
{
	PropertyTagClockRate TagClockRate;
	TagClockRate.nClockId = CLOCK_ID_EMMC;

	if (!MailboxInterface_getTag (mbox, PROPTAG_GET_CLOCK_RATE, &TagClockRate, sizeof TagClockRate, 4))
	{
		ZF_LOGE ("Cannot get clock rate");
		TagClockRate.nRate = 0;
	}

	return TagClockRate.nRate;
}

unsigned GetClockRate (mailbox_dev_t mbox, uint32_t nClockId)
{
    PropertyTagClockRate TagClockRate;
	TagClockRate.nClockId = nClockId;
	if (MailboxInterface_getTag (mbox, PROPTAG_GET_CLOCK_RATE, &TagClockRate, sizeof TagClockRate, 4))
	{
		return TagClockRate.nRate;
	}

	// if clock rate can not be requested, use a default rate
	unsigned nResult = 0;

	switch (nClockId)
	{
	case CLOCK_ID_EMMC:
		nResult = 100000000;
		break;

	case CLOCK_ID_UART:
		nResult = 48000000;
		break;

	case CLOCK_ID_CORE:
		if (RASPPI < 3)
		{
			nResult = 250000000;
		}
		else
		{
			nResult = 300000000;		// TODO
		}
		break;

	default:
		assert (0);
		break;
	}

	return nResult;
}

unsigned SetClockRate (mailbox_dev_t mbox, uint32_t nClockId, uint32_t rate)
{
    PropertyTagClockRate TagClockRate;
	TagClockRate.nClockId = nClockId;
	TagClockRate.nRate 	  = rate;
	TagClockRate.skip_setting_turbo = 1;
	if (MailboxInterface_getTag (mbox, PROPTAG_SET_CLOCK_RATE, &TagClockRate, sizeof TagClockRate, 12))
	{
		return TagClockRate.nRate;
	}

	// // if clock rate can not be requested, use a default rate
	// unsigned nResult = 0;

	// switch (nClockId)
	// {
	// case CLOCK_ID_EMMC:
	// 	nResult = 100000000;
	// 	break;

	// case CLOCK_ID_UART:
	// 	nResult = 48000000;
	// 	break;

	// case CLOCK_ID_CORE:
	// 	if (RASPPI < 3)
	// 	{
	// 		nResult = 250000000;
	// 	}
	// 	else
	// 	{
	// 		nResult = 300000000;		// TODO
	// 	}
	// 	break;

	// default:
	// 	assert (0);
	// 	break;
	// }

	// return nResult;

	return 0;
}

unsigned GetMaxClockRate (mailbox_dev_t mbox, uint32_t nClockId)
{
    PropertyTagClockRate TagClockRate;
	TagClockRate.nClockId = nClockId;
	if (MailboxInterface_getTag (mbox, PROPTAG_GET_MAX_CLOCK_RATE, &TagClockRate, sizeof TagClockRate, 4))
	{
		return TagClockRate.nRate;
	}

	return 0;
}

unsigned GetMinClockRate (mailbox_dev_t mbox, uint32_t nClockId)
{
    PropertyTagClockRate TagClockRate;
	TagClockRate.nClockId = nClockId;
	if (MailboxInterface_getTag (mbox, PROPTAG_GET_MIN_CLOCK_RATE, &TagClockRate, sizeof TagClockRate, 4))
	{
		return TagClockRate.nRate;
	}

	return 0;
}

int SetSDHostClock (mailbox_dev_t mbox, uint32_t *msg, size_t length)
{
	struct
	{
        MailboxInterface_PropertyTag	Tag;
		uint32_t		msg[length];
	}
	PACKED SetSDHOSTClock;

	// memcpy (SetSDHOSTClock.msg, msg, sizeof *msg);
	memcpy (SetSDHOSTClock.msg, msg, length * 4);

#define PROPTAG_SET_SDHOST_CLOCK 0x00038042
	if (!MailboxInterface_getTag (mbox, PROPTAG_SET_SDHOST_CLOCK, &SetSDHOSTClock, sizeof SetSDHOSTClock, 3*4))
	{
		ZF_LOGE("MailboxInterface_getTag() failed.");
		return 0;
	}

	// memcpy (msg, SetSDHOSTClock.msg, sizeof *msg);
	memcpy (msg, SetSDHOSTClock.msg, length * 4);

    return 1;
}

int GetMachineModel (mailbox_dev_t mbox)
{
    PropertyTagBoardModel TagBoardModel;
	if (!MailboxInterface_getTag (mbox, PROPTAG_GET_BOARD_MODEL, &TagBoardModel, sizeof TagBoardModel, 0))
	{
		ZF_LOGE ("Cannot get board model.");
        TagBoardModel.nBoardModel = MachineModel3BPlus;
	}

    return TagBoardModel.nBoardModel;
}

// Set the clock dividers to generate a target value
uint32_t GetClockDivider (uint32_t base_clock, uint32_t target_rate)
{
	// TODO: implement use of preset value registers

	uint32_t targetted_divisor = 1;
	if (target_rate <= base_clock)
	{
		targetted_divisor = base_clock / target_rate;
		if (base_clock % target_rate)
		{
			targetted_divisor--;
		}
	}

	// Decide on the clock mode to use
	// Currently only 10-bit divided clock mode is supported

	// HCI version 3 or greater supports 10-bit divided clock mode
	// This requires a power-of-two divider

	// Find the first bit set
	int divisor = -1;
	for (int first_bit = 31; first_bit >= 0; first_bit--)
	{
		uint32_t bit_test = (1 << first_bit);
		if (targetted_divisor & bit_test)
		{
			divisor = first_bit;
			targetted_divisor &= ~bit_test;
			if (targetted_divisor)
			{
				// The divisor is not a power-of-two, increase it
				divisor++;
			}

			break;
		}
	}

	if(divisor == -1)
	{
		divisor = 31;
	}
	if(divisor >= 32)
	{
		divisor = 31;
	}

	if(divisor != 0)
	{
		divisor = (1 << (divisor - 1));
	}

	if(divisor >= 0x400)
	{
		divisor = 0x3ff;
	}

	uint32_t freq_select = divisor & 0xff;
	uint32_t upper_bits = (divisor >> 8) & 0x3;
	uint32_t ret = (freq_select << 8) | (upper_bits << 6) | (0 << 5);

	return ret;
}
