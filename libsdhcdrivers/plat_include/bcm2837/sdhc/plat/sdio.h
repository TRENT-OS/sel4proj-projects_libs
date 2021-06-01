/*
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <platsupport/gpio.h>
#include <platsupport/plat/gpio.h>
#include <platsupport/plat/mailbox.h>
#include <platsupport/plat/mailbox_util.h>

#define SDHC1_PADDR         0x3f300000
#define SDHC1_SIZE          0x1000
#define SDHC1_IRQ           126

#define MAILBOX_PADDR       0x3f00b000
#define MAILBOX_SIZE        0x1000

enum sdio_id {
    SDHC1 = 1,
    NSDHC,
    SDHC_DEFAULT = SDHC1
};

mailbox_t           mbox;