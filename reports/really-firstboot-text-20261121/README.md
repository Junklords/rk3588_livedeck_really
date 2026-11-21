# Really 首次启动默认改用文本向导

Author: Codex (GPT-5)

Generated: 2026-11-21T10:15:00+08:00

Context: Really TVI2340R TF image booted, but the internal LVDS panel did
not show Armbian's graphical first-run wizard.

Applicable branch: `/home/user/github/build`, Armbian edge, Linux 7.3.y
rockchip64.

Environment: Really TVI2340R, LVDS on VOP2 VP3, Slint/KMS first-run GUI
from armbian-firstrun-gui.

Historical topology note: at the time of this investigation HDMI1 used
VOP2 VP0. The later USB-C DP baseline moved DP0 to VP0 and HDMI1 to VP1,
so current validation must use the topology documented in the main
README rather than reusing the old VP mapping.

## Observed state

The system was healthy and reachable over SSH:

```text
/dev/mmcblk1p1 ext4 on /
no failed systemd units
armbian-firstrun-gui active
getty@tty1 inactive
GDM inactive
```

The GUI logged:

```text
Using Software renderer
Rendering at 1280x720 with 270 rotation_degrees rotation
```

However, DRM debugfs showed its plane on `video_port0`, which is wired to
HDMI1. The LVDS panel uses `video_port3`. The HDMI connector was also
connected, so the Slint KMS backend selected HDMI and the internal LVDS
remained on fbcon.

The framebuffer was additionally left at:

```text
/sys/class/graphics/fb0/blank = 4
```

## Live-system recovery

The graphical wizard was disabled and the historical text wizard restored:

```bash
systemctl disable --now armbian-firstrun-gui.service
systemctl mask armbian-firstrun-gui.service
rm -f /run/armbian-firstrun-gui.active
systemctl start getty@tty1.service
echo 0 > /sys/class/graphics/fb0/blank
```

Result:

```text
armbian-firstrun-gui.service: masked/inactive
getty@tty1.service: active
bash /usr/lib/armbian/armbian-firstlogin: active on tty1
fb0/blank: 0
```

The temporary getty override autologs root on tty1 solely so the Armbian
first-login wizard can run.

## Build-tree policy

`userpatches/config-really.conf` now defaults to:

```bash
REALLY_FIRSTRUN_GUI="${REALLY_FIRSTRUN_GUI:-no}"
```

`config/boards/really.conf` therefore masks `armbian-firstrun-gui.service`
and leaves `getty@tty1.service` with the text wizard in the rotated fbcon.
The GUI path remains available explicitly with:

```bash
REALLY_FIRSTRUN_GUI=yes ./compile.sh really
```

Do not enable the GUI wizard by default while both LVDS and HDMI can report
connected: the current Slint KMS backend can bind to HDMI VP0 instead of the
internal LVDS VP3.
