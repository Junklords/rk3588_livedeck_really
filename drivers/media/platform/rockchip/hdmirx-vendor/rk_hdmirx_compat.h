/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Compatibility shims for porting the Rockchip vendor rk_hdmirx driver
 * (kernel 5.10 / rockchip-linux develop-6.1) to mainline 7.3.
 *
 * - media/v4l2-controls_rockchip.h : private control IDs (verbatim)
 * - soc/rockchip/rockchip-system-status.h : no-op stubs, mainline has no
 *   rockchip system monitor
 * - sound/hdmi-codec.h : optional; the HDMI audio codec device is only
 *   registered when HDMIRX_VENDOR_AUDIO is set to 1 below (the target
 *   tree ships include/sound/hdmi-codec.h as a stub, so the default is 0)
 */

#ifndef _RK_HDMIRX_COMPAT_H
#define _RK_HDMIRX_COMPAT_H

#include <linux/videodev2.h>

/* --------------------------------------------------------------------- */
/* media/v4l2-controls_rockchip.h (vendor)                                */
/* --------------------------------------------------------------------- */

#define V4L2_CID_USER_RK_BASE (V4L2_CID_USER_BASE + 0x1080)

#define RK_V4L2_CID_AUDIO_SAMPLING_RATE (V4L2_CID_USER_RK_BASE + 0x100)
#define RK_V4L2_CID_AUDIO_PRESENT (V4L2_CID_USER_RK_BASE + 0x101)

/* --------------------------------------------------------------------- */
/* soc/rockchip/rockchip-system-status.h (vendor)                         */
/* --------------------------------------------------------------------- */

#define SYS_STATUS_HDMIRX	0

static inline void rockchip_set_system_status(unsigned long status) {}
static inline void rockchip_clear_system_status(unsigned long status) {}

/* --------------------------------------------------------------------- */
/* sound/hdmi-codec.h (optional HDMI audio codec support)                 */
/* --------------------------------------------------------------------- */

/* Set to 1 (and make sure include/sound/hdmi-codec.h is the real header)
 * to register the hdmi-codec ASoC device for HDMI RX audio. */
#define HDMIRX_VENDOR_AUDIO	0

#if HDMIRX_VENDOR_AUDIO
#include <sound/hdmi-codec.h>
#else
/*
 * Keep the plugged_cb plumbing compiling: this is exactly the
 * hdmi_codec_plugged_cb signature from include/sound/hdmi-codec.h.
 */
typedef void (*hdmi_codec_plugged_cb)(struct device *dev, bool plugged);
#endif

#endif /* _RK_HDMIRX_COMPAT_H */
