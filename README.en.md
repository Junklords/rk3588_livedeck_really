# Really TVI2340R RK3588 Live Streaming Box Device Trees

[中文版本](README.md)

This repository documents the reverse-engineered device trees for the Really TVI2340R RK3588 live streaming box. Hardware facts are taken from the factory Android 12 / vendor 5.10.110 DTB. Android, vendor Linux and mainline Linux startup semantics are kept separate.

## Directories

| Directory | Target | Purpose |
| --- | --- | --- |
| `dts-5.10-android` | Android 12 / vendor 5.10 | FIQ debugger, Android boot arguments and vendor devices |
| `dts-6.1-linux` | Armbian / vendor 6.1 | Normal UART2 console and vendor Linux peripheral stack |
| `dts-7.3-linux` | Linux 7.3 mainline | Mainline MMC, PCIe, USB, VOP2, NPU, RGA, VPU/VEPU, RKVDEC and AV1 |
| `dtb-5.10-android` | Factory DTB | Original extracted DTB only |

Factory DTB:

```text
dtb-5.10-android/rk3588-really.dtb
SHA256: D516E8F62CE040E76CAB2CBC394DBE46AB53F3F82E9CEC1ED8E5714F51358521
```

## Hardware

| Function | Configuration |
| --- | --- |
| SoC | RK3588, 4x Cortex-A76 + 4x Cortex-A55 |
| Memory | LPDDR4 topology in the factory DTS |
| Primary storage | 8-bit eMMC 5.1, HS400, non-removable |
| SD | 4-bit SDMMC, UHS SDR104, switchable 3.3 V supply |
| Internal panel | DSI1 -> LT9211 -> 24-bit LVDS, 1920x1080 |
| Alternate internal panel | eDP0 with EDID |
| External display | HDMI1 and DP0 over USB-C Alt Mode |
| Ethernet | GMAC0, RGMII, PHY address 1 |
| PCIe | PCIe 2.0 x1 lane 1 and lane 2 |
| USB | USB3 OTG0, USB3 Host1, two USB2 hosts, HUSB311 Type-C PD |
| Audio | ES8388 on I2S1, HDMI1 audio, DP0 audio |
| RTC | HYM8563 at I2C4 `0x51` |
| Touch | Goodix GT9110 at I2C4 `0x14` |
| HDMI input | RK3588 built-in HDMI RX with GPIO4_C6 HPD/detect; LT6911UXC2 at I2C6 `0x2b` |
| Camera | IMX577 at I2C4 `0x1a` |
| Wireless | AP6275P Wi-Fi and Bluetooth |
| Fan | PWM2, 40 kHz, inverted output |
| Watchdog | Internal RK3588 DW watchdog and external GPIO0_D3 watchdog are both explicitly disabled |

## Hardware Topology

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
                                     +--> DP0 --> USB-C Alt Mode
                                     +--> eDP0 --> eDP panel
                                     +--> DSI1 --> LT9211 --> LVDS panel
```

Display routes:

```text
VOP2 VP0 --> HDMI1
VOP2 VP1 --> DP0 -> USB-C DP Alt Mode
VOP2 VP2 --> eDP0 -> eDP panel
VOP2 VP3 --> DSI1 4-lane -> LT9211 -> LVDS -> 1920x1080 panel
```

HDMI input paths:

```text
HDMI IN --> RK3588 built-in HDMI RX --> V4L2 capture
HDMI audio --> I2S7 --> rockchip,hdmiin sound card

LT6911UXC2 secondary path:
HDMI IN --> LT6911UXC2 --> MIPI DPHY0 --> MIPI CSI2 #2 --> RKCIF2
```

Storage and high-speed links:

```text
eMMC 5.1      -> mmc0
SD card       -> mmc1
PCIe lane 1   -> combphy2_psu
PCIe lane 2   -> combphy0_ps
combphy1_ps   -> disabled
SATA0 / PCIe3 -> disabled
```

## GPIO Assignments

| Function | Pin | Polarity | Consumer |
| --- | --- | --- | --- |
| Type-C PD interrupt | GPIO0_D7 | Low | HUSB311 interrupt |
| RTC interrupt | GPIO0_B0 | Low | HYM8563 interrupt |
| Touch reset | GPIO1_D3 | Low | GT9110 |
| eDP panel enable | GPIO1_D0 | High | eDP panel |
| Touch interrupt | GPIO4_B0 | Low | GT9110 |
| GMAC0 PHY reset | GPIO2_C4 | Low | Ethernet PHY |
| LT9211 reset | GPIO3_A6 | High | DSI-to-LVDS bridge |
| LVDS backlight enable | GPIO3_B4 | High | PWM backlight |
| LVDS power enable | GPIO3_B6 | High | LVDS panel supply |
| HDMI1 enable | GPIO3_C5 | High | HDMI1 |
| HDMI RX HPD/detect | GPIO4_C6 | Low | Built-in `hdmi_receiver` |
| LT6911 reset | GPIO3_A7 | High | LT6911UXC2 reset |
| LT6911 HPD control | GPIO2_C3 | High | LT6911UXC2 HPD control |
| LT6911 interrupt | GPIO1_C7 | Low | LT6911UXC2 interrupt |
| PCIe lane 2 reset | GPIO3_C7 | High | `pcie2x1l2` |
| PCIe lane 1 reset | GPIO1_A7 | High | `pcie2x1l1` |
| SD power | GPIO4_C3 | High | `vcc_sd` |
| Type-C VBUS enable | GPIO4_A1 | High | HUSB311 VBUS |
| USB3 5 V enable | GPIO4_C6 | High | USB3 power |
| eDP backlight enable | GPIO4_B6 | High | PWM6 backlight |
| Speaker amplifier enable | GPIO4_A2 | Low | ES8388 speaker path |
| Headphone amplifier enable | GPIO4_A4 | High | ES8388 headphone path |
| Power LED | GPIO3_C0 | High | GPIO LED |

## PWM and Thermal Control

| Function | Controller | Timing |
| --- | --- | --- |
| LVDS backlight | PWM12 | 1 MHz, normal polarity |
| eDP backlight | PWM6 | 25 kHz, normal polarity |
| Fan | PWM2 | 40 kHz, inverted polarity |

The 7.3 tree uses the standard `pwm-fan` binding with cooling levels `0/76/153/204` and package thermal cooling maps.

## Watchdog State

Really includes an external GPIO-controlled watchdog. The factory DTB uses GPIO0_D3, level-mode servicing and a 500 ms hardware margin. All three rebuilt trees explicitly set this device to `disabled`.

The RK3588 internal DW watchdog at `watchdog@feaf0000` is also explicitly disabled in all three trees to match the factory DTB. No `/dev/watchdog` is registered, so the device tree does not create an implicit dependency on a userspace watchdog service.

If watchdog support is enabled later, use only the internal or external device and configure systemd `RuntimeWatchdogSec` or a dedicated watchdog daemon.

## Status Policy

The factory DTB explicitly enables the PMIC and CPU rails, eMMC, SD, GMAC0, both PCIe 2.0 x1 links, USB2/USB3, Type-C, VOP2, DSI1/LT9211/LVDS, eDP0, HDMI1, DP0, ES8388, RTC, touch and fan.

It also enables the board-independent RK3588 media and accelerator blocks, including NPU, RGA2/RGA3, JPEG, VENC/VDEC, IEP, AV1, MPP, RKCIF and RKISP.

SDIO, SATA0, PCIe 3.0, `pcie2x1l0`, HDMI0, DP1, eDP1 and DSI0 are disabled or unused.

The vendor 5.10 and 6.1 trees retain the vendor media and accelerator nodes, including the LT6911UXC2 -> DPHY0 -> MIPI CSI2 -> RKCIF2 path. The 7.3 mainline tree uses its own bindings to explicitly enable all three RKNN NPU cores, RGA3/RGA2, VPU121, four VEPU121 JPEG encoders, RKVDEC0/1, AV1 decoding and their IOMMUs. LT6911UXC2 and its mainline VICAP graph are present, but UXC2 driver compatibility still needs validation.

## Linux 7.3 Mainline Port

`dts-7.3-linux/rk3588-really.dts` includes:

- RK806 PMIC, RK8602/RK8603 rails and all eight CPU OPP supplies;
- UART2/M0 console at `1500000n8`;
- eMMC HS400 and SD UHS SDR104;
- GMAC0/RGMII, PHY address 1 and the factory GPIO2_C4 PHY reset;
- both PCIe 2.0 x1 links with their combphy, reset and 3.3 V resources;
- USB2/DP/USB3 PHYs, DWC3 controllers and HUSB311 Type-C role switching;
- four VOP2 display routes for HDMI1, DP0, eDP0 and DSI1;
- the built-in `hdmi_receiver`, its 160 MiB CMA region, GPIO4_C6 HPD and HDMI RX pinmux;
- LT6911UXC2, its 27 MHz clock, GPIO3_A7 reset, GPIO2_C3 HPD, GPIO1_C7 interrupt and `csi_dphy0 -> csi2 -> vicap` graph;
- `rknn_core_0/1/2`, their IOMMUs, the `vdd_npu_s0` rails and the `pd_npu` domain;
- `rga3_core0/1`, dual-core RGA2, VPU121, four `vepu121_0..3` JPEG encoders, `vdec0/1` and AV1 decoding;
- mainline VDENC/RKVDEC power-domain supplies from `vdd_vdenc_s0`;
- LT9211 DSI-to-LVDS, GT9110 touch, HYM8563 RTC and ES8388 audio;
- PWM2 fan, PWM6 eDP backlight and PWM12 LVDS backlight;
- SARADC keys, thermal protection and fan cooling maps.

## Known Limitations

1. Linux 7.3 enables the available mainline NPU, RGA, JPEG encoder, VPU decoder, RKVDEC and AV1 nodes. RK3588 RKCIF/RKISP capture nodes and standalone RKVENC device nodes are not provided by this mainline release.
2. AP6275P wireless and IMX577 capture remain limited by mainline driver support. The built-in HDMI RX path is restored, and the LT6911UXC2 graph is restored, but Linux 7.3 only provides an LT6911UXE driver; UXC2 compatibility or a dedicated driver backport still needs validation.
3. The eDP panel path depends on EDID. A panel without EDID needs explicit timings.
4. LT9211 timings, Type-C/DP Alt Mode and external ES8388 amplifier controls still require hardware validation.
5. The device tree describes kernel hardware only. Booting also requires a matching loader, U-Boot, memory initialization and root filesystem.
