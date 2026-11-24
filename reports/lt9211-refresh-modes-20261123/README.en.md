# Really LT9211 48/60 Hz Modes and Stabilization

> Updated 2026-11-24: this document records the earlier 48/60 Hz experiment.
> It is superseded by the final single-DTB 50/60 Hz design. The 48 Hz profile
> has been removed; see `../lt9211-50hz-single-dtb-20261124/README.en.md`.

[中文说明](README.md)

- Author: Codex (AI assistant)
- Generated: 2026-11-23
- Context: Some Really assemblies lose LT9211 lock and flash at 1920x1080@60.
- Branch: Armbian `edge`, Linux 7.3.
- Test environment: Really TVI2340R, DSI1, LT9211, dual-link VESA-24 LVDS.
- Default policy: 60 Hz is native/preferred; 48 Hz remains a compatibility
  fallback.

## Timing parameters

Both modes keep the same blanking:

```text
htotal = 2200
vtotal = 1125
```

| Mode | Pixel clock | Result |
| --- | ---: | ---: |
| 1920x1080@48 | 118.8 MHz | 118800000 / (2200 x 1125) = 48.0 |
| 1920x1080@60 | 148.5 MHz | 148500000 / (2200 x 1125) = 60.0 |

## DTS and driver

The board DTS uses the standard `display-timings` node:

```text
native-mode = <&panel_timing_60>;
timing48 -> 118800000
timing60 -> 148500000
```

The upstream `panel-lvds` driver previously parsed only one `panel-timing`.
The build adds `general-drm-panel-lvds-support-multiple-timings.patch`. It
registers every `display-timings` entry as a DRM mode and marks the native
timing as preferred. Existing single-timing panels keep their old behavior.

## Automatic fallback boundary

The system selects the validated stable 60 Hz mode by default. Units that
cannot sustain 60 Hz may select 48 Hz manually. Making 48 Hz the default is not
safe because otherwise functional LVDS panels may reject that refresh rate and
stay black during cold boot.

Live fallback from an unstable running 60 Hz mode is not implemented. LT9211
does not currently report a runtime lock-loss event back to DRM, and changing
only bridge registers cannot change the VOP/DSI pixel clock. A reliable
implementation needs connector recovery, a mode change and a failure
threshold.

## 60 Hz stabilization candidates

Test one variable at a time:

1. Compare `0x8235`, `0x8237`, `0x823b`, `0x8737`, `0x855c`, `0xd087` and
   `0x8720` on stable and unstable units.
2. Test the vendor value `0x8737 = 0x0e`; mainline currently writes `0x14`.
3. Test DSI EoT, non-burst and continuous/non-continuous clock-lane modes.
4. Tune LT9211 RX PHY calibration and compare DSI PHY drive/termination.
5. Measure LT9211 `vccio`, 1.8 V and PHY analog power sequencing.
6. Keep 60 Hz as the default preferred mode; use 48 Hz only as a fallback for
   affected hardware batches.

Increasing the PCR poll timeout is not a fix. The observed failure is repeated
lock loss after initial operation.

## Build validation

- The multi-timing patch passes `git apply --check`.
- `panel-lvds.o` builds with the Armbian 7.3 kernel configuration.
- The Really DTS preprocesses and produces a DTB.
- Decompilation confirms `timing60` is native and `timing48` remains present.
- Hardware validation: an otherwise stable 60 Hz LVDS panel stayed black with
  48 Hz preferred. Restoring 60 Hz as preferred recovered the display.
- Only the existing PCIe duplicate-address and VOP graph-address warnings
  remain.
