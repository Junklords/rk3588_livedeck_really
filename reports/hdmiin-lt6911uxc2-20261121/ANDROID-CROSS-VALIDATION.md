# HDMI-in 双系统交叉验证附录 — 2026-11-22

用户要求:刷回 Android 深度分析,不轻信"固件未烧"结论。已执行。

## 方法
- eMMC 完整恢复原厂 Android 12(root 版,来自 box-backup-20261101,SHA 全过)
  - 恢复过程修了两层坑:gpt_head 写入被脚本算术错误吞掉(已补写);
    boot0/1 裸 dd 失败 → upgrade-loader 写入 DC_Y588 loader 后链通
  - 首次启动卡 init(data/metadata 不匹配)→ recovery factory reset 后正常
- ADB root (192.168.68.135:5555) + 静态 i2cseq 直读芯片

## 结果:原厂系统读数与主线逐位一致

| 指标 | Android(OEM 驱动) | 主线(我们的驱动) |
|---|---|---|
| chip ID @0xa000/0xa001 | 0x0516(驱动日志"chipid ok, id:0x516") | 0x0516 |
| **fw 版本 @0x86a7-0xaa** | **0x00000000(驱动日志"fw_ver:0x0")** | 0x00000000 |
| hpd-ctl GPIO | 驱动拉 LOW(=plugin 检测无 5V) | 高/低均无效果 |
| 时序 hact/vact | 0/0(默认 720x480 兜底) | 0/0 |
| 像素时钟 | 噪声计数器(~127M 漂移) | 同 |
| 运行时 DT 节点 | reset=GPIO3_A7, hpd-ctl=GPIO2_C3,无 power/plugin-det | 同 |

## 结论
1. "LT6911UXC2 固件未运行"结论由 OEM 系统独立证实(非主线侧推断)
2. 引脚/寄存器映射无遗漏(运行时 DT + 金表 GPIO 全对齐)
3. OEM 驱动无固件加载逻辑(反汇编)→ 出厂未烧固件,软件侧无从补救
4. 固件获取途径:龙迅 NDA(hex + LT Programmer);或改用 USB UVC 采集卡

## 附:Android 恢复要点(本次实战沉淀)
- GPT 头必须写(wl 0);boot0/1 用 upgrade-loader(裸 dd 会被写保护挡)
- 恢复 metadata 而不恢复 data → 首启卡死,需 recovery factory reset
- 三启动格局落地:TF 在位=bootmenu(USB>TF>eMMC);拔 TF=eMMC Android;
  USB 盘系统经 TF 承载内核启动(纯 USB 引导仍缺 U-Boot 枚举,另案)
