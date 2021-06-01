/*
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <mmc.h>

uint32_t mmc_get_voltage(mmc_card_t card UNUSED)
{
    return (1 << 30)
         | MMC_VDD_33_34
         | MMC_VDD_32_33
         | MMC_VDD_31_32
         | MMC_VDD_30_31
         | MMC_VDD_29_30;
}