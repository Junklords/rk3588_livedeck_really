# Really mainline U-Boot board patches

- Author: OpenAI Codex (GPT-5); executor: user
- Generated: 2026-11-15T18:20:00+08:00
- Context: archive of the Armbian build-time U-Boot patches for Really.
- Source of truth:
  `/home/user/github/build/patch/u-boot/v2026.07/board_really/`
- Target U-Boot: v2026.07, source commit
  `ece349ade2973e220f524ce59e59711cc919263f`

The files in this directory are exact copies of the build-tree patches. They
are archived here so another AI can understand the complete boot chain without
depending on the build workstation.

## Patch inventory

| Patch | Status | Purpose |
|---|---|---|
| `0001-really-persistent-boot-log.patch` | diagnostic | Persist selected SPL/U-Boot markers to eMMC sectors. Not a community-ready feature. |
| `0002-really-prefer-sata-before-emmc.patch` | build policy | Set `boot_targets` to `usb mmc1 scsi mmc0`. |
| `0003-really-act-on-reboot-mode.patch` | partially validated | `reboot loader` is verified; storage-specific targets map SD/eMMC/USB/NVMe and recovery to U-Boot shell. |
| `0004-really-sdmmc-power-domain-in-spl.patch` | experimental / ineffective | Direct PMU write for `PD_SDMMC`. Tested and did not power the TF controller. Do not treat as the fix. |

Module-rail power is intentionally not in this patch directory. It is
implemented with a standard GPIO hog in the board U-Boot DTS and
`CONFIG_GPIO_HOG=y` in the board defconfig, so no generic Rockchip source file
is changed for board power sequencing.

## Important TF warning

`0004` does not make TF boot work. The current evidence is:

```text
mmc_init       = -EIO
U-Boot PMU     = 0x11
PD_SDMMC       = absent
direct PMU RMW = ignored from normal world
SCMI pd get/on = -ENODEV / -ENODEV
```

See `../TF-BOOT-STATUS.md` before changing this patch. The likely remaining
work is in ATF/SCMI secure power-domain initialization, not in Linux DTS or
boot-order strings.

## Sync check

```sh
repo=/home/user/device/rk3588-really/rk3588_livedeck_really
build=/home/user/github/build

for patch in "$build"/patch/u-boot/v2026.07/board_really/*.patch; do
  cmp "$patch" "$repo/u-boot-mainline/board_really/$(basename "$patch")"
done

cmp \
  "$build/patch/u-boot/v2026.07/dt_uboot/rk3588-really-u-boot.dtsi" \
  "$repo/u-boot-mainline/arch/arm/dts/rk3588-really-u-boot.dtsi"

cmp \
  "$build/patch/u-boot/v2026.07/defconfig/really-rk3588_defconfig" \
  "$repo/u-boot-mainline/configs/really-rk3588_defconfig"
```
