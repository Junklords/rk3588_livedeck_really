/*
 * Copyright (c) 2024, Rockchip, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <common/debug.h>
#include <common/runtime_svc.h>
#include <drivers/scmi-msg.h>
#include <lib/mmio.h>
#include <plat_sip_calls.h>
#include <rockchip_sip_svc.h>

#define HDMIRX_FN_AUTO_TOUCH	1
#define HDMIRX_CTRL_BASE	0xfdee0000UL
#define HDMIRX_PHY_FILE_BASE	0xfdf10000UL
#define SYS_GRF_GATE		0xfd586054UL

static const uint8_t phy_tbl_348[] = {
#include "phy-tbl-348.h"
};

static void __maybe_unused hdmirx_phy_table_load(void)
{
    int i;
    mmio_setbits_32(HDMIRX_CTRL_BASE + 0x290, 0x1);
    mmio_clrbits_32(HDMIRX_CTRL_BASE + 0x24, 0x400);
    mmio_write_32(SYS_GRF_GATE, 0x40000000);
    for (i = 0; i < 5 && i < (int)sizeof(phy_tbl_348); i++)
        mmio_write_32(HDMIRX_PHY_FILE_BASE + 0x04 + i*4, phy_tbl_348[i]);
    for (; i < (int)sizeof(phy_tbl_348); i++)
        mmio_write_32(HDMIRX_PHY_FILE_BASE + 0x24 + (i-5)*4, phy_tbl_348[i]);
    mmio_write_32(SYS_GRF_GATE, 0x40004000);
    mmio_setbits_32(HDMIRX_CTRL_BASE + 0x24, 0x400);
    NOTICE("HDMIRX: PHY table loaded (%d bytes)\n", (int)sizeof(phy_tbl_348));
}

uintptr_t rockchip_plat_sip_handler(uint32_t smc_fid,
    u_register_t x1, u_register_t x2, u_register_t x3, u_register_t x4,
    void *cookie, void *handle, u_register_t flags)
{
    switch (smc_fid) {
    case RK_SIP_SCMI_AGENT0:
        scmi_smt_fastcall_smc_entry(0);
        SMC_RET1(handle, 0);
    case RK_SIP_HDMIRX_CFG:
        hdmirx_phy_table_load();
        SMC_RET1(handle, 0);
    default:
        SMC_RET1(handle, SMC_UNK);
    }
}
