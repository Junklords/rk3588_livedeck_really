# Really YT8531 PHY LED 修复记录

[English version](README.en.md)

- 作者：Codex（AI 助手）
- 生成时间：2026-11-23
- 背景：Really 新镜像中的 YT8531 PHY LED 没有绑定 `end0`，链路和活动指示灯不受控。
- 适用分支：Armbian `edge`，Linux 7.3。
- 验证环境：Really TVI2340R、YT8531C、`stmmac-0:01`、`end0`。
- 涉及组件：`userpatches/overlay/really-udev/etc/udev/rules.d/71-really-phy-led.rules`。

## 根因

Linux `netdev` LED trigger 按需创建以下属性：

```text
device_name
link_1000
full_duplex
rx
tx
```

旧 udev 规则没有先选择 `trigger=netdev`，直接写这些属性。启动日志出现：

```text
Failed to write "0" to sysfs attribute "rx", ignoring: Operation not supported
Could not chase sysfs attribute ".../link_1000", ignoring: No such file or directory
```

因此 LED 保持 `trigger=none`，udev 没有真正控制 PHY LED。

## 修复

规则顺序改为：

```text
trigger=netdev
device_name=end0
链路 / 双工 / RX / TX 条件
```

最终行为：

| LED | 条件 |
| --- | --- |
| 绿色 | 1 Gbps 全双工链路 |
| 黄色 | RX/TX 活动 |

## 运行验证

修复后 sysfs 状态：

```text
green: trigger=netdev device_name=end0 link_1000=1 full_duplex=1 rx=0 tx=0
yellow: trigger=netdev device_name=end0 link_1000=0 full_duplex=0 rx=1 tx=1
```

`end0` 当时为 1 Gbps 全双工链路。规则只匹配
`stmmac-0:01:green:lan` 和 `stmmac-0:01:yellow:lan`，不匹配其他 LED。

## 构建落点

- 规则：`/home/user/github/build/userpatches/overlay/really-udev/etc/udev/rules.d/71-really-phy-led.rules`
- 安装逻辑：`/home/user/github/build/userpatches/customize-image.sh`

不需要 systemd 常驻服务或轮询脚本。
