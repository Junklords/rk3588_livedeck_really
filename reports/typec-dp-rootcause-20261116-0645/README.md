# Type-C DP / 全功能未闭环的真机根因分析

- 作者:ZCode(GLM);执行端:user
- 生成时间:2026-11-16T06:45:00+08:00
- 分析对象:Really TVI2340R 真机,`192.168.68.248`,内核 `7.3.4-edge-rockchip64 #68`
- 证据来源:真机 SSH 现场采集 + 本仓三套 DTS + 工厂原始 DTB 反编译 + git 历史
- 本报告修正并细化 `mainline-open-issues-20261115-2130` 中 "HUSB311 反复 detach"
  的定性:当前真机上的直接死因是 **Type-C 中断被内核禁用,tcpm 整体失明**。

## 0. 一句话结论

**rt1711h/husb311 的中断线(GPIO0_D7)持续为低,驱动连续 10 万次返回
IRQ_NONE,内核在开机第 204 秒以 "nobody cared" 永久禁用了 IRQ#132;
此后 tcpm 状态机卡死在 `SRC_ATTACH_WAIT`,partner 永不注册,DP Alt Mode
无从谈起。** 工厂 DTB 里该中断脚是 GPIO0_B7 而不是 D7——移植时引脚写错
是第一嫌疑。USB 数据面(CC 直连 + dwc3 默认 host)不受此影响,因此
"Type-C 口能传数据但 PD/DP 全废" 与现场完全吻合。

## 1. 现场快照(2026-11-16 06:18,uptime 6h24m)

```text
内核            7.3.4-edge-rockchip64 #68(注意:比 20261115 报告的 #61 新)
DRM             LVDS-1 connected / HDMI-A-1 connected / DP-1 disconnected
typec port0     data_role=host [device], power_role=source [sink]
typec partner   不存在
USB             仅一个 dwc3(fc000000,双 roothub 480+5000)在线,
                GL3523 hub(05e3:0610/0620)已枚举 → Type-C 上实际插着 hub/dock
vbus5v0_typec   state=disabled,num_users=0
```

## 2. 决定性证据链

### 2.1 dmesg:IRQ 被内核处决

```text
[    5.070363] rt1711h 6-004e: tcpm: CC1: 0 -> 0, CC2: 0 -> 2 [state TOGGLING, connected]
[    5.070392] rt1711h 6-004e: tcpm: state change TOGGLING -> SRC_ATTACH_WAIT
   (此后 tcpm 再无任何状态迁移)
[  204.822250] irq 132: nobody cared (try booting with the "irqpoll" option)
[  204.822449] handlers: irq_default_primary_handler threaded rt1711h_irq [tcpci_rt1711h]
[  204.822470] Disabling IRQ #132
```

### 2.2 /proc/interrupts:10 万零 1 次的标准指纹

```text
132: 100001 0 0 0 0 0 0 0  rockchip_gpio_irq  31 Level  6-004e
```

计数恰好停在 100001 = 内核 "连续 10 万次未处理即禁用" 机制触发点;
line 31 = GPIO0_D7;owner `6-004e` 即 I2C6 上的 HUSB311。

### 2.3 GPIO 实时电平

```text
gpio-31 (GPIO0_D7, DTS 声明的 husb311 中断)  in  lo  IRQ   ← 持续为低(持续触发)
gpio-15 (GPIO0_B7, 工厂声明的 husb311 中断)  未被任何驱动占用 ← 切换无冲突
```

### 2.4 tcpm 状态机卡死

开机 5.07s 检测到 CC attach 进入 `SRC_ATTACH_WAIT` 后,日志再无迁移。
正常流程应数十毫秒内进入 `SRC_ATTACHED` → 注册 partner → PD 协商。
`/sys/class/typec/` 下没有 `port0-partner` 目录,与 "partner absent" 一致。
USB 数据正常(GL3523 枚举)证明 CC 物理连接在、dwc3 host 角色在,
坏的只有 "需要中断驱动的 PD/AltMode 层"。

## 3. 与工厂 DTB 的逐项对照(反编译 dtb-5.10-android)

| 项目 | 工厂 DTB(硬件事实) | 运行 DTB(#68) | 判定 |
| --- | --- | --- | --- |
| HUSB311 中断 | `<0x0f 0x08>` = **GPIO0_B7** level-low,pinctrl 带 pull-up | `<0x1f 0x08>` = GPIO0_D7 | **不一致(首要嫌疑)** |
| GPIO0_D7 用途 | 全树零使用 | 声明为 husb311 INT,实测恒低 | D7 身份存疑 |
| GPIO0_B7 用途 | usbc0-int 专用 | 无人占用 | 切换无代价 |
| usbdp_phy0 SBU | GPIO3_A0 / GPIO1_B2 | GPIO3_A0 / GPIO1_B2 | 一致 |
| dp-lane-mux | 无此属性 | `<2 3>` | 移植新增,待验证驱动是否解析 |
| connector vdo | 0xffffffff | 0x1c46 | 移植收紧,合理 |
| Type-C 控制器 | usbdrd3_0/**usb@fc000000**(otg,role-switch,u2phy0+usbdp_phy0)→HUSB311 | 相同拓扑 | **一致,#68 无误** |
| fcd00000(USB3 HOST/USB-A) | usbhost3_0/usb@fcd00000 | status=disabled | 一致(USB-A 走 EHCI/GL3523 路径) |
| route-dp0 | **status = "disabled"** | (mainline 无 route 概念) | 出厂 Android 未启用 DP;README 中 "工厂启用 DP0" 仅控制器节点层面成立 |

注意:vendor dtsi 命名里 **usbdrd3_0 = 0xfc000000(USB3 OTG0,Type-C)**,
usbhost3_0 = 0xfcd00000(USB3 HOST)。mainline 分析时不要按字面地址想当然。

## 4. 根因假设与判别实验

- **H1(首选):中断脚写错。** 物理 INT 在 B7(工厂语义,Android 下 PD/USB
  正常,无中断风暴);D7 被板上其它网络拉低或悬空读低,造成风暴。
- **H2:脚对但驱动不认。** D7 确为芯片 INT,HUSB311(clone)挂起了
  rt1711h 驱动不清除/不认识的 vendor alert 位,INT 常拉低 → 驱动每次
  读标准 ALERT=0 → IRQ_NONE ×100k。
- **H3(旁证):** Android 基线 vendor 驱动日志出现
  `husb311_check_revision: i2c read retry`(见 android-dmesg-husb311.txt),
  说明该芯片上电初期响应慢,可能存在初始化时序敏感问题。

判别顺序:

1. 把 `interrupts`/pinctrl 改回 **GPIO0_B7**(B7 现在空闲,零冲突成本),
   冷启动复测。风暴消失 → H1 成立,一行修复。
2. 若 B7 仍风暴:开 tcpm/tcpci_rt1711h 动态调试,并给驱动加
   "IRQ_NONE 时 dump ALERT 寄存器" 的调试补丁,确认是哪个未清位 → 按 H2
   处理(初始化时屏蔽 vendor 中断源或补 ack 序列)。
3. 临时诊断手段(不改根因):`irqpoll` 内核参数可验证 "中断丢失" 假设,
   但不能作为产品方案。

## 5. 对既有分析的修正与保留

- 运行基线 #68 与本仓 HEAD(253d4b3)不完全相同:#68 **没有**
  typec-extcon-bridge、没有 GPIO4_A6 的 5V 开关,但 **同样的中断风暴**。
  因此 **中断问题独立于且优先级高于** 此前基于 git diff 找出的三个回归
  变量(SBU 换脚 / vcc5v0_usb 加开关 / extcon 桥)。
- 三个回归变量的 bisect(见 2026-11-15 会话分析)在修好中断、partner
  能注册之后仍然有效,用于解决 "Alt Mode 阶段反复 attach/detach" 的
  历史现象。
- 出厂 Android 从未启用 DP(route-dp0 disabled),不存在工厂 DP 黄金
  基线;11-14 mainline 4K30 配置仍是唯一黄金参考。

## 6. 修复路线(按优先级)

1. **P0:中断脚改回 GPIO0_B7**(对应 dts-6.1/7.3 与构建树
   `patch/kernel/archive/rockchip64-7.3/dt/rk3588-really.dts` 同步改,
   两边 `cmp` 核对),冷启动复测:
   - `dmesg | grep -E "nobody cared|Disabling IRQ"` 应为空;
   - `/proc/interrupts` 中新中断号计数应只有个位数正常增长;
   - 插拔 Type-C 应出现 partner 注册/注销。
2. **P1:partner 恢复后**,按 2026-11-15 会话的 bisect 清单处理 AltMode
   阶段的历史 detach 循环(SBU 变量、5V 开关、extcon 桥逐个回加)。
3. **P2:全功能验收**(=本问题闭环定义):
   - 自带电源的 Type-C dock 上同时挂 USB3 设备 + DP 显示器;
   - 3840x2160@30(Pin C,2+2 lane 硬件上限,不追 4K60);
   - 正插/反插两个方向;
   - 内屏 LVDS 与 HDMI 全程不受影响;
   - ≥10 次冷启动、≥20 次热插拔无 "nobody cared"、无合成器卡死。

## 7. 附:本次采集环境

```text
设备     root@192.168.68.248(密码由 user 提供)
运行 DTB  /sys/firmware/fdt 副本 = running-fdt.dtb(SHA256 见 commit 时登记)
原始文件  live-dmesg-full.txt / live-interrupts-typec.txt / live-gpio-debug.txt
         android-dmesg-husb311.txt / running-fdt.dtb
```
