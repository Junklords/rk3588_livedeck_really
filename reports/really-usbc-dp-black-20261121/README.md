# Really USB-C DP 黑屏定位与修复记录

Author: Codex (GPT-5)

Generated: 2026-11-21T12:00:00+08:00

Context: Really TVI2340R 的 RK3588 USB-C DisplayPort Alt Mode 可以完成
HPD、EDID 和链路训练，但显示器只有背光、没有图像。问题在直连一线通
显示器和 `Type-C -> 扩展坞 -> HDMI` 两条链路上都能复现。

Applicable branch: `/home/user/github/build` Armbian edge, Linux 7.3.y
rockchip64；设备 DTS 源位于 `dts-7.3-linux/`。

Environment: Really TVI2340R, RK3588, HUSB311 TCPC, Type-C DP Alt Mode
with dynamic two/four-lane pin assignments, VOP2, internal LT9211
dual-link LVDS panel.

## 最终可用配置

三项改动共同构成当前已经实机验证的基线：

1. `&dp0` 保留 `rockchip,force-8bpc`，避免显示器选择 10/12/16bpc。
2. `&dp0` 增加 `rockchip,max-link-rate = <540000>`，把链路限制为
   HBR2（5.4 Gbps/lane）。
3. DP0 改走 VP0，HDMI1 改走 VP1，LVDS 继续使用 VP3。

```dts
&dp0 {
	rockchip,force-8bpc;
	rockchip,max-link-rate = <540000>;
	status = "okay";
};

&dp0_in {
	dp0_in_vp0: endpoint {
		remote-endpoint = <&vp0_out_dp0>;
	};
};

&hdmi1_in {
	hdmi1_in_vp1: endpoint {
		remote-endpoint = <&vp1_out_hdmi1>;
	};
};

&vp0 {
	vp0_out_dp0: endpoint@ROCKCHIP_VOP2_EP_DP0 {
		reg = <ROCKCHIP_VOP2_EP_DP0>;
		remote-endpoint = <&dp0_in_vp0>;
	};
};

&vp1 {
	vp1_out_hdmi1: endpoint@ROCKCHIP_VOP2_EP_HDMI1 {
		reg = <ROCKCHIP_VOP2_EP_HDMI1>;
		remote-endpoint = <&hdmi1_in_vp1>;
	};
};
```

内核侧使用 opt-in patch：

```text
userpatches/kernel/archive/rockchip64-7.3/
0018-drm-bridge-dw-dp-allow-board-link-rate-cap.patch
```

该补丁只增加 `rockchip,max-link-rate` 设备属性，不改变其它 RK3588 单板
的默认 HBR3 行为。

## 定位过程中排除的方向

### 8bpc 不是唯一根因

`0016-really-prefer-8bpc-dp-and-keep-husb-irq.patch` 和 DTS 的
`rockchip,force-8bpc` 已生效。现场 MSA 为：

```text
DW_DP_VIDEO_MSA2 = 0x20000000
MISC0            = 0x20
```

这明确表示 RGB444、8bpc。仅设置 8bpc 后仍然黑屏。

### 4K60 EDID 能力不是 4K30 黑屏原因

显示器确实在 EDID 中声明 4K60，但手动选择 1080p60 和 4K30 都黑屏。
问题不在 EDID 是否列出 4K60。

### HBR3 训练成功不等于视频流可用

在旧配置下，DP 链路实际训练结果：

```text
full-training link: 2 lanes at 8100 MHz
clock recovery succeeded
channel equalization succeeded
```

虽然 CR/EQ 均成功，sink 仍无法解码视频。最终限制到 HBR2 后：

```text
full-training link: 2 lanes at 5400 MHz
clock recovery succeeded
channel equalization succeeded
```

画面恢复。由此确认问题至少位于 HBR3 下的视频流/高速信号路径，而不是
AUX、HPD、EDID 或 DPCD 训练失败。

### V0PLL 266.64 MHz 实验已撤销

中途曾把 4K30 误判为需要 `266.64 MHz`，并试图用 V0PLL 固定
`DCLK_VOP1_SRC`。实机证明标准 4K30 模式的 VOP 计时是 `297 MHz`，强制
`266.64 MHz` 会造成额外时钟失配。相关 V0PLL 参数和固定父时钟补丁均已
删除。

最终 VP0 路由下：

```text
dclk_vop0_src parent = gpll
dclk_vop0_src rate   = 297000000
```

## 实机验证结果

运行内核：

```text
Linux really 7.3.5-edge-rockchip64 #82 SMP PREEMPT
```

扩展坞链路 `Type-C -> dock -> HDMI`：

```text
DP-1 connected
Video Port0 ACTIVE
DP bus format RGB888_1X24
1920x1080p60: image OK
3840x2160p30: image OK
```

直连链路 `Type-C one-cable -> monitor`：

```text
full-training link: 4 lanes at 5400 MHz
clock recovery succeeded
channel equalization succeeded
DP bus format: RGB888_1X24
DP MSA: RGB444, 8bpc
3840x2160p30: image OK
3840x2160p60: image OK and stable
```

升级后的 VOP 拓扑：

```text
Video Port0 -> DP0
Video Port1 -> HDMI1
Video Port3 -> DSI1 -> LT9211 -> dual-link LVDS
```

在正确绘制后，DP 通用中断中的
`VIDEO_FIFO_OVERFLOW_STREAM0` 状态可清为 0，并在持续出图期间保持为 0。

## two-lane 与 four-lane 的运行时选择

板级 DTS 保留 `<2 3>`，表示优先支持“两 lane DP + USB3”的映射。运行时
Type-C mux 会根据伙伴的 pin assignment 选择最终模式：

- `C/E`：四条高速 pair 全部切给 DP，PHY 报告 4 lanes。直连显示器属于
  这一类，已验证 four-lane HBR2 RGB444 4K60。
- `D/F`：两 lane 给 DP，另外两 lane 保留 USB3。当前扩展坞属于这一类，
  在 HBR2 上限下验证 4K30。

因此不需要把 `rockchip,dp-lane-mux` 静态改成 `<0 1 2 3>`。静态改成
four-lane 反而会破坏 D/F 下“两 lane DP + USB3”的映射语义。

## 当前限制与待回归项

- four-lane 模式占用全部 Type-C 高速 pair，不能同时使用 USB3。
- 两 lane 扩展坞模式在 HBR2 上限下只能验证 4K30；完整 RGB 4K60 依赖
  Type-C pin assignment 进入 `C/E` four-lane 模式。
- 尚未做单变量二分来证明“VP0 路由”和“HBR2 限速”各自是否单独充分；
  当前只把两者同时实机验证通过的组合作为生产基线。
- 需要补做多次冷启动、热插拔和 Type-C 正反插回归。
- HDMI1 已从 VP0 移到 VP1。HDMI 输出逻辑未改，但重新布线后仍需在实物
  显示器上补一次回归。
- HUSB311 冷启动电源角色协商仍有历史不稳定记录，需要把直连显示器的
  sink 场景与供电扩展坞的 source 场景都加入冷启动矩阵。

## 复现与检查命令

```bash
cat /sys/class/drm/card1-DP-1/status
cat /sys/kernel/debug/dri/display-subsystem/vop2/summary

for f in dclk_vop0 dclk_vop0_src; do
  printf '%s parent=' "$f"
  cat "/sys/kernel/debug/clk/$f/clk_parent"
  printf '%s rate=' "$f"
  cat "/sys/kernel/debug/clk/$f/clk_rate"
done

echo 'module dw_dp +p' > /sys/kernel/debug/dynamic_debug/control
dmesg | grep -E 'full-training|clock recovery|channel equalization'
```

预期训练日志必须出现 `2 lanes at 5400 MHz`；若再次出现 `8100 MHz`，
说明 `rockchip,max-link-rate` 没有进入运行 DTB。
