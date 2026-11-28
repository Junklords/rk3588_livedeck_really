#!/bin/bash
# HDMI-In 一键启动: 时钟 → GIC修复 → SMC PHY表 → vendor驱动
# 前提: 已断电重启到主线, /tmp 有以下模块:
#   clk-of.ko, gic-fix.ko, rockchip-hdmirx-vendor.ko

set -e
echo "[1/5] 启用 6 个 HDMI RX 时钟..."
insmod /tmp/clk-of.ko do_smc=1 2>/dev/null || echo "  (已加载)"
sleep 1

echo "[2/5] GIC 中断修复..."
insmod /tmp/gic-fix.ko 2>/dev/null || echo "  (已加载)"
sleep 1

echo "[3/5] 卸载旧驱动..."
rmmod rockchip_hdmirx_vendor 2>/dev/null || true
sleep 1

echo "[4/5] 加载 vendor 驱动 (5V bypass)..."
# 先确保模块文件在位
if [ -f /tmp/rockchip-hdmirx-vendor.ko ]; then
    insmod /tmp/rockchip-hdmirx-vendor.ko 2>/dev/null || echo "  (可能已加载)"
else
    modprobe rockchip_hdmirx_vendor 2>/dev/null || echo "  WARNING: driver not found"
fi

echo "[5/5] 等待信号锁定..."
echo "  请确保 HDMI IN 已接好信号源"
for i in $(seq 1 30); do
  if dmesg | grep -q "signal lock ok" 2>/dev/null; then
    echo "  *** LOCKED ***"
    dmesg | grep "format" | tail -1
    break
  fi
  sleep 1
done

echo ""
echo "== 中断计数 =="
grep "hdmirx-" /proc/interrupts | head -4
echo ""
echo "== 尝试采集 =="
echo "  v4l2-ctl -d /dev/video\$(... | grep hdmirx...) --stream-mmap --stream-count=5"
