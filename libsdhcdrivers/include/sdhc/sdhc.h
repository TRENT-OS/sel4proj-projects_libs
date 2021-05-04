/*
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <sdhc/sdio.h>

typedef enum {
    CLOCK_INITIAL = 0,
    CLOCK_OPERATIONAL
} clock_mode;

int sdhc_set_clock(volatile void *base_addr, clock_mode clk_mode);