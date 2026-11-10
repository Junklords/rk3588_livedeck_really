# Really TVI2340R RK3588 直播机设备树

[English version](README.en.md)

本仓库保存 Really TVI2340R RK3588 直播机的设备树逆向与移植结果。硬件事实以 Android 12 / vendor 5.10.110 系统提取的工厂 DTB 为准，并按 Android、vendor Linux 和 mainline Linux 三套启动语义分别维护。

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
| 内屏 | DSI1 → LT9211 → 24 bit LVDS，1920×1080 |
| 备用内屏 | eDP0，EDID 面板链路 |
| 外部显示 | HDMI1、DP0（USB-C DP Alt Mode） |
| 有线网络 | GMAC0，RGMII，PHY 地址 1 |
| PCIe | PCIe 2.0 x1 lane 1 与 lane 2，两路均启用 |
| USB | USB3 OTG0、USB3 Host1、两路 USB2 Host、HUSB311 Type-C PD |
| 音频 | ES8388 + I2S1，HDMI1 音频，DP0 音频 |
| RTC | HYM8563，I2C4 `0x51`，GPIO0_B0 中断 |
| 触摸 | Goodix GT9110，I2C4 `0x14` |
| HDMI 输入 | RK3588 内置 HDMI RX，HPD/检测 GPIO4_C6；LT6911UXC2，I2C6 `0x2b` |
| MIPI 摄像头 | IMX577，I2C4 `0x1a` |
| 无线 | AP6275P，Wi-Fi + Bluetooth |
| 风扇 | PWM2，40 kHz，反相输出 |
| 看门狗 | 内部 RK3588 DW watchdog 与外部 GPIO watchdog GPIO0_D3 均显式禁用 |

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
I2C4: IMX577 0x1a, GT9110 0x14, LT9211 0x2d, HYM8563 0x51
I2C6: LT6911UXC2 0x2b, HUSB311 0x4e
```

显示链路：

```text
VOP2 VP0 --> HDMI1 --> HDMI-A
VOP2 VP1 --> DP0 --> USB-C DP Alt Mode
VOP2 VP2 --> eDP0 --> eDP panel
VOP2 VP3 --> DSI1 4-lane --> LT9211 --> LVDS 24-bit --> 1920x1080 panel
```

HDMI 输入：

```text
HDMI IN --> RK3588 built-in HDMI RX --> V4L2 capture
HDMI audio --> I2S7 --> rockchip,hdmiin sound card

Lontium LT6911UXC2 secondary path:
HDMI IN --> LT6911UXC2 --> MIPI DPHY0 --> MIPI CSI2 #2 --> RKCIF2
```

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
| Type-C PD 中断 | GPIO0_D7 | Low | HUSB311 interrupt |
| RTC 中断 | GPIO0_B0 | Low | HYM8563 interrupt |
| 外部看门狗 | GPIO0_D3 | High | SGM760B GPIO watchdog，显式禁用 |
| 触摸 reset | GPIO1_D3 | Low | GT9110 `reset-gpios` |
| eDP 面板 enable | GPIO1_D0 | High | eDP panel `enable-gpios` |
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
| PCIe lane 2 reset | GPIO3_C7 | High | `pcie2x1l2` |
| PCIe lane 1 reset | GPIO1_A7 | High | `pcie2x1l1` |
| SD 卡电源 | GPIO4_C3 | High | `vcc_sd` |
| Type-C VBUS enable | GPIO4_A1 | High | HUSB311 VBUS control |
| USB3 5 V enable | GPIO4_C6 | High | `vcc5v0_USB30` |
| eDP 背光 enable | GPIO4_B6 | High | PWM6 backlight enable |
| 扬声器功放 enable | GPIO4_A2 | Low | ES8388 speaker amplifier |
| 耳机功放 enable | GPIO4_A4 | High | ES8388 headphone amplifier |
| 耳机检测 | GPIO4_B3 | High | ES8388 headphone detect |
| 电源灯 | GPIO3_C0 | High | GPIO LED |

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
| LVDS 背光 | PWM12 | 1 MHz，正相 |
| eDP 背光 | PWM6 | 25 kHz，正相 |
| 风扇 | PWM2 | 40 kHz，反相 |

7.3 版本将风扇描述为标准 `pwm-fan`，冷却级数为 `0/76/153/204`，并保留 package thermal 的两级主动散热策略。

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

## Linux 7.3 移植内容

`dts-7.3-linux/rk3588-really.dts` 已按 mainline binding 重建以下部分：

- RK806 单 PMIC、RK8602/RK8603 供电和八核 CPU OPP 引用；
- UART2/M0 `1500000n8` 控制台；
- eMMC HS400、SD UHS SDR104；
- GMAC0/RGMII、PHY@1、`tx_delay = 0x44` 和 GPIO2_C4 PHY reset；
- PCIe 2.0 lane 1/lane 2、对应的 combphy 资源、reset 与 3.3 V 供电；
- USB2/DP/USB3 PHY、DWC3、HUSB311 Type-C 角色切换；
- VOP2 到 HDMI1、DP0、eDP0、DSI1 的四条显示通路；
- `hdmi_receiver` 内置 HDMI RX、160 MiB CMA、GPIO4_C6 HPD 和 HDMI RX pinmux；
- LT6911UXC2、27 MHz 外部时钟、GPIO3_A7 reset、GPIO2_C3 HPD、GPIO1_C7 中断，以及 `csi_dphy0 → csi2 → vicap` graph；
- `rknn_core_0/1/2` 三核 NPU、对应 IOMMU、`vdd_npu_s0` 与 `pd_npu` 电源域；
- `rga3_core0/1`、双核 RGA2、VPU121、`vepu121_0..3` JPEG 编码、`vdec0/1` 和 AV1 解码；
- VDENC/RKVDEC 电源域与 `vdd_vdenc_s0` 的 mainline 供电绑定；
- LT9211 DSI→LVDS、GT9110 触摸、HYM8563 RTC、ES8388 声卡；
- PWM2 风扇、PWM6 eDP 背光、PWM12 LVDS 背光；
- SARADC 与 GPIO 按键、温度保护和风扇冷却映射。

## 已知限制

1. 7.3 已启用 mainline 提供的 NPU、RGA、JPEG 编码、VPU 解码、RKVDEC 和 AV1 节点；RKCIF/RKISP 采集链与独立 RKVENC 节点在该版本的 RK3588 DTSI 中尚未提供。
2. AP6275P 无线和 IMX577 摄像头仍受 mainline 驱动支持范围限制；内置 HDMI RX 已恢复，LT6911UXC2 DTS graph 已恢复，但 Linux 7.3 仅提供 LT6911UXE 驱动，仍需验证兼容性或回移专用驱动。
3. eDP 面板依赖 EDID；若实际屏体没有 EDID，需要在 `aux-bus/panel` 内补充面板时序。
4. LT9211 输出时序、Type-C/DP Alt Mode 和 ES8388 外部功放控制仍需实机验证。
5. 设备树只描述内核侧硬件，正式启动仍依赖匹配的 loader、U-Boot、内存初始化和根文件系统。
