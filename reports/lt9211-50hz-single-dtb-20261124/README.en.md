# Really LT9211 Final Single-DTB 50/60 Hz Design

- Author: Codex
- Date: 2026-11-24
- Branch: Armbian `edge`, Linux 7.3.6
- Environment: Really TVI2340R, DSI1 -> LT9211 -> dual-link VESA-24 LVDS

## Final layout

The image contains one `rk3588-really.dtb`, which registers two DRM modes:

| Mode | Pixel clock | htotal | vtotal | Purpose |
| --- | ---: | ---: | ---: | --- |
| 1920x1080@50 | 132.0 MHz | 2200 | 1200 | preferred image default |
| 1920x1080@60 | 148.5 MHz | 2200 | 1125 | selectable in GNOME Settings |

The 50 Hz timing keeps vendor horizontal blanking and uses a 111-line vertical
back porch, giving exactly `132000000 / (2200 x 1200) = 50 Hz`.

## Hardware results

- Both known panel batches displayed normally at 50 Hz without LT9211 PCR
  lock loss.
- On the locally validated 60 Hz unit, 50 Hz produced a 132 MHz VOP clock,
  LVDS `connected`, DPMS `On`, and zero `PCR unstable` messages.
- A 56 Hz experiment resolved to 136.36 MHz (about 55.1 Hz) and failed with
  `PCR unstable, ret=-110`. The 56 Hz and 48 Hz profiles were therefore not
  retained.

## User selection

50 Hz is the preferred mode and the preset GNOME display configuration.
Users can select 60 Hz through the standard GNOME Settings display panel;
GNOME persists that per-user choice. No custom CLI, service, or udev rule was
added.

## initrd cache fix

The previous image retained `71-really-phy-led.rules` in initrd after it had
been removed from rootfs. Armbian's initrd cache key now includes
`/etc/udev` and `/usr/lib/udev/rules.d`. The final image contains no PHY LED
rule in either rootfs or initrd; LEDs are configured by a systemd oneshot
after `end0` exists.
