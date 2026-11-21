# HDMI-in (LT6911UXC2) 根因链 + 当前状态 — 2026-11-21

执行：ZCode 会话（2026-11-20/21）。全部结论带实证。

## 一、路径裁决（已实证）
- **内部 HDMI RX (fdee0000) 与 HDMI IN 座无关**。Android 原厂各版 DTB 里
  `hdmi_receiver@fdee0000` 全部 `disabled`；主线 snps_hdmirx 的"5V 检测"高是
  GPIO4_C6 悬空假阳性（ACTIVE_LOW 逻辑恒真）。
- **真实路径：HDMI IN → LT6911UXC2 (i2c6@0x2b) → MIPI CSI2 (fdd30000) → rkcif (fdce0000)**
- 真正的 Android 内核 DTB 藏在 `boot_a.img` 内（0x4a4c800 处），不是之前提取的 v7x
  （那些是 U-Boot 的）。关键内容：
  - `lt6911uxc2@2b` on i2c6：reset=GPIO3_A7(ACT_HIGH)，hpd-ctl=GPIO2_C3(ACT_HIGH)，
    INT=GPIO1_C7(LEVEL_HIGH)，xvclk=**假 fixed-clock 27M "CLK_CAMERA_27MHZ"**
    （即 27M 为板载振晶，与 chmayco 板一致）
  - i2c6 同总线：gs_stk8baxx@18（重力计）、husb311@4e（TypeC）

## 二、已修复并部署（全部在 TF 卡系统上生效）
1. **mipi_cam_pwen (GPIO1_A2) 电源开关**：DTS 稳压器原先被注释 → 已解注释 + always-on。
   （Android 金表：`gpio-34 mipi-cam-pwen out hi` 佐证）
2. **UXC2 I2C 闩锁极性反转**：0x80EE 写 0x01 = 关死（全 0），写 0x00 = 开。
   vendor UXC1 驱动语义相反。三步实验钉死：脉冲复位后可读 → 写 0x01 死 → 写 0x00 活。
3. **主线 CSI 管线打通**：新编 `rockchip-cif.ko` + `dw-mipi-csi2rx.ko`
   （CONFIG_VIDEO_ROCKCHIP_CIF / VIDEO_DW_MIPI_CSI2RX = m，需先开 V4L_PLATFORM_DRIVERS）。
   media graph 全链：lt6911uxc2 → dw-mipi-csi2rx → rkcif-mipi2 → /dev/video0-3。
4. **STREAMON 成功**（修掉三只拦路虎：field 不匹配→pad 补 field:none；
   缺 V4L2_CID_LINK_FREQ→加控件 445.5MHz；res-change 轮询用错映射把乱码时序推进管线
   →set_fmt 真存储/g_fmt 读存储+越界拒绝）。
5. 驱动 probe 顺序修复：power-supply 依赖稳压器（devm_regulator_get_enable_optional
   "power"）→ 先上电再复位；清掉 check_chip_id 里遗留的 0x80EE=0x01 杀手写。

## 三、当前卡点：芯片寄存器堆"噪声模式"
- 现象：任何上电序列（含教科书 POR：复位按住→断电→爬升→释放）后，
  全部 bank 读回快速变化的伪随机值（限字母表 ~60 个值）。
- 曾经好过一次（11-20 清晨）：bank0 读出完整合法 EDID（名字描述符 "Lontium Semi\n"
  对齐标准偏移、DTD/范围描述符俱全），控制 bank 全零（= 闩锁关的健康 ROM 空闲态）。
  那次的前置：pwen 悬空数小时 → 拉高 2s（芯片从总线消失）→ 复位脉冲 → 1s 后可读。
- 已排除：DDC 干扰（拔线无变化）、写坏（零写入 POR 仍噪声）、POR 时序、断电循环。
- macOS 全程看不到第二台显示器 → HPD/DDC 握手从未完成过（与固件未跑一致）。
- 采集：STREAMON 成功但 0 帧（无 MIPI 数据）——芯片固件没起来就无输出。

## 四、进行中：从 Android 内核映像反汇编真实驱动
- boot_a.img → /tmp/android-Image（5.10.110）→ vmlinux-to-elf 重建符号
  （/tmp/android-vmlinux.elf，lt6911uxc2_* 符号齐全）。
- 目标：真实 chip-id 寄存器/期望值、初始化写序列、stream on/off、锁状态寄存器、
  HPD/plugin-det 处理 —— 反汇编 agent 进行中。

## 五、遗留
- TypeC partner：dock 无 CC 线（硬件），阻塞。
- hdmirx (snps) 主线模块仍会自动加载 probe fdee0000（无害但可关）。
- 复刻 Android GPIO 时 0x2c 幽灵应答未解释（无对应 DTB 节点）。
