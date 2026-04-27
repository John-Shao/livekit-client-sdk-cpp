#!/bin/sh
# BoardLoopback 冒烟测试脚本（版本控制权威版本）。
#
# 部署：scp 到板子 /opt/livekit/smoke.sh：
#   scp scripts/smoke.sh rv1126b-board:/opt/livekit/smoke.sh
#
# Token 解析顺序（自上而下，优先用先命中的）：
#   1. --token <JWT>              命令行直传
#   2. LIVEKIT_TOKEN env 变量     ssh 时 export
#   3. /opt/livekit/.token 文件   板上本地存（gitignored）
# 任一缺失则后退到下一项；都没有则报错退出。
#
# 用法:
#   ./smoke.sh                    MPP H.264 双向硬件 (默认)
#   ./smoke.sh --no-mpp           关 MPP，走 OpenH264/libvpx 软 codec
#   ./smoke.sh --codec vp8        切 publish codec (h264/vp8/vp9/h265)
#   ./smoke.sh --token <JWT>      显式传 token
#   ./smoke.sh --bg               后台跑，日志写到 /tmp/smoke.log
#   ./smoke.sh --tail             追看最近一次后台跑的日志
#
# Ctrl-C 退出前台模式。
# 例: BOARD_LOOPBACK_MIC_GAIN=4 ./smoke.sh
set -e

# ---- 默认值 ----
URL="${LIVEKIT_URL:-wss://live.jusiai.com}"
TOKEN_FILE="/opt/livekit/.token"
USE_MPP=1
CODEC=h264
TOKEN=""
BG=0
LOG=/tmp/smoke.log

# ---- 参数解析 ----
while [ $# -gt 0 ]; do
  case "$1" in
    --no-mpp)   USE_MPP=0 ;;
    --mpp)      USE_MPP=1 ;;
    --codec)    CODEC="$2"; shift ;;
    --token)    TOKEN="$2"; shift ;;
    --url)      URL="$2"; shift ;;
    --bg)       BG=1 ;;
    --tail)
                if [ -f "$LOG" ]; then
                  tail -f "$LOG"
                else
                  echo "no log at $LOG"
                fi
                exit 0
                ;;
    --help|-h)  sed -n "1,/^set -e/p" "$0" | sed "s|^# \?||"; exit 0 ;;
    *) echo "unknown arg: $1"; exit 1 ;;
  esac
  shift
done

# ---- Token 解析 ----
if [ -z "$TOKEN" ] && [ -n "$LIVEKIT_TOKEN" ]; then
  TOKEN="$LIVEKIT_TOKEN"
fi
if [ -z "$TOKEN" ] && [ -f "$TOKEN_FILE" ]; then
  TOKEN=$(cat "$TOKEN_FILE")
fi
if [ -z "$TOKEN" ]; then
  echo "ERROR: no LIVEKIT_TOKEN provided." >&2
  echo "  - pass --token <JWT>, OR" >&2
  echo "  - export LIVEKIT_TOKEN, OR" >&2
  echo "  - write JWT to $TOKEN_FILE (chmod 600)" >&2
  echo "" >&2
  echo "  generate via host: lk token create --room <ROOM> --identity test-user-001 \\" >&2
  echo "                                       --join --valid-for 240h" >&2
  exit 1
fi

# ---- ES8389 mic PGA 硬件增益 + DAC 扬声器衰减 ----
# 跟 board-audio-setup.sh 一致；冗余 set 一遍兜底（即便 alsactl restore 失败）。
amixer -c 0 cset numid=39 9   > /dev/null 2>&1 || true  # ADCL PGA = +27 dB
amixer -c 0 cset numid=40 9   > /dev/null 2>&1 || true  # ADCR PGA = +27 dB
amixer -c 0 cset numid=48 171 > /dev/null 2>&1 || true  # DACL = -10 dB
amixer -c 0 cset numid=49 171 > /dev/null 2>&1 || true  # DACR = -10 dB

# ---- 杀残留 ----
pkill -f BoardLoopback 2>/dev/null || true
pkill -f weston       2>/dev/null || true
sleep 1

# ---- env ----
cd /opt/livekit
export LD_LIBRARY_PATH=/opt/livekit:/usr/lib
export LIVEKIT_URL="$URL"
export LIVEKIT_TOKEN="$TOKEN"
export BOARD_LOOPBACK_VIDEO_CODEC="$CODEC"
if [ "$USE_MPP" = "1" ]; then
  export BOARD_LOOPBACK_USE_MPP=1
else
  unset BOARD_LOOPBACK_USE_MPP
fi

# ---- run ----
echo "==== smoke ===="
echo "  url      = $URL"
echo "  codec    = $CODEC"
echo "  use_mpp  = $USE_MPP"
echo "  bg       = $BG"
echo "  log      = $LOG (only when --bg)"
echo "==============="

if [ "$BG" = "1" ]; then
  rm -f "$LOG"
  nohup stdbuf -o0 -e0 ./BoardLoopback > "$LOG" 2>&1 &
  PID=$!
  echo "started pid=$PID"
  echo "tail with: $0 --tail"
  echo "stop with: kill $PID"
else
  exec stdbuf -o0 -e0 ./BoardLoopback
fi
