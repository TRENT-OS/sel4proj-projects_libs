/*
 * Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <platsupport/clock.h>
#include <platsupport/plat/clock.h>

/* NVIDIA interface */
#include <tx2bpmp/bpmp.h> /* struct mrq_clk_request, struct mrq_clk_response */

typedef struct xavier_clk {
    ps_io_ops_t *io_ops;
    void *car_vaddr;
    struct tx2_bpmp *bpmp;
} xavier_clk_t;

static inline bool check_valid_gate(enum clock_gate gate)
{
    return (TEGRA194_GATE_CLK_ACTMON <= gate && gate < NCLKGATES);
}

static inline bool check_valid_clk_id(enum clk_id id)
{
    return (TEGRA194_CLK_ACTMON <= id && id < NCLOCKS);
}

static int xavier_car_gate_enable(clock_sys_t *clock_sys, enum clock_gate gate, enum clock_gate_mode mode)
{
    if (!check_valid_gate(gate)) {
        ZF_LOGE("Invalid clock gate!");
        return -EINVAL;
    }

    if (mode == CLKGATE_IDLE || mode == CLKGATE_SLEEP) {
        ZF_LOGE("Idle and sleep gate modes are not supported");
        return -EINVAL;
    }

    uint32_t command = (mode == CLKGATE_ON ? CMD_CLK_ENABLE : CMD_CLK_DISABLE);

    uint32_t bpmp_gate_id = gate;

    /* Setup the message and make a call to BPMP */
    struct mrq_clk_request req = { .cmd_and_id = (command << 24) | bpmp_gate_id };
    struct mrq_clk_response res = {0};
    xavier_clk_t *clk = clock_sys->priv;

    int bytes_recvd = tx2_bpmp_call(clk->bpmp, MRQ_CLK, &req, sizeof(req), &res, sizeof(res));
    if (bytes_recvd < 0) {
        return -EIO;
    }

    return 0;
}

static uint32_t xavier_car_get_clock_source(clk_t *clk)
{
    uint32_t bpmp_clk_id = clk->id;

    struct mrq_clk_request req = { .cmd_and_id = (CMD_CLK_GET_PARENT << 24) | bpmp_clk_id};
    struct mrq_clk_response res = {0};
    xavier_clk_t *xavier_clk = clk->clk_sys->priv;

    int bytes_recvd = tx2_bpmp_call(xavier_clk->bpmp, MRQ_CLK, &req, sizeof(req), &res, sizeof(&res));
    if (bytes_recvd < 0) {
        ZF_LOGE("Received < 0 bytes in xavier_car_set_clock_source");
        return 0;
    }

    return res.clk_get_parent.parent_id;
}

static uint32_t xavier_car_set_clock_source(clk_t *clk, uint32_t clk_src)
{
    uint32_t bpmp_clk_id = clk->id;

    struct mrq_clk_request req = { .cmd_and_id = (CMD_CLK_SET_PARENT << 24) | bpmp_clk_id};
    req.clk_set_parent.parent_id = clk_src;
    struct mrq_clk_response res = {0};
    xavier_clk_t *xavier_clk = clk->clk_sys->priv;

    int bytes_recvd = tx2_bpmp_call(xavier_clk->bpmp, MRQ_CLK, &req, sizeof(req), &res, sizeof(&res));
    if (bytes_recvd < 0) {
        ZF_LOGE("Received < 0 bytes in xavier_car_set_clock_source");
        return 0;
    }

    return res.clk_get_parent.parent_id;
}

static freq_t xavier_car_get_freq(clk_t *clk)
{
    uint32_t bpmp_clk_id = clk->id;

    struct mrq_clk_request req = { .cmd_and_id = (CMD_CLK_GET_RATE << 24) | bpmp_clk_id };
    struct mrq_clk_response res = {0};
    xavier_clk_t *xavier_clk = clk->clk_sys->priv;

    int bytes_recvd = tx2_bpmp_call(xavier_clk->bpmp, MRQ_CLK, &req, sizeof(req), &res, sizeof(&res));
    if (bytes_recvd < 0) {
        return 0;
    }

    return (freq_t) res.clk_get_rate.rate;
}

static freq_t xavier_car_set_freq(clk_t *clk, freq_t hz)
{
    uint32_t bpmp_clk_id = clk->id;

    struct mrq_clk_request req = { .cmd_and_id = (CMD_CLK_SET_RATE << 24) | bpmp_clk_id };
    req.clk_set_rate.rate = hz;
    struct mrq_clk_response res = {0};
    xavier_clk_t *xavier_clk = clk->clk_sys->priv;

    int bytes_recvd = tx2_bpmp_call(xavier_clk->bpmp, MRQ_CLK, &req, sizeof(req), &res, sizeof(&res));
    if (bytes_recvd < 0) {
        return 0;
    }

    clk->req_freq = hz;

    return (freq_t) res.clk_set_rate.rate;
}

static clk_t *xavier_car_get_clock(clock_sys_t *clock_sys, enum clk_id id)
{
    if (!check_valid_clk_id(id)) {
        ZF_LOGE("Invalid clock ID");
        return NULL;
    }

    clk_t *ret_clk = NULL;
    xavier_clk_t *xavier_clk = clock_sys->priv;
    size_t clk_name_len = 0;
    int error = ps_calloc(&xavier_clk->io_ops->malloc_ops, 1, sizeof(*ret_clk), (void **) &ret_clk);
    if (error) {
        ZF_LOGE("Failed to allocate memory for the clock structure");
        return NULL;
    }

    bool clock_initialised = false;

    uint32_t clk_gate_id = id;

    /* Enable the clock while we're at it, clk_id is also a substitute for clock_gate */
    error = xavier_car_gate_enable(clock_sys, clk_gate_id, CLKGATE_ON);
    if (error) {
        goto fail;
    }

    clock_initialised = true;

    uint32_t bpmp_clk_id = id;

    /* Get info about this clock so we can fill it in */
    struct mrq_clk_request req = { .cmd_and_id = (CMD_CLK_GET_ALL_INFO << 24) | bpmp_clk_id };
    struct mrq_clk_response res = {0};
    char *clock_name = NULL;
    int bytes_recvd = tx2_bpmp_call(xavier_clk->bpmp, MRQ_CLK, &req, sizeof(req), &res, sizeof(res));
    if (bytes_recvd < 0) {
        ZF_LOGE("Failed to initialise the clock");
        goto fail;
    }
    clk_name_len = strlen((char *) res.clk_get_all_info.name) + 1;
    error = ps_calloc(&xavier_clk->io_ops->malloc_ops, 1, sizeof(char) * clk_name_len, (void **) &clock_name);
    if (error) {
        ZF_LOGE("Failed to allocate memory for the name of the clock");
        goto fail;
    }
    strncpy(clock_name, (char *) res.clk_get_all_info.name, clk_name_len);

    ret_clk->name = (const char *) clock_name;

    /* There's no need for the init nor the recal functions as we're already
     * doing it now and that the BPMP handles the recalibration for us */
    ret_clk->get_freq = xavier_car_get_freq;
    ret_clk->set_freq = xavier_car_set_freq;
    ret_clk->get_source = xavier_car_get_clock_source;
    ret_clk->set_source = xavier_car_set_clock_source;

    ret_clk->id = id;
    ret_clk->clk_sys = clock_sys;

    return ret_clk;

fail:
    if (ret_clk) {
        if (ret_clk->name) {
            ps_free(&xavier_clk->io_ops->malloc_ops, sizeof(char) * clk_name_len, (void *) ret_clk->name);
        }

        ps_free(&xavier_clk->io_ops->malloc_ops, sizeof(*ret_clk), (void *) ret_clk);
    }

    if (clock_initialised) {
        ZF_LOGF_IF(xavier_car_gate_enable(clock_sys, id, CLKGATE_OFF),
                   "Failed to disable clock following failed clock initialisation operation");
    }

    return NULL;
}

static int interface_search_handler(void *handler_data, void *interface_instance, char **properties)
{
    /* Select the first one that is registered */
    xavier_clk_t *clk = handler_data;
    clk->bpmp = (struct tx2_bpmp *) interface_instance;
    return PS_INTERFACE_FOUND_MATCH;
}

int clock_sys_init(ps_io_ops_t *io_ops, clock_sys_t *clock_sys)
{
    if (!io_ops || !clock_sys) {
        if (!io_ops) {
            ZF_LOGE("null io_ops argument");
        }

        if (!clock_sys) {
            ZF_LOGE("null clock_sys argument");
        }

        return -EINVAL;
    }

    int error = 0;
    xavier_clk_t *clk = NULL;
    void *car_vaddr = NULL;

    error = ps_calloc(&io_ops->malloc_ops, 1, sizeof(xavier_clk_t), (void **) &clock_sys->priv);
    if (error) {
        ZF_LOGE("Failed to allocate memory for clock sys internal structure");
        error = -ENOMEM;
        goto fail;
    }

    clk = clock_sys->priv;

    // car_vaddr = ps_io_map(&io_ops->io_mapper, TX2_CLKCAR_PADDR, TX2_CLKCAR_SIZE, 0, PS_MEM_NORMAL);
    // if (car_vaddr == NULL) {
    //     ZF_LOGE("Failed to map tx2 CAR registers");
    //     error = -ENOMEM;
    //     goto fail;
    // }

    clk->car_vaddr = NULL;

    /* See if there's a registered interface for the BPMP, if not, create one
     * ourselves */
    error = ps_interface_find(&io_ops->interface_registration_ops, TX2_BPMP_INTERFACE,
                              interface_search_handler, clk);
    if (error) {
        error = ps_calloc(&io_ops->malloc_ops, 1, sizeof(struct tx2_bpmp), (void **) &clk->bpmp);
        if (error) {
            ZF_LOGE("Failed to allocate memory for the BPMP structure");
            goto fail;
        }

        error = tx2_bpmp_init(io_ops, clk->bpmp);
        if (error) {
            ZF_LOGE("Failed to initialise BPMP interface");
            goto fail;
        }
    }

    clk->io_ops = io_ops;

    clock_sys->gate_enable = &xavier_car_gate_enable;
    clock_sys->get_clock = &xavier_car_get_clock;

    return 0;

fail:

    // if (car_vaddr) {
    //     ps_io_unmap(&io_ops->io_mapper, car_vaddr, TX2_CLKCAR_SIZE);
    // }

    if (clock_sys->priv) {
        if (clk->bpmp) {
            ZF_LOGF_IF(ps_free(&io_ops->malloc_ops, sizeof(struct tx2_bpmp), (void *) clk->bpmp),
                       "Failed to free the BPMP structure after failing to initialise");
        }
        ZF_LOGF_IF(ps_free(&io_ops->malloc_ops, sizeof(xavier_clk_t), (void *) clock_sys->priv),
                   "Failed to free the clock private structure after failing to initialise");
    }

    return error;
}

void clk_print_clock_tree(clock_sys_t *sys)
{
    /* TODO Implement this function. The manual doesn't really give us a nice
     * diagram of the clock hierarchy, but there is information about it. It's
     * kind of hard to find however and would require scrolling through the
     * manual and constructing it by hand. */
    ZF_LOGE("Unimplemented");
}