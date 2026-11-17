# Really TVI2340R Linux 7.3 mainline board enablement log

Author: Codex (GPT-5); executor: user

Generated: 2026-11-14T21:50:00+08:00

Last updated: 2026-11-17T23:55:00+08:00

Context: Board-level hardware enablement and validation on the Really
TVI2340R, starting from the reconstructed vendor DTB and the mainline
RK3588 device-tree bindings.

Applicable branch: `/home/user/github/build`, `main`, Armbian edge,
Linux 7.3.y rockchip64.

Environment: Really TVI2340R, RK3588, eMMC boot, 15.6-inch portrait
dual-link LVDS panel behind a Lontium LT9211, HDMI output, Type-C DP
Alt Mode, AP6275P Wi-Fi/Bluetooth and ES8388 audio.

The board also defines eDP0 as an optional factory-supported internal
panel route. The default image keeps the fitted DSI1/LT9211/LVDS path;
the same DTS retains the VP2/eDP0/AUX panel route with every component
marked disabled by default.

## Integrated board changes

### Serial console

- UART2 stays on M0 at `0xfeb50000`, `1500000 8N1`.
- UART0 is not used for the Linux console because its M2 pins conflict
  with the RK806 PMIC connection on SPI2-CS0.

### Audio

- ES8388 uses I2S1 M0 to avoid the UART2 M0 pin conflict.
- MCLK is GPIO4_A0 at 12.288 MHz.
- The ES8388 card exposes speaker, headphone and analog-input DAPM paths.
- Speaker amplifier enable is GPIO4_B2, active low.
- Headphone amplifier enable is GPIO4_B4, active high.
- Headphone detect is GPIO4_B3, active high.
- Factory Audio-jack microphone routing selects ES8388 Line 1.
- Factory independent LINE IN routing selects ES8388 Line 2 through the
  private `linein_det_handler` on GPIO1_B6.
- The built-in microphone is the factory `rockchip,pdm-mic-array` on PDM0,
  not the ES8388 analog capture path. PDM0 uses M0 CLK/SDI0/SDI2 and
  `rockchip,path-map = <0 2 1 3>`.
- The factory play/pause key is on SARADC channel 3, threshold 200 mV,
  key-up threshold 1.8 V and key code `KEY_PLAYPAUSE`.

Playback baseline: ES8388 playback enumerates and the speaker path produces
audio; the amplifier is switched off after playback stops. The play/pause
ADC input registers as a second `adc-keys` input device.

ES8388 analog capture is **not closed**. Every tested ES8388 route returns
hard digital zero on both channels, including after increasing Mic PGA and
confirming the I2C register write.

The built-in microphone is the factory PDM0 path and now works on mainline:
after installing the kernel/DTB and rebooting, `Really-PDM-Mics` binds to
`fe4b0000.pdm`, enables the 300 MHz PDM clock and captures non-zero speech
at 48 kHz. The captured left/right channels are identical, indicating the
current single PDM line is duplicated into a stereo PipeWire source.

The headset mic, the independent LINE IN switch and speaker/headphone
quality remain open. See `../reports/audio-mic-linein-20261117/README.md`.

### USB and Type-C

- DWC3 is configured for dual-role operation in the shared rockchip64
  edge kernel configuration.
- Type-C PD/mux is provided by HUSB311/RT1711H.
- Type-C VBUS enable is GPIO1_D1, active high.
- The connector advertises dual power and data roles. It prefers sink for a
  powered monitor and can source 5 V to a dock.
- Both sink and source advertise one fixed 5 V/3 A PDO. Operational sink
  power is 15 W.
- HUSB311 owns VBUS through `vbus-supply`. `u2phy0_otg` must not bind the
  same regulator as `phy-supply`.
- USB-A host power is controlled from GPIO3_A5.
- DP uses two Type-C lanes while USB3 uses the other two lanes.

Validated result: Type-C USB2 hub and USB-A keyboard enumerate. Type-C DP
reached 3840x2160@30 in an earlier test configuration, but this is not a
closed result for the current baseline. Current runs repeatedly leave
`DP-1` disconnected and cycle the HUSB311 partner through
`SRC_READY -> disconnected -> TOGGLING`.

### PCIe, Wi-Fi and Bluetooth

- M.2/5G 3.3 V switch: GPIO3_B3, active high.
- M.2/5G reset: GPIO2_C5, active high.
- M.2/5G DPR control: GPIO3_D5, active high. It is represented as an
  independently controllable regulator and remains disabled unless a
  consumer requests it.
- AP6275P remains on its independent PCIe 2.0 x1 link.
- The AP6275P 32.768 kHz LFO comes from HYM8563 CLKOUT. The temporary
  `wifi-lpo` fixed-clock masked the RTC bring-up race but also made the
  firmware fail with `FW failed to initialize`.
- Both the WiFi PCI function and Bluetooth UART reference `<&hym8563>`.
  HYM8563 now binds during the initial I2C4 probe, so AP6275P also probes
  normally; there is no userspace late-load, PCIe rescan, or forced module
  binding.

Cold-boot validation on 2026-11-16:

```text
BCM43752/2 wl0: Jul 12 2022 ... 18.35.387.23.146
wlP4p65s0: connected to HUAWEI-7409_5G, 192.168.3.253/24
hci0: BCM43752A2 UART 37.4MHz Ampak AP6398
```

A hand-built kernel must still retain the `7.3.4-edge-rockchip64` module
release suffix.

### Accelerometer

- The STK8BAXX accelerometer is on I2C6 address `0x18`.
- IRQ is GPIO4_B7.
- It uses the upstream `sensortek,stk8ba50` IIO driver.
- The Really board config builds `CONFIG_STK8BA50=y`.
- Runtime manual instantiation produced an `stk8ba50` IIO device and live
  X/Y/Z readings.
- Identity axes make `iio-sensor-proxy` report upright as `bottom-up`.
  A udev mount matrix `1,0,0;0,-1,0;0,0,1` makes upright report `normal`.
- GNOME 50 in this image has no `gsd-orientation` service. A per-user
  `really-auto-rotate.service` applies temporary Mutter transforms to LVDS-1.
- The transform map is `normal=0`, `left-up=1`, `bottom-up=2`, `right-up=3`.
  External monitors are not changed. The user orientation lock remains enabled
  as a manual override.
- The 7.3 DTS now contains `accelerometer@18` and its pinctrl entry.
- Automatic USB-camera rotation is not implemented. The current camera keeps
  a fixed GStreamer `videoflip` direction; any gravity-following behavior must
  be implemented in userspace with hysteresis.

### Display

- VOP2 VP0 -> HDMI1.
- VOP2 VP1 -> DP0 -> USB-C DP Alt Mode.
- VOP2 VP3 -> four-lane DSI1 -> LT9211 -> dual-link 24-bit VESA LVDS.
- LT9211 port 2 carries odd pixels and port 3 carries even pixels. The
  panel sink exposes the matching `dual-lvds-odd-pixels` and
  `dual-lvds-even-pixels` properties.
- The LT9211/DSI controller scans 1920x1080@60 at 148.5 MHz; the physical
  15.6-inch panel is mounted portrait.  The panel node declares
  `rotation = <270>` so DRM reports the correct portrait orientation and
  Mutter presents a 1080x1920 logical desktop.
- The early kernel console does not consume panel orientation, so board
  bootargs add `fbcon=rotate:3` to rotate fbcon as well.
- Android boot avoids the equivalent early landscape frame by setting
  `ro.surface_flinger.primary_display_orientation=ORIENTATION_270` at
  SurfaceFlinger startup. The layered Android/Linux comparison and deferred
  optimization candidates are recorded in
  `../reports/display-orientation-android-vs-linux-20261117/README.md`.
- LVDS panel supply enable is GPIO3_B6, active high and always-on.
- LVDS backlight enable is GPIO3_B4; PWM is PWM12 with normal polarity.
- This panel's brightness decreases as the PWM high-level duty cycle rises.
  The mainline user-facing curve therefore reverses the factory 256-step
  values. Logical brightness 255 produces the minimum PWM level and lower
  logical values produce progressively longer high-level pulses.
- New images default to logical brightness 154, the first stable ~40% PWM
  point validated on the panel. The range remains `0..255`; zero powers the
  backlight off.
- The temporary `lontium,dsi-mode-flags` override patch has been retired.
  The Really dual-link mainline topology uses the upstream driver's
  non-burst defaults directly.
- The vendor value `2051` (`VIDEO | BURST | LPM`) was tested after the
  dual-link conversion and produced vertical-line striping/flicker.
  Removing the override eliminated the artifact on 2026-11-15.
- Vendor mode 3 writes `0x8237 = 0x2a`, `0x823b = 0xb8` and
  `0x855c = 0x07`. The upstream dual-link path now writes the same
  channel-enable mask through
  `0015-lt9211-enable-dual-link-lvds-channels.patch`.

Validated result: HDMI output works. Type-C DP previously reached 4K30, but
the current final baseline does not maintain a Type-C partner/Alt Mode
connection. DP must be documented as open until a repeated cold-boot and
hot-plug regression passes.

### Optional internal eDP panel

- Factory data defines eDP0 on VP2, with PWM6 backlight, backlight enable
  GPIO4_B6 and panel enable GPIO1_D0.
- The production LVDS path remains DSI1 on VP3 through LT9211.
- The board has a physical eDP connector, but the current assembly has no
  eDP panel. The single board DTS retains eDP0, VP2, input/output
  endpoints and the AUX panel node, but marks the whole route disabled.
  Enabling an eDP SKU only requires changing those status values.
- Keeping an empty eDP controller enabled is not neutral: a transient HPD
  pulse lets DRM add a 1024x768 no-EDID fallback, after which fbcon keeps
  the 1920x1080 framebuffer window at 1024x768.
- GPIO1_D0 is a shared touch/panel enable, not an eDP-only signal. It is
  represented by the always-on `touch_panel_pwr` regulator. Removing its
  assertion makes GT9110 and HYM8563 disappear from I2C4 together.
- The panel description assumes EDID. A panel without EDID needs its exact
  model, power sequence and timing.
- No eDP panel has yet been fitted for physical validation.

### Touch and RTC (closed 2026-11-16)

- GT9110: I2C4 `0x14`, reset GPIO1_D3, IRQ GPIO4_B0. The mainline driver
  uses ACTIVE_HIGH reset/IRQ semantics and a successful reset drives the IRQ
  strap for the `0x14` address.
- HYM8563: I2C4 `0x51`, IRQ GPIO0_B0.

Pre-fix result: there was no Goodix input device, and I2C4 `0x14` and `0x51`
did not acknowledge. LT9211 at `0x2d` remained bound, proving the I2C
controller and pinmux were alive.

Root cause and verified fix:

- The factory `vcc1v8_sys` rail is controlled by GPIO0_C0 and needs its
  dedicated GPIO-function/pull-up pinctrl. A raw gpio-hog did not preserve
  the boot-time power dependency.
- The factory also has an always-on 2.8 V `vcc28_dvp` rail; GT9110 AVDD28
  was incorrectly mapped to 3.3 V.
- Restoring `vcc1v8_sys` as a regulator, adding its pinctrl, removing the
  hog, restoring `vcc28_dvp`, and mapping GT9110
  `AVDD28=<&vcc28_dvp>` / `VDDIO=<&vcc1v8_sys>` fixed both GT9110 and
  HYM8563 on hardware.
- GT9110 works at I2C4 `0x14`; HYM8563 works at `0x51`; LT9211 remains
  functional. No I2C bus remap or Goodix polarity change was required.
- Post-fix `evtest` corner capture on the 1080x1920 portrait desktop:
  top-left `(31,28)`, top-right `(1052,20)`, bottom-left `(14,1901)`,
  bottom-right `(1062,1901)`.
- Final DTS mapping is `touchscreen-inverted-x` plus
  `touchscreen-swapped-x-y`. The resulting input ranges are X=`0..1919`,
  Y=`0..1079`. This is the coordinate transform that matches the panel's
  270-degree rotation and the GT9110's native portrait axis order.
- The final GNOME nine-target test mapped all nine physical targets
  correctly, including TL/TC/TR, ML/C/MR and BL/BC/BR. A mapping with only
  `touchscreen-inverted-x` produced an anti-diagonal transform in GNOME;
  adding `touchscreen-inverted-y` produced a 180-degree transform.
- Final GNOME compositor-space samples:

  ```text
  TL (101,230)   TC (554,208)   TR (974,196)
  ML (122,968)   C  (552,941)   MR (979,976)
  BL (115,1725)  BC (546,1708)  BR (985,1732)
  ```

- Factory comparison: the runtime DTB has no touchscreen orientation or
  calibration properties. It only sets `tp-size=<9110>`, GPIOs, and
  `max-x/max-y`; the vendor driver parses but does not assign the latter
  two. For `tp-size=9110`, the vendor gt9xx driver selects its embedded
  GT9110 config path with no X/Y swap and no axis reversal.
- The GT9110 chip currently stores a `1080x1920` config (`0x0438 x
  0x0780`). Mainline does not overwrite it because
  `goodix_9110_cfg.bin` is absent. Do not blindly copy the generic tree's
  `GT9110P(2020)V71...cfg`: that source file describes a `1920x1200`
  panel and is not the runtime config on this unit.
- Libinput reports an identity calibration matrix and 0 degree rotation.
  A fullscreen GNOME/Gtk nine-target test placed every compositor-space
  touch within one 32 px target radius.
- With the empty eDP route disabled, fbcon returned to
  `mode "1920x1080"` / `geometry 1920 1080 1920 1080`; keeping the empty
  controller enabled left `xres=1024, yres=768`.

### Ethernet, fans, keys and power

- GMAC0 uses RGMII, PHY address 1, GPIO2_C4 reset and `tx_delay = 0x44`.
- YT8531 PHY LEDs use the DTS-selected `ledtrig-netdev` trigger. A narrow
  udev rule sets the hardware green-link and yellow-RX/TX rules.
- PWM2 drives the fan with a 40000 ns period (25 kHz) and inverted
  polarity.
- SARADC channel 1 provides volume-up, volume-down, menu and back;
  channel 3 provides play/pause.
- The Riitek `1997:2535` receiver and its GL3523 `05e3:0610` USB2 hub are
  kept out of runtime suspend by a narrow udev rule. The old boot-time
  re-authorize timer was removed because it caused a duplicate HID
  enumeration and did not address later idle disconnects.
- Internal RK3588 watchdog and external GPIO0_D3 watchdog remain disabled.

## Electronically validated facts

Original Android DTB and the extracted vendor 5.10 kernel establish:

- LVDS output is dual-link. A single-link graph displays only half of the
  1920-pixel scan and produces severe aliasing.
- LVDS mapping is `vesa-24`.
- The DSI input is four lanes and RGB888.
- The vendor LT9211 `output-mode = <3>` selects the dual LVDS path.
- The vendor DSI flags are `2051` (video, burst and LPM), but they do not
  map cleanly to the mainline dual-link driver path. Really intentionally
  uses the upstream non-burst defaults.
- The panel power enable is GPIO3_B6 active high.

The physical board voltage selectors are separate:

- `LVDS 3V3 / 5V / 12V`: panel-side voltage selection, currently 12 V.
- `LVDS_BLTTL`: backlight control-level selection, currently 5 V.

These jumpers are static and do not explain the Android boot-time speaker
click. The click is more likely ES8388/amplifier power-on or mute timing.

## Open issues

1. Dual-link output has been validated on hardware: the full 1920x1080
   scan is now visible instead of only one half. Residual pixel-column
   jitter is being closed using the vendor `0x855c = 0x07` sequence.
2. The vendor 5.10 kernel has been reversed and confirms the dual-link
   PLL/channel values. A live Android dump is no longer required to
   establish single-vs-dual mode.
3. Panel-side 12 V, backlight power and LVDS common-mode/swing have not been
   measured with a meter or oscilloscope.
4. HDMI output can remain logically connected after cable removal, causing
   EDID I2C retries and a compositor stall. The image currently starts HDMI
   disabled by default.
5. Type-C DP is open, not merely limited to 4K30. Current `DP-1` is
   disconnected and the HUSB311 Type-C partner repeatedly detaches. Previous
   two-lane 4K30 success must not be used as a final validation claim.
6. TF boot is closed. A cold boot through the community SPL/U-Boot chain
   mounted ext4 UUID `7faf7863-d5d5-419e-81bd-902914de6883` from
   `/dev/mmcblk1p1`.

## Relevant build-tree files

```text
patch/kernel/archive/rockchip64-7.3/dt/rk3588-really.dts
config/kernel/linux-rockchip64-edge.config
userpatches/customize-image.sh
```

## 2026-11-17 backport audit

The hardware repository was behind the build tree by nine regulator nodes.
They are now synchronized locally:

- `vcc4v0_sys`, `vcc3v3_ext`, `vcc12_dvp`;
- `vcc1v8_pmu_ddr_s3`;
- `pcie20_avdd0v85`, `pcie20_avdd1v8`;
- `pcie30_avdd0v75`, `pcie30_avdd1v8`;
- `power_on_led`, with GPIO3_D0 pinctrl and disabled boot state.

`vcc28_dvp` now derives from `vcc4v0_sys`. The HUSB311 interrupt was corrected
to GPIO0_B7. HUSB311 owns Type-C VBUS, and AP6275P host-wake was added on
GPIO0_B2.

I2C6 `0x18` exposes the STK8BA50 accelerometer. The board config builds its
driver into the kernel.

I2C4 `0x56` matches the factory 24C04 EEPROM node. The chip does not ACK on the
current unit. No bus, address or compatible mismatch is known. Treat the
EEPROM as absent for now.

The default product camera is USB UVC. The IMX577 MIPI camera interface is
on the core board rather than the carrier board. Linux 7.3 currently has no
mainline IMX577 sensor driver, so no enabled IMX577 node or
CSI0 capture path is enabled in 7.3.

The factory DTB provides no label, nvmem cell, EDID or MAC consumer for the
24C04. Do not rely on it until a future unit ACKs at `0x56`.

Validation:

```text
7.3 DTB: 213085 bytes
SHA256:  d7fe7721b1ea819dbf7984d54d056dca89b797b899825216904a6f392a243f93
```

Runtime validation on 2026-11-17:

```text
kernel: 7.3.5-edge-rockchip64 #75
root:   /dev/mmcblk1p1
CONFIG_STK8BA50=y
IIO:    stk8ba50 bound without modprobe
matrix: 1,0,0;0,-1,0;0,0,1
pose:   normal, vertical
lock:   false
rotate: normal=0, left-up=1, bottom-up=2, right-up=3
Type-C: sink-pdos=source-pdos=0x401912c (5 V / 3 A)
```
