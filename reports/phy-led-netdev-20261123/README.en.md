# Really YT8531 PHY LED Fix

[中文说明](README.md)

- Author: Codex (AI assistant)
- Generated: 2026-11-23
- Context: YT8531 PHY LEDs in the new Really image were not bound to `end0`.
- Branch: Armbian `edge`, Linux 7.3.
- Test environment: Really TVI2340R, YT8531C, `stmmac-0:01`, `end0`.
- Component: `userpatches/overlay/really-udev/etc/udev/rules.d/71-really-phy-led.rules`.

## Root cause

The Linux `netdev` LED trigger creates these attributes on demand:

```text
device_name
link_1000
full_duplex
rx
tx
```

The old rule wrote these attributes without first selecting
`trigger=netdev`. Boot logs reported unsupported or missing attributes, and the
LEDs remained in `trigger=none`.

## Fix

The rule now performs these operations in order:

```text
trigger=netdev
device_name=end0
link, duplex, RX and TX conditions
```

Final behavior:

| LED | Condition |
| --- | --- |
| Green | 1 Gbps full-duplex link |
| Yellow | RX/TX activity |

## Runtime validation

```text
green: trigger=netdev device_name=end0 link_1000=1 full_duplex=1 rx=0 tx=0
yellow: trigger=netdev device_name=end0 link_1000=0 full_duplex=0 rx=1 tx=1
```

`end0` reported a 1 Gbps full-duplex link. The rule matches only
`stmmac-0:01:green:lan` and `stmmac-0:01:yellow:lan`.

## Build integration

- Rule: `/home/user/github/build/userpatches/overlay/really-udev/etc/udev/rules.d/71-really-phy-led.rules`
- Installer: `/home/user/github/build/userpatches/customize-image.sh`

No systemd service or polling script is required.
