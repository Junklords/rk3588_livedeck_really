#!/bin/bash
# HDMI-in 一键启动(合并版): 单模块自带 GIC修复+SMC PHY表
set -e
insmod /root/hdmirx/rockchip-hdmirx-vendor.ko 2>/dev/null || echo "driver maybe loaded"
echo "等待信号锁定..."
for i in $(seq 1 60); do
  dmesg | tail -200 | grep -q "signal lock ok" && { echo LOCKED; break; }
  sleep 1
done
runuser -u user -- env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
  setsid nohup /root/hdmirx/stream2screen.sh </dev/null >/dev/null 2>&1 &
echo "内屏推流已启动 (KVM 网页模式则勿开, 两者互斥占用 video12)"
