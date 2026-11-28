#!/bin/bash
# v4l2-ctl(MPLANE采集) | mpv(Wayland全屏)
v4l2-ctl -d /dev/video12 --get-fmt-video >/dev/null 2>&1
exec v4l2-ctl -d /dev/video12 --stream-mmap --stream-count=100000 --stream-to=- --stream-poll 2>/dev/null | \
  mpv --no-config --demuxer=rawvideo \
      --demuxer-rawvideo-w=1920 --demuxer-rawvideo-h=1080 \
      --demuxer-rawvideo-fps=60 --demuxer-rawvideo-mp-format=bgr24 \
      --fullscreen --untimed --really-quiet - 2>>/tmp/mpv-err.log
