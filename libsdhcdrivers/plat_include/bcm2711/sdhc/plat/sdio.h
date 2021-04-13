/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#pragma once

#define SDHC1_PADDR 0xfe340000
#define SDHC1_SIZE  0x1000
#define SDHC1_IRQ   158

#define RASPPI          4

// #define USE_SDHOST

enum sdio_id {
    SDHC1 = 1,
    NSDHC,
    SDHC_DEFAULT = SDHC1
};
