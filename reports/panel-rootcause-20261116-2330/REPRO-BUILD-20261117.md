# DeepSeek 镜像复现构建：方法重建、差异清单与证据

- 生成：2026-11-17（ZCode 会话）
- 目标：用 Armbian 框架在本机复现
  `Armbian-unofficial_26.11.0-trunk_Really_resolute_edge_7.3.4_gnome_desktop.img`
  (SHA256 `cb4645f9...`，已实机验证可启动的那份)
- 原则：每一步差异都有镜像内证据；无法取证的（个人镜像源等）用框架默认并注明

## 1. 构建方法重建（全部来自公开证据）

| 项 | 值 | 证据 |
|---|---|---|
| 构建命令 | `./compile.sh RKBIN_GIT_REF=commit:98f1065... really` | 交接文档 §12.1 |
| BOARD | `really` | 镜像 `/etc/armbian-image-release` |
| BOARDFAMILY / LINUXFAMILY | rockchip-rk3588 / rockchip64 | 同上 |
| RELEASE | resolute (Ubuntu 26.04) | 镜像文件名 + release 佐证 |
| BRANCH / 内核 | edge / 7.3 (=v7.3.4 tag 7595a62) | `#61` 内核 + 镜像 boot 文件 |
| 桌面 | gnome, tier=**mid** | 镜像无 LibreOffice、101 个 .desktop |
| EXTENSIONS | `really-firstrun` | release 元数据 |
| rkbin pin | commit 98f1065（含 BL31 v1.48 + DDR v1.20_20250926） | 运行时 banner fwver 实证 |
| U-Boot | mainline tag:v2026.07 + v2026.07/board_really 补丁 | SPL banner `2026.07_armbian-...` 16:39:33 构建 |
| 启动布局 | binman `u-boot-rockchip.bin` dd@sector64; itb@+8MB | 镜像头 RKNS/0x7f8000 FIT 实测 |
| boot.cmd | 镜像内 boot.cmd 原样复用（含 reallylog 打点） | /boot/boot.cmd |
| 账户 | user(1000)+GDM 自动登录；密码预设 | /etc/shadow 有哈希；密码值改为文档化 'armbian' |
| 内核 config | CONFIG_DEBUG_INFO_NONE（无 BTF）→ KERNEL_BTF=no | 镜像 /boot/config-7.3.4 |

本机复现新增（非 DeepSeek 内容）：
- `userpatches/config-really.conf`（他们未公开的个人预设，按上表重建）
- `userpatches/config/boards/really.conf`（fork 里已提交的板级文件重建）
- `userpatches/extensions/really-firstrun.sh`（部署镜像内提取的 really-overlay/）
- `userpatches/u-boot/v2026.07/{defconfig,dt_uboot,dt_upstream_rockchip,board_really}`
  ← 逐字节来自公开仓 u-boot-mainline/
- `userpatches/kernel/archive/rockchip64-7.3/dt/rk3588-really.dts` ← 公开仓 dts-7.3

## 2. 与镜像的实测差异（DTB 符号化逐节点比对）

除 phandle 编号漂移外，实质差异三项：

1. **reboot-mode 节点缺失（已补回）**：镜像 DTB 有 `syscon@fd588000 → reboot-mode`
   (mode-bootloader/loader/normal/recovery/emmc/nvme/sd/tf/usb)。它与 U-Boot
   0003 补丁（读 PMU0_GRF 0x80 决定 boot_targets）配对，是 `reboot sd/usb/
   loader` 免 maskrom 工作流的基础。公开仓 dts 无此节点 → 判定为同步 v85
   时误删，已按镜像字节补回（含证据注释）。
2. **eDP0 aux-bus edp-panel**：repo HEAD 有、镜像无（21:27 之后的未提交实验，
   handover §2.1 列名）。保留 repo HEAD 状态 = 保留他们最后意图。
3. **touchscreen-size x/y**：repo HEAD=1080/1920（竖屏，Android 基线一致），
   镜像=1920/1080。保留 repo HEAD（对 ENXIO 无影响，属正确方向）。

## 3. 工厂链静态分析（零刷机，全部来自 box-backup-20261101/uboot_a.img）

FIT「ATF/OP-TEE/U-Boot/MCU」(2023-10-11 构建)：

```text
fac-bl31  @0x140e00 198732B  v2.3-557-g9609b9c19 (2023-03-08, = rkbin v1.38 系)
fac-optee @0x17d800 461200B  plat-rockchip OP-TEE (com.rockchip.tee.{jtagID,chipID,cpuID,seed} PTA)
fac-vuboot@0x1000  1310088B vendor U-Boot (2017.09 系, 含 rk3588 OTP)
```

结论：
- 工厂 OP-TEE 与社区 SPL 组合曾致「BL31: Initializing BL32」挂死——版本配对
  （BL31 v1.38 系 + 其同代 OP-TEE）与加载方（vendor miniloader 预留 0x08400000
  等）强相关。社区链 BL31 v1.48 无 OP-TEE 已实测可完整启动 Linux。
- **内屏/触摸不需要 OP-TEE**（工厂内屏数据链 = vendor 驱动 + 100kHz I2C4，
  无安全服务参与）。OP-TEE 议题仅在 TF/SCMI 语境下有意义，且已有 PMU GRF
  hiword 直写绕过（P0 报告 §二）。

## 4. 未复现项（诚实清单）

- `/usr/local/bin/{mpv,ffmpeg,ffprobe}`（镜像内为源码编译的 V4L2-request 硬解
  版，~1.5GB 工具链工作）→ 首版不含，不影响启动/显示/内屏验证
- 镜像内两文件 I/O error（51-really-camera-hide.conf、user 级 camera 服务）
  → 源文件损坏，等价物按 handover §7 重建或跳过
- goodix__cfg.bin（触摸固件配置）：镜像内本就缺失（-2 报错），非复现目标
