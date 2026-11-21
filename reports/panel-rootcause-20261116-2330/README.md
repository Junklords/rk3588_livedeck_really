# 内屏（LVDS）黑屏根因：Goodix 时序连坐 → LT9211 atomic_enable 静默失败

- 生成：2026-11-17（ZCode 会话）
- 对象：DeepSeek 预构建镜像 7.3.4（SHA256 `cb4645f9...`）在真机上的启动行为
- 方法：全部结论来自已有静态/运行时证据，**零刷机、零新增真机操作**
- 关联：`P0-I2C4-touch-rootcause-20261116.md`（I2C4 总线卡死）、`typec-dp-rootcause-20261116-0645/live-dmesg-full.txt`（完整 dmesg）

## 0. 一句话结论

**内屏黑屏不是 LT9211 配置错误，而是启动竞态：Goodix probe 在 t≈1.99s 拖死
I2C4（GPIO1_D3 复位线连坐，见 P0 报告），而 LT9211 的 chip ID 读取发生在
t≈2.34s 的 DRM atomic_enable 里——每次冷启动都注定读到 `0x00×3`，bridge
enable 静默失败且永不重试。probe 早已成功注册 connector，所以 DRM 显示
`LVDS-1 connected`、背光（PWM12，独立于 I2C）正常亮，但永远没有画面。**

## 1. 证据链（时间轴，全部可复现）

运行时 dmesg（`live-dmesg-full.txt`，2026-11-15 深夜冷启动）：

```text
[    1.310320] rtc-hym8563 4-0051: could not init device, -6     ← 总线尚健康，RTC 自身 NAK
[    1.994639] Goodix-TS 4-0014: ID , version: 0000              ← 读到全零（总线已被拖）
[    1.995980] Goodix-TS 4-0014: Direct firmware load for goodix__cfg.bin failed (-2)
[    2.003164] input: Goodix Capacitive TouchScreen ... input1     ← 垃圾配置(1919,1079)下照样 bind
[    2.344554] lt9211 4-002d: Unknown Chip ID: 0x00 0x00 0x00      ← atomic_enable 里读 ID，失败
[    2.455081] rockchip-drm ... fb0: rockchipdrmfb                   ← 显示子系统继续（HDMI 路径）
...
[    7.328598] rtc-hym8563 4-0051: registered as rtc0               ← 注册不需要总线
[    7.328891] rtc-hym8563 4-0051: hctosys: unable to read ...      ← 但读时间失败=总线仍死
```

P0 报告已实证：GPIO1_D3 拉高后 i2cdetect 立即恢复 → 1.99–2.34s 之间总线
死亡由 Goodix 错误路径造成，因果关系闭环。

## 2. 为什么是"connected + 背光 + 无画面"三件套

主线 `lontium-lt9211.c`（7.3.4，工作树逐行核对）：

```text
probe():  devm_gpiod_get("reset", GPIOD_OUT_LOW) → 低 10ms
          parse_dt → regmap → drm_bridge_add → host_attach(DSI1)
          ← 不读 chip ID；只要 DSI host 就位 probe 就成功 → connector 存在
atomic_enable(): vccio 上电 → gpiod_set_value(reset,1) + 20ms
          → lt9211_read_chipid() ← 唯一读 ID 的地方（约 522 行）
          → 失败仅 return，DRM 不重试 → 画面链路死在这里
```

- connector `LVDS-1` 来自 probe 注册的 bridge → status=connected（06:18 快照实测）
- 背光 = PWM12 + lvds-pwr-en(B6)/backlight-en(B4) GPIO，与 I2C4 无关 → 亮
- 像素链 = atomic_enable 配置 LT9211 → 从未成功 → 黑

reset 语义核对：DT flag `ACTIVE_HIGH`（运行 FDT `reset-gpios = <... 0x00>`，
v85/6.1 验证树一致）下，probe 置物理低=复位、enable 置物理高=运行——
与驱动注释（"nRST line low"/"Deassert reset"）吻合，**v85 极性没有错**。

## 3. 工厂基准（静态分析 dts-5.10-android/rk3588-really-full.dts，未刷机）

| 项 | 工厂(安卓,内屏正常) | 主线 7.3 运行态 | 判定 |
|---|---|---|---|
| I2C4 频率 | 无属性 → vendor 回退 100kHz | 400kHz | 不一致(交接文档§6.1 已知) |
| I2C4 pinmux | `i2c4m1-xfer`(0x138) | `i2c4m1-xfer`(0xd4) | 一致 ✓ |
| LT9211 复位 | GPIO3_A6(`lt9211,reset-gpio`, 延时20ms) | GPIO3_A6 ACTIVE_HIGH | 一致 ✓ |
| LT9211 背光脚 | GPIO3_B4(`lt9211,backlight-gpio`) | backlight-en=B4(6.1)/v85 | 一致 ✓ |
| LVDS 电源 | (vendor 驱动内管理) | hog GPIO3_B6 always-on | 一致 ✓ |
| LT9211 vccio | (vendor 内管理) | dcdc-reg10(常供轨) | 供电无嫌疑 ✓ |
| GT9110 复位 | GPIO1_D3 ACTIVE_LOW | GPIO1_D3 ACTIVE_LOW | 一致；差异在驱动错误路径 |
| 输出拓扑 | output-mode=3 双链路 | dual-lvds odd/even | 一致 ✓ |

结论：**引脚/拓扑/供电全部对齐工厂基准，唯一致命差异是主线 Goodix 驱动
probe 失败路径不复位线**（vendor goodix 驱动不会这样）+ chip ID 读取时机
（probe 不读、enable 才读）放大了时序连坐。

## 4. 遗留反例与解释（诚实记录）

- 22585s/23091s/23112s 的 `Failed to read Chip ID: -6`：这些是**人工诊断
  触发的 display-subsystem 重绑**（unbind/bind 循环）再次进入
  atomic_enable 的读数，当时总线已 heal。健康总线上仍 -6 的候选解释：
  (a) 芯片经历了总线被拖期间的上电状态，需要完整复位脉冲/断电恢复，
  而人工诊断没有做过 A6 手动脉冲；(b) heal 时序与 vccio 使能顺序问题。
  → 复现镜像刷入后第一个实验（§5-E1）会正面回答。
- 交接文档声称 DeepSeek 自机"LVDS 完整画面闭环"：同一镜像同一时序下
  该声明与 2.34s 证据矛盾，除非他们做过等价的 heal+重绑操作。不采信为
  基线，以本报告实测链为准。

## 5. 修复路线（按证据强度排序，全部可在新镜像上 SSH 迭代、无需再刷机）

- **E1（决定性实验）**：`/boot/dtb` 里把 `touchscreen@14` status 改
  `disabled`（fdtput + 重启，5 分钟一轮）→ 预期 lt9211 atomic_enable 读到
  正确 chip ID、内屏出画面。成立则根因定案。
- **F1（正式修复 A）**：内核补丁——goodix 主线驱动错误路径释放 reset
  （`goodix_reset+exit` 类），可上游化；I2C4 恢复 100kHz 与工厂一致。
- **F2（正式修复 B）**：lontium-lt9211 probe 期做一次 chip ID 预读 +
  重试，或 atomic_enable 失败后标记 connector 为 disconnected 并可重试，
  增强时序鲁棒性（可作为上游 RFC）。
- **触摸本体**：保持 P0 结论——先 cfg 提取（android 基线已抓）再谈
  gt9110 正常 bind；修好前 goodix 保持 disabled 是内屏可用的前提。

## 6. 证据文件索引

```text
live-dmesg-full.txt        typec 报告目录，含 2.34s/22585s 全部 lt9211 行
running-fdt.dtb            06:18 运行态 FDT（i2c4m1/A6/B4/B6/vccio 全部可核）
P0-I2C4-touch-rootcause    GPIO1_D3 连坐实证 + heal 复测
dts-5.10-android/*-full.dts 工厂基准（本报告§3 表格来源）
lontium-lt9211.c@v7.3.4    驱动时序语义（§2 引用行号可查）
```
