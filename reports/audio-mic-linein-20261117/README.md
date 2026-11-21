# Really TVI2340R 音频、耳麦与麦克风专项交接

- 作者：OpenAI Codex（GPT-5），执行端：user
- 生成时间：2026-11-17T00:20:00+08:00
- 适用设备：Really / 锐丽 TVI2340R，RK3588，型号 `22011211C`
- 适用硬件仓：`/home/user/device/rk3588-really/rk3588_livedeck_really`
- 适用构建仓：`/home/user/github/build`
- 适用构建分支：`main`
- 适用内核线：Armbian `edge` / Linux `7.3.y` / `rockchip64`
- 工厂参考：Android 12 / vendor 5.10.110
- 当前运行设备：`root@192.168.3.233`
- 当前启动介质：TF 卡 `/dev/mmcblk1p1`
- 当前根文件系统 UUID：`7faf7863-d5d5-419e-81bd-902914de6883`
- 最新 DP 运行内核：`7.3.5-edge-rockchip64 #82`；本报告最近一次音频回归
  使用 `#76`，后续仍需在最终镜像中重新验收
- 最近实机回归：2026-11-21，冷启动后 PDM 能出现非零采样，但持续采集仍会
  在约 0.8 秒后停顿；已试过厂家驱动的 clock/filter delay，未闭环，补丁已撤回。
  扬声器静音 PCM 链路验证进入 `DAPM On`，模拟输出档位与默认 sink 已修正并
  固化到构建树

## 0. 接手规则：必须记录并回填进展

本文不是只读分析报告。任何 AI 或人工接手后，在开始下一轮修改前必须先
完成以下步骤：

1. 在 §12 的“接手运行日志”新增一行，记录开始时间、执行者、计划动作。
2. 每次探测、寄存器修改、DTS 修改、内核构建、安装和重启后，立即回填：
   - 时间；
   - 实际执行的命令或代码路径；
   - 原始结果摘要，不写“应该正常”等推断；
   - 结论：`通过`、`失败`、`不确定` 或 `已排除`；
   - 下一步。
3. 不接受“抓到一个文件”或“声卡成功注册”作为录音成功。必须证明采样值
   非全零，并明确声音从哪个物理接口输入。
4. 修改 DTS 或驱动后，必须同步：
   - `/home/user/github/build/patch/kernel/archive/rockchip64-7.3/dt/rk3588-really.dts`
   - Git 工程中的 `dts-7.3-linux/rk3588-really.dts`
   - 本报告的“当前主线改动”和验收状态。
5. 任何实机安装或重启后，记录：
   - 内核 release；
   - DTB SHA256；
   - `/proc/asound/cards`；
   - 相关驱动 bind/unbind 状态；
   - 失败日志的关键原文。
6. 结束一轮工作前，必须更新本文顶部状态、§10 待办清单和 §12 日志。没有
   回填的修改视为未完成交接。

> 安全约束：麦克风排查期间禁止自动播放测试音、`aplay`、GNOME 左右声道
> 测试或任何会让扬声器/耳机发声的动作。扬声器输出测试必须由用户明确要求，
> 并使用低音量、用户控制的普通音频，不使用突然出现的测试音。

### 0.1 内核构建成本控制

后续助手必须遵守以下构建规则：

1. 涉及内核代码、内核 config 或需要全量内核重编的改动，不得在未告知用户并
   获得确认的情况下直接启动。
2. 多个相邻的 DTS、config、driver 修改应尽量合并，一次完成验证，避免每改
   一个属性就全量编译一次。
3. 仅验证 DTS 语法和生成 DTB 时，优先使用 `kernel-dtb` 等窄目标，不启动
   完整 kernel/module/package 构建。
4. 不得为了省事默认使用 `ARTIFACT_IGNORE_CACHE=yes` 强制清理并全量重编。
   只有代码、config 或缓存一致性确实需要时才使用，并先说明原因。
5. 记录每次构建前已经合并的修改点，避免重复编译相同内容。

---

## 1. 结论摘要

当前最重要的结论是：

1. **原厂 Android 的内置麦克风使用 PDM0，不是 ES8388。**
2. 原厂同时注册了 `rockchip-es8388` 和 `rockchip,pdm-mic-array` 两张采集相关
   声卡；Android Audio HAL 启动时会枚举 PDM 声卡。
3. ES8388 负责扬声器、耳机、`Audio` 接口的耳麦输入以及独立的 `LINE IN`
   模拟输入。
4. 原厂板厂内核包含私有 `linein_det_handler`，会在 ES8388 `Line 1` 与
   `Line 2` 之间切换：
   - 耳机麦克风：`in1`，对应 Line 1；
   - 独立 LINE IN：`in2`，对应 Line 2。
5. 当前 mainline 上对 ES8388 抓到的硬数字零，不能再视为“麦克风增益低”或
   “ES8388 PGA 没打开”。内置麦克风很可能根本不在 ES8388 这张卡上。
6. 当前构建树已经加入 PDM0、`dmic-codec` 和 simple-audio-card 候选配置，
   但尚未完成内核包安装和实机采样闭环。

当前状态：

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| ES8388 播放 | ✅ 硬件链路通过，待听感确认 | 冷启动后默认 sink 为 ES8388；`Output 1/2 = 27 (-3 dB)`、`PCM = 189`；静音 PCM 下 `Speaker`、功放 `VCC/DRV`、`LOUT2/ROUT2` 全部为 `On` |
| ES8388 耳机检测 | ⚠️ 待完善 | GPIO4_B3 检测、GPIO4_B4 耳机功放使能；需验证插拔自动切换 |
| ES8388 Audio 耳麦输入 | ❌ 未闭环 | 原厂走 Line 1；mainline 当前所有已试路由均为数字零 |
| 独立 LINE IN | ❌ 未闭环 | 原厂走 Line 2，并由 GPIO1_B6 handler 动态切换 |
| 内置 PDM 麦克风 | ⚠️ **仅短时通过，持续采集失败** | 冷启动后 48 kHz 曾得到 `nonzero=2798`、`RMS=1865`、`peak=32317`；加入厂家同款约 20 ms clock/filter 等待后仅从约 0.55 秒延长到约 0.81 秒，仍会停顿；直接 `hw:0,0` 返回 `Input/output error`。驱动时序不是唯一根因 |
| 扬声器音质/爆音/低频 | ⚠️ 待完善 | 历史上有低频 hum、偏置和增益配置问题，不能按“能响”即关闭 |

---

## 2. 硬件和板级拓扑

```text
RK3588
  |
  +-- I2S1 M0 ---> ES8388 @ I2C3 0x11
  |                  |
  |                  +-- Speaker: GPIO4_B2 active-low enable
  |                  +-- Headphones: GPIO4_B4 active-high enable
  |                  +-- Headset detect: GPIO4_B3 active-high
  |                  +-- ADC/headset type detect: SARADC channel 3
  |                  +-- Audio jack mic: ES8388 Line 1
  |                  +-- LINE IN: ES8388 Line 2
  |                  |
  |                  +-- GPIO1_B6 line-in detect / private handler
  |
  +-- PDM0 M0 ----> built-in digital microphone array
                     CLK  GPIO1_C6
                     SDI0 GPIO1_D5
                     SDI2 GPIO1_D2
```

ES8388 的接口语义：

| 物理接口 | 原厂语义 | mainline 当前状态 |
| --- | --- | --- |
| `Audio` 3.5 mm 耳机口 | 耳机输出 + 头戴麦克风输入 | 输出基线存在，麦克风和自动切换未闭环 |
| `LINE IN` | 独立模拟线路输入 | 未闭环 |
| 内置麦克风 | PDM0 数字麦克风阵列 | 候选 DTS 已添加，未实机确认采到信号 |

### 2.1 mainline I2S1 M0 引脚

```text
LRCK  GPIO4_A2
SCLK  GPIO4_A1
SDI0  GPIO4_A5
SDI2  GPIO4_A7
SDO0  GPIO4_A9
MCLK  GPIO4_A0  (12.288 MHz)
```

这些引脚已经在现有 7.3 DTS 中验证能够完成 ES8388 probe，不能随意改回
I2S1 M1。M1 的 MCLK 与控制台 UART2 M0 的引脚存在冲突。

### 2.2 PDM0 M0 引脚

| 信号 | GPIO |
| --- | --- |
| `pdm0m0_clk` | GPIO1_C6 |
| `pdm0m0_sdi0` | GPIO1_D5 |
| `pdm0m0_sdi2` | GPIO1_D2 |

原厂只启用 CLK、SDI0 和 SDI2。不要因为声卡没有声音就同时打开 SDI1/SDI3；
先把原厂的三根线复现出来。

---

## 3. 原厂 DTS 定义

### 3.1 ES8388 声卡

主要定义：

```text
dts-5.10-android/rk3588-really.dts:123
```

关键属性：

```dts
es8388_sound {
	compatible = "rockchip,multicodecs-card";
	rockchip,card-name = "rockchip-es8388";
	rockchip,cpu = <&i2s1_8ch>;
	rockchip,codec = <&es8388>;
	rockchip,format = "i2s";
	rockchip,mclk-fs = <256>;

	hp-det-gpio  = <&gpio4 RK_PB3 GPIO_ACTIVE_HIGH>;
	hp-con-gpio  = <&gpio4 RK_PB4 GPIO_ACTIVE_HIGH>;
	spk-con-gpio = <&gpio4 RK_PB2 GPIO_ACTIVE_LOW>;
	linein-det-gpio = <&gpio1 RK_PB6 GPIO_ACTIVE_HIGH>;

	io-channels = <&saradc 3>;
	io-channel-names = "adc-detect";
};
```

原厂录音 DAPM 路由：

```text
LINPUT1  <- Main Mic
LINPUT2  <- Main Mic
RINPUT1  <- Headset Mic
RINPUT2  <- Headset Mic
```

注意：这里的 `Main Mic` 和 `Headset Mic` 是 multicodecs-card / ES8388 的
DAPM 名称，不代表 GPADC 或 PDM 内置麦。当前 mainline 的
`Really-ES8388` simple-audio-card 也保留了相同命名。

### 3.2 PDM 麦克风阵列

原厂定义：

```dts
dummy_codec
    compatible = "rockchip,dummy-codec";

pdm_mic_array {
	compatible = "simple-audio-card";
	simple-audio-card,name = "rockchip,pdm-mic-array";

	simple-audio-card,cpu {
		sound-dai = <&pdm0>;
	};

	simple-audio-card,codec {
		sound-dai = <&dummy_codec>;
	};
};
```

完整解出的原厂启动 DTB 中，PDM0 为：

```dts
&pdm0 {
	compatible = "rockchip,rk3588-pdm";
	reg = <0x0 0xfe4b0000 0x0 0x1000>;
	clocks = <&cru MCLK_PDM0>, <&cru HCLK_PDM0>;
	clock-names = "pdm_clk", "pdm_hclk";
	dmas = <&dmac0 4>;
	dma-names = "rx";
	pinctrl-0 = <&pdm0m0_clk &pdm0m0_sdi0 &pdm0m0_sdi2>;
	rockchip,path-map = <0 2 1 3>;
	status = "okay";
};
```

`rockchip,path-map = <0 2 1 3>` 是原厂实际 DTB 属性，不是推测：

```text
/home/user/device/rk3588-really/docs/rk3588-tvi2340r-orig.dts:4779
```

该映射表示：

```text
PATH0 <- SDI0
PATH1 <- SDI2
PATH2 <- SDI1
PATH3 <- SDI3
```

板子只接 SDI0/SDI2，所以当前候选设置为 `<0 2 1 3>` 与工厂一致。

### 3.3 `linein-det` 的真实用途

原厂 DTS 同时定义：

```dts
linein-det-gpio = <&gpio1 RK_PB6 GPIO_ACTIVE_HIGH>;
pinctrl-0 = <&hp_det &spk_ctrl &hp_con &linein_det>;
```

当前克隆的 vendor 5.10 `rockchip_multicodecs.c` 并没有直接消费
`linein-det-gpio`，但原厂实际运行的 kernel 中存在私有逻辑。证据：

- Android dmesg 出现 `linein_det_handler`；
- 原厂 `stock_boot.img` 内包含以下字符串：

```text
try to switch to headphone mic(in1)
try to switch to linein mic(in2)
get linein det gpio failed
Failed to request linein detect irq_lin
linein_det_handler
```

因此可以确认：

```text
Audio/headset mic -> ES8388 Line 1
LINE IN           -> ES8388 Line 2
GPIO1_B6          -> 选择当前模拟输入组
```

这部分属于动态 mux，单纯增加一个静态 DTS route 不能完整表达插拔切换。
后续实现可选择：

1. 在主线上增加小型板级 driver，读取 GPIO1_B6 并更新 ES8388 PGA/ADC mux；
2. 用窄范围用户态 helper 监听 GPIO 并调用 ALSA control；
3. 若内核 ASoC 已有合适的 jack/route 机制，优先用现有 binding 表达。

在完成证据采集前，不要直接改社区 `es8323` 通用驱动。

---

## 4. 原厂 Android 运行证据

### 4.1 最终 ALSA 设备列表

来源：

```text
/home/user/device/rk3588-really/reports/android-touch-baseline-20261115-1820/dmesg.txt:1036
```

```text
ALSA device list:
  #0: rockchip-es8388
  #1: rockchip,hdmiin
  #2: rockchip,pdm-mic-array
  #3: rockchip-hdmi1
```

### 4.2 Audio HAL 枚举

来源：

```text
/home/user/device/rk3588-really/reports/android-touch-baseline-20261115-1820/logcat.txt:18075
```

```text
card0 id:rockchipes8388
card1 id:rockchiphdmiin
card2 id:rockchippdmmica
card3 id:rockchiphdmi1
```

### 4.3 内置麦克风输入请求

来源：

```text
/home/user/device/rk3588-really/reports/android-touch-baseline-20261115-1820/logcat.txt:5246
```

```text
audio hal adev_open_input_stream devices = 0x80000004
in->Device : 0x4
in->SampleRate : 48000
in->Channels : 2
```

Android 的 `0x4` 是 built-in mic。结合 card2 的 PDM 枚举，内置麦克风的
主路径应判定为 PDM0。

### 4.4 已知 DTS 提取矛盾

某份 runtime DTS 中 PDM0 显示为 disabled，且 pinctrl 列表被展开成全部
SDI0..3；但最终 Android dmesg 明确注册了 `rockchip,pdm-mic-array`。
因此：

- 不要只用那份 runtime DTS 判断最终状态；
- 以板级 source override、完整解出的原厂 DTB 和最终 dmesg 三方交叉验证；
- 当前证据优先级是：最终 dmesg > 完整原厂 DTB > 板级 source > runtime 可疑副本。

---

## 5. 当前 mainline 改动

截至本报告生成时，`/home/user/github/build` 有未提交音频改动：

```text
M config/boards/really.conf
M patch/kernel/archive/rockchip64-7.3/dt/rk3588-really.dts
?? out/
```

上述 DTS 改动已在 2026-11-17 同步到硬件 Git 仓：

```text
M dts-7.3-linux/rk3588-really.dts
```

同步后两份 DTS 的 `cmp` 返回 0，SHA256 均为：

```text
4ca663f6c104bc9291ff49db8a5b1c6993df7f346a084510649da0c1c962fe7b
```

### 5.1 内核配置

`config/boards/really.conf` 新增：

```text
SND_SOC_ROCKCHIP_PDM
SND_SOC_DMIC
```

构建产物确认：

```text
CONFIG_SND_SOC_ROCKCHIP_PDM=y
CONFIG_SND_SOC_DMIC=y
```

### 5.2 DTS 候选

构建仓 DTS 已增加：

```dts
pdm_codec: pdm-codec {
	compatible = "dmic-codec";
	#sound-dai-cells = <0>;
	num-channels = <2>;
};

pdm_mics: pdm-mics {
	compatible = "simple-audio-card";
	simple-audio-card,name = "Really-PDM-Mics";

	simple-audio-card,cpu {
		sound-dai = <&pdm0>;
	};

	simple-audio-card,codec {
		sound-dai = <&pdm_codec>;
	};
};
```

并新增 PDM0 节点：

```dts
pdm0: pdm@fe4b0000 {
	compatible = "rockchip,rk3588-pdm", "rockchip,rv1126-pdm";
	reg = <0x0 0xfe4b0000 0x0 0x1000>;
	#sound-dai-cells = <0>;
	clocks = <&cru MCLK_PDM0>, <&cru HCLK_PDM0>;
	clock-names = "pdm_clk", "pdm_hclk";
	dmas = <&dmac0 4>;
	dma-names = "rx";
	pinctrl-names = "default";
	pinctrl-0 = <&pdm0m0_clk &pdm0m0_sdi0 &pdm0m0_sdi2>;
	rockchip,path-map = <0 2 1 3>;
	status = "okay";
};
```

`kernel-dtb` 已编译成功，生成的 DTB 中包含：

```text
pdm@fe4b0000
pdm-codec
pdm-mics
PDM clocks/DMA/pins
```

完整内核构建在本报告生成时仍在进行。后续助手必须先确认该构建最终成功，
安装匹配的 Armbian kernel package，而不是只替换裸 kernel 或裸 DTB。

### 5.2.1 用户态扬声器和默认路由

2026-11-21 的实机回归确认，ES8388 驱动默认把 `Output 1/2` 重置为 `0`
（`-30 dB`），同时 HDMI 卡可能抢到默认 sink。两者叠加会让 GNOME
“扬声器测试”既送到错误设备，又把模拟输出保持在最低档。

构建仓已加入：

```text
/home/user/github/build/userpatches/overlay/really-audio/usr/local/sbin/really-audio-route
/home/user/github/build/userpatches/overlay/really-audio/etc/systemd/system/really-audio-route.service
```

服务在 `sound.target` 后把扬声器开关打开、`Output 1/2` 设为 `27`、
`PCM` 设为 `189`。WirePlumber 规则同时设置 ES8388 的
`priority.driver` 和 `priority.session` 为 `2000`，避免 HDMI PCM 成为默认
sink。

### 5.3 mainline 驱动差异和风险

当前 Linux 7.3 mainline `rockchip_pdm.c` 的 compatible 表没有
`rockchip,rk3588-pdm`，只有：

```text
rockchip,rk3568-pdm
rockchip,rv1126-pdm
```

因此候选 DTS 使用 RV1126 回退以完成 bind。进一步差别：

- vendor 6.1 对 RK3588 使用 300 MHz signoff；
- mainline 7.3 RV1126 回退路径使用 100 MHz signoff；
- 两者寄存器路径高度兼容，但时钟选择和板级稳定性不能仅靠 DTB 编译通过来证明。

若 PDM 能注册但采样异常，优先检查：

1. `/sys/kernel/debug/clk/clk_summary` 中 PDM 时钟；
2. `pdm0m0_clk/sdi0/sdi2` pinmux；
3. DMA 是否持续有数据；
4. 是否应增加最小 RK3588-vendor 兼容差异，而不是改通用 RV1126 行为。

---

## 6. 当前 ES8388 采集失败结论

### 6.1 测试现象

当前 PipeWire source：

```text
node 47
alsa_input.platform-sound.stereo-fallback
```

使用 `pw-record` 采集 4 秒，48 kHz、stereo、S16_LE：

```text
peak=0
rms=0
nonzero=0
```

这是硬数字零，不是低音量。直接 `arecord hw:0,0` 会因 PipeWire 占用失败，
不能据此判断硬件。

### 6.2 DAPM 和寄存器状态

已经确认：

```text
Main Mic On
Headset Mic On
Mic Bias On
LINPUT1/RINPUT1 On
PGA Mux On
ADC On
Speaker Off
Headphone Off
```

ES8388 controls：

```text
Mic PGA Volume = 8,8 后仍全零
Capture Digital Volume = 192,192
PGA Mux = Line 1
Differential Mux = Line 1
Capture Route = Stereo
```

把 Mic PGA 从 0 改到 8 后，寄存器 `0x09` 从 `0x00` 变为 `0x88`，说明
I2C 和 ALSA 写入有效，但采样仍全零。

### 6.3 已测试路由

每条路由静默采集 2 秒，左右声道仍全部为零：

```text
Line 1
Line 2
Differential Line 1
Differential Line 2
Mono Left
Digital Mono
```

结论：

- 继续在 ES8388 mixer 中盲试没有优先级；
- 内置麦克风应先转到 PDM0 验证；
- ES8388 的 Line 1/Line 2 只用于 `Audio` 耳麦和独立 `LINE IN`。

---

## 7. 假设与状态

| 假设 | 状态 | 证据 |
| --- | --- | --- |
| 内置麦克风走 PDM0 | 高可信，待实机采样确认 | 原厂 card2、Android built-in mic 打开、板级 PDM 节点 |
| ES8388 负责 Audio 耳麦和 LINE IN | 高可信 | 工厂 routing、linein GPIO、stock kernel 切换字符串 |
| Audio 耳麦是 ES8388 Line 1 | 高可信 | stock kernel `headphone mic(in1)` |
| LINE IN 是 ES8388 Line 2 | 高可信 | stock kernel `linein mic(in2)` |
| 当前 ES8388 全零是 PGA 太小 | 已排除 | PGA 0→8 后仍为硬零 |
| 当前 ES8388 全零是 DAPM 未上电 | 已排除 | Main/Headset Mic、Mic Bias、ADC 均 On |
| 当前 ES8388 全零是 I2C/ALSA 写失败 | 已排除 | 寄存器值实际变化 |
| 当前全零是因为内置麦克风选错声卡 | 已确认 | ES8388 仍为零；PDM card1 可采到非零语音 |
| PDM RV1126 回退足以覆盖 RK3588 | 首轮通过 | 300 MHz 时钟实际开启，48 kHz 可录音；仍需长时稳定性回归 |

---

## 8. 下一轮执行顺序

### P0：完成 PDM 内置麦克风闭环（已通过首轮实机验证）

#### PDM 录音命令陷阱

本机 PipeWire 中 PDM source 同时有：

```text
object.id   = 34
object.serial = 70
node.name   = alsa_input.platform-pdm-mics.stereo-fallback
```

`pw-record --target` 接受的是 **node serial 或 node name，不是 object.id**。

错误方式：

```bash
pw-record --target 34 ...
```

这里的 `34` 是 object.id，不是 serial；命令没有绑定 PDM，实际落到默认
ES8388 source，录出的 8 秒数据全为零。硬件和驱动没有发生变化。

正确方式：

```bash
timeout 8s pw-record \
  --target alsa_input.platform-pdm-mics.stereo-fallback \
  --rate 48000 --channels 2 --format s16 \
  /tmp/pdm-mic-test.wav
```

也可以使用 serial：

```bash
pw-record --target 70 ...
```

但 node id 会随重启和 PipeWire 重连变化，`object.id` 不应作为稳定标识。
优先使用 `node.name`。

另外，PipeWire 1.6.2 的 `pw-record --sample-count` 在达到数量后可能不退出，
应使用 `timeout` 或由外部终止录音进程。

1. 确认完整 Armbian kernel build 成功。
2. 安装与运行内核 release 完全匹配的 kernel package。
3. 重启后记录：

```bash
uname -a
sha256sum /boot/dtb/rockchip/rk3588-really.dtb
cat /proc/asound/cards
arecord -l
wpctl status
dmesg | grep -Ei 'pdm|pdm-mic|Really-PDM|rockchip-pdm|asoc'
```

4. 确认 `Really-PDM-Mics` 或 `rockchip,pdm-mic-array` 出现在声卡列表。
5. 确认 platform driver 中有 `fe4b0000.pdm` bind：

```bash
find /sys/bus/platform/drivers/rockchip-pdm -maxdepth 1 -type l -ls
```

6. 在不产生任何扬声器/耳机声音的前提下，从 PDM source 录制用户说话。
7. 分析采样：

```bash
ffmpeg -hide_banner -i /tmp/pdm-mic-test.wav \
  -af astats=metadata=1:reset=0 -f null -
```

或使用 Python `wave` 读取 PCM，统计 `min/max/rms/nonzero`。

通过标准：

- PDM 声卡存在；
- 抓取样本非全零；
- 用户说话时 RMS/峰值随语音变化；
- 采样尾部日志没有 DMA、xrun 或 clock error；
- 重启后仍自动出现。

### P1：ES8388 Audio 耳麦

1. 保持扬声器和耳机功放关闭。
2. 插拔 3.5 mm `Audio` 插头，记录：

```bash
gpioget gpiochip4 11
gpioget gpiochip1 14
cat /sys/kernel/debug/iio/iio:device*/in_voltage3_raw
```

具体 GPIO 编号以现场 `gpioinfo` 为准，必须记录实际 chip/line 和测得值。
不要只按 SoC 编号推算后写结论。

3. 使用 ES8388 `Line 1`、Stereo capture route，静默录音。
4. 若仍为零，再验证 `Differential Line 1`，但每次只改一个变量。
5. 明确区分：
   - 无头戴麦克风；
   - 有头戴麦克风但检测错误；
   - ADC 有信号但 I2S/DMA 无数据。

### P1：独立 LINE IN

1. 先测量并记录 GPIO1_B6 插拔前后状态。
2. 用外部播放器提供低电平线路信号；不得通过本机扬声器产生测试音。
3. 比较 ES8388 Line 2 与 Differential Line 2。
4. 若两者均可采到信号，再实现自动切换。
5. 实现动态切换时，必须记录切换前后：

```bash
amixer -c Really-ES8388 contents
```

以及实际 GPIO 状态。

### P1：扬声器和耳机完善

1. 验证扬声器功放 GPIO4_B2 的开关时序，确认停止播放后关闭。
2. 验证耳机功放 GPIO4_B4 与检测 GPIO4_B3，插耳机时应关闭扬声器并启用耳机。
3. 检查低频 hum、pop/click 和 codec bias 时序。
4. 输出测试只能使用低音量普通音频，并且必须由用户明确要求。
5. 回归 ES8388 的 PCM Volume、Output 1/2 Volume，防止再次出现
   “数字增益满值、模拟输出最小”的反向配置。

### P2：构建固化

1. 将实机验证有效的最小改动同步回：

```text
/home/user/github/build/patch/kernel/archive/rockchip64-7.3/dt/rk3588-really.dts
/home/user/device/rk3588-really/rk3588_livedeck_really/dts-7.3-linux/rk3588-really.dts
```

2. 更新 `config/boards/really.conf`，仅保留验证需要的配置。
3. 用 Armbian 构建框架重新生成完整镜像。
4. 构建后从镜像内检查 DTB 和内核 config，不只看源文件。
5. 记录镜像路径、大小和 SHA256。

---

## 9. 禁止事项

- 不要把 ES8388 的 `Main Mic` DAPM 名称直接解释成内置数字麦。
- 不要只看到 `arecord` 生成 WAV 就判定录音成功。
- 不要在未确认声卡选择时反复调整 ES8388 PGA。
- 不要为 PDM 同时随意打开 SDI1/SDI3。
- 不要把 runtime DTB 的 PDM `disabled` 当成最终运行态唯一证据。
- 不要改社区通用 `es8323` 驱动来塞入板级 LINE IN handler。
- 不要让扬声器自动播放测试音。
- 不要只替换裸 kernel/DTB；交付必须通过 Armbian 构建框架。
- 不要未经用户确认就启动涉及内核代码或内核 config 的全量编译。
- 不要把多个小改动拆成多轮全量内核构建；先合并、审查，再统一编译。
- 不要在本报告中覆盖旧失败记录。追加新结果并明确哪些旧结论被推翻。

---

## 10. 待办清单

- [ ] 完整 kernel package 构建成功并安装到实机。
- [x] `Really-PDM-Mics` 声卡出现。
- [x] RK3588 PDM driver bind 成功。
- [x] PDM 时钟、pinmux、DMA 状态取证。
- [!] 从 PDM 静默采集到非零语音；短时有数据，但持续采集会停顿。
- [x] 验证 PDM 当前为单路麦克风复制到左右双声道。
- [!] 确认重启后 PDM 自动工作；冷启动只恢复短时数据，尚不可连续录音。
- [ ] 确认 ES8388 Audio 耳麦使用 Line 1。
- [ ] 确认独立 LINE IN 使用 Line 2。
- [ ] 确认 GPIO1_B6 插拔状态和极性。
- [ ] 实现 Audio/LINE IN 动态路由。
- [ ] 验证耳机插入后扬声器关闭、耳机输出启用。
- [ ] 回归扬声器低频 hum、pop/click 和偏置时序。
- [ ] 用户实际听一次低音量扬声器测试，确认可闻响度。
- [ ] 用下一次完整 Armbian 镜像回归 `really-audio-route.service` 和默认 sink。
- [x] 验证厂家驱动的 DMIC 上电等待和滤波器初始化时序；不足以闭环，补丁已撤回。
- [ ] 对比原厂 Android 运行中的 PDM 寄存器/供电，确认 DMIC 数据 lane 与板级
  电源；要求至少 60 秒连续 48 kHz 采集。
- [ ] 将最终有效改动同步到构建仓和硬件仓。
- [ ] 重新构建完整 Armbian 镜像并记录 SHA256。
- [ ] 更新本报告 §1、§5、§7、§10、§11、§12。

状态标记：

```text
[ ] 未开始
[-] 进行中
[x] 实机通过
[!] 失败或阻塞
```

---

## 11. 验收标准

只有以下全部满足，音频专项才可以标记完成：

1. 内置麦克风能通过 PDM0 采集到清晰语音，且重启后保持。
2. `Audio` 接头戴麦克风能录入，拔插检测正确。
3. 独立 `LINE IN` 能录入外部线路信号，GPIO1_B6 切换正确。
4. 插入耳机后扬声器输出被关闭；拔出后恢复。
5. 扬声器无明显持续 hum、爆音或异常 pop。
6. 无自动测试音或意外扬声器输出。
7. 所有改动来自 Armbian 构建产物，并已同步到两个 Git 工程。
8. 本文已完成进度回填和最终验收记录。

---

## 12. 接手运行日志

后续助手必须追加，不要删除历史行。

| 时间 | 执行者 | 动作 | 结果 | 结论 | 下一步 |
| --- | --- | --- | --- | --- | --- |
| 2026-11-17T00:20:00+08:00 | Codex / user | 审计 factory 5.10 DTS、完整原厂 DTB、Android dmesg/logcat、stock boot strings | 内置麦为 PDM0；Audio/Line In 对应 ES8388 Line1/Line2 | 原厂路径已定位 | 完成 PDM kernel 并实机静默采集 |
| 2026-11-17T00:20:00+08:00 | Codex / user | 复核当前 ES8388 capture | 所有已测路由均 peak/rms/nonzero=0 | ES8388 全零不是 PGA/DAPM 问题 | 改测 PDM |
| 2026-11-17T00:30:54+08:00 | Codex / user | 发现构建仓已有 PDM DTS，但硬件 Git 仓尚未同步 | 已将 `pdm-codec`、`pdm-mics`、PDM0 节点补入 `dts-7.3-linux/rk3588-really.dts`；两份文件 `cmp=0` | 源码现已同步，PDM 仍未实机验证 | 完成内核包安装后静默采集 PDM |
| 2026-11-17T00:43:00+08:00 | Codex / user | 安装内核+DTB、重启并静默测试内置 PDM 麦克风 | 内核 `7.3.4-edge-rockchip64 #72`；DTB `33c3fa37...044dcf7`；card1 `Really-PDM-Mics`；驱动 bind `fe4b0000.pdm`；录音 peak=32317、RMS≈887、nonzero=1399；左右声道完全相同 | **PDM 内置麦克风可以收音**；当前为单路复制双声道；`pw-record --sample-count` 收尾会挂住，需要 `timeout`/外部终止 | 做长时稳定性与 PDM 通道映射回归 |
| 2026-11-17T00:46:31+08:00 | Codex / user | 复盘“先全零、后有声”差异 | `--target 34` 使用了 object.id，并非 `pw-record` 要求的 node serial/name，实际落到默认 ES8388；改用 `alsa_input.platform-pdm-mics.stereo-fallback` 后 PDM 时钟 RUNNING 且采样非零 | 修复点在录音命令的 source 选择，不在内核或硬件 | 后续录音统一使用 PDM node.name |
| 2026-11-21T10:35:00+08:00 | Codex / user | 复核扬声器测试、录音和默认设备 | 初始默认 sink 为 HDMI；ES8388 `Output 1/2=0 (-30 dB)`；PDM 在异常热状态采样全零 | 默认 sink 和模拟输出档位均为确定性问题；热解绑/重绑后的 PDM 状态不可验收 | 冷启动复测 PDM，并固化输出默认值 |
| 2026-11-21T10:40:00+08:00 | Codex / user | 冷启动 Really 并按 PipeWire `node.name` 重测 PDM | 内核 `7.3.5-edge-rockchip64 #76`；DTB `08068e501bf38fb1ffc87f480d4ef6865645955a232b4e43e92ea8a4397cf562`；PDM `nonzero=2798`、`RMS=1864.99`、`peak=32317` | **冷启动后 PDM 收音正常**；此前全零来自运行态音频图/热重绑异常 | 固化扬声器默认值并做实际听感确认 |
| 2026-11-21T10:43:00+08:00 | Codex / user | 设置 ES8388 安全输出档位并播放全静音 PCM | `Output 1/2=27`、`PCM=189`；`Speaker`、功放 `VCC/DRV`、`LOUT2/ROUT2` 全部为 `On`；功放 enable GPIO 为有效低电平；播放无报错 | **扬声器硬件 DAPM/GPIO 链路通过** | 用户以低音量系统测试音确认可闻输出 |
| 2026-11-21T10:45:00+08:00 | Codex / user | 将音频默认值写入 Armbian 构建树 | 新增 `really-audio-route.service` 和脚本；WirePlumber 同时提升 `priority.driver/session` | 源码改动完成，待下一次完整镜像验收 | 构建并检查镜像内服务、默认 sink 与冷启动录音 |
| 2026-11-21T10:48:00+08:00 | Codex / user | 做 PDM 持续采集回归 | 8 秒 `pw-record` 仅写 26624 帧（约 0.55 秒）后 `hw_ptr` 停住，ALSA 状态仍为 `RUNNING`；直接 `arecord hw:0,0` 返回 `pcm_read: Input/output error`；8/16/48 kHz 均只得到约 0.25 秒数据 | PDM 尚未闭环；厂家驱动在 clock enable 后等待约 20 ms，并在 RX 启动后等待约 20 ms filter，mainline 当前均无该时序 | 增加最小 PDM 时序补丁并做长时采集 |
| 2026-11-21T11:10:00+08:00 | Codex / user | 编译并实机验证 PDM clock/filter delay 补丁 | 运行中 `mclk_pdm0=300 MHz`、PDM `CTRL0/CTRL1/CLK_CTRL` 持续有效；12 秒 `pw-record` 写入 38912 帧（约 0.81 秒），`nonzero=923`、`RMS=740`、`peak=32316`，之后 `hw_ptr` 停住；裸 `hw:0,0` 仍报 I/O error | **延迟补丁不足，不能保留**；当前证据更指向 PDM 数据/DMA 通路或板级 DMIC 供电，而非单纯启动等待 | 撤回补丁；后续对比原厂 Android 运行态、PDM 供电和 DMA 请求 |
|  |  |  |  |  |  |

建议每次追加记录以下固定字段：

```text
Kernel:
DTB SHA256:
Sound cards:
Command:
Raw result:
Conclusion:
Next:
Artifacts:
```

---

## 13. 关键路径

工厂 DTS：

```text
/home/user/device/rk3588-really/rk3588_livedeck_really/dts-5.10-android/rk3588-really.dts
```

完整原厂运行 DTS：

```text
/home/user/device/rk3588-really/docs/rk3588-tvi2340r-orig.dts
```

Android 基线日志：

```text
/home/user/device/rk3588-really/reports/android-touch-baseline-20261115-1820/dmesg.txt
/home/user/device/rk3588-really/reports/android-touch-baseline-20261115-1820/logcat.txt
```

原厂 boot 镜像：

```text
/home/user/device/rk3588-really/EMMC-mod/magisk/stock_boot.img
```

mainline 硬件仓 DTS：

```text
/home/user/device/rk3588-really/rk3588_livedeck_really/dts-7.3-linux/rk3588-really.dts
```

Armbian 构建 DTS：

```text
/home/user/github/build/patch/kernel/archive/rockchip64-7.3/dt/rk3588-really.dts
```

Armbian 板级配置：

```text
/home/user/github/build/config/boards/really.conf
```

当前 mainline PDM 驱动：

```text
/home/user/github/build/cache/sources/linux-kernel-worktree/7.3__rockchip64__arm64/sound/soc/rockchip/rockchip_pdm.c
```
