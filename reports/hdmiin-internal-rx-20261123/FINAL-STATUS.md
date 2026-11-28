# HDMI-in 最终状态 — 2026-11-28 会话结束

## 已验证突破（从零到锁定）

### 稳定工作链
```
Collabora BL31 (自定义 SMC 0x82000027 handler)
    ↓ (内核模块 clk-of.ko: of_clk_get_by_name 启用 6 时钟 + SMC PHY表)
    ↓ (内核模块 gic-fix.ko: GICD ISENABLER 使能 hdmi/dma/cec 中断)
    ↓ (vendor 驱动 5V bypass + auto-hotplug)
    ↓
信号锁定 ✓ → 格式检测 ✓ (1920x1080p60) → 中断触发 ✓
```

### 关键数字
- signal lock ok, i=321 (321次轮询后锁定)
- New format: 1920x1080p60.00 (2200x1125)
- GIC 中断计数从 0 → 1 (hdmi + dma)
- dma_st10 从 0x10 → 0x10010 → 0x10014

## 剩余工程

### 问题: vendor 驱动轮询循环最终导致系统挂死
根因: CR (Configuration Register) 接口超时 → 每次超时阻塞 CPU 10ms → 
     连续失败 → 内核 wedge

### 三个解决方案（按工作量排序）

**方案 A: 驱动超时限制（最简单，1小时）**
- 在 hdmirx_wait_lock_and_get_timing 中加计数器
- N 次失败后暂停轮询（改为定时器低频探测）
- 发现信号后恢复轮询

**方案 B: BL31 完整 PHY 初始化（中等，1-2天）**
- 从 vendor BL31 逆向提取完整初始化序列
- 包括: SRAM 加载 + CDR 配置 + PDDQ 控制
- BL31 SMC handler 做完整初始化，驱动只负责上层

**方案 C: A/B 双 FIT 引导（最完整，2-3天）**
- vendor BL31 原样运行（PHY 完整支持）
- 主线内核通过 vendor 链引导
- 设计已入库

## 所有归档文件
- `reports/bl31-smc-analysis-20261123/` — BL31 逆向 + 参数表
- `reports/hdmiin-internal-rx-20261123/` — 全部状态报告
- `scripts/hdmirx-start.sh` — 一键启动
- `scripts/hdmirx-modules/` — 编译好的模块
- `tools/hdmirx-phy-init.c` — PHY 表写入模块
- `tools/clk-of.c` — OF 时钟启用
- `tools/gic-fix.c` — GIC 中断修复
