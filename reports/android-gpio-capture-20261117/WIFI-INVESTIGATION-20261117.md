# WiFi AP6275P 调查记录（2026-11-17，未闭环）

## 已排除（每次一启动验证）
- 固件三件套齐换（vendor fw_bcm43752a2 + nvram_ap6275p + vendor CLM）：仍 "FW failed to initialize"
- pcie_aspm=off：无效
- RTC CLKE 强制 32k 输出（i2c 直写 reg02）：无效
- fw/nvram 矩阵（a2/b4 × p/r3/hh3/V1.1）：全部失败

## 两个系统级死锁源（重要教训）
1. fw_bcm4375b4 变体会使 NM 每次开机死锁（深层内核锁）——已回滚，勿再装
2. wifi 节点挂 clk32k_out0（GPIO0_B2 func2）同样引发死锁——B2 可能有未明争用，
   下次先做全引脚占用图再动

## 下一步方向
- GPIO0_B2 运行态占用调查（两棵树 pinmux 数据库逐位比对）
- brcmfmac debugfs 深调（fw console 输出）
- 对照安卓 bcmdhd 的初始化序列差异

## 现状
良性崩溃态（原厂固件），系统其余功能全部健康。
