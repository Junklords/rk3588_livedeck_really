# HDMI-in 终案：LT6911UXC2 出厂无固件 — 2026-11-22

## 一句话结论
**这台 TVI2340R 的 LT6911UXC2 SPI flash 是空的（出厂未烧固件）。
芯片 ROM 活着（ID 可读），但转换固件从未执行 —— DDC/HPD/TMDS 处理/MIPI 输出
全部不存在。固件 hex 由龙迅 NDA 发放，任何 OS 侧努力都无法绕过。**

## 证据链（全部真机实证）
1. **真实寄存器映射**（反汇编 Android 5.10.110 boot_a.img 内 OEM 驱动获得）：
   - ID @0xa000/0xa001 = **0x0516**（一直稳定可读 → ROM/数字基底健康）
   - 0x80ee 门铃：访问组前写 1、后写 0（之前所有"噪声"= 读不存在的寄存器）
   - 时序 @0xd2xx（a034=0x21 时钟测量 → d283=0x10 捕获触发）
   - **OEM 驱动无任何初始化/固件下载写序列**（芯片应自举）
2. **fw 版本寄存器 0x86a7-0xaa 稳定读 0x00000000** —— 固件未运行
3. **回环实验**（自家 HDMI OUT → HDMI IN）：
   - TX "connected" = 我们自己的 hpd-ctl GPIO 绕回 TX（非芯片断言）
   - TX 经 DDC 读 EDID → LT6911 DDC 从机 NAK（`dwhdmiqp: i2c read error`×5）
   - DDC 风暴会把芯片控制 I2C 打瘫（全 NAK），断电循环可恢复
4. **外部佐证**：
   - github.com/JakubVanek/lt6911uxc-hacks：RPi 作者同型号症状一模一样
     （"能读 ID，HDMI 输入无反应"），结论 = 芯片默认无固件，需向龙迅索取
   - MediaTek Genio 文档：LT6911UXE 系列 hex "Please get in touch with Lontium"
     烧录需 LT Programmer + USB-I2C（SDA/SCL/VCC 5V）
   - Android 备份全文搜索：无 hex/固件文件
5. **Android 也一样**：OEM 内核驱动无固件加载逻辑（反汇编证实）；
   本机 Android 已被早期实验覆盖（eMMC 现为社区 loader + fix-test 镜像）

## 排除项（沿途证伪的假说，防止翻旧账）
- 内部 HDMI RX (fdee0000)：与 HDMI IN 座无关（Android 原厂 DTB disabled）
- "噪声模式"：读不存在寄存器的幻觉
- cif_clkout 27M 缺失：Android 金表 B4=hp-con（GPIO），非时钟脚；27M 为板载
- I2C_EN 极性 / POR 时序 / DDC 干扰 / 读法协议 / 0x2c 幽灵：全部排除

## 主线侧成果（固件一旦到位即刻可用）
- lt6911uxc2 驱动：真实寄存器映射版（本仓库 drivers/media/i2c/）
- CSI 管线：rockchip-cif + dw-mipi-csi2rx 模块编译打通，media graph 完整，
  STREAMON 成功（field:none / LINK_FREQ / 格式污染三坑已修）
- DTS：mipi_cam_pwen 稳压器 + power-supply probe 顺序

## 出路
1. 正式渠道联系龙迅申请 LT6911UXC2 hex + LT Programmer（NDA）
2. 拿到 hex 后：LT Programmer + USB-I2C 烧录（或研究协议后用 i2cseq 直烧）
3. 务实替代：USB UVC HDMI 采集卡即插即用（主线零开发）
4. 立创开源的 LT6911C 社区方案可参考（注意：型号不同、固件不通用）

## 附：本机其他状态
- eMMC：社区 loader + fix-test 镜像（Android 全套分区在 box-backup-20261101/）
- 恢复 Android 需全量回写 + loader_gap_pre_community.bin(Nov15) @LBA34
