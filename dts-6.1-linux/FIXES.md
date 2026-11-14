# dts-6.1-linux 实测修复记录（vendor 6.1 内核，Armbian rk35xx BRANCH=vendor）

> 基准内核：`armbian/linux-rockchip` 分支 `rk-6.1-rkr7.2`（KERNEL_MAJOR_MINOR=6.1，
> 实测编译为 6.1.172-really-vendor）。以下修改均在 Really TVI2340R 实机验证通过：
> 系统完整启动、串口/eMMC/千兆网（大帧 0 丢包）/USB3/HDMI 1080p60 输出正常。

## 修复内容（对上游 dts 的 6 处变更）

### 1. `&uart0` → disabled（关键修复）

UART0 m2 引脚与 **SPI2-CS0（GPIO0_B1/pin9）冲突**。本板 RK806 PMIC 挂在 SPI2
（见 `rk3588-rk806-single.dtsi`）。uart0 使能时 SPI2 pinctrl 申请失败：

```
rockchip-pinctrl: pin gpio0-9 already requested by fd890000.serial; cannot claim for feb20000.spi
rockchip-spi feb20000.spi: Error applying setting
```

→ RK806 不 probe → 全部主调压器缺失（vdd_cpu_lit/gpu/log/vdenc/ddr…）→
cpufreq 放弃 + VOP/GPU/NPU/panel/dmc 全部 deferred，系统仅剩基础功能。
**禁用 uart0 后 RK806 正常，29 个调压器全部注册。**

### 2. `&pwm12` 移除板级 pinctrl 覆盖

vendor `rockchip-pwm` 驱动要求 pinctrl-names 为 `"active"`（dtsi 默认）。
板级写 `pinctrl-names = "default"` 会导致：

```
rockchip-pwm febf0000.pwm: No active pinctrl state
```

→ pwm12 不注册 pwmchip → backlight deferred → panel deferred →
**rockchip-drm 聚合整体被拖死（HDMI 陪葬）**。修复：删除板级覆盖，用 dtsi 默认。

### 3. DSI1 → LT9211 → panel-lvds 整链 disabled（v6.1 临时）

vendor 6.1 内核**没有 LT9211（lontium,lt9211）bridge 驱动**（主线 5.16 引入，
BSP 未回合）。DSI1 链的 remote bridge 永远等不到，rockchip-drm 聚合无法 bind，
`/dev/dri` 为空。禁用 `&dsi1 / &dsi1_in_vp3 / &route_dsi1 / lt9211 / panel-lvds /
backlight-lvds` 后 HDMI 独立成路，card0 正常出现。
（后续回合 lt9211 驱动后可恢复。）

### 4. `hdmi1-con`（hdmi-connector 节点）删除

主线写法的辅助 connector 节点，vendor DRM 图解析不需要且可能干扰。

### 5. `&hdmi1` 使用 `force-output` + `force_timing`（关键修复）

本板 **HDMI HPD 线未接进 SoC**（工厂 5.10 DTB 用 `force-hpd`；但 vendor 6.1 的
`dw_hdmi_qp` 驱动不识别 `force-hpd`——该属性仅 `dw-dp.c` 支持）。表现为
connector 永远 disconnected。vendor 6.1 正确机制（见
`dw_hdmi_qp-rockchip.c` `of_property_read_bool(np, "force-output")` +
`of_get_display_timing(np, "force_timing", ...)`）：

```dts
&hdmi1 {
	force-output;
	force_timing: force_timing { /* 1080p60 CEA 时序 */ ... };
};
```

实测：`card0-HDMI-A-1 connected/enabled`、VOP2 输出 1920x1080p60、fbcon 上屏。

### 6. rk3588s.dtsi SCMI 依赖（记录，dts 层无解）

rkr7.2 把 CPU/GPU/NPU/DMC/SDMMC/crypto/rng 时钟移交 `firmware/scmi`
（SMC 0x82000010）。原厂 OP-TEE 3.13 不实现 SCMI → `arm-scmi probe -22` →
上述设备 deferred（CPU 固定 boot 频率、SD 卡不可用）。CRU 中无对应时钟，
板级 dts 无法改回；需回退 rkr6 之前 dtsi 或更换支持 SCMI 的 trust（后者受
Boot1 校验限制不可行）。**不挡基本功能。**

## 附：这台设备（原厂 Boot1/U-Boot 链）的刷写要点

- boot image id@576 校验：两步哈希法（刷入 → U-Boot 拒绝打印 real → 写回）
- U-Boot shell 的 `mmc read/write` LBA 参数是**十六进制**：
  boot_a=0x13000（十进制 77824），id 扇区=0x13001，id 在第二扇区偏移 0x40
- maskrom loader 的裸 LBA 小扇区写入存在假成功（同会话回读命中 RAM 缓存），
  大块/整镜像或 U-Boot shell 直写可靠

## 实测启动日志关键证据

```
ANDROID: Hash OK
Linux version 6.1.172-really-vendor
Machine model: Really TVI2340R RK3588 Live Streaming Box
rk806 spi2.0: chip id: RK806,ver:0x2
rk_gmac-dwmac fe1b0000.ethernet: TX delay(0x44).   ← 工厂真值生效
end0: PHY [YT8531] ... 8000B 大帧 TX 0% 丢包（主线 7.3 时代 100% 丢）
rockchip-vop2: Update mode to 1920x1080p60 for vp0 ... if:HDMI1
card0-HDMI-A-1: connected enabled
```
