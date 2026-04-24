#!/bin/sh
# Phase 0: 板端（ATK-DLRV1126B）事实收集
# 用法（在板子上）：
#   sh phase0-collect-board.sh > /tmp/phase0-board.txt
#   # 把 /tmp/phase0-board.txt scp 回主机，贴到 docs/port/rv1126b/facts.md

echo "========================================"
echo "Phase 0 板端侦察 — $(date -Iseconds 2>/dev/null || date)"
echo "========================================"

echo ""
echo "=== [1] CPU / 架构 ==="
uname -a
echo "---"
head -20 /proc/cpuinfo

echo ""
echo "=== [2] OS / Rootfs ==="
cat /etc/os-release 2>/dev/null || echo "(no /etc/os-release)"
echo "---"
echo "libc:"
ls -la /lib/libc.so* /lib/ld-linux* 2>/dev/null
echo "---"
echo "libstdc++:"
find / -name "libstdc++.so.6*" 2>/dev/null | head -3

echo ""
echo "=== [3] 内存 / 磁盘 ==="
free -h 2>/dev/null || cat /proc/meminfo | head -5
echo "---"
df -h /

echo ""
echo "=== [4] 硬件设备节点 ==="
echo "MPP:"
ls -la /dev/mpp_service /dev/rkvenc* /dev/rkvdec* 2>&1 | head -5
echo "RGA:"
ls -la /dev/rga 2>&1
echo "DRM:"
ls -la /dev/dri/ 2>&1 | head -10
echo "V4L2:"
ls -la /dev/video* 2>&1 | head -10
echo "DMA-BUF heaps:"
ls -la /dev/dma_heaps/ 2>&1

echo ""
echo "=== [5] MPP / RGA / Rockit 运行时库 ==="
for lib in librockchip_mpp librga librockit libasound libssl libcrypto; do
  hit=$(find /usr /opt /lib -name "${lib}*" 2>/dev/null | head -3)
  echo "  $lib:"
  echo "$hit" | sed 's/^/    /'
done

echo ""
echo "=== [6] 网络 ==="
ip addr show 2>/dev/null | grep -E 'inet |link/ether' | head -10 || ifconfig
echo "---"
echo "DNS / gateway:"
cat /etc/resolv.conf 2>/dev/null | head -3
ip route 2>/dev/null | head -3

echo ""
echo "=== [7] 时钟（TLS 证书校验依赖） ==="
date
echo "(若时间年份不对，需要 date -s 或 ntpdate 同步)"

echo ""
echo "=== [8] 当前进程 / 关键服务 ==="
ps aux 2>/dev/null | grep -iE 'mpp|rga|rkipc|rockit' | grep -v grep | head -5

echo ""
echo "=== 收集完成 ==="
