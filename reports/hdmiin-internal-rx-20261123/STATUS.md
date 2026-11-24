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

## 2026-11-23 补充: Junklords 仓库对照 + PD_VO1 修正

### Junklords/rk3588_livedeck_really 对照结论
- 其 Android 侧分析与本报告一致(面板口=内部RX@fdee0000, LT6911=内部隐藏P2路,
  原厂 DT status=okay, Android 全链路可用含 1080p NV12 抓帧)
- **其主线侧 P0 从未闭环**: 11-16 的排查在错误的机器上(Skysi X5, 其 DT 该节点
  本来 disabled), 报告结尾"需在真 Really 复验"后再未涉及 HDMI-in
- 有用洞见: mainline snps_hdmirx 不调 SMC(不需要 ATF); 引脚与原厂逐根一致

### PD_VO1 修正(已入 DTB, 未解锁锁定)
- live DT power-domains = <phandle 26> = PD_VO1(两树编号同: 26)
- 原移植误设 PD_VI(27) → 已改 PD_VO1 并重刷 DTB
- 结果: 锁定仍未通(mu_st=0), PD 非唯一根因

### 下一步(优先级序)
1. **双系统锁态寄存器对比**: Android(可写入 hdmirx 全寄存器 dump 工具) vs 主线,
   差异寄存器 = 缺失的初始化步骤(重点: PHY clock 输出到 MU 的路由位)
2. BL31 能力: Android trust v2.3 vs 我们的 rkbin v1.48 — sip_hdmirx_config
  (AUTO_TOUCH/REG_PRE_FETCH)在 v1.48 是否实现, 缺失则换 Android 的 trust.img 到主线链
3. 主线 snps_hdmirx(非 vendor 移植版)在标准资源下按 Junklords 思路再试一次
