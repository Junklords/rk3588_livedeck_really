/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal local replacement for the vendor kernel's
 * include/linux/rockchip/rockchip_sip.h + drivers/firmware/rockchip_sip.c.
 *
 * The Rockchip BL31 (rkbin ATF) implements these SMC functions; function
 * IDs and calling conventions below are copied verbatim from the vendor
 * kernel (rockchip-linux develop-6.1) so behaviour is identical without
 * depending on out-of-tree firmware code.
 */

#ifndef _RK_HDMIRX_SIP_H
#define _RK_HDMIRX_SIP_H

#include <linux/arm-smccc.h>
#include <linux/io.h>
#include <linux/sizes.h>

/* SIP function IDs (vendor include/linux/rockchip/rockchip_sip.h) */
#define RK_SIP_FIQ_CTRL			0x82000024
#define SIP_HDCP_CONFIG			0x82000025
#define SIP_WDT_CFG			0x82000026
#define SIP_HDMIRX_CFG			0x82000027
#define SIP_SHARE_MEM			0x82000009

#define SIP_RET_SUCCESS			0

/* Software FIQ number agreed with BL31 for the HDMI RX interrupt */
#define RK_IRQ_HDMIRX_HDMI		210

/* fiq control sub func */
enum {
	RK_SIP_FIQ_CTRL_FIQ_EN = 1,
	RK_SIP_FIQ_CTRL_FIQ_DIS,
	RK_SIP_FIQ_CTRL_SET_AFF
};

/* hdcp function types */
enum {
	HDCP_FUNC_STORAGE_INCRYPT = 1,
	HDCP_FUNC_KEY_LOAD,
	HDCP_FUNC_ENCRYPT_MODE
};

/* support hdcp device list */
enum {
	DP_TX0,
	DP_TX1,
	EDP_TX0,
	EDP_TX1,
	HDMI_TX0,
	HDMI_TX1,
	HDMI_RX,
	MAX_DEVICE,
};

/* SIP_WDT_CONFIG call types */
enum {
	WDT_START = 0,
	WDT_STOP = 1,
	WDT_PING = 2,
};

/* SIP_HDMIRX_CONFIG child configs */
enum {
	HDMIRX_AUTO_TOUCH_EN = 0,
	HDMIRX_REG_PRE_FETCH = 1,
	HDMIRX_INFO_NOTIFY = 2,
};

/* Share mem page types (only the HDCP page is needed here) */
enum {
	SHARE_PAGE_TYPE_INVALID = 0,
	SHARE_PAGE_TYPE_HDCP,
};

static inline int sip_fiq_control(u32 sub_func, u32 irq, unsigned long data)
{
	struct arm_smccc_res res;

	arm_smccc_smc(RK_SIP_FIQ_CTRL, sub_func, irq, data, 0, 0, 0, 0, &res);
	return res.a0;
}

static inline int sip_wdt_config(u32 sub_func, u32 arg1, u32 arg2, u32 arg3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SIP_WDT_CFG, sub_func, arg1, arg2, arg3, 0, 0, 0, &res);
	return res.a0;
}

static inline int sip_hdmirx_config(u32 sub_func, u32 arg1, u32 arg2, u32 arg3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SIP_HDMIRX_CFG, sub_func, arg1, arg2, arg3,
		      0, 0, 0, &res);
	return res.a0;
}

static inline struct arm_smccc_res sip_hdcp_config(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SIP_HDCP_CONFIG, arg0, arg1, arg2, 0, 0, 0, 0, &res);
	return res;
}

/*
 * Request one shared page of HDCP memory from BL31.  The vendor driver
 * (sip_hdcp_request_share_memory()) requests 2 pages and hands out
 * 1KB per device id; only HDMI_RX is used by this driver.
 */
static inline void __iomem *sip_hdcp_request_share_memory(int id)
{
	static void __iomem *base;
	struct arm_smccc_res res;

	if (id < 0 || id >= MAX_DEVICE)
		return NULL;

	if (!base) {
		arm_smccc_smc(SIP_SHARE_MEM, 2, SHARE_PAGE_TYPE_HDCP, 0,
			      0, 0, 0, 0, &res);
		if (res.a0 != SIP_RET_SUCCESS)
			return NULL;
		/* vendor kernel maps this DRAM with memremap() */
		base = memremap(res.a1, 2 * SZ_4K, MEMREMAP_WB);
		if (!base)
			return NULL;
	}

	return base + id * 1024;
}

#endif /* _RK_HDMIRX_SIP_H */
