# Really LT9211 48/60 Hz 模式与稳定化记录

[English version](README.en.md)

- 作者：Codex（AI 助手）
- 生成时间：2026-11-23
- 背景：部分 Really 装配的 LT9211 在 1920×1080@60 下间歇失锁并闪屏。
- 适用分支：Armbian `edge`，Linux 7.3。
- 验证环境：Really TVI2340R、DSI1、LT9211、双通道 VESA-24 LVDS。
- 默认策略：60 Hz 为 native/preferred，48 Hz 保留为兼容回退模式。

## 模式参数

两个模式保持相同的 porch：

```text
htotal = 2200
vtotal = 1125
```

| 模式 | pixel clock | 计算结果 |
| --- | ---: | ---: |
| 1920×1080@48 | 118.8 MHz | 118800000 / (2200 × 1125) = 48.0 |
| 1920×1080@60 | 148.5 MHz | 148500000 / (2200 × 1125) = 60.0 |

## DTS 与驱动

板级 DTS 使用标准 `display-timings`：

```text
native-mode = <&panel_timing_60>;
timing48 -> 118800000
timing60 -> 148500000
```

上游 `panel-lvds` 原先只解析单个 `panel-timing`。构建增加通用补丁：

```text
general-drm-panel-lvds-support-multiple-timings.patch
```

该补丁把 `display-timings` 中每个 timing 注册成 DRM mode，并将
`native-mode` 标成 preferred。未提供 `display-timings` 的旧单板继续使用
`panel-timing`，行为不变。

## 自动回落边界

系统默认选择已验证稳定的 60 Hz；无法稳定运行 60 Hz 的批次可手动选择
48 Hz。默认值不能固定为 48 Hz，因为部分完全正常的 LVDS 屏不接受该
刷新率，会在冷启动阶段直接黑屏。

尚未实现的是：系统正在运行 60 Hz，LT9211 中途 PCR 失锁后自动重配 CRTC
并切到 48 Hz。LT9211 的 `atomic_enable` 当前没有向 DRM 返回运行时锁失
事件；只改 bridge 寄存器也不能同步改变 VOP/DSI pixel clock。可靠实现需要
connector 恢复、mode change 和失败次数门限。

## 60 Hz 稳定化候选

按单变量顺序验证：

1. 对比稳定机与闪屏机的 `0x8235`、`0x8237`、`0x823b`、`0x8737`、
   `0x855c`、`0xd087` 和 `0x8720`。
2. 验证 vendor 的 `0x8737 = 0x0e`，当前主线为 `0x14`。
3. 验证 DSI EoT、非 burst 和连续/非连续 clock lane 组合。
4. 调整 LT9211 RX PHY 校准值，并对比 DSI PHY drive/termination。
5. 测量 LT9211 `vccio`、1.8 V 和 PHY 模拟电源的上电顺序。
6. 60 Hz 必须保持为默认 preferred；48 Hz 仅用于异常批次回退。

不应把 PCR poll 超时简单加长当作修复；当前问题是运行中反复掉锁。

## 构建验证

- 多 timing 补丁 `git apply --check` 通过。
- `panel-lvds.o` 使用 Armbian 7.3 内核配置编译通过。
- Really DTS 预处理和 DTB 编译通过。
- 反编译确认 `timing60` 为 native，`timing48` 保留。
- 实机验证：默认 48 Hz 会在 60 Hz LVDS 屏上黑屏；改回 60 Hz preferred
  后屏幕恢复。
- DTB 编译只有既有的 PCIe 重复 unit-address 和 VOP graph unit-address
  warning。
