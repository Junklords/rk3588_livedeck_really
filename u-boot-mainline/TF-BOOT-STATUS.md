# Really TVI2340R TF boot status

| Item | Value |
|---|---|
| Author | OpenAI Codex; executor: user |
| Generated / validated | 2026-11-16T23:52:00+08:00 |
| Board | Really TVI2340R / RK3588 live-streaming box |
| Status | **Closed; cold boot from TF verified** |
| Build project | `/home/user/github/build` |
| U-Boot | v2026.07, package `2026.07-Sece3-P743c-Hae08-V6f98-Bd800-R448a` |
| Kernel | `7.3.4-edge-rockchip64` |
| Boot media | `/dev/mmcblk1` (59.4 GiB TF card) |
| Root filesystem | ext4 UUID `7faf7863-d5d5-419e-81bd-902914de6883` |

## Validated boot result

The final P743c U-Boot image was written to the TF card at byte offset
`0x8000`, read back, and compared byte-for-byte before reboot.

After a cold boot:

```text
findmnt -n -o SOURCE,FSTYPE /
/dev/mmcblk1p1 ext4

findmnt -n -o UUID /
7faf7863-d5d5-419e-81bd-902914de6883
```

The result proves that BootROM/SPL read U-Boot from TF, U-Boot read
`mmc 1:1`, and Linux mounted the TF root filesystem rather than falling
back to eMMC.

## Final boot policy

```text
USB -> TF microSD -> M.2 SATA -> eMMC
```

`u-boot,spl-boot-order` remains:

```dts
u-boot,spl-boot-order = &sdmmc, &sdhci;
```

U-Boot proper sets `boot_targets` to `usb mmc1 scsi mmc0`.

## SD power chain

The working TF path retains the vendor-derived power tree in the U-Boot DTS:

```text
RK806 PMIC on SPI2
pldo-reg5 = vccio_sd_s0, 3.3 V
RK806 dcdc-reg8 = vcc_3v3_s3
vcc_sd = GPIO4_C19 controlled 3.3 V
sdmmc vmmc = vcc_sd
sdmmc vqmmc = vccio_sd_s0
```

The earlier `PD_SDMMC` SPL write experiment (`0004`) remains archived as an
ineffective diagnostic. It is not the fix and must not be presented as one.

## Module-rail power

U-Boot no longer changes generic Rockchip source code to raise four board
rails. The final implementation is board-DTS only:

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

`CONFIG_GPIO_HOG=y` is enabled in `really-rk3588_defconfig`. This drives
only the switched GPIO0_C0 rail used by GT9110 and HYM8563. The fixed
2.8 V rail, always-on 5 V rail and Linux-owned LVDS rail are not forced
from U-Boot.

## Recovery artifact

Before writing the P743c U-Boot, the first 16 MiB of the TF card were saved
on the device:

```text
/root/really-tf-uboot-backup-20261116.bin
SHA256 224a912059d7a329f1bc0e09f2ae30923923bcd46e36308fe5911e4bd459f39c
```

The written U-Boot payload SHA-256 was:

```text
0d30dd15b2ac2bc78d292d7b8cbbb80b20e491d2ba8dd46a8acf42c06ce77291
```

## Historical note

Earlier revisions of this file described a real `mmc_init()` failure and
incorrectly treated TF boot as unresolved. Those measurements belong to
U-Boot trees that had lost the RK806 `vqmmc` provider or otherwise predated
the final board DTS. They are superseded by the cold-boot result above.
