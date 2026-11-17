# Really TVI2340R — 6.1 Android 设备树

本目录存放 **Android 侧**（vendor 6.1 / Android 16 EVB7）使用的设备树源。

## 文件

| 文件 | 说明 |
|---|---|
| `rk3588-really-android.dts` | Really 的 6.1 Android 设备树 |

## 与 dts-6.1-linux 的关系

两者同源，**唯一区别是末尾的 dtsi 包含**：

```diff
# dts-6.1-linux/rk3588-really.dts
- #include "rk3588-linux.dtsi"

# dts-6.1-android/rk3588-really-android.dts
+ #include "rk3588-android.dtsi"
```

两个 dtsi 的差异：

| 项目 | rk3588-linux.dtsi | rk3588-android.dtsi |
|---|---|---|
| bootargs | 含 `root=PARTUUID=... rw rootwait` | 无 root 参数（由 Android init 接管） |
| mmc aliases | 有（mmc0/1/2） | 无 |
| vendor-storage | 无 | **有**（`rockchip,vendor-storage-rm`） |
| 显示 | `disable-win-move` | `support-multi-area` |

**判断依据**：Android 16 已刷入设备的 resource DTB（191066 字节）
反编译后含 `vendor-storage-rm` 节点，证明 Android 侧确实必须用
`rk3588-android.dtsi`。

## 重要差异：LVDS 链路

| 项目 | 当前本目录版本 | 已刷入设备的 Android16 版 |
|---|---|---|
| panel-lvds dual-link | ✅ `dual-lvds-odd/even-pixels` | ❌ 单链路（port@0 + port@2） |
| LT9211 port@3 | ✅ 有 | ❌ 无 |
| DTB 大小 | 295964 | 191066 |

**已刷入的那版是单链路**，对应 mainline 侧曾出现的"只显示半幅桌面"
问题（见 `dts-7.3-linux/FIXES.md`）。本目录版本已按双链路生成。

## 编译

依赖 rockchip vendor 6.1 内核树（需含 `rk3588-android.dtsi`）：

```bash
K=<rockchip-vendor-6.1-tree>
cp rk3588-really-android.dts $K/arch/arm64/boot/dts/rockchip/
cd $K
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- rockchip/rk3588-really-android.dtb
```

已验证可编译的环境：

```text
使用目标 vendor 6.1 内核源码树
（Armbian rockchip64 6.1.172 树，含 rk3588-android.dtsi）
```

注意：该树版本为 6.1.172，而设备 Android 内核是 6.1.145。
DTB 结构兼容，但若有 vendor 专有属性需求，应以
目标 Android 内核实际使用的 DTSI 和驱动为准。

## 产物校验

```text
rk3588-really-android.dtb
  size  295964
  sha256 f2801118cc36dc919cd33ec1beeca2275e8fea25cb29d861b92bd75bb86633dd
```

## 2026-11-17 更新

本节取代前文“仅 include 不同”、LT9211 继续禁用和旧 DTB 尺寸/哈希记录。
Linux 与 Android 继续共享供电、GPIO、PDM、LT9211 和 Type-C 硬件事实，
但触控旋转由不同用户态栈处理：

- Linux 使用 `inverted-x + swapped-x-y`；
- Android 保留原厂 framework 旋转映射。

当前 Android DTS 已恢复 DSI1 → MIPIDCPHY1 → LT9211 → 双链路 LVDS，
并补齐 PDM0、HYM8563 LPO、Wi-Fi host-wake、HUSB311 GPIO0_B7、
ES8388 耳机检测和无用 `wifi_lpo` 清理。

本轮 DTC 校验：

```text
Android DTB: 202865 bytes
SHA256:      952c21b31e730c09f302b4db3fdca9eeb1e63540c125faf301a1beedcde12575
```

目标内核需启用 `CONFIG_DRM_LONTIUM_LT9211`；DSI1 双链路、触控映射、
PDM0、Wi-Fi LPO 和 Type-C 仍需在目标 Android 镜像上实机验收。

## 最终交叉审计

Android 与 Linux 现已同步补齐：

- 摄像头/5G/电源灯 GPIO regulator 缺失的 pinmux；
- ES8388 原厂 I2S1 M0、耳机/线路输入 pinmux 与 DAPM；
- 标准 SARADC3 播放暂停键；
- 24C04 EEPROM；IMX577 与 CSI0 图随核心板 MIPI 接口恢复；
- I2C6 `0x18` STK8BAxx 加速度计；
- HYM8563 LPO、PDM0、LT9211 双链路、HUSB311 GPIO0_B7。

最终 DTB：

```text
size   202865
sha256 952c21b31e730c09f302b4db3fdca9eeb1e63540c125faf301a1beedcde12575
```

`lontium,lt6911uxc2` 在目标 vendor 树中仍没有完全匹配的 compatible。
在补到对应 UXC2 驱动前，隐藏 HDMI IN 路径只能视为“DTS 已描述、驱动未闭环”。

合并前还需确认目标 config 已启用 LT9211/LVDS panel、Rockchip
PDM/DMIC、HYM8563 和 HUSB311。Really 板级配置将 STK8BA50 设为 `y`。
AT24 EEPROM 保持可选；当前机器的 `0x56` 不 ACK。Type-C Sink 与 Source
都声明固定 5V/3A PDO。
