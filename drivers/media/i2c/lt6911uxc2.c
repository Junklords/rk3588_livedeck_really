// SPDX-License-Identifier: GPL-2.0
/*
 * Mainline V4L2 subdev driver for Lontium LT6911UXC2 HDMI-to-MIPI CSI bridge
 *
 * Based on Rockchip vendor driver lt6911uxc.c (1670 lines), stripped of
 * vendor-specific APIs and ported to standard V4L2 subdev framework.
 *
 * Copyright (c) 2026 Really Board Support
 * Original vendor driver: Copyright (c) 2021 Rockchip Electronics
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Chip registers (from vendor lt6911uxc.h) */
/* Real register map, extracted 2026-11-21 by disassembling the OEM
 * lt6911uxc2 driver in the Android 5.10.110 boot image (boot_a.img):
 *   - ID registers are 0xa000/0xa001, expected 0x0516 (NOT 0x8100!)
 *   - 0x80ee is a doorbell: 1 before each register-access group, 0 after
 *   - timing regs live in the 0xd2xx window, values are half the pixel count
 *   - there is NO init/firmware-download sequence; the chip self-boots */
#define LT6911UXC_I2C_EN_REG	0x80EE
#define LT6911UXC_CHIPID_L	0xa000
#define LT6911UXC_CHIPID_H	0xa001
#define LT6911UXC_CHIPID_VAL	0x0516
#define LT6911UXC_FW_VER_A	0x86a7
#define LT6911UXC_FW_VER_D	0x86aa
#define LT6911UXC_CLK_MEAS	0xa034
#define LT6911UXC_CLK_MEAS_START	0x21
#define LT6911UXC_TIMING_TRIGGER	0xd283
#define LT6911UXC_PIXCLK_19_16	0xb8b1
#define LT6911UXC_PIXCLK_15_8	0xb8b2
#define LT6911UXC_PIXCLK_7_0	0xb8b3
#define LT6911UXC_VSYNC		0xd286
#define LT6911UXC_VBPP		0xd287
#define LT6911UXC_VFP		0xd288
#define LT6911UXC_VTOTAL_H	0xd289
#define LT6911UXC_VTOTAL_L	0xd28a
#define LT6911UXC_HACT_H	0xd28b
#define LT6911UXC_HACT_L	0xd28c
#define LT6911UXC_HSYNC_H	0xd294
#define LT6911UXC_HSYNC_L	0xd295
#define LT6911UXC_VACT_H	0xd296
#define LT6911UXC_VACT_L	0xd297
#define LT6911UXC_HBP_H		0xd298
#define LT6911UXC_HBP_L		0xd299
#define LT6911UXC_HFP_H		0xd29c
#define LT6911UXC_HFP_L		0xd29d
#define LT6911UXC2_NAME		"lt6911uxc2"

struct lt6911uxc2 {
	struct i2c_client *client;
	struct regmap *regmap;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct mutex lock;
	struct delayed_work work_hotplug;
	struct delayed_work work_res_change;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *hpd_gpio;
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_ctrl *detect_tx_5v;
	struct v4l2_dv_timings timings;
	u32 mbus_code;
	u32 fmt_width;
	u32 fmt_height;
	bool nosignal;
	int irq;
};

/* MIPI link frequency (half the per-lane bitrate): 1080p60 RGB888
 * needs ~891 Mbps/lane on 4 lanes -> 445.5 MHz. Bring-up default. */
static const s64 lt6911uxc2_link_freq_menu[] = { 445500000 };

static inline struct lt6911uxc2 *to_lt6911uxc2(struct v4l2_subdev *sd)
{
	return container_of(sd, struct lt6911uxc2, sd);
}

/* --- I2C register access: LT6911UXC2 bank-switching protocol ---
 * 16-bit register addresses are split: high byte = bank, low byte = offset.
 * Bank is selected by writing [0xFF, bank] first. */

static int lt6911uxc2_read(struct lt6911uxc2 *priv, u16 reg, u8 *val)
{
	struct i2c_client *client = priv->client;
	u8 bank = reg >> 8;
	u8 offset = reg & 0xFF;
	u8 bank_buf[2] = { 0xFF, bank };
	struct i2c_msg msgs[3] = {
		{ .addr = client->addr, .flags = 0, .len = 2, .buf = bank_buf },
		{ .addr = client->addr, .flags = 0, .len = 1, .buf = &offset },
		{ .addr = client->addr, .flags = I2C_M_RD, .len = 1, .buf = val },
	};
	int ret;

	ret = i2c_transfer(client->adapter, msgs, 3);
	return (ret == 3) ? 0 : -EIO;
}

static int lt6911uxc2_write(struct lt6911uxc2 *priv, u16 reg, u8 val)
{
	struct i2c_client *client = priv->client;
	u8 bank = reg >> 8;
	u8 offset = reg & 0xFF;
	u8 bank_buf[2] = { 0xFF, bank };
	u8 data[2] = { offset, val };
	struct i2c_msg msgs[2] = {
		{ .addr = client->addr, .flags = 0, .len = 2, .buf = bank_buf },
		{ .addr = client->addr, .flags = 0, .len = 2, .buf = data },
	};
	int ret;

	ret = i2c_transfer(client->adapter, msgs, 2);
	return (ret == 2) ? 0 : -EIO;
}

static int lt6911uxc2_read16(struct lt6911uxc2 *priv, u16 reg_h, u16 reg_l,
			     u16 *val)
{
	u8 hi, lo;
	int ret;

	ret = lt6911uxc2_read(priv, reg_h, &hi);
	if (ret)
		return ret;
	ret = lt6911uxc2_read(priv, reg_l, &lo);
	if (ret)
		return ret;
	*val = (hi << 8) | lo;
	return 0;
}

/* Register-access doorbell: the OEM driver writes 0x80ee=1 before every
 * access group and 0x80ee=0 after. Unwrapped reads return garbage. */
static int lt6911uxc2_i2c_enable(struct lt6911uxc2 *priv)
{
	return lt6911uxc2_write(priv, LT6911UXC_I2C_EN_REG, 0x01);
}

static void lt6911uxc2_i2c_disable(struct lt6911uxc2 *priv)
{
	lt6911uxc2_write(priv, LT6911UXC_I2C_EN_REG, 0x00);
}

/* --- Chip identity --- */

static int lt6911uxc2_check_chip_id(struct lt6911uxc2 *priv)
{
	u8 id_l = 0, id_h = 0, v[4] = { 0 };
	u16 chip_id;
	int ret;

	ret = lt6911uxc2_i2c_enable(priv);
	if (ret)
		return ret;

	lt6911uxc2_read(priv, LT6911UXC_CHIPID_L, &id_l);
	lt6911uxc2_read(priv, LT6911UXC_CHIPID_H, &id_h);
	lt6911uxc2_read(priv, LT6911UXC_FW_VER_A, &v[0]);
	lt6911uxc2_read(priv, LT6911UXC_FW_VER_A + 1, &v[1]);
	lt6911uxc2_read(priv, LT6911UXC_FW_VER_A + 2, &v[2]);
	lt6911uxc2_read(priv, LT6911UXC_FW_VER_A + 3, &v[3]);
	lt6911uxc2_i2c_disable(priv);

	chip_id = (id_h << 8) | id_l;
	if (chip_id != LT6911UXC_CHIPID_VAL) {
		dev_err(&priv->client->dev,
			"chip ID 0x%04x != expected 0x%04x\n",
			chip_id, LT6911UXC_CHIPID_VAL);
		return -ENODEV;
	}
	dev_info(&priv->client->dev,
		 "LT6911UXC2 chip ID 0x%04x, fw 0x%02x%02x%02x%02x\n",
		 chip_id, v[0], v[1], v[2], v[3]);
	return 0;
}

/* --- HDMI timing detection --- */

static int lt6911uxc2_get_detected_timings(struct lt6911uxc2 *priv,
					   struct v4l2_dv_timings *timings)
{
	u8 vh = 0, vl = 0, hh = 0, hl = 0, c19 = 0, c15 = 0, c7 = 0;
	u8 vsh = 0, vsl = 0, vfph = 0, vfpl = 0, vfp = 0;
	u16 hact, vact, hsync, hfp, hbp;
	u64 pixclk;
	u32 htotal, vtotal;
	int ret;

	memset(timings, 0, sizeof(*timings));
	timings->type = V4L2_DV_BT_656_1120;

	ret = lt6911uxc2_i2c_enable(priv);
	if (ret)
		return ret;

	/* clock measurement: start, wait 12 ms, read 20-bit value in 2 kHz units */
	lt6911uxc2_write(priv, LT6911UXC_CLK_MEAS, LT6911UXC_CLK_MEAS_START);
	usleep_range(12000, 13000);
	lt6911uxc2_read(priv, LT6911UXC_PIXCLK_19_16, &c19);
	lt6911uxc2_read(priv, LT6911UXC_PIXCLK_15_8, &c15);
	lt6911uxc2_read(priv, LT6911UXC_PIXCLK_7_0, &c7);

	/* stable-timing capture trigger, wait 10 ms */
	lt6911uxc2_write(priv, LT6911UXC_TIMING_TRIGGER, 0x10);
	usleep_range(10000, 11000);

	lt6911uxc2_read(priv, LT6911UXC_HACT_H, &hh);
	lt6911uxc2_read(priv, LT6911UXC_HACT_L, &hl);
	lt6911uxc2_read(priv, LT6911UXC_VACT_H, &vh);
	lt6911uxc2_read(priv, LT6911UXC_VACT_L, &vl);
	lt6911uxc2_read(priv, LT6911UXC_HSYNC_H, &vsh);
	lt6911uxc2_read(priv, LT6911UXC_HSYNC_L, &vsl);
	lt6911uxc2_read(priv, LT6911UXC_HFP_H, &vfph);
	lt6911uxc2_read(priv, LT6911UXC_HFP_L, &vfpl);
	lt6911uxc2_read(priv, LT6911UXC_HBP_H, &vsh);
	lt6911uxc2_read(priv, LT6911UXC_HBP_L, &vsl);
	lt6911uxc2_read(priv, LT6911UXC_VFP, &vfp);

	lt6911uxc2_i2c_disable(priv);

	hact = ((hh << 8) | hl) * 2;
	vact = ((vh << 8) | vl) * 2;
	hsync = ((vsh << 8) | vsl) * 2;
	hfp = ((vfph << 8) | vfpl) * 2;
	hbp = ((vsh << 8) | vsl) * 2;
	pixclk = (((c19 & 0xf) << 16) | (c15 << 8) | c7) * 2000ULL;
	htotal = hact + hsync + hfp + hbp;
	vtotal = vact + vfph + vfpl + vfp;

	if (hact < 320 || hact > 8000 || vact < 200 || vact > 4400 ||
	    !pixclk || htotal <= hact || vtotal <= vact)
		return -ENOLINK;

	timings->bt.width = hact;
	timings->bt.height = vact;
	timings->bt.hfrontporch = hfp;
	timings->bt.hsync = hsync;
	timings->bt.hbackporch = hbp;
	timings->bt.vfrontporch = vfp;
	timings->bt.pixelclock = pixclk;
	timings->bt.interlaced = V4L2_DV_PROGRESSIVE;
	timings->bt.polarities = V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL;

	dev_dbg(&priv->client->dev,
		"%ux%u@%lluHz hfp=%u hs=%u hbp=%u vfp=%u\n",
		hact, vact, pixclk, hfp, hsync, hbp, vfp);

	return 0;
}

static int lt6911uxc2_g_tx_5v(struct lt6911uxc2 *priv)
{
	/* No chip-side 5V status register exists in the OEM map; the real
	 * driver samples a plugin-det GPIO we do not wire. Treat timings
	 * validity as the signal indicator during bring-up. */
	return 1;
}

/* --- Stream control --- */

static int lt6911uxc2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct lt6911uxc2 *priv = to_lt6911uxc2(sd);

	/* OEM banked driver performs NO register writes on stream on/off:
	 * the chip streams autonomously once locked. Only manage HPD. */
	dev_dbg(&priv->client->dev, "%s stream\n",
		enable ? "enable" : "disable");
	if (priv->hpd_gpio)
		gpiod_set_value_cansleep(priv->hpd_gpio, enable ? 1 : 0);

	return 0;
}

/* --- V4L2 subdev ops --- */

static int lt6911uxc2_s_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
				   struct v4l2_dv_timings *timings)
{
	struct lt6911uxc2 *priv = to_lt6911uxc2(sd);

	if (!v4l2_valid_dv_timings(timings, NULL, NULL, NULL))
		return -ERANGE;

	priv->timings = *timings;
	return 0;
}

static int lt6911uxc2_g_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
				   struct v4l2_dv_timings *timings)
{
	struct lt6911uxc2 *priv = to_lt6911uxc2(sd);
	int ret;

	mutex_lock(&priv->lock);
	ret = lt6911uxc2_get_detected_timings(priv, timings);
	if (!ret)
		priv->timings = *timings;
	mutex_unlock(&priv->lock);

	return ret;
}

static int lt6911uxc2_query_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
				       struct v4l2_dv_timings *timings)
{
	struct lt6911uxc2 *priv = to_lt6911uxc2(sd);
	int ret;

	mutex_lock(&priv->lock);
	ret = lt6911uxc2_get_detected_timings(priv, timings);
	mutex_unlock(&priv->lock);

	return ret;
}

static int lt6911uxc2_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_RGB888_1X24;
	return 0;
}

static int lt6911uxc2_get_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *format)
{
	struct lt6911uxc2 *priv = to_lt6911uxc2(sd);

	format->format.width = priv->fmt_width;
	format->format.height = priv->fmt_height;
	format->format.code = priv->mbus_code;
	format->format.field = V4L2_FIELD_NONE;
	format->format.colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int lt6911uxc2_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *format)
{
	struct lt6911uxc2 *priv = to_lt6911uxc2(sd);

	/* For bring-up the output format is configured by userspace;
	 * the chip's status registers use a different map than the
	 * UXC1 offsets, so live-derived sizes cannot be trusted yet. */
	if (format->format.code != MEDIA_BUS_FMT_RGB888_1X24)
		format->format.code = MEDIA_BUS_FMT_RGB888_1X24;
	priv->mbus_code = format->format.code;
	if (format->format.width < 16)
		format->format.width = 1920;
	if (format->format.height < 16)
		format->format.height = 1080;
	priv->fmt_width = format->format.width;
	priv->fmt_height = format->format.height;

	return 0;
}

static int lt6911uxc2_g_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_frame_interval *fi)
{
	struct lt6911uxc2 *priv = to_lt6911uxc2(sd);

	fi->interval.numerator = 1;
	fi->interval.denominator = DIV_ROUND_CLOSEST_ULL(
		priv->timings.bt.pixelclock,
		(priv->timings.bt.width + priv->timings.bt.hfrontporch +
		 priv->timings.bt.hsync + priv->timings.bt.hbackporch) *
		(priv->timings.bt.height + priv->timings.bt.vfrontporch +
		 priv->timings.bt.vsync + priv->timings.bt.vbackporch));

	return 0;
}

static const struct v4l2_subdev_video_ops lt6911uxc2_video_ops = {
	.s_stream = lt6911uxc2_s_stream,
};

static const struct v4l2_subdev_pad_ops lt6911uxc2_pad_ops = {
	.enum_mbus_code = lt6911uxc2_enum_mbus_code,
	.get_fmt = lt6911uxc2_get_fmt,
	.set_fmt = lt6911uxc2_set_fmt,
	.s_dv_timings = lt6911uxc2_s_dv_timings,
	.g_dv_timings = lt6911uxc2_g_dv_timings,
	.query_dv_timings = lt6911uxc2_query_dv_timings,
};

static const struct v4l2_subdev_ops lt6911uxc2_ops = {
	.video = &lt6911uxc2_video_ops,
	.pad = &lt6911uxc2_pad_ops,
};

/* --- HPD (Hot Plug Detect) --- */

static void lt6911uxc2_enable_hotplug_work(struct work_struct *work)
{
	struct lt6911uxc2 *priv = container_of(to_delayed_work(work),
					       struct lt6911uxc2, work_hotplug);
	/* Assert HPD and keep it high: the source only reads EDID /
	 * starts TMDS while HPD is asserted (display present). */
	if (!priv->hpd_gpio)
		return;
	gpiod_set_value_cansleep(priv->hpd_gpio, 0);
	msleep(150);
	gpiod_set_value_cansleep(priv->hpd_gpio, 1);
}

/* --- Format change work --- */

static void lt6911uxc2_res_change_work(struct work_struct *work)
{
	struct lt6911uxc2 *priv = container_of(to_delayed_work(work),
					       struct lt6911uxc2,
					       work_res_change);
	struct v4l2_event ev = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};
	struct v4l2_dv_timings timings;
	int ret;

	mutex_lock(&priv->lock);
	ret = lt6911uxc2_get_detected_timings(priv, &timings);
	if (!ret)
		priv->timings = timings;
	priv->nosignal = !!ret;
	mutex_unlock(&priv->lock);

	if (!ret)
		v4l2_subdev_notify_event(&priv->sd, &ev);
}

static irqreturn_t lt6911uxc2_irq_handler(int irq, void *dev_id)
{
	struct lt6911uxc2 *priv = dev_id;

	/* No readable INT status registers in the OEM map: just poll. */
	schedule_delayed_work(&priv->work_res_change, msecs_to_jiffies(20));

	return IRQ_HANDLED;
}

/* --- DT parsing --- */

static int lt6911uxc2_parse_dt(struct lt6911uxc2 *priv)
{
	struct device *dev = &priv->client->dev;
	struct fwnode_handle *ep;
	struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	int ret;

	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpio))
		return PTR_ERR(priv->reset_gpio);

	priv->hpd_gpio = devm_gpiod_get_optional(dev, "hpd-ctl", GPIOD_OUT_LOW);
	if (IS_ERR(priv->hpd_gpio))
		return PTR_ERR(priv->hpd_gpio);

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!ep) {
		dev_err(dev, "no endpoint found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	fwnode_handle_put(ep);
	if (ret) {
		dev_err(dev, "failed to parse endpoint: %d\n", ret);
		return ret;
	}

	if (vep.bus.mipi_csi2.num_data_lanes < 1 ||
	    vep.bus.mipi_csi2.num_data_lanes > 4) {
		dev_err(dev, "invalid number of data lanes: %d\n",
			vep.bus.mipi_csi2.num_data_lanes);
		return -EINVAL;
	}

	dev_info(dev, "MIPI CSI2: %d data lanes\n",
		 vep.bus.mipi_csi2.num_data_lanes);

	return 0;
}

/* --- Regmap config (16-bit register addresses, 8-bit values) --- */

/* --- Probe / Remove --- */

static int lt6911uxc2_probe(struct i2c_client *client)
{
	struct lt6911uxc2 *priv;
	struct device *dev = &client->dev;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	priv->mbus_code = MEDIA_BUS_FMT_RGB888_1X24;
	priv->fmt_width = 1920;
	priv->fmt_height = 1080;
	struct v4l2_dv_timings def_tim = V4L2_DV_BT_CEA_640X480P59_94;
	priv->timings = def_tim;

	ret = lt6911uxc2_parse_dt(priv);
	if (ret)
		return ret;

	/* Power first: with power-supply in DT this defers probe until
	 * the regulator is up; the chip must never be reset while
	 * unpowered (leaves it in a scrambled all-noise state). */
	ret = devm_regulator_get_enable_optional(dev, "power");
	if (ret && ret != -ENODEV)
		return ret;	/* -EPROBE_DEFER until mipi_cam_pwen is up */
	msleep(100);

	/* Full GPIO reset per vendor driver:
	 * LOW(2ms) -> HIGH(120ms) -> LOW(300ms) */
	if (priv->reset_gpio) {
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		usleep_range(120000, 121000);
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		usleep_range(300000, 310000);
	}

	/* Register-access latch: on this UXC2 silicon the polarity is
	 * INVERTED vs the vendor UXC driver - 0x80EE=0x01 closes the
	 * latch (all banks read 0x00), 0x80EE=0x00 keeps it open.
	 * Verified empirically 2026-11-20. */
	{
		int i;

		for (i = 0; i < 5; i++) {
			ret = lt6911uxc2_write(priv, 0x80EE, 0x00);
			if (!ret)
				break;
			msleep(50);
		}
	}
	msleep(50);

	ret = lt6911uxc2_check_chip_id(priv);
	if (ret)
		return ret;

	mutex_init(&priv->lock);

	INIT_DELAYED_WORK(&priv->work_hotplug, lt6911uxc2_enable_hotplug_work);
	INIT_DELAYED_WORK(&priv->work_res_change, lt6911uxc2_res_change_work);

	/* V4L2 subdev (controls omitted for bring-up) */
	v4l2_i2c_subdev_init(&priv->sd, client, &lt6911uxc2_ops);
	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			 V4L2_SUBDEV_FL_HAS_EVENTS;
	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&priv->sd.entity, 1, &priv->pad);
	if (ret)
		goto err_entity;

	/* Controls: dw-mipi-csi2rx derives the D-PHY config from
	 * V4L2_CID_LINK_FREQ on its source pad, so it is mandatory. */
	v4l2_ctrl_handler_init(&priv->ctrl_hdl, 1);
	v4l2_ctrl_new_int_menu(&priv->ctrl_hdl, NULL, V4L2_CID_LINK_FREQ,
			       0, 0, lt6911uxc2_link_freq_menu);
	if (priv->ctrl_hdl.error) {
		ret = priv->ctrl_hdl.error;
		goto err_ctrl;
	}
	priv->sd.ctrl_handler = &priv->ctrl_hdl;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret) {
		dev_err(dev, "failed to register subdev: %d\n", ret);
		goto err_entity;
	}

	/* IRQ for format change */
	priv->irq = client->irq;
	if (priv->irq > 0) {
		ret = devm_request_threaded_irq(dev, priv->irq, NULL,
						lt6911uxc2_irq_handler,
						IRQF_ONESHOT |
						IRQF_TRIGGER_FALLING,
						LT6911UXC2_NAME, priv);
		if (ret) {
			dev_warn(dev, "failed to request IRQ: %d\n", ret);
			priv->irq = 0;
		}
	}

	/* Enable hotplug detection */
	schedule_delayed_work(&priv->work_hotplug, msecs_to_jiffies(100));

	dev_info(dev, "LT6911UXC2 HDMI-to-MIPI CSI bridge driver probed\n");

	return 0;

err_entity:
	media_entity_cleanup(&priv->sd.entity);
err_ctrl:
	v4l2_ctrl_handler_free(&priv->ctrl_hdl);
	mutex_destroy(&priv->lock);
	return ret;
}

static void lt6911uxc2_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct lt6911uxc2 *priv = to_lt6911uxc2(sd);

	cancel_delayed_work_sync(&priv->work_hotplug);
	cancel_delayed_work_sync(&priv->work_res_change);
	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&priv->ctrl_hdl);
	mutex_destroy(&priv->lock);
}

static const struct of_device_id lt6911uxc2_of_match[] = {
	{ .compatible = "lontium,lt6911uxc2" },
	{ .compatible = "lontium,lt6911uxc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lt6911uxc2_of_match);

static struct i2c_driver lt6911uxc2_driver = {
	.driver = {
		.name = LT6911UXC2_NAME,
		.of_match_table = lt6911uxc2_of_match,
	},
	.probe = lt6911uxc2_probe,
	.remove = lt6911uxc2_remove,
};

module_i2c_driver(lt6911uxc2_driver);

MODULE_AUTHOR("Really Board Support <build@really.local>");
MODULE_DESCRIPTION("Lontium LT6911UXC2 HDMI to MIPI CSI bridge driver");
MODULE_LICENSE("GPL");
