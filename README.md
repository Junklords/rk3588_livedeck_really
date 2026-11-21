# Really TVI2340R RK3588 直播机设备树

[English version](README.en.md)

本仓库保存 Really TVI2340R RK3588 直播机的设备树逆向与移植结果。硬件事实以 Android 12 / vendor 5.10.110 系统提取的工厂 DTB 为准，并按 Android、vendor Linux 和 mainline Linux 三套启动语义分别维护。

Linux 7.3 的当前板级改动和实测边界见
[`dts-7.3-linux/FIXES.md`](dts-7.3-linux/FIXES.md)。

音频专项的原厂证据、当前失败边界、安全测试要求、待办和后续 AI 强制
进度回填规则见
[`reports/audio-mic-linein-20261117/README.md`](reports/audio-mic-linein-20261117/README.md)。

Android 与原厂 5.10 的显示方向匹配机制、Linux 7.3 横屏闪现原因和后续
优化顺序见
[`reports/display-orientation-android-vs-linux-20261117/README.md`](reports/display-orientation-android-vs-linux-20261117/README.md)。

锁屏触摸唤醒、恢复后方向校正和首次启动 GDM/控制台竞争的分析与修复见
[`reports/really-lock-orientation-firstboot-20261118/README.md`](reports/really-lock-orientation-firstboot-20261118/README.md)。

USB-C DP 黑屏的定位、候选路径和最终 HBR2 基线见
[`reports/really-usbc-dp-black-20261121/README.md`](reports/really-usbc-dp-black-20261121/README.md)。

当前完整状态、触控 Android 基线和群友 AI 接手顺序见
[`docs/AI-HANDOVER-CHECKLIST-20261115.md`](docs/AI-HANDOVER-CHECKLIST-20261115.md)。

vendor 6.1 的现有修复、7.3 回移项与明确不适用项见
[`dts-6.1-linux/FIXES.md`](dts-6.1-linux/FIXES.md)。

2026-11-15 的 mainline 实机未闭环问题、现场日志和已排除项见
[`reports/mainline-open-issues-20261115-2130/README.md`](reports/mainline-open-issues-20261115-2130/README.md)。

HDMI IN 两路（面板内置 HDMIRX / 内部 LT6911UXC2）的待办、已排除项与
验收标准见
[`reports/hdmi-in-dual-path-20261116/README.md`](reports/hdmi-in-dual-path-20261116/README.md)。

TF 卡已按 community 启动链重写并完成冷启动验证：
[`reports/tf-write-community-chain-20261116/README.md`](reports/tf-write-community-chain-20261116/README.md)。

2026-11-16 的 late-bind、U-Boot 电源、USB 休眠与 PHY LED 审计见
[`reports/workaround-audit-20261116/README.md`](reports/workaround-audit-20261116/README.md)。

5.10 / 6.1 设备树补齐原厂电源拓扑的原因、缺口清单和验证状态见
[`docs/DTS-POWER-RESTORE-20261117.md`](docs/DTS-POWER-RESTORE-20261117.md)。

## 版本目录

| 目录 | 目标系统 | 主要差异 |
| --- | --- | --- |
| `dts-5.10-android` | Android 12 / vendor 5.10 | FIQ debugger、Android 启动参数、vendor 媒体与加速节点 |
| `dts-6.1-linux` | Armbian / vendor 6.1 | 普通 UART2 控制台、Linux 根设备与 vendor 外设栈 |
| `dts-7.3-linux` | Linux 7.3 mainline | mainline MMC、PCIe、USB、VOP2、NPU、RGA、VPU/VEPU、RKVDEC、AV1 |
| `dtb-5.10-android` | 工厂原始 DTB | 只保存提取文件，不保存任何重建产物 |

工厂 DTB：

```text
dtb-5.10-android/rk3588-really.dtb
SHA256: D516E8F62CE040E76CAB2CBC394DBE46AB53F3F82E9CEC1ED8E5714F51358521
```

## 板级硬件

| 功能 | 配置 |
| --- | --- |
| SoC | RK3588，4×Cortex-A76 + 4×Cortex-A55 |
| 内存 | 工厂 DTS 标示为 LPDDR4 拓扑 |
| 主存储 | 8 bit eMMC 5.1，HS400，不可移除 |
| SD 卡 | 4 bit SDMMC，UHS SDR104，独立 3.3 V 供电控制 |
| 内屏 | 15.6 吋竖屏；DSI1 4-lane → LT9211 → 双通道 odd/even 24 bit VESA LVDS。控制器扫描 1920×1080@60，DT 提供 270° 方向，桌面和 fbcon 均呈 1080×1920 竖屏 |
| 备用内屏 | eDP0 是板级物理接口和可选内屏装配路径；同一 DTS 保留 VP2/eDP0/AUX panel 拓扑，但默认将控制器与整条 route 标为 disabled |
| 外部显示 | HDMI1；DP0（USB-C DP Alt Mode）采用 two-lane HBR2 + 8bpc，直连和扩展坞链路均已验证 4K30 |
| 有线网络 | GMAC0，RGMII，PHY 地址 1 |
| PCIe | PCIe 2.0 x1 lane 1 与 lane 2，两路均启用 |
| USB | USB3 OTG0、USB3 Host1、两路 USB2 Host、HUSB311 Type-C 双角色电源/数据，Sink/Source 均为固定 5V/3A |
| 音频 | ES8388 + I2S1（扬声器/耳机/Audio 耳麦/LINE IN）；内置麦克风走 PDM0；另有 HDMI1/DP0 音频 |
| RTC | HYM8563，I2C4 `0x51`，GPIO0_B0 中断 |
| 触摸 | Goodix GT9110，I2C4 `0x14` |
| HDMI 输入 | RK3588 内置 HDMI RX，HPD/检测 GPIO4_C6；LT6911UXC2，I2C6 `0x2b` |
| 摄像头 | 默认 USB UVC，5 V 使能 GPIO4_A6；IMX577 MIPI 接口位于核心板 |
| 无线 | AP6275P，Wi-Fi + Bluetooth |
| 风扇 | PWM2，周期 40000 ns（25 kHz），反相输出 |
| 看门狗 | 内部 RK3588 DW watchdog 与外部 GPIO watchdog GPIO0_D3 均显式禁用 |

## Mainline 7.3 当前实测状态

截至 `2026-11-17T22:00:00+08:00`，运行现场为
`7.3.5-edge-rockchip64`，U-Boot
`2026.07-Sece3-P743c-Hae08-V6f98-Bd800-R448a`，根文件系统为
`/dev/mmcblk1p1`（TF 卡，UUID `7faf7863-d5d5-419e-81bd-902914de6883`）。


| 功能 | 当前状态 |
| --- | --- |
| LVDS 内屏 | `LVDS-1 connected`，双链路画面正常 |
| HDMI OUT | 当前未接显示器，`HDMI-A-1 disconnected` |
| USB-C DP | ✅ **基线可用**。DP0→VP0，两 lane HBR2（5.4 Gbps/lane），RGB444 8bpc；1080p60、4K30、直连一线通和 Type-C 扩展坞均已出图 |
| GT9110 触控 | ✅ **已工作**。`ID 9110`；随 270° 面板方向使用 `inverted-x + swapped-x-y`，九点映射验证通过 |
| HYM8563 RTC | ✅ **已工作**。`registered as rtc0` |
| I2C4 控制器 | `0x14` / `0x2d` / `0x51` 均正常绑定；`0x56` EEPROM 不 ACK |
| STK8BA50 加速度计 | ✅ **已工作**。I2C6 `0x18` 绑定，GPIO4_B7 中断正常；内建驱动通过 mount matrix 校准，并由 `really-auto-rotate.service` 自动旋转 LVDS |
| AP6275P Wi-Fi | ✅ **已工作**。固件 BCM43752/2 `18.35.387.23.146` 启动，`wlP4p65s0` 已连接 `HUAWEI-7409_5G` |
| AP6275P Bluetooth | ✅ **已工作**。`hci0` 识别为 `BCM43752A2`，控制器已上电 |
| TF 卡启动 | ✅ **已闭环，但依赖 eMMC 中的社区 U-Boot**。eMMC 前 16 MiB 全零时 BootROM 不会读取 SD 并直接进入 Maskrom；写入与镜像匹配的 `u-boot-rockchip.bin` 后，系统可从 TF 正常启动 |
| 音频 | ⚠️ **部分闭环**。ES8388 扬声器 DAPM/模拟链路已验证，默认 sink 与输出档位已修正；PDM 内置麦克风冷启动后有短时非零数据，但持续采集仍会停顿；Audio 耳麦、LINE IN、耳机自动切换仍待完善 |

### I2C4 设备修复说明

触控与 RTC 曾经长期不工作，根因**不是**硬件、排线或 Android 移植，而是
mainline DTS 缺少原厂的模块供电拓扑：

- `vcc1v8_sys`（GPIO0_C0 控制）原本是个没有 pinctrl 的裸 `gpio-hog`，从未真正拉高；
- `vcc28_dvp` 根本没定义；
- 触控的 `AVDD28-supply` / `VDDIO-supply` 指向了错误的供电轨。

补全后 `vcc1v8_sys 1800mV → 4-0014-VDDIO`、`vcc28_dvp 2800mV →
4-0014-AVDD28`。U-Boot 只用板级 DTS 的标准 GPIO hog 提前拉高
GPIO0_C0；Linux 启动时 GT9110 和 HYM8563 都由首次原生 probe 直接绑定，
不再依赖用户态 late-bind。

LT9211（`0x2d`）一直正常，是因为它挂在常开的 `vcc_1v8_s3` 上，不受
`vcc1v8_sys` 开关影响——这个对照关系是定位问题的关键。

经过禁用服务的对照冷启动，GT9110、HYM8563、LT9211 和 AP6275P
均在首次 probe 中成功。以下旧 workaround 已从镜像构建流程删除：

- `really-touch-late-bind`
- `really-rtc-late-bind`
- `really-wifi-late-load`
- `really-usb-hid-recovery` 的 30 秒强制重授权 timer
- `really-phy-leds` 常驻 service

USB 键盘接收器与其 GL3523 USB2 hub 改由窄匹配 udev 规则永久关闭
runtime autosuspend；PHY LED 的 `netdev` trigger 和 `end0` 关联来自
主设备树，udev 只设置 YT8531 的链路/活动硬件规则。

GPIO1_D0 实际上是触摸/面板共用使能，不是 eDP 专属信号。当前 DTS 用
独立常开的 `touch_panel_pwr` regulator 保持原厂上电状态，因此无论
LVDS 装配还是未来 eDP 装配都适用。当前机型没有 eDP 面板，eDP0、
VP2 endpoint、eDP in/out endpoint 和 AUX panel 均保留但默认
`disabled`，避免瞬时 HPD 误判给 fbcon 注入 1024×768 fallback。

触控方向不能只照搬原厂 DTB。原厂方向逻辑在 GT9xx 驱动中，而主线由
`touchscreen-inverted-x`、`touchscreen-inverted-y`、
`touchscreen-swapped-x-y` 组合表达。实机九点回归最终确定为：

```dts
touchscreen-inverted-x;
touchscreen-swapped-x-y;
```

输入范围随 swap 变为 X=`0..1919`、Y=`0..1079`；GNOME 九宫格
TL/TC/TR、ML/C/MR、BL/BC/BR 全部落在对应目标。

修复细节见
[`reports/uboot-tf-boot-power-20261116/README.md`](reports/uboot-tf-boot-power-20261116/README.md)
与
[`reports/mainline-open-issues-20261115-2130/README.md`](reports/mainline-open-issues-20261115-2130/README.md)。

## 硬件拓扑

```text
12 V DC
  |
  +--> vcc5v0_sys --> RK806 PMIC --> CPU-L / GPU / DDR / VDENC / 3V3 / 1V8
  |
  +--> vcc5v0_usb --> USB host / USB3 / Type-C VBUS

                         +----------------+
                         |     RK3588     |
                         +--+--+--+--+--+-+
                            |  |  |  |  |
              +-------------+  |  |  |  +------------------+
              |                |  |  |                     |
         eMMC 8-bit      SDMMC |  |  GMAC0/RGMII       PCIe 2.0 x1
         HS400                |  |     |                lane 1 + lane 2
                              |  |  PHY@1
                              |  |
                              |  +--> USB3 OTG0 --+--> Type-C / HUSB311
                              |      USB3 Host1   +--> USB3-A
                              |      USB2 Host0/1 ---> USB2-A
                              |
                              +--> VOP2
                                     |
                                     +--> HDMI1
                                     +--> DP0 --> Type-C Alt Mode
                                     +--> eDP0 --> eDP panel
                                     +--> DSI1 --> LT9211 --> LVDS panel

I2C0: RK8602 0x42 (CPU Big0), RK8603 0x43 (CPU Big1)
I2C1: RK8602 0x42 (NPU)
I2C3: ES8388 0x11
I2C4: IMX577 0x1a (core board), GT9110 0x14, LT9211 0x2d, HYM8563 0x51,
      24C04 0x56 (DTS 与原厂一致，当前硬件不 ACK)
I2C6: LT6911UXC2 0x2b, STK8BAxx 0x18, HUSB311 0x4e
```

显示链路：

```text
VOP2 VP0 --> DP0 --> USB-C DP Alt Mode
VOP2 VP1 --> HDMI1 --> HDMI-A
VOP2 VP2 --> eDP0 --> optional internal eDP panel
VOP2 VP3 --> DSI1 4-lane --> LT9211 --> dual-link LVDS 24-bit
                                              |
                          controller scan 1920x1080
                                              |
                       DT orientation 270 -> 1080x1920
                                              |
                                  15.6-inch portrait panel
```

HDMI 输入（两条独立通路，物理口不同）：

```text
面板 HDMI IN --> RK3588 built-in HDMI RX @fdee0000 --> V4L2 capture
HDMI audio   --> I2S7 --> rockchip,hdmiin sound card

机身内部隐藏口（不引到面板）
             --> LT6911UXC2 @I2C6 0x2b
             --> MIPI DPHY0 --> MIPI CSI2 #2 --> RKCIF2
```

面板口拆机确认无 LT6911 桥接芯片，直连 SoC 内置 HDMIRX；
LT6911UXC2 只存在于机身内部的隐藏接口。Android 下自写
`hdmi-viewer` 走的就是内置 HDMIRX（`/dev/video31`，用 `VIDIOC_S_EDID`）。
两路 mainline 均未闭环，见
[`reports/hdmi-in-dual-path-20261116/README.md`](reports/hdmi-in-dual-path-20261116/README.md)。

存储与扩展：

```text
eMMC 5.1      --> mmc0
SD card       --> mmc1
PCIe lane 1   --> combphy2_psu
PCIe lane 2   --> combphy0_ps
combphy1_ps   --> disabled
SATA0 / PCIe3 --> disabled
```

## GPIO 分配

引脚以 Rockchip bank + port + index 表示，有效电平取自工厂 DTB。

| 功能 | 引脚 | 有效电平 | 设备树用途 |
| --- | --- | --- | --- |
| Type-C PD 中断 | GPIO0_B7 | Low | HUSB311 interrupt |
| RTC 中断 | GPIO0_B0 | Low | HYM8563 interrupt |
| 外部看门狗 | GPIO0_D3 | High | SGM760B GPIO watchdog，显式禁用 |
| 触摸 reset | GPIO1_D3 | Low | GT9110 `reset-gpios` |
| 触摸/面板共用使能 | GPIO1_D0 | High | 常开 `touch_panel_pwr`，同时服务 LVDS 触摸和可选 eDP 面板 |
| 触摸中断 | GPIO4_B0 | Low | GT9110 interrupt |
| GMAC0 PHY reset | GPIO2_C4 | Low | Ethernet PHY reset |
| LT9211 reset | GPIO3_A6 | High | DSI-to-LVDS bridge reset |
| LVDS 背光 enable | GPIO3_B4 | High | PWM backlight enable |
| LVDS 电源 enable | GPIO3_B6 | High | LVDS panel power |
| HDMI1 enable | GPIO3_C5 | High | HDMI1 power/enable |
| HDMI RX HPD/检测 | GPIO4_C6 | Low | Built-in `hdmirx`/`hdmi_receiver` |
| LT6911 reset | GPIO3_A7 | High | LT6911UXC2 `reset-gpios` |
| LT6911 HPD control | GPIO2_C3 | High | LT6911UXC2 `hpd-ctl-gpios` |
| LT6911 interrupt | GPIO1_C7 | Low | LT6911UXC2 interrupt |
| PCIe lane 2 reset | GPIO3_B7 | High | AP6275P `pcie2x1l2` |
| PCIe lane 1 reset | GPIO1_A7 | High | `pcie2x1l1` |
| SD 卡电源 | GPIO4_C3 | High | `vcc_sd` |
| Type-C VBUS enable | GPIO1_D1 | High | HUSB311 source-role VBUS control |
| USB-A host 5 V enable | GPIO3_A5 | High | `vcc5v0_host` |
| eDP 背光 enable | GPIO4_B6 | High | PWM6 backlight enable |
| 扬声器功放 enable | GPIO4_B2 | Low | ES8388 speaker amplifier |
| 耳机功放 enable | GPIO4_B4 | High | ES8388 headphone amplifier |
| 耳机检测 | GPIO4_B3 | High | ES8388 headphone detect |
| 电源灯 | GPIO3_C0 | High | GPIO LED |

触控 GPIO 表中的 Low 是原厂 vendor 驱动语义；mainline Goodix 驱动使用
ACTIVE_HIGH reset/IRQ，并在复位阶段驱动 IRQ 选择 `0x14` 地址。最终
实机修复来自共享电源链，而不是改变 reset/IRQ 极性。

按键使用 SARADC1：

| 阈值 | 功能 |
| --- | --- |
| 17 mV | Volume Up |
| 417 mV | Volume Down |
| 890 mV | Menu |
| 1235 mV | Back |

## PWM 与热管理

| 功能 | 控制器 | 周期 / 极性 |
| --- | --- | --- |
| LVDS 背光 | PWM12 | 1000000 ns（1 kHz），正相 |
| eDP 背光 | PWM6 | 25000 ns（40 kHz），正相 |
| 风扇 | PWM2 | 40000 ns（25 kHz），反相 |

7.3 和 6.1 均将风扇描述为标准 `pwm-fan`，冷却级数为
`0/76/153/204`。7.3 使用 package thermal cooling map，6.1 使用
vendor 的 `rockchip,temp-trips` 两档策略。

## 看门狗状态

Really 额外包含一个 GPIO 控制的外部看门狗，工厂 DTB 使用 GPIO0_D3、Level 型喂狗和 500 ms 硬件裕量。三套重建 DTS 均将其显式写为 `status = "disabled"`。

RK3588 内部 DW watchdog `watchdog@feaf0000` 也按原厂状态在三套 DTS 中显式设为 `status = "disabled"`。因此当前设备树不注册 `/dev/watchdog`，不会与用户态喂狗服务产生隐式依赖。

如果后续需要启用，应只选择内部或外部其中一种，并同时配置 systemd `RuntimeWatchdogSec` 或专用 watchdog daemon。

## DTS 状态策略

工厂 FDT 中明确启用的启动相关节点包括：

- RK806 PMIC、RK8602/RK8603 CPU 供电和 RK8602 NPU 供电；
- eMMC、SD、GMAC0、PCIe 2.0 lane 1/lane 2；
- USB3 OTG0/Host1、USB2 Host0/1、HUSB311 Type-C；
- VOP2、DSI1/LT9211/LVDS、eDP0、HDMI1、DP0；
- RK3588 内置 HDMI RX、GPIO4_C6 HPD/检测和 I2S7 HDMI-in 音频；
- ES8388、HDMI1 音频、RTC、触摸、风扇；
- NPU、RGA2/RGA3、JPEG、VENC/VDEC、IEP、AV1、MPP、RKCIF/RKISP 等 SoC 节点。

工厂中关闭或未启用：

- SDIO、SATA0、PCIe 3.0、`pcie2x1l0`；
- HDMI0、DP1、eDP1、DSI0；
- 不属于本板 BOM 的额外 PCIe/SATA/显示组合。

5.10 与 6.1 版本保留 vendor 媒体和加速节点，并启用 LT6911UXC2 → DPHY0 → MIPI CSI2 → RKCIF2 采集链路。7.3 mainline 使用自身 binding，显式启用三核 RKNN NPU、RGA3/RGA2、VPU121、四核 VEPU121 JPEG 编码、RKVDEC0/1 和 AV1 解码及其 IOMMU；LT6911UXC2 已按 mainline VICAP graph 写入设备树，但该版本内核仍需确认 UXC2 驱动兼容性。

工厂 DTB 同时描述两条可选内屏路径：DSI1/LT9211/LVDS 与 VP2/eDP0。
当前 BOM 默认装配 LVDS 方案。7.3 在同一份 DTS 中保留完整的 eDP0、
VP2、PWM6 背光和 AUX panel 描述，但默认全部 disabled；启用实际 eDP
组件时只需在同一份 DTS 中打开这些状态，不需要重建 route。

## Linux 7.3 移植内容

`dts-7.3-linux/rk3588-really.dts` 已按 mainline binding 重建以下部分：

- RK806 单 PMIC、RK8602/RK8603 供电和八核 CPU OPP 引用；
- UART2/M0 `1500000n8` 控制台；
- eMMC HS400、SD UHS SDR104；
- GMAC0/RGMII、PHY@1、`tx_delay = 0x44` 和 GPIO2_C4 PHY reset；
- PCIe 2.0 lane 1/lane 2、对应的 combphy 资源、reset 与 3.3 V 供电；
- USB2/DP/USB3 PHY、DWC3、HUSB311 Type-C 双角色切换；VBUS 仅由 TCPM 控制，
  不再由 USB2 PHY 无条件拉起；
- VOP2 到 HDMI1、DP0、eDP0、DSI1 的四条显示通路；
- `hdmi_receiver` 内置 HDMI RX、160 MiB CMA、GPIO4_C6 HPD 和 HDMI RX pinmux；
- LT6911UXC2、27 MHz 外部时钟、GPIO3_A7 reset、GPIO2_C3 HPD、GPIO1_C7 中断，以及 `csi_dphy0 → csi2 → vicap` graph；
- `rknn_core_0/1/2` 三核 NPU、对应 IOMMU、`vdd_npu_s0` 与 `pd_npu` 电源域；
- `rga3_core0/1`、双核 RGA2、VPU121、`vepu121_0..3` JPEG 编码、`vdec0/1` 和 AV1 解码；
- VDENC/RKVDEC 电源域与 `vdd_vdenc_s0` 的 mainline 供电绑定；
- LT9211 DSI→LVDS、GT9110 触摸、HYM8563 RTC、ES8388 声卡；
- STK8BA50 加速度计，驱动内建；
- LT9211 双通道 odd/even LVDS、原厂 DSI `BURST + LPM` flags 和板级可配置
  `lontium,dsi-mode-flags`；
- AP6275P 使用 HYM8563 的真实 32.768 kHz CLKOUT；RTC 晚绑定后才重新
  扫描 PCIe 并加载 `brcmfmac`，避免在 LPO 未运行时形成 inactive link；
- PWM2 风扇、PWM6 eDP 背光、PWM12 LVDS 背光；
- SARADC 与 GPIO 按键、温度保护和风扇冷却映射。

## 已知限制

1. 7.3 已启用 mainline 提供的 NPU、RGA、JPEG 编码、VPU 解码、RKVDEC 和 AV1 节点；RKCIF/RKISP 采集链与独立 RKVENC 节点在该版本的 RK3588 DTSI 中尚未提供。
2. AP6275P Wi‑Fi/BT 已通过冷启动验证。BCM43752 的 PCIe endpoint 必须等
   HYM8563 LPO 生效后再扫描；不要把启动顺序改回早期直接 probe。
3. eDP0 物理接口存在，但当前装配没有面板，因此默认整条 route disabled。
   这样可以避免空接口在冷启动时瞬时误报 connected，导致 fbcon 固定在
   1024×768。装配 eDP 面板时需在同一 DTS 中启用 eDP0、VP2 和 AUX
   panel 状态；面板应提供 EDID，无 EDID 时必须补准确时序。
4. 内屏是双通道 VESA-24 LVDS。只配一条 LVDS link 会把桌面裁成半幅并使像素时基错乱；原厂 mode 3 的 `0x855c = 0x07` 通道使能已由板级内核补丁补齐。
5. Type-C DP 基线已闭环：HBR3 虽能完成训练但其视频流异常，因此当前把
   链路限制为 two-lane HBR2，并强制 RGB444 8bpc；DP0 使用 VP0，
   HDMI1 使用 VP1。1080p60 和 4K30 已在直连及扩展坞两条链路出图。
   two-lane HBR2 不支持完整 RGB 4K60；4K60 属于后续实验项。
6. TF 启动不是“eMMC 完全为空也能直接启动”。BootROM 阶段没有把
   GPIO4_C3 的 SD 供电拉起，因此 eMMC 至少要保留社区
   `u-boot-rockchip.bin`；完整 rootfs 仍可从 TF 启动。详见
   `reports/tf-boot-empty-emmc-20261121/README.md`。
7. 设备树只描述内核侧硬件，正式启动仍依赖匹配的 loader、U-Boot、内存初始化和根文件系统。
