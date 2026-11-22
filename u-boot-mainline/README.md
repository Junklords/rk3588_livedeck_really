# Really TVI2340R — mainline U-Boot device-tree archive

| Item | Value |
|---|---|
| Author | Codex |
| Generated | 2026-11-15 |
| Board | Really TVI2340R / RK3588 live-streaming box |
| Scope | Mainline U-Boot 2026.07 board DTS, U-Boot DTSI, and defconfig |
| Source tree | `u-boot/u-boot` commit `ece349ade2973e220f524ce59e59711cc919263f` |
| Build project | `/home/user/github/build` |
| Armbian family | `rockchip-rk3588` / `rockchip64` |
| Environment | Armbian build framework, native arm64, `really-rk3588_defconfig` |

## Why this directory exists

Linux and mainline U-Boot use separate device trees and separate DTB
artifacts. The kernel DTS in `dts-7.3-linux/` is not consumed by U-Boot.

At build time, Armbian maps these archived files to:

```text
u-boot-mainline/dts/upstream/src/arm64/rockchip/rk3588-really.dts
  -> <u-boot>/dts/upstream/src/arm64/rockchip/rk3588-really.dts

u-boot-mainline/arch/arm/dts/rk3588-really-u-boot.dtsi
  -> <u-boot>/arch/arm/dts/rk3588-really-u-boot.dtsi

u-boot-mainline/configs/really-rk3588_defconfig
  -> <u-boot>/configs/really-rk3588_defconfig
```

The corresponding build-repository copies are under:

```text
patch/u-boot/v2026.07/dt_upstream_rockchip/rk3588-really.dts
patch/u-boot/v2026.07/dt_uboot/rk3588-really-u-boot.dtsi
patch/u-boot/v2026.07/defconfig/really-rk3588_defconfig
```

## Build usage

```bash
cd /home/user/github/build
./compile.sh really uboot
```

The resulting DTB is named `u-boot.dtb`; the final bootloader artifact is
`u-boot-rockchip.bin`.

## Current hardware assumptions

```text
mmc0 = &sdhci  (eMMC)
mmc1 = &sdmmc  (microSD/TF)
UART2 = M0 on the on-board UART header
SD power enable = GPIO4_C3, active high
```

The U-Boot DTS carries the minimum SD power chain taken from the vendor 5.10
DTS, not from a generic RK3588 board:

```text
RK806 PMIC on SPI2
pldo-reg5 = vccio_sd_s0, 3.3 V, boot-on/always-on
RK806 dcdc-reg8 = vcc_3v3_s3, parent of vcc_sd
SD vmmc = GPIO4_C3 controlled vcc_sd
SD vqmmc = pldo-reg5 vccio_sd_s0
SD bus = the SoC's default clk/cmd/det/bus4 pinctrl
SD card detect = no board-level cd-gpios override
```

**This chain was briefly lost and has been restored.** An earlier module-rail
change deleted the whole `&spi2` + RK806 block along with `vqmmc-supply` and
the `vin-supply` link on `vcc_sd`. The final tree restores the SD chain and
keeps module power limited to the one switched GPIO0_C0 rail needed by the
I2C4 devices. Whenever you touch this DTS, diff the whole file rather than
reading the commit subject.

### Switched module rail

The U-Boot board DTS uses the standard GPIO hog mechanism:

```dts
&gpio0 {
	vcc1v8-sys-en-hog {
		gpio-hog;
		gpios = <16 0>;
		output-high;
		line-name = "vcc1v8_sys_en";
	};
};
```

`CONFIG_GPIO_HOG=y` is part of `really-rk3588_defconfig`. This replaces the
old generic `arch/arm/mach-rockchip/board.c` patch that forced four rails.
The fixed 2.8 V rail and always-on 5 V rail need no software enable; the LVDS
rail belongs to the Linux display stack.

### Power domains

`sdmmc` declares `power-domains = <&power RK3588_PD_SDMMC>` in the upstream
DTSI, but U-Boot cannot act on it: there is no `rockchip,rk3588-power-controller`
driver in the U-Boot tree, the DM core does not parse `power-domains`, and the
SCMI firmware exposes no power-domain protocol. `sdhci` (eMMC) has no
`power-domains` property and lives in an always-on domain, which is why eMMC has
always worked here and TF has not. See
[TF-BOOT-STATUS.md](TF-BOOT-STATUS.md) for what this means for bring-up.

`u-boot,spl-boot-order` mirrors the proven Skysi-X5 structure:
`same-as-spl`, then `&sdmmc`, then `&sdhci`. U-Boot proper uses the normal
`boot_targets` mechanism after SPL: USB -> TF -> M.2 SATA -> eMMC. No
board-specific `preboot` loader is part of the final configuration.

See [TF-BOOT-STATUS.md](TF-BOOT-STATUS.md) for the final cold-boot evidence.
TF boot is closed.

The Armbian build-time board patches are archived under
[`board_really/`](board_really/README.md). They include boot logging,
boot-target selection, reboot-mode handling, and one explicitly ineffective
SDMMC power-domain experiment (`0004`, kept for the record). Module-rail
power is now board-DTS only and has no generic-source patch. The build-tree
copies remain the source of truth.

This is separate from the Linux DTS. Changes to either side must be checked
against its own binding and runtime behavior.
