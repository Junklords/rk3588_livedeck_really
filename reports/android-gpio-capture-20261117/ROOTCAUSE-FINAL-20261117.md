# I2C4 三件套根因终案 + 修复实证（2026-11-17）

- 执行：ZCode 会话；关键实验全部真机、带阴性对照、可复现
- 战果：**RTC/触摸/内屏桥三件套在主线全部复活**（见 §4 实测日志）

## 一、根因（三段独立、全部实证）

### 1. GPIO1_D0 = I2C4 设备组总使能（触摸 + RTC 支路）★总钥匙
- 位摆 A/B：D0=HIGH → `0x14/0x2d/0x51` 全应答；D0=LOW → 仅 `0x2d`
- Android 金表：`gpio-56 (GPIO1_D0) "enable" out hi`（vendor 驱动持有）
- 主线：无人驱动（悬空）→ goodix/RTC 物理性不应答 → 数周来一切极性/
  频率/pinctree 调试全部无效的统一解释
- DeepSeek 交接文档 §6 全部"已试过无效"的实验均被此覆盖（总线根本没通）

### 2. LT9211 复位脉冲致命
- 驱动 probe→enable 会打"低 10ms→高"脉冲；实测脉冲后芯片从总线消失
- A6 恒高 hog（Android 生态位：上电即释放、永不重打）后芯片稳定应答
- 400kHz 下读 ID 得 0x00×3（总线半死），100kHz（工厂基准）正常

### 3. 触摸配置固件缺失
- 主线 goodix 驱动要求 `goodix_9110_cfg.bin`（末位 Config_Fresh=1）
- 已从活的安卓芯片 dump 186 字节，校验和原生匹配，fresh 位补 1

## 二、实验方法学（防再翻车）
- 所有"应答"结论必须带阴性对照（无设备地址 0x37/0x68 必须 NAK）——
  本日早些时候一次无对照位摆曾给出假阳性
- 位摆用 direction 切换实现真开漏；GPIO 编号注意主线基址 512+（安卓 0+）

## 三、修复清单（已烘焙进构建树）
`userpatches/kernel/archive/rockchip64-7.3/dt/rk3588-really.dts`:
1. `clock-frequency = <100000>`（工厂基准）
2. lt9211 节点删除 reset-gpios + `&gpio3 { lt9211-rst-hold-hog }` 恒高
3. `&gpio1 { i2c4-dev-en-hog }`（RK_PD0）恒高 ← 总钥匙
4. `really-overlay/lib/firmware/goodix_9110_cfg.bin` + extension 安装钩子

## 四、验收实测（fix-test 镜像 + hog 后真机日志）
```
[3.213502] rtc-hym8563 4-0051: registered as rtc0
[3.215345] rtc-hym8563: setting system clock to 2026-11-17T00:41:09 UTC
[7.657376] Goodix-TS 4-0014: ID 9110, version: 1050
[125.178856] input: Goodix Capacitive TouchScreen ... input4 (event4)
LVDS-1: connected + enabled, lt9211 零报错
```
待人工目视：内屏画面、触摸滑动（用户确认中）。

## 五、安卓复原路径（如需回 Android）
- 全套分区在 `box-backup-20261101/`；关键：**loader gap 必须写
  `loader_gap_pre_community.bin`（Nov15 版）@LBA34**——Nov1 版会触发
  vendor U-Boot `optee check api revision fail` 死循环
- 安卓下 adb root：以太网自动 DHCP（本日实测 192.168.68.78:5555 可用）

## 六、遗留
- 主线 u-boot 的 TF 卡识别仍 -110（SCMI 电源域议题，本日未动）
- Type-C 中断脚（GPIO0_D7→B7）修复未入本镜像（typec-dp 报告已有方案）
- eMMC 上现为 fix-test 镜像；正式重建（含本修复）随时可出
