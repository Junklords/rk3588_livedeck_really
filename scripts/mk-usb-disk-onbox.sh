#!/bin/bash
# USB 硬盘系统盘制作 — 在盒子(运行中的 Armbian/TF)上执行,目标 /dev/sda(500G HGST 机械盘,全盘清除)
set -e
DISK=/dev/sda
MNT=/mnt/usbroot
LOG=/tmp/mk-usb.log
exec > >(tee -a $LOG) 2>&1

echo "=== [1/6] 卸载 NTFS + 清盘 ==="
umount /run/media/user/* 2>/dev/null || true
systemctl stop udisks2 2>/dev/null || true
umount ${DISK}1 2>/dev/null || true
wipefs -a $DISK
sync

echo "=== [2/6] MBR + 单分区(LBA 32768 起,与 TF 同构) ==="
sfdisk --no-reread $DISK <<'EOF'
label: dos
start=32768, type=83
EOF
partprobe $DISK || true
for i in $(seq 1 15); do [ -b ${DISK}1 ] && break; sleep 1; done
[ -b ${DISK}1 ] || { echo "分区未出现"; exit 1; }

echo "=== [3/6] mkfs.ext4 ==="
mkfs.ext4 -F -L armbi_usb -m 2 ${DISK}1

echo "=== [4/6] rsync 运行系统 → USB ==="
mkdir -p $MNT
mount ${DISK}1 $MNT
rsync -aHAXx --numeric-ids --info=progress2 \
  --exclude=/proc/* --exclude=/sys/* --exclude=/dev/* --exclude=/run/* \
  --exclude=/tmp/* --exclude=/mnt/* --exclude=/media/* --exclude=/lost+found \
  / $MNT

echo "=== [5/6] fstab / hostname / extlinux ==="
USBUUID=$(blkid -s UUID -o value ${DISK}1)
echo "USB root UUID=$USBUUID"
# 重写 fstab:去掉旧 root 行,写入新 root(TF 的教训:sed 匹配不可靠,用 grep -v)
grep -v -E "^[^#].*[[:space:]]/[[:space:]]" $MNT/etc/fstab | grep -v "^UUID=6e9a0dd1" > $MNT/etc/fstab.new
mv $MNT/etc/fstab.new $MNT/etc/fstab
echo "UUID=$USBUUID / ext4 defaults,errors=remount-ro,commit=15 0 1" >> $MNT/etc/fstab
grep " / " $MNT/etc/fstab
echo really-usb > $MNT/etc/hostname
sed -i 's/127\.0\.1\.1.*/127.0.1.1\treally-usb/' $MNT/etc/hosts
KVER=7.3.4-edge-rockchip64
mkdir -p $MNT/boot/extlinux
cat > $MNT/boot/extlinux/extlinux.conf <<EOF
default usb
menu title Armbian USB boot (TV2340R)
prompt 0
timeout 30

label usb
    menu label Armbian edge ${KVER} (USB HDD)
    linux /boot/vmlinuz-${KVER}
    initrd /boot/initrd.img-${KVER}
    fdt /boot/dtb-${KVER}/rockchip/rk3588-really.dtb
    append root=UUID=${USBUUID} rootwait rootfstype=ext4 console=ttyS2,1500000 console=tty1 consoleblank=0 loglevel=7 usb-storage.quirks=0x2537:0x1066:u,0x2537:0x1068:u pcie_aspm=off cma=512M cfg80211.ieee80211_regdom=CN fbcon=rotate:3 cgroup_enable=cpuset cgroup_memory=1 cgroup_enable=memory
EOF
head -3 $MNT/boot/extlinux/extlinux.conf

echo "=== [6/6] 校验与卸载 ==="
sync
du -sh $MNT
ls -la $MNT/boot/vmlinuz-${KVER} $MNT/boot/initrd.img-${KVER} $MNT/boot/dtb-${KVER}/rockchip/rk3588-really.dtb
umount $MNT
sync
echo "[OK] USB 系统盘就绪 root UUID=$USBUUID(下次开机菜单默认 USB 优先)"
