/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "sdhc.h"

#include <autoconf.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "services.h"
#include "mmc.h"

static inline sdhc_dev_t sdio_get_sdhc(sdio_host_dev_t *sdio)
{
    return (sdhc_dev_t)sdio->priv;
}

/** Print uSDHC registers. */
UNUSED static void print_sdhc_regs(struct sdhc *host)
{
    int i;
    for (i = DS_ADDR; i <= HOST_VERSION; i += 0x4) {
        ZF_LOGD("%x: %X", i, readl(host->base + i));
    }
}

static inline enum dma_mode get_dma_mode(struct sdhc *host, struct mmc_cmd *cmd)
{
    if (cmd->data == NULL) {
        return DMA_MODE_NONE;
    }
    if (cmd->data->pbuf == 0) {
        return DMA_MODE_NONE;
    }
    /* Currently only SDMA supported */
    return DMA_MODE_SDMA;
}

static inline int cap_sdma_supported(struct sdhc *host)
{
    uint32_t v;
    v = readl(host->base + HOST_CTRL_CAP);
    return !!(v & HOST_CTRL_CAP_DMAS);
}

static inline int cap_max_buffer_size(struct sdhc *host)
{
    uint32_t v;
    v = readl(host->base + HOST_CTRL_CAP);
    v = ((v >> HOST_CTRL_CAP_MBL_SHF) & HOST_CTRL_CAP_MBL_MASK);
    return 512 << v;
}

static int sdhc_next_cmd(sdhc_dev_t host)
{
    struct mmc_cmd *cmd = host->cmd_list_head;
    uint32_t val;
    uint32_t mix_ctrl;

    /* Enable IRQs */
    val = (INT_STATUS_ADMAE | INT_STATUS_OVRCURE | INT_STATUS_DEBE
           | INT_STATUS_DCE   | INT_STATUS_DTOE    | INT_STATUS_CRM
           | INT_STATUS_CINS  | INT_STATUS_CIE     | INT_STATUS_CEBE
           | INT_STATUS_CCE   | INT_STATUS_CTOE    | INT_STATUS_TC
           | INT_STATUS_CC);
    if (get_dma_mode(host, cmd) == DMA_MODE_NONE) {
        val |= INT_STATUS_BRR | INT_STATUS_BWR;
    }
    writel(val, host->base + INT_STATUS_EN);

    /* Check if the Host is ready for transit. */
    while (readl(host->base + PRES_STATE) & (SDHC_PRES_STATE_CIHB | SDHC_PRES_STATE_CDIHB));
    while (readl(host->base + PRES_STATE) & SDHC_PRES_STATE_DLA);

    /* Two commands need to have at least 8 clock cycles in between.
     * Lets assume that the hcd will enforce this. */
    // The bcm2837 platform needs this delay.
    udelay(1000);

    /* Write to the argument register. */
    ZF_LOGD("CMD: %d with arg %x ", cmd->index, cmd->arg);
    writel(cmd->arg, host->base + CMD_ARG);

    if (cmd->data) {
        /* Use the default timeout. */
        val = readl(host->base + SYS_CTRL);
        val &= ~(0xffUL << 16);
        val |= 0xE << 16;
        writel(val, host->base + SYS_CTRL);

        /* Set the DMA boundary. */
        val = (cmd->data->block_size & BLK_ATT_BLKSIZE_MASK);
        val |= (cmd->data->blocks << BLK_ATT_BLKCNT_SHF);
        writel(val, host->base + BLK_ATT);

        /* Set watermark level */
        val = cmd->data->block_size / 4;
        if (val > 0x80) {
            val = 0x80;
        }
        if (cmd->index == MMC_READ_SINGLE_BLOCK) {
            val = (val << WTMK_LVL_RD_WML_SHF);
        } else {
            val = (val << WTMK_LVL_WR_WML_SHF);
        }
        writel(val, host->base + WTMK_LVL);

        /* Set Mixer Control */
        mix_ctrl = MIX_CTRL_BCEN;
        if (cmd->data->blocks > 1) {
            mix_ctrl |= MIX_CTRL_MSBSEL;
        }
        if (cmd->index == MMC_READ_SINGLE_BLOCK) {
            mix_ctrl |= MIX_CTRL_DTDSEL;
        }

        /* Configure DMA */
        if (get_dma_mode(host, cmd) != DMA_MODE_NONE) {
            /* Enable DMA */
            mix_ctrl |= MIX_CTRL_DMAEN;
            /* Set DMA address */
            writel(cmd->data->pbuf, host->base + DS_ADDR);
        }
        /* Record the number of blocks to be sent */
        host->blocks_remaining = cmd->data->blocks;
    }

    /* The command should be MSB and the first two bits should be '00' */
    val = (cmd->index & CMD_XFR_TYP_CMDINX_MASK) << CMD_XFR_TYP_CMDINX_SHF;
    val &= ~(CMD_XFR_TYP_CMDTYP_MASK << CMD_XFR_TYP_CMDTYP_SHF);
    if (cmd->data) {
        if (host->version >= 2) {
            /* Some controllers implement MIX_CTRL as part of the XFR_TYP */
            val |= mix_ctrl;
        } else {
            writel(mix_ctrl, host->base + MIX_CTRL);
        }
    }

    /* Set response type */
    val &= ~CMD_XFR_TYP_CICEN;
    val &= ~CMD_XFR_TYP_CCCEN;
    val &= ~(CMD_XFR_TYP_RSPTYP_MASK << CMD_XFR_TYP_RSPTYP_SHF);
    switch (cmd->rsp_type) {
    case MMC_RSP_TYPE_R2:
        val |= (0x1 << CMD_XFR_TYP_RSPTYP_SHF);
        val |= CMD_XFR_TYP_CCCEN;
        break;
    case MMC_RSP_TYPE_R3:
    case MMC_RSP_TYPE_R4:
        val |= (0x2 << CMD_XFR_TYP_RSPTYP_SHF);
        break;
    case MMC_RSP_TYPE_R1:
    case MMC_RSP_TYPE_R5:
    case MMC_RSP_TYPE_R6:
        val |= (0x2 << CMD_XFR_TYP_RSPTYP_SHF);
        val |= CMD_XFR_TYP_CICEN;
        val |= CMD_XFR_TYP_CCCEN;
        break;
    case MMC_RSP_TYPE_R1b:
    case MMC_RSP_TYPE_R5b:
        val |= (0x3 << CMD_XFR_TYP_RSPTYP_SHF);
        val |= CMD_XFR_TYP_CICEN;
        val |= CMD_XFR_TYP_CCCEN;
        break;
    default:
        break;
    }

    if (cmd->data) {
        val |= CMD_XFR_TYP_DPSEL;
    }

    /* Issue the command. */
    writel(val, host->base + CMD_XFR_TYP);
    return 0;
}

/** Pass control to the devices IRQ handler
 * @param[in] sd_dev  The sdhc interface device that triggered
 *                    the interrupt event.
 */
static int sdhc_handle_irq(sdio_host_dev_t *sdio, int irq UNUSED)
{
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    struct mmc_cmd *cmd = host->cmd_list_head;
    uint32_t int_status;

    int_status = readl(host->base + INT_STATUS);
    if (!cmd) {
        /* Clear flags */
        writel(int_status, host->base + INT_STATUS);
        return 0;
    }
    /** Handle errors **/
    if (int_status & INT_STATUS_TNE) {
        ZF_LOGE("Tuning error");
    }
    if (int_status & INT_STATUS_OVRCURE) {
        ZF_LOGE("Bus overcurrent"); /* (exl. IMX6) */
    }
    if (int_status & INT_STATUS_ERR) {
        ZF_LOGE("CMD/DATA transfer error"); /* (exl. IMX6) */
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_AC12E) {
        ZF_LOGE("Auto CMD12 Error");
        cmd->complete = -1;
    }
    /** DMA errors **/
    if (int_status & INT_STATUS_DMAE) {
        ZF_LOGE("DMA Error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_ADMAE) {
        ZF_LOGE("ADMA error");       /*  (exl. IMX6) */
        cmd->complete = -1;
    }
    /** DATA errors **/
    if (int_status & INT_STATUS_DEBE) {
        ZF_LOGE("Data end bit error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_DCE) {
        ZF_LOGE("Data CRC error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_DTOE) {
        ZF_LOGE("Data transfer error");
        cmd->complete = -1;
    }
    /** CMD errors **/
    if (int_status & INT_STATUS_CIE) {
        ZF_LOGE("Command index error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CEBE) {
        ZF_LOGE("Command end bit error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CCE) {
        ZF_LOGE("Command CRC error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CTOE) {
        ZF_LOGE("CMD Timeout...");
        cmd->complete = -1;
    }

    if (int_status & INT_STATUS_TP) {
        ZF_LOGD("Tuning pass");
    }
    if (int_status & INT_STATUS_RTE) {
        ZF_LOGD("Retuning event");
    }
    if (int_status & INT_STATUS_CINT) {
        ZF_LOGD("Card interrupt");
    }
    if (int_status & INT_STATUS_CRM) {
        ZF_LOGD("Card removal");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CINS) {
        ZF_LOGD("Card insertion");
    }
    if (int_status & INT_STATUS_DINT) {
        ZF_LOGD("DMA interrupt");
    }
    if (int_status & INT_STATUS_BGE) {
        ZF_LOGD("Block gap event");
    }

    /* Command complete */
    if (int_status & INT_STATUS_CC) {
        /* Command complete */
        switch (cmd->rsp_type) {
        case MMC_RSP_TYPE_R2:
            cmd->response[0] = readl(host->base + CMD_RSP0);
            cmd->response[1] = readl(host->base + CMD_RSP1);
            cmd->response[2] = readl(host->base + CMD_RSP2);
            cmd->response[3] = readl(host->base + CMD_RSP3);
            break;
        case MMC_RSP_TYPE_R1b:
            if (cmd->index == MMC_STOP_TRANSMISSION) {
                cmd->response[3] = readl(host->base + CMD_RSP3);
            } else {
                cmd->response[0] = readl(host->base + CMD_RSP0);
            }
            break;
        case MMC_RSP_TYPE_NONE:
            break;
        default:
            cmd->response[0] = readl(host->base + CMD_RSP0);
        }

        /* If there is no data segment, the transfer is complete */
        if (cmd->data == NULL) {
            assert(cmd->complete == 0);
            cmd->complete = 1;
        }
    }
    /* DATA: Programmed IO handling */
    if (int_status & (INT_STATUS_BRR | INT_STATUS_BWR)) {
        volatile uint32_t *io_buf;
        uint32_t *usr_buf;
        assert(cmd->data);
        assert(cmd->data->vbuf);
        assert(cmd->complete == 0);
        if (host->blocks_remaining) {
            io_buf = (volatile uint32_t *)((void *)host->base + DATA_BUFF_ACC_PORT);
            usr_buf = (uint32_t *)cmd->data->vbuf;
            if (int_status & INT_STATUS_BRR) {
                /* Buffer Read Ready */
                int i;
                for (i = 0; i < cmd->data->block_size; i += sizeof(*usr_buf)) {
                    *usr_buf++ = *io_buf;
                }
            } else {
                /* Buffer Write Ready */
                int i;
                for (i = 0; i < cmd->data->block_size; i += sizeof(*usr_buf)) {
                    *io_buf = *usr_buf++;
                }
            }
            host->blocks_remaining--;
        }
    }
    /* Data complete */
    if (int_status & INT_STATUS_TC) {
        assert(cmd->complete == 0);
        cmd->complete = 1;
    }
    /* Clear flags */
    writel(int_status, host->base + INT_STATUS);

    /* If the transaction has finished */
    if (cmd != NULL && cmd->complete != 0) {
        if (cmd->next == NULL) {
            /* Shutdown */
            host->cmd_list_head = NULL;
            host->cmd_list_tail = &host->cmd_list_head;
        } else {
            /* Next */
            host->cmd_list_head = cmd->next;
            sdhc_next_cmd(host);
        }
        cmd->next = NULL;
        /* Send callback if required */
        if (cmd->cb) {
            cmd->cb(sdio, 0, cmd, cmd->token);
        }
    }

    return 0;
}

static int sdhc_get_nth_irq(sdio_host_dev_t *sdio, int n)
{
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    if (n < 0 || n >= host->nirqs) {
        return -1;
    } else {
        return host->irq_table[n];
    }
}

static int sdhc_send_cmd(sdio_host_dev_t *sdio, struct mmc_cmd *cmd, sdio_cb cb, void *token)
{
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    int ret;

    /* Initialise callbacks */
    cmd->complete = 0;
    cmd->next = NULL;
    cmd->cb = cb;
    cmd->token = token;
    /* Append to list */
    *host->cmd_list_tail = cmd;
    host->cmd_list_tail = &cmd->next;

    /* If idle, bump */
    if (host->cmd_list_head == cmd) {
        ret = sdhc_next_cmd(host);
        if (ret) {
            return ret;
        }
    }

    /* finalise the transacton */
    if (cb == NULL) {
        /* Wait for completion */
        while (!cmd->complete) {
            sdhc_handle_irq(sdio, 0);
        }
        /* Return result */
        if (cmd->complete < 0) {
            return cmd->complete;
        } else {
            return 0;
        }
    } else {
        /* Defer to IRQ handler */
        return 0;
    }
}

static int sdhc_is_voltage_compatible(sdio_host_dev_t *sdio, int mv)
{
    uint32_t val;
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    val = readl(host->base + HOST_CTRL_CAP);
    if (mv == 3300 && (val & HOST_CTRL_CAP_VS33)) {
        return 1;
    } else {
        return 0;
    }
}

/** Software Reset */
static int sdhc_reset(sdio_host_dev_t *sdio)
{
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    uint32_t val;

    /* Reset the host */
    val = readl(host->base + SYS_CTRL);
    val |= SYS_CTRL_RSTA;
    /* Wait until the controller is ready */
    writel(val, host->base + SYS_CTRL);
    do {
        val = readl(host->base + SYS_CTRL);
    } while (val & SYS_CTRL_RSTA);

    /* Enable IRQs */
    val = (INT_STATUS_ADMAE | INT_STATUS_OVRCURE | INT_STATUS_DEBE
           | INT_STATUS_DCE   | INT_STATUS_DTOE    | INT_STATUS_CRM
           | INT_STATUS_CINS  | INT_STATUS_BRR     | INT_STATUS_BWR
           | INT_STATUS_CIE   | INT_STATUS_CEBE    | INT_STATUS_CCE
           | INT_STATUS_CTOE  | INT_STATUS_TC      | INT_STATUS_CC);
    writel(val, host->base + INT_STATUS_EN);
    writel(val, host->base + INT_SIGNAL_EN);

    /* Configure clock for initialization */
    sdhc_set_clock(host->base, CLOCK_INITIAL);

    /* TODO: Select Voltage Level */

    /* Set bus width */
    val = readl(host->base + PROT_CTRL);
    val |= MMC_MODE_4BIT;
    writel(val, host->base + PROT_CTRL);

    /* Wait until the Command and Data Lines are ready. */
    while ((readl(host->base + PRES_STATE) & SDHC_PRES_STATE_CDIHB) ||
           (readl(host->base + PRES_STATE) & SDHC_PRES_STATE_CIHB));

    /* Send 80 clock ticks to card to power up. */
    val = readl(host->base + SYS_CTRL);
    val |= SYS_CTRL_INITA;
    writel(val, host->base + SYS_CTRL);
    while (readl(host->base + SYS_CTRL) & SYS_CTRL_INITA);

    /* Check if a SD card is inserted. */
    val = readl(host->base + PRES_STATE);
    if (val & SDHC_PRES_STATE_CINST) {
        ZF_LOGD("Card Inserted");
        if (!(val & SDHC_PRES_STATE_WPSPL)) {
            ZF_LOGD("(Read Only)");
        }
    } else {
        ZF_LOGE("Card Not Present...");
    }

    return 0;
}

static int sdhc_set_operational(struct sdio_host_dev *sdio)
{
    /*
     * Set the clock to a higher frequency for the operational state.
     *
     * As of now, there are no further checks to validate if the card and the
     * host controller could be driven with a higher rate, therefore the
     * operational clock settings are chosen rather conservative.
     */
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    return sdhc_set_clock(host->base, CLOCK_OPERATIONAL);
}

static uint32_t sdhc_get_present_state_register(sdio_host_dev_t *sdio)
{
    return readl(sdio_get_sdhc(sdio)->base + PRES_STATE);
}

int sdhc_init(void *iobase, const int *irq_table, int nirqs, ps_io_ops_t *io_ops,
              sdio_host_dev_t *dev)
{
    sdhc_dev_t sdhc;
    /* Allocate memory for SDHC structure */
    sdhc = (sdhc_dev_t)malloc(sizeof(*sdhc));
    if (!sdhc) {
        ZF_LOGE("Not enough memory!");
        return -1;
    }
    /* Complete the initialisation of the SDHC structure */
    sdhc->base = iobase;
    sdhc->nirqs = nirqs;
    sdhc->irq_table = irq_table;
    sdhc->dalloc = &io_ops->dma_manager;
    sdhc->cmd_list_head = NULL;
    sdhc->cmd_list_tail = &sdhc->cmd_list_head;
    sdhc->version = ((readl(sdhc->base + HOST_VERSION) >> 16) & 0xff) + 1;
    ZF_LOGD("SDHC version %d.00", sdhc->version);
    /* Initialise SDIO structure */
    dev->handle_irq = &sdhc_handle_irq;
    dev->nth_irq = &sdhc_get_nth_irq;
    dev->send_command = &sdhc_send_cmd;
    dev->is_voltage_compatible = &sdhc_is_voltage_compatible;
    dev->reset = &sdhc_reset;
    dev->set_operational = &sdhc_set_operational;
    dev->get_present_state = &sdhc_get_present_state_register;
    dev->priv = sdhc;
    /* Clear IRQs */
    writel(0, sdhc->base + INT_STATUS_EN);
    writel(0, sdhc->base + INT_SIGNAL_EN);
    writel(readl(sdhc->base + INT_STATUS), sdhc->base + INT_STATUS);
    return 0;
}
