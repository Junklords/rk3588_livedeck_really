# Really TF 启动与空 eMMC 引导验证

Author: Codex (GPT-5)

Generated: 2026-11-21T10:05:00+08:00

Context: Really TVI2340R was flashed with
`Armbian-unofficial_26.11.0-trunk_Really_resolute_edge_7.3.5_gnome_desktop.img`
and the eMMC was believed to be fully erased. The board did not boot from
TF and stopped in Maskrom.

Applicable branch: `/home/user/github/build`, Armbian edge, Linux 7.3.y
rockchip64.

Environment: Really TVI2340R, community U-Boot v2026.07, TF rootfs,
Samsung eMMC.

## Image integrity

The complete image checksum passed before any storage write:

```text
Armbian-unofficial_26.11.0-trunk_Really_resolute_edge_7.3.5_gnome_desktop.img: OK
SHA256 e6f670bb0dbdebc8071f3d1451f89510da17a5ef0a00f67e8d7b6aaacb0651aa
```

The image contains the expected community boot chain:

```text
RKNS IDB header at LBA64
U-Boot SPL 2026.07_armbian-2026.07-Sece3-Pf968-Hae08-V6f98-Bd800-R448a
```

## Maskrom investigation

Maskrom access worked:

```text
DevNo=1 Vid=0x2207,Pid=0x350b Maskrom
Samsung eMMC, 118016 MB
```

Reading eMMC LBA0 through 32767 showed that the drive was not fully empty.
LBA64 still contained an `RKNS` header:

```text
52 4b 4e 53 00 00 00 00 ...
```

LBA16384, where U-Boot proper is expected in the old layout, was zero.
This left BootROM with a half-present eMMC boot chain.

The first 16 MiB were backed up and then written to zero:

```text
/home/user/skysi-x5/backups/really-emmc-head16m-before-sd-boot-20261121.bin
SHA256 8d2e1045af69569bc1d32de96e76d2d3e611eecc5621df555e23b6794db1e363
```

Readback confirmed LBA64 was all zero. A reset still returned to Maskrom,
with no TF boot and no network.

## Required bootstrap

The board's SD power switch enable is GPIO4_C3. The BootROM does not bring
that rail up for a direct TF-only boot. A valid U-Boot must therefore be
loaded from eMMC first; its SPL/U-Boot then enables SD power and boots the
complete rootfs from TF.

The exact U-Boot embedded in the image was written starting at eMMC LBA64:

```text
u-boot-rockchip.bin size: 9622016
u-boot-rockchip.bin SHA256:
f97a8b5f9f77cbea174913b32b0650a40477424752ea5d85a35d993206f646f9
```

The bytes matched the corresponding region of the Armbian image. An eMMC
readback matched the source binary byte-for-byte.

After reset:

- the Rockchip Maskrom USB device disappeared;
- no Android userdata/rootfs was written to eMMC;
- the board obtained the Armbian address `192.168.3.233`;
- TCP port 22 opened.

The freshly imaged TF system rejected the old SSH key, so the final root
device was not remotely inspected. That is an authentication result, not a
boot failure.

## Operational rule

For a TF-rootfs installation, write only the matching community U-Boot to
eMMC LBA64. Do not claim that completely empty eMMC supports direct
BootROM-to-TF startup on this hardware.

Recovery remains available through Maskrom:

```bash
rkdeveloptool boot rk3588_spl_loader_v1.16.113.bin
rkdeveloptool write 64 u-boot-rockchip.bin
rkdeveloptool reset
```
