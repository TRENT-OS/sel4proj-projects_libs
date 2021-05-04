/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sdhc.h>

static void sdhc_enable_clock(volatile void *base_addr)
{
    uint32_t val;

    val = readl(base_addr + SYS_CTRL);
    val |= SYS_CTRL_CLK_INT_EN;
    writel(val, base_addr + SYS_CTRL);

    do {
        val = readl(base_addr + SYS_CTRL);
    } while (!(val & SYS_CTRL_CLK_INT_STABLE));

    val |= SYS_CTRL_CLK_CARD_EN;
    writel(val, base_addr + SYS_CTRL);

    return;
}

/* Set the clock divider and timeout */
static int sdhc_set_clock_div(
    volatile void *base_addr,
    divisor dvs_div,
    sdclk_frequency_select sdclks_div,
    data_timeout_counter_val dtocv)
{
    /* make sure the clock state is stable. */
    if (readl(base_addr + PRES_STATE) & SDHC_PRES_STATE_SDSTB) {
        uint32_t val = readl(base_addr + SYS_CTRL);

        /* The SDCLK bit varies with Data Rate Mode. */
        if (readl(base_addr + MIX_CTRL) & MIX_CTRL_DDR_EN) {
            val &= ~(SYS_CTRL_SDCLKS_MASK << SYS_CTRL_SDCLKS_SHF);
            val |= ((sdclks_div >> 1) << SYS_CTRL_SDCLKS_SHF);

        } else {
            val &= ~(SYS_CTRL_SDCLKS_MASK << SYS_CTRL_SDCLKS_SHF);
            val |= (sdclks_div << SYS_CTRL_SDCLKS_SHF);
        }
        val &= ~(SYS_CTRL_DVS_MASK << SYS_CTRL_DVS_SHF);
        val |= (dvs_div << SYS_CTRL_DVS_SHF);

        /* Set data timeout value */
        val |= (dtocv << SYS_CTRL_DTOCV_SHF);
        writel(val, base_addr + SYS_CTRL);
    } else {
        ZF_LOGE("The clock is unstable, unable to change it!");
        return -1;
    }

    return 0;
}

int sdhc_set_clock(volatile void *base_addr, clock_mode clk_mode)
{
    int rslt = -1;

    const bool isClkEnabled = readl(base_addr + SYS_CTRL) & SYS_CTRL_CLK_INT_EN;
    if (!isClkEnabled) {
        sdhc_enable_clock(base_addr);
    }

    /* TODO: Relate the clock rate settings to the actual capabilities of the
    * card and the host controller. The conservative settings chosen should
    * work with most setups, but this is not an ideal solution. According to
    * the RM, the default freq. of the base clock should be at around 200MHz.
    */
    switch (clk_mode) {
    case CLOCK_INITIAL:
        /* Divide the base clock by 512 */
        rslt = sdhc_set_clock_div(base_addr, DIV_16, PRESCALER_32, SDCLK_TIMES_2_POW_14);
        break;
    case CLOCK_OPERATIONAL:
        /* Divide the base clock by 8 */
        rslt = sdhc_set_clock_div(base_addr, DIV_4, PRESCALER_2, SDCLK_TIMES_2_POW_29);
        break;
    default:
        ZF_LOGE("Unsupported clock mode setting");
        rslt = -1;
        break;
    }

    if (rslt < 0) {
        ZF_LOGE("Failed to change the clock settings");
    }

    return rslt;
}