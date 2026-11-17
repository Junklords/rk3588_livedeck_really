# Really accelerometer auto-rotation

- Author: Codex (GPT-5)
- Generated: 2026-11-17T22:45:00+08:00
- Device: Really TVI2340R
- Build tree: `/home/user/github/build`
- Kernel: Armbian edge, Linux `7.3.5-edge-rockchip64 #75`
- Boot medium: TF card, `/dev/mmcblk1p1`
- Sensor: STK8BA50, I2C6 `0x18`, IRQ GPIO4_B7

## Result

The sensor driver is built into the kernel. The upright panel is portrait,
and the display follows all four gravity orientations.

| Sensor orientation | Mutter transform |
|---|---:|
| `normal` | 0 |
| `left-up` | 1 |
| `bottom-up` | 2 |
| `right-up` | 3 |

## Axis calibration

The raw sensor uses the opposite Y direction from the chassis frame. This udev
property corrects only the sensor axes:

```text
ACCEL_MOUNT_MATRIX=1,0,0;0,-1,0;0,0,1
```

The rule matches only an IIO device named `stk8ba50`.

## Display rotation

GNOME 50 in Armbian 26.11 provides `iio-sensor-proxy`, but it does not include
`gsd-orientation`. The `really-auto-rotate.service` user service claims the
accelerometer and applies temporary Mutter transforms.

- It changes only logical monitors containing `LVDS-1`.
- It leaves HDMI and Type-C DP monitors unchanged.
- It does not persist the current orientation to `monitors.xml`.
- It honors the GNOME `orientation-lock` setting.
- A polkit rule allows the local `sudo` group to claim the accelerometer.

Build files:

```text
/home/user/github/build/config/boards/really-auto-rotate/
/home/user/github/build/config/boards/really.conf
```

Installed files:

```text
/usr/local/libexec/really-auto-rotate
/etc/systemd/user/really-auto-rotate.service
/etc/polkit-1/rules.d/49-really-sensor.rules
/etc/udev/rules.d/60-really-accel-mount.rules
```

## Validation

The following values were observed after reboot:

```text
really-auto-rotate.service: active (running)
orientation: normal
LVDS transform: 0
orientation-lock: false
```

Forced sensor directions produced:

```text
left-up   -> transform 1
right-up  -> transform 3
bottom-up -> transform 2
normal    -> transform 0
```

The panel was reported correct while vertical. The horizontal direction was
also checked after swapping transform 1 and 3.
