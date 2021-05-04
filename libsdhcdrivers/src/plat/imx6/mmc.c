/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <mmc.h>

uint32_t mmc_get_voltage(mmc_card_t card UNUSED)
{
    uint32_t voltage = MMC_VDD_29_30 | MMC_VDD_30_31;
    if (host_is_voltage_compatible(card, 3300) && (card->ocr & voltage)) {
        /* Voltage compatible */
        voltage |= (1 << 30);
        voltage |= (1 << 25);
        voltage |= (1 << 24);
    }
    return voltage;
}