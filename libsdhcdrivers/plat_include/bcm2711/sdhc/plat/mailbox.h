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

#include <platsupport/io.h>
#include <sdhc/sdio.h>

#define MAILBOX_PADDR 0xfe00b000
#define MAILBOX_SIZE  0x1000

// SD Clock Frequencies (in Hz)
#define SD_CLOCK_ID         400000
#define SD_CLOCK_NORMAL     25000000
#define SD_CLOCK_HIGH       50000000
#define SD_CLOCK_100        100000000
#define SD_CLOCK_208        208000000

struct mailbox {
    /* Device data */
    volatile void *base;
    /* DMA allocator */
    ps_dma_man_t *dalloc;
};
typedef struct mailbox *mailbox_dev_t;
