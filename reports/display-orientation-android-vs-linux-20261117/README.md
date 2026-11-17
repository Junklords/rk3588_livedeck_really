# Android 与 Linux 显示方向匹配机制对照

- Author: Codex (GPT-5); executor: user
- Generated: 2026-11-17T23:55:00+08:00
- Context: 对照 Really TVI2340R 原厂 Android 12 与当前 Armbian mainline
  Linux 7.3 的开机显示方向和重力自动旋转实现，确认 DTS、内核启动参数、
  显示合成器和传感器 HAL 各自负责的边界。
- Applicable branch: `/home/user/device/rk3588-really/rk3588_livedeck_really`,
  `main`; Armbian edge `rockchip64-7.3`; Android 12 vendor 5.10 baseline.
- Environment: RK3588 TVI2340R，15.6 英寸竖装 LVDS 屏，DSI1 -> LT9211 ->
  dual-link LVDS，STK8BA50 加速度计。
- Inputs: 原厂 Android `getprop`/WindowManager 抓取、原厂 5.10 DTS、
  Linux 7.3 DTS、`fbcon` 启动参数及 `really-auto-rotate` 实测记录。
- Outputs: 机制分析、当前 GAP、后续优化顺序和验收标准。
- Status: 仅记录分析结论；本次不修改 DTS、Plymouth、GDM、Mutter 或
  自动旋转服务。
- Safety: 无运行时或镜像改动。

## 结论

原厂 Android 不是“用 DTS 实时旋转屏幕”。它把方向问题拆成了三层：

1. SurfaceFlinger 在启动时按固定方向组合主显示；
2. Rockchip 私有传感器驱动通过 DTS `layout` 属性校准加速度计轴；
3. Android WindowManager 根据 SensorService 数据决定应用内容旋转。

当前 Linux 也采用分层实现，但固定初始方向的时机晚于 Android，因此冷启动
时可能先出现一帧或一段短暂的横屏内容，再切到竖屏。

## 原厂 Android 证据

### 初始显示方向

原厂运行属性：

```text
ro.surface_flinger.primary_display_orientation=ORIENTATION_270
ro.sf.fakerotation=false
```

来源：
[getprop.txt](../../../reports/android-touch-baseline-20261115-1820/getprop.txt)。

含义：

- `ORIENTATION_270` 由 SurfaceFlinger 在主显示初始化阶段应用；
- 不依赖桌面或应用启动后的方向服务；
- Logical Display 从一开始就是 `1080x1920`，不是先建立
  `1920x1080` 再旋转；
- `ro.sf.fakerotation=false` 说明它不是 SurfaceFlinger 的 fake rotation
  测试路径。

Android minui 的 Recovery/fastbootd 方向另有固定配置：

```text
ro.minui.default_rotation=ROTATION_LEFT
```

因此 Android 的早期文本/恢复界面也不会依赖自动旋转服务。

### 传感器安装方向

原厂 5.10 DTS 的 STK8BAXX 节点：

```dts
gs_stk8baxx@18 {
	compatible = "gs_stk8baxx";
	reg = <0x18>;
	layout = <7>;
};
```

来源：
[rk3588-tvi2340r-orig.dts](../../../docs/rk3588-tvi2340r-orig.dts)。

Rockchip 私有传感器驱动读取 `layout` 并应用轴方向矩阵。原厂确实把
“传感器相对机壳如何安装”固化在 DTS 中，但它依赖：

- `gs_stk8baxx` 私有 compatible；
- Rockchip `drivers/input/sensors` 私有驱动；
- 私有 `layout` 属性，不是 mainline IIO 标准 `mount-matrix`。

当前 mainline `sensortek,stk8ba50` 驱动没有调用
`iio_read_mount_matrix()`，所以不能直接照搬这个 DTS 属性。Linux 当前使用：

```text
ACCEL_MOUNT_MATRIX=1,0,0;0,-1,0;0,0,1
```

### 动态重力旋转

原厂 WindowManager 抓取：

```text
mSupportAutoRotation=true
mUserRotationMode=USER_ROTATION_FREE
mCurrentRotation=ROTATION_0
mProposedRotation=0
```

来源：
[window-display.txt](../../../reports/android-touch-baseline-20261115-1820/window-display.txt)。

Android `WindowOrientationListener` 和 `AccelSensorJudge` 持续读取
SensorService 的加速度计数据，并处理倾斜、抖动和方向迟滞。该过程属于
Android framework，不属于 DTS。

## 当前 Linux 7.3 实现

| 层 | 当前实现 | 生效时机 |
| --- | --- | --- |
| 面板原始时序 | DSI/LT9211 扫描 `1920x1080@60` | 内核 DRM modeset |
| 静态面板方向 | `panel-lvds` 节点 `rotation = <270>` | DRM connector 注册后 |
| fbcon | `fbcon=rotate:3` | framebuffer console 初始化 |
| Plymouth/GDM | 分别作为独立 KMS 客户端启动 | 用户态图形启动 |
| 传感器轴校准 | udev `ACCEL_MOUNT_MATRIX` | iio-sensor-proxy 读取前 |
| 动态旋转 | `really-auto-rotate.service` -> iio-sensor-proxy -> Mutter | GNOME 用户会话 |

相关实现：

- DTS：
  [rk3588-really.dts](../../dts-7.3-linux/rk3588-really.dts)
- `fbcon`：
  `/home/user/github/build/config/boards/really.conf`
- 自动旋转：
  `/home/user/github/build/config/boards/really-auto-rotate/`

## 为什么会先横屏再竖屏

Linux 启动链同时存在多个 KMS/framebuffer 客户端：

```text
kernel modeset -> fbcon -> Plymouth -> GDM -> GNOME session
```

每个阶段使用方向策略的时机不同：

- DRM `rotation` 主要是 panel orientation 元数据，不会把 LT9211/LVDS
  原始扫描时序变成物理 `1080x1920`；
- fbcon 只负责文本控制台；
- Plymouth 和 GDM 不会自动共享同一个 Mutter logical transform；
- `really-auto-rotate.service` 只在图形用户会话建立后运行；
- 为避免误触，脚本还要求方向稳定约 2.1 秒才应用 transform。

因此，当前横屏闪现的根因是“多个显示客户端没有使用同一个首帧方向”，
不是 LT9211、LVDS 双链路或 DTS 面板时序错误。

## DTS 能固化与不能固化的部分

可由 DTS 固化：

- 面板物理安装方向；
- 原始扫描时序；
- 传感器安装矩阵，前提是驱动支持标准 `mount-matrix` 或保留私有
  `layout` 语义；
- connector 与 panel 的硬件拓扑。

不能仅由 DTS 固化：

- Plymouth、GDM、Mutter 的首帧 logical transform；
- 重力方向到桌面内容的动态映射；
- 防抖/迟滞时间和方向锁定；
- 多显示器布局与 Type-C DP 主屏策略；
- 用户会话建立前的图形客户端切换时序。

## 后续优化候选

### P1: GDM/system Mutter 固定首帧方向

目标是参考 Android
`ro.surface_flinger.primary_display_orientation=ORIENTATION_270` 的时机，
在 GDM greeter 和用户会话建立前提供确定的 LVDS transform。

需要先确认二选一，避免双重旋转：

- 继续使用 DRM panel orientation，只修复 GDM 对该元数据的首帧消费；
- 或关闭对该 connector 的隐式方向假设，由系统级 `monitors.xml` 明确指定
  初始 transform。

候选落点：

- GDM greeter 的 `monitors.xml`；
- 系统级 Mutter monitor configuration；
- GNOME session 的初始 display config，而不是用户登录后的临时 D-Bus
  调用。

### P2: 分离“首次应用”和“后续方向变化”

评估自动旋转服务是否可以在确认当前静止方向后立即应用第一次 transform，
后续方向变化仍保留稳定采样窗口，以维持防误触能力。

该优化只能缩短 GNOME 会话内的切换时间，不能消除 Plymouth 或 GDM 阶段的
横屏首帧。

### P3: Plymouth/早期 KMS 方向

单独检查 Plymouth 是否在启动时创建横向 scanout。该层与 GDM 配置不共享，
必须在解决 P1 后继续观察。

可选方向：

- 配置 Plymouth 使用与面板一致的方向；
- 缩短 Plymouth 可见时间；
- 在保留串口日志的前提下减少早期图形切换。

### P4: 传感器矩阵是否迁入 DTS

只有在上游 `stk8ba50` 驱动正式支持标准 `mount-matrix` 后才考虑迁移。
不建议为单板复制 Rockchip 私有 `layout` ABI。

## 验收标准

- 冷启动第一帧可见桌面/Plymouth 内容即为竖屏；
- 不应出现完整可见的 `1920x1080` 横向桌面；
- 正常竖放时无需等待自动旋转服务建立方向；
- 四个重力方向的映射仍与现有实机结果一致；
- 轻微碰撞不会触发横竖屏切换；
- HDMI 和 Type-C DP 外部显示器方向不受影响；
- 不出现 DTS panel orientation 与 Mutter transform 的双重旋转；
- 串口仍只输出日志，不抢占首次配置界面。

## 当前建议

保持现有 DTS、`fbcon=rotate:3` 和自动旋转实现不变。下一步优先验证 P1：
确定 GNOME 50 是否消费 DRM panel orientation，以及 GDM 首帧能否通过
系统级 monitor configuration 在会话创建前固定为竖屏。
