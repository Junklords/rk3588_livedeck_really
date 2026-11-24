# Really LT9211 单 DTB 50/60Hz 最终方案

- 作者：Codex
- 日期：2026-11-24
- 适用分支：Armbian `edge`，Linux 7.3.6
- 环境：Really TVI2340R，DSI1 → LT9211 → dual-link VESA-24 LVDS

## 最终结构

镜像只包含一个 `rk3588-really.dtb`。DTB 内同时注册两个 DRM mode：

| 模式 | pixel clock | htotal | vtotal | 用途 |
| --- | ---: | ---: | ---: | --- |
| 1920x1080@50 | 132.0 MHz | 2200 | 1200 | preferred，镜像默认 |
| 1920x1080@60 | 148.5 MHz | 2200 | 1125 | 稳定批次可在 GNOME 设置中选择 |

50Hz 保留原厂水平 blanking，将 `vback-porch` 调整为 111 行，得到精确
`132000000 / (2200 × 1200) = 50Hz`。60Hz 仍是标准原厂时序。

## 实机结果

- 两个已知硬件批次均验证 50Hz 正常，未出现 LT9211 PCR 失锁。
- 在这台可稳定 60Hz 的机器上，50Hz 实测 VOP clock 为 132MHz，
  LVDS `connected`、DPMS `On`，`PCR unstable` 计数为 0。
- 测试 56Hz 时，VOP 实际只能得到 136.36MHz，约 55.1Hz；LT9211 报
  `PCR unstable, ret=-110`。因此没有保留 56Hz 或 48Hz profile。

## 用户切换

50Hz 是 DTB 的 preferred mode，也是预置 GNOME 显示器配置的默认值。
用户可直接在标准 GNOME「设置 → 显示器」中把内屏切换为 60Hz；GNOME 会
保存用户选择。没有新增命令行工具、服务或 udev 规则。

## initrd 缓存修复

上一版镜像发现 rootfs 已删除 `71-really-phy-led.rules`，但旧 initrd 中仍
包含该规则。原因是 Armbian initrd 缓存键没有包含 `/etc/udev`。构建框架现已
将 `/etc/udev` 和 `/usr/lib/udev/rules.d` 纳入缓存哈希；发布镜像同时验证：

- rootfs 无 `71-really-phy-led.rules`
- initrd 无 `71-really-phy-led.rules`
- PHY LED 由 `end0` 就绪后的 systemd oneshot 配置

## 交付验证

- Armbian 构建仓提交：`81a85af76`
- Device 仓提交：`990b325`
- 发布镜像 SHA256：
  `51c8d9b62ddd2aa7f6556192894f034bcf745d11c0ffe3b9eca5f228b2a1fd5c`
- 分享 ZIP SHA256：
  `ed20c1c04b80402f791fa980d91c5f479cb0638a7e083e559ba35e3cd295b78a`

已刷入 Really eMMC 并实机验证：默认 50Hz、LVDS connected/On、
`dclk_vop3=132MHz`、旧 PHY LED udev 规则不在 rootfs/initrd、
`really-phy-led.service` 为 enabled/active，系统无 failed units。
