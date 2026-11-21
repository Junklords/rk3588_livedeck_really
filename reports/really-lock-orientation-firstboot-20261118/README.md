# Really 锁屏触摸唤醒、方向恢复与首次启动闪烁

- Author: Codex (GPT-5); executor: user
- Generated: 2026-11-18T22:20:00+08:00
- Context: 实机定位三个回归：LVDS DPMS off 后触摸不亮屏、锁屏恢复后
  方向顺时针旋转 90 度、部分新镜像首次启动时控制台/GDM 黑亮闪烁。
- Applicable branch: `/home/user/github/build`, `main`; Armbian edge
  `rockchip64-7.3`; Really mainline image.
- Environment: Really TVI2340R，Goodix GT9110，STK8BA50，15.6 英寸竖装
  LVDS，GNOME 50 / Wayland。
- Inputs: 实机 SSH、`evtest`、libinput、Mutter DisplayConfig、GNOME
  ScreenSaver、`loginctl` 和 systemd 状态。
- Outputs: 根因证据、用户态修复和待实机回归项。
- Safety: 触摸唤醒 helper 只发送 `KEY_WAKEUP`，不会点击、解锁或绕过
  GNOME 锁屏。

## 1. 锁屏熄屏后触摸不亮

实机状态：

```text
LVDS DPMS = Off
GNOME ScreenSaver = Active
Goodix event1 = complete touch events
Mutter IdleMonitor = not reset by touch
```

`evtest /dev/input/event1` 在 DPMS off 时仍能收到完整的
`ABS_MT_TRACKING_ID`、坐标、`BTN_TOUCH` 和 `SYN_REPORT`，说明触摸控制器、
中断、I2C 和输入子系统都正常。问题位于 GNOME/Mutter 对熄屏首触的处理。

验证：

- 早期测试曾观察到注入 `KEY_WAKEUP` 后 LVDS 变为 `On`，但 2026-11-18
  夜间复测无法稳定重现；在 `ScreenSaver.GetActive=true`、DPMS `Off`
  时注入后，DPMS 仍可保持 `Off`；
- 向 `/dev/uinput` 注入无副作用的功能键 `KEY_F24` 后，实测从注入到
  `LVDS-1 dpms=On` 约 61 ms，两秒后 `LockedHint=yes` 仍成立；
- `org.gnome.ScreenSaver.SetActive(false)` 虽然也能点亮，但随后会解除
  `LockedHint`，因此明确禁止用于触摸唤醒。

修复：

```text
userpatches/really/touch-wake/really-touch-wake
userpatches/really/touch-wake/really-touch-wake.service
```

helper 只在以下条件同时满足时发送 `KEY_F24` 活动事件：

```text
event.type=EV_KEY
event.code=BTN_TOUCH
event.value=1
/sys/class/drm/card*-LVDS-1/dpms=Off
```

正常亮屏和普通触摸不会触发额外事件。

`KEY_WAKEUP` 不再使用。GNOME 50 的锁屏路径会忽略该键；`KEY_F24` 在锁屏
状态下没有可见字符或快捷动作，但会立即唤醒显示并把 idle 计数归零。

## 2. 锁屏恢复后方向顺时针旋转 90 度

实机抓取到：

```text
SensorProxy AccelerometerOrientation = normal
Mutter LVDS-1 logical transform = uint32 3
```

正常竖屏映射应为 `normal -> transform 0`。旧版
`really-auto-rotate` 只在传感器方向字符串变化时应用 transform，因此
Mutter 在锁屏/熄屏/恢复过程中重置 transform 后，服务不会再次修正。

修复：

- 保留 700 ms 采样和 4 次稳定判定，防止碰撞误触发横屏；
- 在方向未变化时也读取 Mutter 当前 transform；
- 如果 LVDS transform 与目标不一致，立即重新应用；
- 实机日志已经出现 `orientation=normal transform reconciled`，当前
  transform 恢复为 `0`。

代码：

```text
userpatches/really/auto-rotate/really-auto-rotate
```

## 3. 首次启动控制台/GDM 黑亮闪烁

根因假设：

- Armbian first-login 向导运行在 `tty1`；
- GDM 同时在启动并尝试接管图形 VT；
- 两者竞争显示和控制台，可能表现为串口日志阶段屏幕反复黑亮，且无法
  正常操作首次配置。

修复：

- 给 GDM 增加
  `ConditionPathExists=!/root/.not_logged_in_yet`；
- 首次登录期间 GDM 不启动；
- `really-start-gdm.timer` 每秒检查一次，首次登录成功删除 marker 后，
  立即启动 GDM 并自禁用 timer。

文件：

```text
userpatches/really/first-login/gdm3-first-login.conf
userpatches/really/first-login/really-start-gdm.service
userpatches/really/first-login/really-start-gdm.timer
```

该修复只解决启动阶段显示管理器竞争。群友设备上已经出现另一条更底层的
显示链路证据：`lt9211 4-002d: PCR unstable, ret=-110`，运行中 PCR 锁定位
会在黑闪期间反复丢失。实验带外模块在 PCR 失败时执行安卓逆向推断的
fallback，画面有所改善但仍有短黑闪，因此尚未形成可合入的修复。

因此“首次启动闪烁”至少有两种不同故障：

1. GDM 与 tty1 first-login 竞争，表现为首次配置阶段黑亮交替；
2. LT9211/DSI 输入或 PLL 间歇失锁，表现为运行中约 0.1 秒到数秒的随机
   黑闪，且 SSH、GDM、VOP 状态仍可能正常。

当前构建只自动修复第 1 类。第 2 类不能通过修改 GDM 或 `KEY_WAKEUP`
解决，也不能直接采用群友当前的带外实验模块作为正式方案。

若群友设备的闪烁仍存在，需要继续采集：

```text
cat /sys/class/drm/card*-LVDS-1/dpms
journalctl -b -u gdm3 -u plymouth-start --no-pager
journalctl -b -k | grep -iE 'drm|vop|dsi|lt9211|backlight'
systemctl status getty@tty1 gdm3
```

以区分 GDM race、Plymouth/KMS modeset 和面板背光链路问题。

### LT9211 PCR 失锁专项

群友 AI 的原始记录：

```text
/home/user/Downloads/LT9211显示黑屏修复与回退说明-2026-11-18.md
```

关键事实：

- 原厂 DTS 的 LT9211 节点没有标准 DRM graph，只提供私有
  `lt9211,output-mode = <3>`；私有驱动在代码中解释为 dual-link，不能把
  反编译图的单端口外观当成硬件单链路；
- DSI 输入能识别为 1920x1080、RGB888，byteclock 约 111365 kHz；
- LT9211 PCR 检查仍可能超时，错误为 `-ETIMEDOUT`；
- 黑闪期间 `0xd087` bit 3 多次从锁定变为未锁定；
- 仅取消 `MIPI_DSI_MODE_NO_EOT_PACKET` 后仍闪烁；
- 仅把 `0x8737` 从 `0x14` 改为安卓值 `0x0e` 后仍不能确认改善；
- 强制安卓式 PCR fallback 后仍会间歇失锁；
- 同型号、同镜像的另一台机器不闪，仍需核对硬件批次和上电/信号差异。

原厂 Android 的运行时基线与该改时钟方案不同：

```text
SurfaceFlinger: 1080x1920 @ 60.000004 Hz
HWC/DRM:        1920x1080p60
timing:         2008-2052-2200 / 1084-1089-1125
pixel clock:    148500 kHz
```

因此原厂 Android 没有主动降到 50 Hz，而是继续使用 148.5 MHz、60 Hz；
稳定运行依赖 vendor 5.10 LT9211 私有驱动及其 PLL/PCR、DSI mode 和寄存器
初始化，而不是当前 mainline 驱动的同一套实现。原厂节点使用
`lt9211,output-mode = <3>`、`lt9211,input-mode = <0>`、
`lt9211,mipi-in-port = <0>` 等私有属性，mainline LT9211 驱动不会读取这些
属性。

群友把相同 blanking 下的 pixel clock 从 148.5 MHz 改为 123.75 MHz，数学
结果是从 60 Hz 降到 50 Hz，DSI byteclock 从约 111.375 MHz 降到约
92.8125 MHz，确实增加了接收端时序余量。这是有价值的可复现 workaround，
但应表述为“mainline 链路在 60 Hz 下时序余量不足，降频后可稳定”，不能
直接表述为“原厂设计就是 50 Hz”。另外，截图同时记录 VOP `dclk_vop3`
为 118.8 MHz，与请求的 123.75 MHz 不一致，合入前必须确认实际
`clk_summary`、DRM mode 和实测帧率，排除时钟 parent 只能落到 118.8 MHz
而实际约 48 Hz 的情况。

当前主线 dual-link 配置在正常时应产生：

```text
0x8237 = 0x2a
0x823b = 0xb8
0x855c = 0x07
```

群友实验驱动的只读检查已经看到上述值，说明当前黑闪不是“仍按单链路
0x855c=0x00 输出”的直接表现。单链路通常造成半幅、像素时钟/时基错误、
竖条纹或整列抖动；PCR 掉锁发生在 DSI 接收/PLL 阶段，并会让整块画面
短暂消失。两者必须分开定位。

后续应优先做可重复的单变量验证：

- 将实验驱动源码、构建命令和内核源码版本纳入构建树，不继续使用运行时
  手工编译模块；
- 同步采集 PCR 状态变化与实际黑屏时间戳；
- 对照不闪机器的 DSI 寄存器、LT9211 寄存器、复位/供电时序和 Type-C/
  USB 状态；
- 检查 DSI 非 burst、clock lane HS/LP、EoT 和复位延迟；
- 只有在多次冷启动和热重启都通过后，才考虑进入 Armbian 内核补丁。

在完成上述闭环前，不把 `lt9211_trial.ko`、强制 PCR fallback 或安卓寄存器
值固化到社区构建中。

## 文件位置规范

非 `config/boards/*.conf` 的 Really 运行资产统一放在：

```text
userpatches/really/
```

当前分为 `auto-rotate`、`first-login`、`touch-wake` 和 `hotfix`。
`config/boards` 只保留板级定义和 hook。

## 待实机闭环

- 锁屏 DPMS off 后，仅触摸应亮屏且保持锁屏；
- 点亮后 LVDS 应为竖屏 transform 0；
- 首次启动只显示 first-login，不出现 GDM/控制台竞争；
- 完成首次创建用户后，GDM timer 能自动启动图形登录；
- 新镜像需要在至少一台群友设备上验证启动闪烁是否消失。
