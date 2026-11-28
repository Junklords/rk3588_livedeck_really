# RK3588 内部 HDMI RX 主线移植开发全程复盘 —— 教学版

> 日期: 2026-11-28 · 平台: Really TVI2340R (RK3588) + Armbian 主线内核 7.3.4
> 结果: Ubuntu 主机画面 → 盒子 HDMI IN → **主线驱动采集 1080p60** → 内屏显示 / One-KVM 网页 KVM(含 USB 键鼠模拟)
> 姊妹篇: `hdmiin-lt6911uxc2-20261121/`(外部转换芯片路径,出厂无固件,已排除)

---

## 0. 一张图看懂最终架构

```
┌─────────────────┐  HDMI(DP适配器)  ┌───────────────────────────────────────────┐
│  Ubuntu 主机     │ ───────────────> │  TVI2340R 盒子 (RK3588, 主线 7.3)          │
│  (CIX, GNOME)   │                  │                                           │
│                 │   USB(TypeC口)   │  HDMI IN 座                                │
│  USB 口 ◄───────┼─────────────────┼─ TypeC(dwc3 OTG 外设模式)                  │
│  被模拟成键鼠    │   HID gadget     │    └→ 键盘+相对鼠+绝对鼠+多媒体键 ×4 HID   │
└─────────────────┘                  │                                           │
       ▲  浏览器                     │  DWC HDMI RX 控制器 @ fdee0000             │
       │  http://box:8080            │   ├─ 6 路时钟 (clk-of.ko)                  │
       └─────────────────────────────┤   ├─ BL31 SMC 写 PHY 参数表 (0x82000027)   │
            One-KVM (Rust, :8080)    │   ├─ GIC 组1+使能 (gic-fix.ko)            │
            MJPEG/WebRTC + HID 注入  │   └─ vendor 驱动移植版 (stability v2)      │
                                     │        └→ /dev/video12 MPLANE BGR24@60    │
                                     │  内屏: GNOME Wayland + mpv 全屏            │
                                     └───────────────────────────────────────────┘
```

**读法**: 这不是"装个驱动"的故事,而是**四层系统**(固件/时钟/中断/驱动)+**两端系统**(信号源、显示端)的联调故事。
每一层都有自己的"隐形契约",踩坑本质都是契约被打破。

---

## 1. 硬件侦察:先搞清信号走哪条路

### 1.1 两条候选路径

RK3588 平台 HDMI 输入有两种业界做法:

| 路径 | 芯片 | 内核接口 | 本机结论 |
|---|---|---|---|
| A. 外置转换芯片 | LT6911UXC2 (HDMI→MIPI CSI) | i2c6@0x2b + CSI + CIF | **出厂无固件,死路**(见姊妹篇) |
| B. SoC 内置 RX | Synopsys DWC HDMI RX | `fdee0000` 平台设备 + V4L2 | ✅ 本文档主角 |

### 1.2 怎么裁决的(方法论:交叉验证)

- Android 原厂 DTB 里 `hdmi_receiver@fdee0000` 是 **disabled** —— 原厂没用它?不,原厂内核驱动里有它,只是这台机器 OEM 配置选择;
- 但 HDMI IN 座的 DDC/HPD 走向、GPIO 分配在原厂金表里对得上内部 RX;
- 最终实证: 给内部 RX 完整上电后,插入信号源能读到 TMDS 锁定 —— 硬件通路存在。

> **教学点 1**: 原厂 DTB 的 enabled/disabled 只是"OEM 想不想用",不代表硬件没接。
> 判断硬件通路要看:引脚复用(pinmux)、电源、原理图/金表、以及最终"能不能读到芯片的合法状态"。

### 1.3 关键地址表(背下来,调试全靠它们)

| 资源 | 地址/IRQ | 说明 |
|---|---|---|
| RX 控制器寄存器 | `0xfdee0000` | DWC HDMI RX,MAINUNIT_STATUS@+0x150 等 |
| PHY 寄存器堆 | `0xfdf10000` | **EL3 专属**,内核碰一下就总线挂死 |
| SYS GRF 门控 | `0xfd586054` | PHY 堆访问开关,**同为 EL3 专属** |
| IRQ | 468(hdmi) / 211(dma) / 209(cec) | SPI,GICv3 |

---

## 2. 固件层:BL31 里"没公开的代码"

### 2.1 问题

vendor 驱动会调 SMC `0x82000027 (RK_SIP_HDMIRX_CFG)`,让 BL31(EL3 固件)替它做两件事:
1. 打开 GRF 门控,把 **PHY 参数表**写进 0xfdf10000;
2. PHY "auto touch" 等杂务。

主线启动链用的是社区/Collabora BL31 —— 没有这个 SMC 实现。**没有 PHY 表,RX 的 CR 接口就是死的**(后文 CR 超时的根源之一)。

### 2.2 解法:从 vendor BL31 二进制里挖参数表

```
trust.img (vendor v2.3 BL31)
  └─ .rodata 0x28414–0x29427 (4116 字节区间)
       └─ 提取 348 字节 PHY tuning 表 → phy-tbl-348.h (C 数组)
```
然后在 **Collabora BL31 源码**里加一个 SMC handler:

```c
// plat/rockchip/rk3588/plat_sip_calls.c  (示意)
case RK_SIP_HDMIRX_CFG:            // 0x82000027
    switch (x1) {
    case 1: hdmirx_phy_table_load(); break;   // 写348字节到0xfdf10000
    case 2: /* REG_PRE_FETCH */      break;
    case 3: /* INFO_NOTIFY */        break;
    }
```

`hdmirx_phy_table_load()` 的顺序(顺序错了就挂):
```
置位 CTRL+0x290 bit0 → 清 CTRL+0x24 bit10 → 写 GRF gate(0x40000000)
→ 逐字写 PHY 堆(前5个走+0x04,其余走+0x24)
→ 写 GRF gate(0x40004000) → 恢复 CTRL+0x24 bit10
```

> **教学点 2**: EL3 专属寄存器(secure-only)从内核(EL1)写会**总线挂死**而不是返回错误——
> "写下去就死机"是访问权限问题的典型指纹。解法是把这类写挪进 BL31 的 SMC handler。
> 逆向二进制提取常量表(.rodata)是替代 NDA 文档的合法手段。

### 2.3 时钟与中断:两个"名字对不上"的坑

**时钟坑**: 驱动用 consumer 名(`aclk`,`ref`,`pclk`,`cr_para`,`hclk_s_hdmirx`,`hclk_vo1`)从 DT 取时钟,
但 `clk_get(NULL, 全局名)` 会失败 —— 全局时钟框架里叫 `aclk_hdmirx` 之类。
正确姿势: `of_clk_get_by_name(np, con_id)` 沿 DT 引用解析。clk-of.ko 就干这个:

```
aclk   500MHz │ ref 428.5MHz │ pclk 150MHz
cr_para 24MHz │ hclk_s 100MHz │ hclk_vo1 198MHz
```

**中断坑**: GIC-600 里这三条 SPI 默认在 Group 0(FIQ 用,需 BL31 处理器——我们没有)。
直接从内核开 FIQ = 崩。gic-fix.ko 的做法: `IGROUPR` 置 Group1 + `ISENABLER` 使能。

> **教学点 3**: 内核 request_irq 成功 ≠ 中断会到 CPU。GIC 分组(Group0/1)、分发器使能、
> 控制器内部 MASK 三道门,任何一道关着,`/proc/interrupts` 计数就是 0。
> 排查顺序: `/proc/interrupts` 计数 → 控制器中断状态寄存器 → GIC 寄存器。

---

## 3. 驱动层:vendor 驱动移植主线 7.3

### 3.1 移植要点(5.10 → 7.3,约 5300 行)

- `videobuf2` API 更新、`v4l2_ctrl` 框架变化、`devm_*` 资源管理差异;
- DTS 节点兼容: **双份 reset-names**(vendor 的 `rst_a/rst_p/rst_ref/rst_biu` + 主线的 `axi/apb/ref/biu`),
  时钟 7 路,`power-domains = PD_VO1`,`hpd-gpios`/`hdmirx-det-gpios` 都指 GPIO4_C6;
- 5V 检测绕过: 板上 GPIO4_C6 读恒高(悬空),`tx_5v_power_present()` 直接 `return true`;
- 探测尾部挂 auto-hotplug: `schedule_delayed_work(delayed_work_hotplug, 100ms)`,插上信号源自动进锁定流程。

### 3.2 锁定与格式识别(驱动的工作原理)

```
hdmirx_delayed_work_hotplug (事件驱动/重试)
  └→ hdmirx_plugin()
       ├─ submodule_init (SCDC/controller init)
       ├─ hdmirx_phy_config()          ← CR 接口写 PHY,易超时
       └─ hdmirx_wait_lock_and_get_timing()
            └─ 轮询 600 次:
                 TMDSVALID_STABLE_ST && HDMIRX_LOCK && TMDSQPCLK_LOCKED_ST
                 连续稳定 > WAIT_LOCK_STABLE_TIME → 锁定!
       ├─(锁定后) 等 AVI infoframe 包(中断) → 色彩/量化
       └→ hdmirx_format_change: 读 DMA_STATUS11 得 pix_fmt(RGB888→BGR24)
```

实测: `signal lock ok, i:321!` → `New format: 1920x1080p60.00 (2200x1125)`

**踩坑**: 等 AVI 包依赖 PKT_2 中断;中断链没通时它超时,但**格式仍能从寄存器兜底识别**,
输出格式退到 BGR24(RGB888 默认)。采集不依赖 AVI 包。

### 3.3 采集链的隐秘知识(血泪)

1. **`stream->out_fmt` 只有 S_FMT 才赋值** —— 直接 `--stream-mmap` 会 `queue_setup: out_fmt null` 报 EINVAL。
   而 vendor 的 `g_fmt` 有副作用魔改(内部调 `set_fmt(!try)`)—— 所以"先 `v4l2-ctl --get-fmt-video` 再采集"就能过;
2. **设备是 MPLANE-only** —— ffmpeg 的 v4l2 输入只支持单平面,直接 `ffmpeg -f v4l2` 报 ENOTTY;
3. **一次只能一个采集者**(vb2 独占) —— mpv 和 One-KVM 不能同时开 /dev/video12。

---

## 4. 稳定性工程:三轮根因收敛(本文最重要的一章)

### 4.1 症状时间线(重建因果链是调试的核心手段)

```
T+0    insmod 驱动,auto-hotplug 启动
T+5s   signal lock ok + 格式识别成功
T+40s  信号无声无息消失(不是驱动崩,是源停发!)
T+40s~ 1Hz 重试循环: CR 超时风暴 → 系统挂死
```

### 4.2 三层根因,逐层剥洋葱

| 层 | 表象 | 真因 | 修复 |
|---|---|---|---|
| **L1 驱动** | 内核 5 分钟内挂死 | 无信号时 1Hz 重试,每轮 `phy_config` 产生十几个 CR 写超时(各 10ms),紧轮询占死 CPU | 限流: phy_config 60s 滑动窗口 ≤10 次;轮询循环加 usleep |
| **L2 驱动×源** | 信号源"变得不可靠",链路反复断 | 重试循环每轮 plugin/plugout **翻动 HPD**,DP 适配器把抖动传回主机,DRM 判定显示器不可靠 → 关流 | **sticky HPD**: plugout 不再拉低 HPD;重试改**指数退避** 1s→2s→4s→…→30s 封顶;锁定成功双计数器清零 |
| **L3 信号源** | 锁定~40s 后源停止发送 | Ubuntu GNOME **空闲息屏** → 挂起整个显示引擎(`linlondp_dev_suspend`) → TMDS 停发。用户人在盒子边操作,主机一直空闲 | 主机常驻 idle inhibitor(`gnome-session-inhibit --inhibit idle`)+ `idle-delay=0` |

**另两个一次性炸弹**:
- `rmmod` = panic: 卸载后 1Hz 重排的 work 在已释放内存上执行。**对策:永不 rmmod,换版本就重启**;
- HDMI 口断电(盒子重启)会让源侧 DP 适配器报 Unplugged,GNOME 需重新唤醒输出。

> **教学点 4(本项目的灵魂)**: "信号掉了"有三种完全不同的原因——
> ① 我们搞挂了链路(L2);② 源主动停发(L3);③ 物理断电。
> 判据:**看源的 DRM 日志**(`dmesg | grep drm/trilin`): 有 `encoder_disable`/`dev_suspend` = 源侧关的;
> 有 HPD Unplugged 事件 = 物理层;源一直发而盒收不到 = 盒子侧。
> **永远同时看两端日志再下结论。**

### 4.3 稳定性补丁最终形态(rk_hdmirx.c)

```c
/* 1) phy 预算: 60 秒窗口内最多 10 次 */
if (time_after(jiffies, dev->phy_window_start + msecs_to_jiffies(60000))) {
        dev->phy_config_calls = 0;
        dev->phy_window_start = jiffies;
}
dev->phy_config_calls++;

/* 2) sticky HPD: 信号丢失重试时不拉低 HPD */
static bool sticky_hpd = true;   // module_param
if (!sticky_hpd) hdmirx_hpd_ctrl(dev, false);

/* 3) 指数退避重试 */
dev->hotplug_retries++;
delay = min(1000 << min(dev->hotplug_retries, 5), 30000);   // 1s..30s
schedule_delayed_work(..., msecs_to_jiffies(delay));

/* 4) 锁定成功 → 计数器全清零,恢复正常节奏 */
dev->phy_config_calls = 0;  dev->hotplug_retries = 0;
```

效果: 补丁前 5 分钟必挂;补丁后连续运行,`/proc/interrupts` DMA 计数 6 万+,60fps 持续。

---

## 5. 显示输出:为什么"写 fb0 看不到"

### 5.1 现象与真相

ffmpeg 往 `/dev/fb0` 灌帧,fb0 里确实有数据(od 读出非均匀像素),内屏却纹丝不动。

debugfs 一锤定音:

```
/sys/kernel/debug/dri/0/state:
  plane[35] Cluster0-win0 → crtc video_port0(HDMI OUT 口) → fb=100  ← fb0 的缓冲
  plane[47] Cluster2-win0 → crtc video_port3(内屏 LVDS)   → fb=98   ← GNOME 自己的
```

**盒子上跑着完整 GNOME(持 DRM master),内屏扫的是 GNOME 的缓冲;fb0 只是挂在 HDMI OUT 口的兼容节点。**

### 5.2 三条显示路线的取舍

| 路线 | 做法 | 结论 |
|---|---|---|
| fbdev 直写 | `ffmpeg -f fbdev /dev/fb0` | ✗ 画面上了 HDMI OUT 口 |
| 抢 DRM(KMS) | mpv `--vo=drm` | ✗ DRM master 被 GNOME 占用,Permission denied |
| **进 GNOME 显示** | mpv Wayland 窗口全屏 | ✅ 最终方案(在 user 会话里跑) |

```bash
runuser -u user -- env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
  setsid nohup /root/hdmirx/stream2screen.sh &
# 内部: v4l2-ctl(MPLANE采集) | mpv --demuxer-rawvideo-mp-format=bgr24 --fullscreen
```

> **教学点 5**: 有桌面环境(GTK/Wayland)在跑时,**别从下层(fb/KMS)抢显示权**,
> 要么进会话显示,要么停掉桌面。`/sys/kernel/debug/dri/0/state` 是看"谁在扫什么"的 X 光片。

---

## 6. 应用层:One-KVM 部署(从"能采集"到"产品")

### 6.1 为什么顺利

- One-KVM(Rust 单二进制)的 V4L2 采集器**原生支持 MPLANE BGR24** —— ffmpeg 的坑不存在;
- 它自带 USB OTG HID 管理: 写 configfs 造 gadget,绑定我们验证过的 `fc000000.usb` UDC;
- RK3588 HDMI IN 是它文档点名的支持场景。

### 6.2 部署三步

```bash
# 1) 安装(盒子直连 GitHub 超时 → 本机下载再传)
apt-get install ./one-kvm_0.2.9_arm64.deb        # 自动建 one-kvm.service(:8080)

# 2) 改配置(sqlite,绕过网页向导的自动检测——它会选错成 USB 摄像头)
sqlite3 /etc/one-kvm/one-kvm.db "UPDATE config SET value=json_set(value,
  '\$.video.device','/dev/video12',
  '\$.hid.backend','otg',
  '\$.hid.otg_udc','fc000000.usb') WHERE key='app_config';"
systemctl restart one-kvm
# 日志确认: "Video configured: /dev/video12" + "1920x1080 Bgr24 @ 60fps"

# 3) 接线: 盒子 TypeC → 被控机 USB;浏览器开 http://盒子IP:8080
```

验证 gadget:
```
/sys/kernel/config/usb_gadget/one-kvm/UDC        == fc000000.usb
functions/: hid.usb0..3 (键盘/相对鼠/绝对鼠/多媒体键)
```

---

## 7. 复现手册(从断电到出画面)

### 7.1 文件清单

| 位置 | 内容 |
|---|---|
| 盒子 `/root/hdmirx/` | **rockchip-hdmirx-vendor.ko(合并版单模块)**, start.sh, stream2screen.sh |
| 本机 `/data/ruili/tools/` | hdmirx-run.sh(全链), hdmirx-modules/(模块归档), hdmirx-box-kit/(同盒子) |
| 内核源码树 | `drivers/media/platform/rockchip/hdmirx-vendor/`(stability v2 + 合并补丁) |
| BL31 源码 | `/tmp/tfa-collabora/plat/rockchip/rk3588/plat_sip_calls.c`(SMC handler) |

### 7.2 启动流程(盒子重启后)

```bash
/root/hdmirx/start.sh     # 单 insmod → 等锁定 → mpv推内屏
```

**合并版单模块**(2026-11-28 起): 原 clk-of.ko / gic-fix.ko 的职责已并入驱动——
probe 里 `hdmirx_gic_self_fix()`(SPI 468/211/209 → Group1+使能) 和
runtime_resume 里 `hdmirx_smc_phy_table_load()`(SMC 0x82000027 x1=1 写 PHY 表)。
一条 `insmod` 即全通,两枚辅助模块退役(归档保留)。

注意:
- `/tmp` 重启清空,模块放持久路径 `/root/hdmirx/`;
- **永不 rmmod vendor 驱动**(work 竞态 panic)。

### 7.3 信号源(Ubuntu 主机)每次重启后

```bash
# 画面黑了就执行(防 GNOME 息屏断链):
gnome-session-inhibit --inhibit idle --reason kvm sleep infinity &
# 已持久化: idle-delay=0, sleep-inactive-ac-type=nothing
```

### 7.4 KVM 模式(取代内屏 mpv)

One-KVM 服务开机自启;它和 mpv **互斥占用** /dev/video12 —— 开网页 KVM 前先
`kill $(pgrep -x mpv) $(pgrep -x v4l2-ctl)`。

---

## 8. 方法论总结(可迁移的调试套路)

1. **分层解剖**: 固件(BL31)→时钟→中断→驱动→用户态→信号源→显示端。
   每层先问"这层的契约是什么,谁保证它";
2. **两端同时看日志**: "信号消失"类问题,源的 DRM 日志往往比盒子的驱动日志更早给出答案;
3. **时间线重建因果**: 把 dmesg 按时间排开,在"最后一个正常事件"和"第一个异常事件"之间找分界;
4. **二进制是最后的文档**: vendor BL31 的 .rodata 提取 PHY 表、OEM 驱动反汇编出寄存器映射;
5. **预期失败要认得**: clk-of 的 -EBUSY、v4l2 的 out_fmt null、ffmpeg 对 MPLANE 的 ENOTTY,
   都是"设计如此"而非 bug —— 认得它们能省半天;
6. **小动作大灾难**: pkill 模式匹配到自己、root 日志挡 user 写入、/tmp 重启清空、
   rmmod 的 work 竞态 —— 这类"环境脚枪"杀掉的调试时间不比正主少;
7. **稳定性是设计出来的**: 限流(预算窗口)、退避(指数)、粘性(sticky HPD)三板斧,
   把"异常时的行为"从"雪崩"改成"优雅降级"。

---

## 9. 遗留与展望

| 事项 | 状态 |
|---|---|
| 开机全自动(盒子侧 systemd 编排 + 主机侧 inhibitor 自启) | 待做 |
| 驱动单平面 API 补丁(让 `ffmpeg -f v4l2` 直接可用) | 待做 |
| AVI infoframe 中断(PKT_2)不触发 → 色彩信息走寄存器兜底 | 观察中 |
| CEC、音频采集(ALSA/HDMI audio) | 未开 |
| TypeC 主机模式 partner(dock 无 CC 线) | 硬件阻塞 |
| 驱动合入主线化(拆掉 clk-of/gic-fix,probe 内自治) | 长期 |
