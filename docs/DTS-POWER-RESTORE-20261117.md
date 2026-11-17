# 5.10 / 6.1 / 7.3 设备树电源配置修补

- 执行时间：2026-11-17 01:20 — 01:45
- 基准：原厂 Android 12 5.10 DTB（`dtb-5.10-android/rk3588-really.dtb`）
- 参照：7.3 mainline 实机调试结果
- 构建树：目标系统对应的 5.10 / 6.1 / 7.3 内核树

## 一、为什么漏掉了这么多电源配置

还原方法本身有四个盲区，不是个别疏漏。

### 盲区 1：只还原"带 GPIO 的" regulator

原厂 23 个板级 `regulator-fixed` 里有 8 个**没有 `gpio` 属性**：
`pcie20_avdd0v85`、`pcie20_avdd1v8`、`pcie30_avdd0v75`、
`pcie30_avdd1v8`、`vcc12_dvp`、`vcc3v3_ext`、`vcc4v0_sys`、
`vcc1v8_pmu_ddr_s3`。它们看起来是可省的中间节点，实际是供电链枢纽——
删掉后下游 `vin-supply` 引用就断了。

### 盲区 2：整条丢掉了 GPIO0_C0

原厂 `vcc1c8_en`（`gpio0_PC0`，always-on + boot-on），在 **5.10、
6.1android、6.1linux 三个版本里完全不存在**，只有 7.3 调试时补上
（改名 `vcc1v8_sys`）。

这一条同时打掉两个器件，因为**原厂触控和 RTC 都没有 `*-supply` 属性**：

| 器件 | 原厂写法 | 实际依赖 |
|---|---|---|
| `gt9xx@14` | 无 supply | 靠 `vcc1c8_en` 常开拉起 |
| `hym8563@51` | 无 supply | 靠 `vcc1c8_en` 常开拉起 |

这解释了为什么触屏和 RTC 一起坏，而 LT9211（接常开 `vcc_1v8_s3`）一直正常。

### 盲区 3：把共享使能当成专属使能

`GPIO1_D0` 实际是**触控模块 + 面板的共享电源使能**。5.10 / 6.1a /  6.1l
都只把它挂在 eDP 面板的 `enable-gpios` 上。一旦判定"本机没有 eDP 面板"
而 disable eDP，触控跟着断电。

7.3 的修法是抽出独立常开 regulator：

```dts
touch_panel_pwr: regulator-touch-panel-pwr {
    gpio = <&gpio1 RK_PD0 GPIO_ACTIVE_HIGH>;
    regulator-always-on;
    regulator-boot-on;
};
```

### 盲区 4：供电轨接错

6.1 两个版本的触控写 `power-supply = <&vcc_3v3_s3>`，但 GT9110 实际需要：

```dts
AVDD28-supply = <&vcc28_dvp>;   /* 2.8V */
VDDIO-supply  = <&vcc1v8_sys>;  /* 1.8V */
```

`vcc_3v3_s3` 是 PMIC 常开轨，看起来"更安全"，却不是这颗芯片的供电来源。
这类错误比缺失更隐蔽：DTS 能编译、驱动也能 probe，只是电压不对。

## 二、修复内容

### 2.1 5.10 android（`dts-5.10-android/rk3588-really.dts`）

新增 7 个 regulator（20 → 27）：

| regulator | GPIO | 作用 |
|---|---|---|
| `vcc1v8_sys` | gpio0_PC0 | 触控 VDDIO + RTC 主供电 |
| `touch_panel_pwr` | gpio1_PD0 | 触控/面板共享使能，从 eDP 解耦 |
| `pcie20_avdd0v85` | — | 虚拟轨 |
| `pcie20_avdd1v8` | — | 虚拟轨 |
| `pcie30_avdd0v75` | — | 虚拟轨 |
| `pcie30_avdd1v8` | — | 虚拟轨 |
| `vcc1v8_pmu_ddr_s3` | — | 虚拟轨 |

新增 pinctrl 组：`vcc1v8_sys_en`（gpio0_C0）、`touch_panel_en`（gpio1_PD0）。

触控补 `AVDD28-supply` / `VDDIO-supply`。

**触控坐标保持原样**：`max-x = <1920>; max-y = <1080>;` 与原厂
（`0x780` / `0x438`）完全一致，**不加** `touchscreen-swapped-x-y`。
7.3 之所以需要旋转属性，是因为它当时把坐标系写成了 1080x1920，
再靠属性转回来——5.10 这边本来就对。

### 2.2 6.1 android（`dts-6.1-android/rk3588-really-android.dts`）

按 Android 栈单独处理，保留 `rk3588-android.dtsi`。新增 14 个 regulator
（15 → 29），含 `mipi_cam_pwen`、`vcc28_dvp`、`vcc_5g_dpr_ctl`、
`power_on_led` 等此前全缺的项。触控供电从 `vcc_3v3_s3` 改为
`AVDD28` + `VDDIO`。

### 2.3 6.1 linux（`dts-6.1-linux/rk3588-really.dts`）

同 2.2，保留 `rk3588-linux.dtsi`。两版除 include 外内容一致，改动也一致。

### 2.4 7.3 linux（`patch/kernel/archive/rockchip64-7.3/dt/rk3588-really.dts`）

补 9 个此前残留的虚拟轨和 `power_on_led`（43 → 52）。
7.3 已有 `vcc1v8_sys` / `vcc28_dvp` / `touch_panel_pwr`，无需重复。

## 三、验证

### 3.1 编译验证（三套真实内核，全部通过）

不再依赖结构性检查——三个版本都已用各自对应的真实内核树编译出 DTB，
**0 error**。

| 版本 | 内核树 | 命令 | 结果 |
|---|---|---|---|
| 5.10 android | 目标 5.10 vendor 内核树 | `cpp` + `dtc` | 200704 字节，0 error |
| 6.1 linux | `cache/sources/linux-kernel-worktree/6.1__rk35xx__arm64` | `make ARCH=arm64 rockchip/rk3588-really.dtb` | 299021 字节，0 error |
| 6.1 android | 同上 | `make ARCH=arm64 rockchip/rk3588-really-android.dtb` | 299343 字节，0 error |
| 7.3 linux | `cache/sources/linux-kernel-worktree/7.3__rockchip64__arm64` | `make ARCH=arm64 rockchip/rk3588-really.dtb` | 212190 字节，0 error |

5.10 的 SDK 有一个悬空软链接
（`include/dt-bindings/input/linux-event-codes.h` 指向缺失的
`uapi/linux/input-event-codes.h`），编译时用 6.1 的同名头文件放到临时
include 路径绕开，**未修改 SDK 原件**。

### 3.2 DTB 内容验证

编译产物反编译后逐项确认：

```text
版本        regulator  vcc1v8_sys  touch_panel_pwr
5.10           48          有            有
6.1 linux      50          有            有
6.1 android    50          有            有
7.3 linux      52          有            有
```

触控供电 phandle 解析结果（三个版本一致）：

```text
5.10 :  AVDD28-supply -> vcc28_dvp    VDDIO-supply -> vcc1v8_sys
6.1  :  AVDD28-supply -> vcc28_dvp    VDDIO-supply -> vcc1v8_sys
```

触控坐标保持原厂值：

```text
5.10    max-x = <0x780> (1920)   max-y = <0x438> (1080)
6.1     touchscreen-size-x = <0x780>  touchscreen-size-y = <0x438>
```

6.1 的两棵树上 `AVDD28-supply` / `VDDIO-supply` 已验证位于
`touchscreen@14` 节点内部，而非此前误插的 backlight 节点。

### 3.3 GPIO 一致性

已存在的所有 regulator，GPIO 引脚与原厂**逐项一致**（11/11 全对）。
修复前就已正确，问题纯粹是遗漏而非接错。

### 3.4 一处需要记录的错误

第一版补丁在 6.1 的 android 和 linux 两棵树上，把
`AVDD28-supply` / `VDDIO-supply` 插进了 **backlight 节点**，而不是触控节点。
原因是脚本用 `power-supply = <&vcc_3v3_s3>;` 做首匹配，而这个字符串并非
触控节点独有。结果触控仍然挂着 `vcc_3v3_s3`，两个新轨反而落到了背光上。

已在提交 `d032f38` 修正：删掉错位的一对，装回 `touchscreen@14`。
5.10 当时没出错，因为它的锚点用了 `compatible` + `reg` 两行，定位准确。

**这条要记住**：改 DTS 后不能只看"属性存在"，要确认它属于正确的节点。
验证方法是把目标节点整块切出来再 grep，不要全文 grep。

## 四、未验证项

四棵树都已编译通过，**但全部未上机**。编译只能证明语法和引用正确，
不能证明电源真的拉起来了。

- **5.10 / 6.1 上机验证**。需确认 `i2cdetect -y 4` 能看到 `0x14`(GT9110)
  和 `0x51`(HYM8563)，且触控可用、RTC 走时正常。
- **5.10 触控坐标需实测**。虽然 `max-x/max-y` 与原厂一致（1920/1080），
  但 5.10 的 gt9xx 驱动行为与 mainline 不同，方向需上机确认。
- **7.3 已编译但未启动**。新增的虚拟轨不驱动引脚，风险低，
  但仍应在下次启动时确认 `regulator_summary` 无异常。

上机排查第一步统一是：

```bash
cat /sys/kernel/debug/regulator/regulator_summary | grep -E "vcc1v8_sys|vcc28_dvp|touch_panel"
```

三个轨都应显示 `enabled` 且有电压。若 `vcc1v8_sys` 未使能，先查
GPIO0_C0 的 pinctrl 是否生效——7.3 早期就是漏了 pinctrl 导致引脚从未被驱动。

## 五、后续修正：触控语义和遗漏 pinmux

后续对 vendor 6.1 `drivers/input/touchscreen/goodix.c` 做了源码级复核：

- 该驱动使用通用 `touchscreen_parse_properties()`，不是原厂私有 GT9xx
  的方向表；
- reset 先以逻辑 0 拉低，再以逻辑 1 释放，因此 reset-gpios 应为
  ACTIVE_HIGH；
- IRQ 在复位阶段用逻辑 1 选择 `0x14` 地址，因此 irq-gpios 也应为
  ACTIVE_HIGH；
- 固件配置中的轴范围会被读取，DTS 不再硬写 `touchscreen-size-*`；
- Linux 仍需要 inverted-x + swapped-x-y 对应 270 度面板，Android 保留
  原厂 framework 旋转映射。

因此前文“6.1 触控坐标保持 1920x1080、不加上下文”只适用于原厂私有
GT9xx 驱动，不适用于当前 6.1 DTS 实际绑定的 mainline Goodix 驱动。

后续审计还补回天禧 AI 电源树中漏掉的 pinctrl：`mipi_cam_pwen`、
`vcc_5g_dpr_ctl`、`power_on_led`。电源节点只有 `gpio`、没有
`pinctrl-0` 时，引脚仍可能停留在默认复用，不能只看 regulator 是否存在。

## 六、7.3 同步与核心板 MIPI 摄像头确认

7.3 硬件仓库此前只有 43 个 regulator，落后构建树 9 个。现已补回
`vcc4v0_sys`、三个中间轨、四个 PCIe AVDD 轨和 `power_on_led`，并把
`vcc28_dvp` 改为由 `vcc4v0_sys` 供电。

同时回移 HUSB311 GPIO0_B7、Wi-Fi host-wake、24C04 EEPROM 和 STK8BA50。
7.3 的 Type-C VBUS 改由 HUSB311 的 TCPM 控制，USB2 PHY 不再占用同一
regulator。Sink 与 Source 都只声明固定 5V/3A PDO，运行功耗按 15W 计算。

实机确认 MIPI 摄像头接口位于核心板而不是载板，默认产品摄像头是 USB UVC。因此：

- 原厂 5.10 Android 恢复 DTS 保留 IMX577；
- 当前 6.1 Linux/Android 恢复 IMX577、CSI0 图和 `mipi_cam_pwen`；
- 7.3 保留 `mipi_cam_pwen`；主线尚无 IMX577 驱动，暂不启用传感器节点。

24C04 的总线、地址、compatible 和页大小均与原厂 DTB 一致，未发现配置
错误。当前机器的 `0x56` 不 ACK，因此暂不将 EEPROM 视为可用外设，也不
安排写入。
