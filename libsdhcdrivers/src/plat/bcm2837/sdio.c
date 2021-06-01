/*
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sdhc.h>
#include <services.h>

gpio_sys_t  gpio_sys;
gpio_t      gpio;

static const int
_sdhc_irq_table[] = {
    [SDHC1] = SDHC1_IRQ
};

enum sdio_id sdio_default_id(void)
{
    return SDHC_DEFAULT;
}

int sdio_init(enum sdio_id id, ps_io_ops_t *io_ops, sdio_host_dev_t *dev)
{
    //provide cpu frequency to libplatsupport delay module, otherwise it complains
    ps_cpufreq_hint(200000000);

    void *iobase;
    int ret;

    //gpio initialization
    ret = gpio_sys_init(io_ops,&gpio_sys);
    if (ret)
    {
        ZF_LOGE("gpio_sys_init() failed: rslt = %i", ret);
        return -1;
    }

    for (unsigned i = 0; i < 6; i++)
    {
        gpio_sys.init(&gpio_sys,34 + i,0,&gpio);
        bcm2837_gpio_fsel(&gpio,BCM2837_GPIO_FSEL_INPT);
        gpio_sys.init(&gpio_sys,48 + i,0,&gpio);
        bcm2837_gpio_fsel(&gpio,BCM2837_GPIO_FSEL_INPT);
    }
    ZF_LOGD("Routing SD to Arasan.");
    for (unsigned i = 0; i < 6; i++)
    {
        gpio_sys.init(&gpio_sys,48 + i,0,&gpio);
        bcm2837_gpio_fsel(&gpio,BCM2837_GPIO_FSEL_ALT3);
    }

    //mailbox initialization
    ret = mailbox_init(io_ops,&mbox);
    if(ret < 0)
    {
        ZF_LOGE("Failed to initialize mailbox.");
        return -1;
    }

    if(!bcm2837_set_power_state_on (&mbox,DEVICE_ID_SD_CARD))
	{
		ZF_LOGE("BCM2708 controller did not power on successfully");
		return -1;
	}

    //sdio initialization
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
