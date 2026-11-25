# BL31 SMC 能力静态分析 — 2026-11-23

## 方法（纯离线，零设备测试）
1. Android BL31 v2.3 位于 vendor uboot_a 的 FIT 内（atf-1, 0x3084c 字节, load=0x40000）,
   **不在** loader gap、**不在** trust 分区（trust_a/b 全零——该布局 trust 分区未使用）
2. 从 uboot_a.img 按稠密区 carve 出 198732 字节（含版本串
   "v2.3():v2.3-557-g9609b9c19:derrick.huang" ✓）
3. 社区 v1.48 = fit-img1.bin（204860 字节，含 "v1.48" 串 ✓）
4. SMC ID 识别：movz #0x24/26/27 与 movk #0x8200,lsl16 的**相邻配对**（防小常数误报）

## 结论
| SMC 服务 | ID | Android v2.3 | 社区 v1.48 |
|---|---|---|---|
| RK_SIP_FIQ_CTRL | 0x82000024 | 分发方式不透明(clang 字面池/switch) | ✅ 配对 2 处 |
| SIP_WDT_CFG | 0x82000026 | 不透明 | ❌ 无配对 |
| SIP_HDMIRX_CFG | 0x82000027 | 不透明 | ❌ 无配对 |

**实证：社区 v1.48 BL31 缺 WDT/HDMIRX 两个 SMC 服务。**
vendor 驱动调它们时在主线链上无声失败。

## 含义
- FIQ_CTRL 在 v1.48 有 → 中断路由没问题（与 IRQ handler 实测触发一致）
- 缺的是 AUTO_TOUCH / REG_PRE_FETCH / INFO_NOTIFY / WDT 看门狗 —— 与锁定失败的
  关联待验证，但**换装 vendor BL31 可一次性消除全部 BL31 差异**

## 换装方案（下次执行）
1. 精确 carve：从 uboot_a.img FIT 数据区按 data-offset 重新提取 atf-1（当前 carve
   起点可能偏移，头部有零填充待核对）
2. 用 /tmp/ub-really（u-boot-edge-really 树）重编 FIT：atf-1 载荷换 vendor BL31
   （或直接改 FIT 的 data/hash 节点）
3. 写入 eMMC gap + uboot_a 位置（或 TF），主线内核启动后跑 rk_hdmirx 测锁定
4. 回滚物料：fit-img1.bin（v1.48 原版）+ loader_gap 两个版本

## 分析脚本要点（复现）
- movk 掩码: (w & 0xFFE00000) == 0x72A00000 且 imm16==0x8200
- movz 掩码: (w & 0xFFE00000) == 0x52800000
- SMC 指令: b'\x03\x00\x00\xd4'
