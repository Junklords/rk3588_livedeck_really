# 一次刷机验收清单（复现镜像 v1）

- 镜像：`output/images/Armbian-unofficial_26.11.0-trunk_Really_resolute_edge_7.3.4_gnome_desktop.img`
- SHA256：`e54aa4c0cd009242fb45258e0a92c64d88c8fd8b94ecbd9108cb9cc4fd193edd`（v1-final, verify-chain 26 PASS/2 WARN/0 FAIL）
- 大小：6.5GB（原镜像 7.3GB，差额 = 未包含的 mpv/ffmpeg 本地编译栈，见 REPRO §4）
- 刷法：maskrom 下 `rkdeveloptool wl 0 <镜像>`（与原镜像同法，约 6 分钟）
- 密码：root / user 均为 `armbian`（GDM 自动登录 user；SSH root@设备IP）

## 已完成的静态验收（本机）

```text
✓ U-Boot 布局：RKNS@sector64, FIT@+0x7f8000 —— 与原镜像一致
✓ FIT atf-1/2/3 + fdt-1 哈希与原镜像逐字节相同（BL31 v1.48/DDR v1.20 链）
✓ u-boot proper 哈希不同 = 构建时间戳/版本戳（源同为 tag:v2026.07+board_really 补丁）
✓ DTB: i2c4=m1, LT9211@2d ACTIVE_HIGH A6, dual-LVDS, PWM12,
        reboot-mode(mode-sd=0x5242c312) 已补回
✓ armbianEnv: reallylog_enabled=yes, extraargs 含 fbcon=rotate:3
✓ 5 个 really 服务 + timer, wireplumber es8388, dconf, GDM 自动登录
✓ /etc/armbian-image-release: BOARD=really EXTENSIONS='really-firstrun'
```

## 刷机后现场验收（按序执行，全程只需这一次 maskrom）

1. **串口**：SPL `2026.07` + `DDR ... fwver: v1.20` + `BL31 ... fwver: v1.48`
   + `rockchip-drm` bound vop/dp/hdmi/dsi + `Linux 7.3.4` → login
2. **SSH 上线**：`ip a`（千兆 end0）、`dmesg | grep -E "lt9211|Goodix|hym8563"`
   —— 预期与原镜像相同指纹：`lt9211 Unknown Chip ID 0x00×3`（复现 baseline）
3. **HDMI**：GNOME 桌面正常（原镜像同水平）
4. **内屏**：背光亮、无画面（baseline 复现确认）
5. **E1 决定性实验（SSH 迭代，不再刷机）**：
   ```sh
   fdtput -t s /boot/dtb/rockchip/rk3588-really.dtb \
       /i2c@feac0000/touchscreen@14 status disabled
   reboot
   ```
   预期：dmesg 出现 LT9211 正常 init（无 Unknown Chip ID），**内屏出画面**。
   - 若出画面 → 根因定案，进入 F1（goodix 驱动错误路径补丁）+ I2C4 100kHz
   - 若仍无画面 → 读 heal 后 0x2d 手动复位脉冲时序（panel 报告 §4），
     下一轮 SSH 实验，仍无需刷机
6. **reboot 工作流**（验证补回的 reboot-mode）：`reboot loader` 进 BootROM 下载
   模式（免物理 maskrom）、`reboot sd` 切 TF 优先
7. 触摸/RTC：保持 known-broken 状态（P0 报告），不在本次验收范围
