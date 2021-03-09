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

#include "../../sdhc.h"
#include "../../services.h"
#include <sdhc/plat/environment.h>
#include <sdhc/plat/mailbox.h>

static const int
_sdhc_irq_table[] = {
    [SDHC1] = SDHC1_IRQ
};

enum sdio_id sdio_default_id(void)
{
    return SDHC_DEFAULT;
}

int mailbox_init(enum sdio_id id, ps_io_ops_t *io_ops, sdio_host_dev_t *dev)
{
    void *iobase;
    int ret;
    iobase = RESOURCE(io_ops, MAILBOX);
    if (iobase == NULL) {
        ZF_LOGE("Failed to map device memory for mailbox");
        return -1;
    }

    ret = mbox_init(iobase, io_ops, dev);
    if (ret) {
        ZF_LOGE("Failed to initialise mailbox");
        return -1;
    }
    return 0;
}

int sdio_init(enum sdio_id id, ps_io_ops_t *io_ops, sdio_host_dev_t *dev)
{
    //setting default CPU frequency to 200MHz for the RPi3
    ps_cpufreq_hint(200000000);

    void *iobase;
    int ret;
    ret = mailbox_init(id,io_ops,dev);
    if(ret < 0)
    {
        ZF_LOGE("Failed to initialize mailbox.");
        return -1;
    }
    switch (id) {
    case SDHC1:
        iobase = RESOURCE(io_ops, SDHC1);
        break;
    default:
        return -1;
    }
    if (iobase == NULL) {
        ZF_LOGE("Failed to map device memory for SDHC");
        return -1;
    }

    ret = sdhc_init(iobase, _sdhc_irq_table, NSDHC, io_ops, dev);
    if (ret) {
        ZF_LOGE("Failed to initialise SDHC");
        return -1;
    }
    return 0;
}


