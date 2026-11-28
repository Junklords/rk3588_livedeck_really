# HDMI-in 主线突破性进展 — 2026-11-28

## 信号锁定成功！

```
[3001.052826] fdee0000: hdmirx_wait_lock_and_get_timing signal lock ok, i:321!
[3001.965509] fdee0000: hdmirx_format_change: New format: 1920x1080p60.00 (2200x1125)
```

## 完整工作链
1. Collabora TF-A + 自定义 SMC handler (RK_SIP_HDMIRIRX_CFG = 0x82000027)
2. 内核模块 clk-of.ko: 通过 OF 节点启用 6 个 HDMI RX 时钟
3. vendor 驱动(5V bypass): 绕过 GPIO 5V 检测强制启动轮询
4. 信号源: Ubuntu 主机 HDMI 输出
5. PHY: 通过 BL31 SMC 写入 348B 调谐表
6. 锁定: 在 i=321 次轮询后成功

## 关键发现
- **时钟是根本前提**: DT 时钟名(consumer alias)≠全局时钟名, 必须用 of_clk_get_by_name
- **5V bypass**: tx_5v_power_present() 返回 true 绕过物理 5V 检测
- **自定义 BL31**: 只需 PHY 表写入(0xfdf10000+GRF门控), 无需FIQ路由即可锁定
- **DMA 中断**: 仍不工作(hdmi/dma IRQ=0), 需要BL31 FIQ路由才能收帧

## 待完成
- [ ] BL31 FIQ 路由（转发 hdmi/dma 中断到内核 GIC）→ 帧交付
- [ ] 集成到正式驱动（时钟+SMC在probe中完成）
- [ ] EDID 自动写入
