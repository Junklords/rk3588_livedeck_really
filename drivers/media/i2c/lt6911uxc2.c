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
#define LT6911UXC_CHIPID_H	0x8101
#define LT6911UXC_CHIPID_L	0x8100
#define LT6911UXC_CHIPID_VAL	0x0417  /* LT6911UXC */
#define LT6911UXC2_CHIPID_VAL	0x122C  /* LT6911UXC2 (实测 2026-11-19) */

#define LT6911UXC_I2C_EN_REG	0x80EE

/* HDMI timing registers */
#define LT6911UXC_HTOTAL_H	0x867c
#define LT6911UXC_HTOTAL_L	0x867d
#define LT6911UXC_HACT_H	0x8680
#define LT6911UXC_HACT_L	0x8681
#define LT6911UXC_VTOTAL_H	0x867a
#define LT6911UXC_VTOTAL_L	0x867b
#define LT6911UXC_VACT_H	0x867e
#define LT6911UXC_VACT_L	0x867f
#define LT6911UXC_HFP_H		0x8678
#define LT6911UXC_HFP_L		0x8679
#define LT6911UXC_HS_H		0x8672
#define LT6911UXC_HS_L		0x8673
#define LT6911UXC_HBP_H		0x8676
#define LT6911UXC_HBP_L		0x8677
#define LT6911UXC_VBP		0x8674
#define LT6911UXC_VFP		0x8675
#define LT6911UXC_VS		0x8671

#define LT6911UXC_TMDS_CLK_H	0x8750
#define LT6911UXC_TMDS_CLK_M	0x8751
#define LT6911UXC_TMDS_CLK_L	0x8752

#define LT6911UXC_MIPI_LANES	0x86a2
#define LT6911UXC_INT_STATUS_A	0x86a3
#define LT6911UXC_INT_STATUS_B	0x86a5
#define LT6911UXC_INT_COMPARE	0x86a6

#define LT6911UXC_STREAM_CTRL	0x811d
#define LT6911UXC_STREAM_ENABLE	0xFB
#define LT6911UXC_STREAM_DISABLE	0xBB

#define LT6911UXC_AUDIO_STATUS	0xb081
#define LT6911UXC_COLOR_FMT	0xb092

/* DT compatible */
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

/* --- Chip identity --- */

static int lt6911uxc2_check_chip_id(struct lt6911uxc2 *priv)
{
	u16 chip_id = 0;
	int ret, i;

	/* Pure reader: the latch is opened in probe() after the reset.
	 * The chip firmware needs up to ~2 s after reset release. */
	for (i = 0; i < 10; i++) {
		ret = lt6911uxc2_read16(priv, LT6911UXC_CHIPID_H,
					 LT6911UXC_CHIPID_L, &chip_id);
		if (!ret && chip_id)
			break;
		msleep(200);
	}

	if (ret && ret != -EIO)
		return ret;

	if (chip_id != LT6911UXC_CHIPID_VAL &&
	    chip_id != LT6911UXC2_CHIPID_VAL) {
		dev_warn(&priv->client->dev,
			 "unexpected chip ID 0x%04x (expected 0x%04x or 0x%04x), continuing\n",
			 chip_id, LT6911UXC_CHIPID_VAL, LT6911UXC2_CHIPID_VAL);
	}

	dev_info(&priv->client->dev, "LT6911UXC2 chip ID: 0x%04x\n", chip_id);
	return 0;
}

/* --- HDMI timing detection --- */

static int lt6911uxc2_get_detected_timings(struct lt6911uxc2 *priv,
					   struct v4l2_dv_timings *timings)
{
	u16 htotal, hact, vtotal, vact, hfp, hs, hbp, tmds_clk;
	u8 vs, vbp, vfp, lanes;
	int ret;

	memset(timings, 0, sizeof(*timings));
	timings->type = V4L2_DV_BT_656_1120;

	ret = lt6911uxc2_read16(priv, LT6911UXC_HTOTAL_H, LT6911UXC_HTOTAL_L,
				&htotal);
	if (ret)
		return ret;
	ret = lt6911uxc2_read16(priv, LT6911UXC_HACT_H, LT6911UXC_HACT_L,
				&hact);
	if (ret)
		return ret;
	ret = lt6911uxc2_read16(priv, LT6911UXC_VTOTAL_H, LT6911UXC_VTOTAL_L,
				&vtotal);
	if (ret)
		return ret;
	ret = lt6911uxc2_read16(priv, LT6911UXC_VACT_H, LT6911UXC_VACT_L,
				&vact);
	if (ret)
		return ret;
	ret = lt6911uxc2_read16(priv, LT6911UXC_HFP_H, LT6911UXC_HFP_L, &hfp);
	if (ret)
		return ret;
	ret = lt6911uxc2_read16(priv, LT6911UXC_HS_H, LT6911UXC_HS_L, &hs);
	if (ret)
		return ret;
	ret = lt6911uxc2_read16(priv, LT6911UXC_HBP_H, LT6911UXC_HBP_L, &hbp);
	if (ret)
		return ret;
	ret = lt6911uxc2_read16(priv, LT6911UXC_TMDS_CLK_H,
				LT6911UXC_TMDS_CLK_L, &tmds_clk);
	if (ret)
		return ret;
	ret = lt6911uxc2_read(priv, LT6911UXC_VS, &vs);
	if (ret)
		return ret;
	ret = lt6911uxc2_read(priv, LT6911UXC_VBP, &vbp);
	if (ret)
		return ret;
	ret = lt6911uxc2_read(priv, LT6911UXC_VFP, &vfp);
	if (ret)
		return ret;
	ret = lt6911uxc2_read(priv, LT6911UXC_MIPI_LANES, &lanes);
	if (ret)
		return ret;

	if (hact == 0 || vact == 0) {
		dev_dbg(&priv->client->dev, "no signal (hact=%u vact=%u)\n",
			hact, vact);
		return -ENOLINK;
	}

	/* Sanity gate: this UXC2's register map differs from the UXC1 map
	 * these offsets were taken from; unlocked/counter values would
	 * otherwise poison the media pipeline with absurd formats. */
	if (hact < 120 || hact > 8000 || vact < 120 || vact > 4400 ||
	    htotal <= hact || vtotal <= vact) {
		dev_dbg(&priv->client->dev,
			"implausible timing (h=%u/%u v=%u/%u), treating as no signal\n",
			hact, htotal, vact, vtotal);
		return -ENOLINK;
	}

	timings->bt.width = hact;
	timings->bt.height = vact;
	timings->bt.hfrontporch = hfp;
	timings->bt.hsync = hs;
	timings->bt.hbackporch = hbp;
	timings->bt.vfrontporch = vfp;
	timings->bt.vsync = vs;
	timings->bt.vbackporch = vbp;
	timings->bt.pixelclock = (u64)tmds_clk * 5000; /* TMDS/2 = pixel clock */
	timings->bt.interlaced = V4L2_DV_PROGRESSIVE;
	timings->bt.polarities = V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL;

	dev_info(&priv->client->dev,
		 "timing: %ux%u@%lluHz hfp=%u hs=%u hbp=%u vfp=%u vs=%u vbp=%u lanes=%u\n",
		 hact, vact, timings->bt.pixelclock,
		 hfp, hs, hbp, vfp, vs, vbp, lanes);

	return 0;
}

static int lt6911uxc2_g_tx_5v(struct lt6911uxc2 *priv)
{
	u8 status;
	int ret;

	ret = lt6911uxc2_read(priv, LT6911UXC_AUDIO_STATUS, &status);
	if (ret)
		return 0;
	return !!(status & BIT(0));
}

/* --- Stream control --- */

static int lt6911uxc2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct lt6911uxc2 *priv = to_lt6911uxc2(sd);
	int ret;

	dev_dbg(&priv->client->dev, "%s stream\n",
		enable ? "enable" : "disable");

	ret = lt6911uxc2_write(priv, LT6911UXC_STREAM_CTRL,
			       enable ? LT6911UXC_STREAM_ENABLE :
					LT6911UXC_STREAM_DISABLE);
	if (ret)
		dev_err(&priv->client->dev, "failed to %s stream: %d\n",
			enable ? "enable" : "disable", ret);

	return ret;
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
	u8 int_a, int_b;
	int ret;

	ret = lt6911uxc2_read(priv, LT6911UXC_INT_STATUS_A, &int_a);
	if (ret)
		return IRQ_HANDLED;
	ret = lt6911uxc2_read(priv, LT6911UXC_INT_STATUS_B, &int_b);
	if (ret)
		return IRQ_HANDLED;

	if (int_a & 0x01) {
		/* Format changed */
		schedule_delayed_work(&priv->work_res_change,
				      msecs_to_jiffies(50));
	}

	/* Clear interrupts */
	lt6911uxc2_write(priv, LT6911UXC_INT_STATUS_A, int_a);
	lt6911uxc2_write(priv, LT6911UXC_INT_STATUS_B, int_b);

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
