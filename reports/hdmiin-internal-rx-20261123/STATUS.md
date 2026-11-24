# HDMI-in 主线移植状态 — 2026-11-23/24

## 已打通（主线 rk_hdmirx 移植版）
1. 驱动绑定 fdee0000（compatible 对齐 live DT）
2. FIQ/SMC 路由（rkbin BL31 下工作，中断处理器实测触发）
3. 5V 检测 + 边沿 IRQ（热插拔事件触发插件链 ✓）
4. HPD 断言/撤销（hpd_config/hpd_ctrl 日志实证）
5. EDID 写入芯片（VIDIOC_S_EDID 2 blocks OK）
6. DDC 伺服（回环 TX 读到 256 字节有效 EDID ✓✓）
7. EDID 生成器（1080p60+LPCM 音频，工具 gen-edid.py / set-edid.c / i2cseq）

## 未通：最终信号锁定（mu_st=0）
- TX 侧：modetest 强制 1080p 输出运行中（CRTC 84@conn 88）
- RX 侧：插件链循环、ratio 配置跑、但 MAINUNIT_STATUS.TMDSVALID_STABLE_ST 恒 0
- 已排除：电源域差异(PD_VO1=26 两树同号,待改为 PD_VO1)、PHY 握手失败（SRAM/PDDQ/ack 全过）
- 下一步：Android/主线锁态寄存器对比（PHY→MU 时钟路由），或 BL31 v2.3 与 v1.48 的
  hdmirx SMC 能力差异排查

## 引导架构（重要认知修正）
- BootROM 永远先跑 eMMC 用户区 gap 的 loader（与 boot0/1 是否有效无关）
- 之前"TF 优先"= eMMC gap 里社区 U-Boot 的 bootflow 扫描顺序
- 现状：eMMC gap = 社区 bootmenu loader（TF 主线/USB 盘可用）；
  Android 需要把 gap 换回 vendor loader（backup 的 loader_gap_lba34_8191.bin）
- 三系统切换 = 换 gap 内容（各系统内可 dd 自切，无需 maskrom）

## Android 侧最终确认（root adb）
- 193s 内核日志：signal lock ok / 音频 44.1k / VOP 1080p60 转发 OUT
- HDMI-in 硬件与 OEM 全链路无恙 ✓
