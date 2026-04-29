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
#   ./smoke.sh                    MPP H.264 双向硬件, HD 720P30, AEC 开 (默认)
#   ./smoke.sh --res sd           切 360P30   (640x360 @ 30fps，16:9)
#   ./smoke.sh --res hd           切 720P30   (1280x720 @ 30fps，默认)
#   ./smoke.sh --res fhd          切 1080P25  (1920x1080 @ 25fps)
#   ./smoke.sh --rotate 0         关掉默认 +90° 补偿（其他硬件 / sensor 装正了的板）
#   ./smoke.sh --rotate 90        默认。补偿 ATK-DLRV1126B 摄像头 +90° sensor 偏角
#   ./smoke.sh --rotate 180       180° 翻转（如果板子装反了）
#   ./smoke.sh --rotate 270       逆时针 90°
#   ./smoke.sh --fit fill         不拉伸，aspect-fill 裁切（默认；4:3 源在 9:16 屏取中竖条）
#   ./smoke.sh --fit fit          aspect-fit letterbox 完整保留源 + 黑边
#   ./smoke.sh --fit stretch      历史强拉满屏（A/B 对照用）
#   ./smoke.sh --no-mpp           关 MPP，走 OpenH264/libvpx 软 codec
#   ./smoke.sh --codec vp8        切 publish codec (h264/vp8/vp9/h265)
#   ./smoke.sh                    AEC 默认开 (livekit::AudioProcessingModule AEC3+NS+HPF, 400ms delay)
#   ./smoke.sh --no-aec           关 AEC（A/B 对照或排查 AEC 误消语音）
#   ./smoke.sh --aec-delay 300    stream delay (默认 300ms，2026-04-28 sweep 验证)
#   ./smoke.sh --dac 155          DAC 喇叭音量 (默认 155=-18dB，AEC 收敛 sweet spot)
#   ./smoke.sh --token <JWT>      显式传 token
#   ./smoke.sh --http-only        不 auto-join，板子停在 HTTP wait 模式（测 /v1/meeting/join 用）
#   ./smoke.sh --bg               后台跑，日志写到 /tmp/smoke.log
#   ./smoke.sh --tail             追看最近一次后台跑的日志
#
# Ctrl-C 退出前台模式。
# 例: BOARD_LOOPBACK_MIC_GAIN=4 ./smoke.sh --res fhd
set -e

# ---- 默认值 ----
URL="${LIVEKIT_URL:-wss://live.jusiai.com}"
TOKEN_FILE="/opt/livekit/.token"
USE_MPP=1
CODEC=h264
RES=hd
# 远端推过来的画面在屏幕上如何贴：fill 默认（裁中央竖条不留黑边）/ fit
# letterbox（保留全部源，加黑边）/ stretch 历史拉伸。
FIT=fill
# DAC 喇叭播放音量（ES8389 numid=48/49，0..255；step 0.5 dB；191=0 dB ref）。
# 默认 155 (-18 dB) —— 2026-04-28 AEC 收敛 sweep 找到的 sweet spot。
# 压低喇叭 → 物理回声幅度变小 → AEC 有效延迟也跟着短 → 收敛更快、残余更小。
# Sweep 结果（每组同样的双向对话脚本，对端打分；推送音量在 155 时不退化）:
#   DAC 186 / delay 400  →  音爆（之前的旧默认，喇叭过响）
#   DAC 181-171 / 400    →  回声明显
#   DAC 171-165 / 350    →  低弱回声
#   DAC 165 / 300        →  开头有回声
#   DAC 161 / 300        →  开头有低弱回声
#   DAC 155 / 300        →  回声消除 ✅（最终默认）
# 其他硬件请按本套打分流程重新 sweep——AEC 最佳点跟回声路径耦合，不通用。
DAC=155
# ATK-DLRV1126B 摄像头 sensor 物理装 +90° 偏角，编码前要旋转 90° 才能让对端看到
# 直立画面。其他硬件可能不需要补偿，传 --rotate 0 关闭。
ROTATE=90
# Phase 7.4: 帧级 AEC (livekit::AudioProcessingModule)。开启后对每帧
# capture/playback 主动调 APM (AEC3 + NS + HPF)，让 AEC 看到完整双向
# 信号，消除扬声器→麦克风的回声循环。默认开启（双方外放体验提升明显）；
# --no-aec 可关掉做对照（验证之前的回声问题或排查 AEC 误消语音）。
#
# AEC_DELAY_MS 是 stream delay hint —— 跟 DAC 喇叭音量耦合：喇叭压得低，
# 物理回声路径短，最佳 delay 也跟着小。2026-04-28 sweep 找到的 sweet
# spot = (DAC=155, delay=300)，回声完全消除且对端听我们音量不退化。详见
# 上面 DAC 注释里的 sweep 表。换其他硬件 / 改了喇叭距离都要重新打分。
AEC=1
AEC_DELAY_MS=300
TOKEN=""
BG=0
LOG=/tmp/smoke.log
HTTP_ONLY=0
# Phase 8.4.4: BoardLoopback 现在默认起 HTTP API 在 0.0.0.0:8080，所有 POST
# 必须带 Authorization: Bearer <token>。如果用户没 export BOARD_API_TOKEN，
# 这里给一个 dev-mode 默认值方便板上自测；生产部署一定要改成强随机串。
BOARD_API_TOKEN="${BOARD_API_TOKEN:-board-dev-token-change-in-prod}"
BOARD_API_PORT="${BOARD_API_PORT:-8080}"

# ---- 参数解析 ----
while [ $# -gt 0 ]; do
  case "$1" in
    --no-mpp)   USE_MPP=0 ;;
    --mpp)      USE_MPP=1 ;;
    --codec)    CODEC="$2"; shift ;;
    --res)      RES="$2"; shift ;;
    --rotate)   ROTATE="$2"; shift ;;
    --fit)      FIT="$2"; shift ;;
    --aec)      AEC=1 ;;
    --no-aec)   AEC=0 ;;
    --aec-delay) AEC_DELAY_MS="$2"; shift ;;
    --dac)      DAC="$2"; shift ;;
    --token)    TOKEN="$2"; shift ;;
    --url)      URL="$2"; shift ;;
    --bg)       BG=1 ;;
    --http-only) HTTP_ONLY=1 ;;
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
# --http-only 模式不需要 token：板子停在 HTTP wait，由外部 POST /v1/meeting/join 投。
if [ "$HTTP_ONLY" != "1" ]; then
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
    echo "  - write JWT to $TOKEN_FILE (chmod 600), OR" >&2
    echo "  - use --http-only to skip auto-join and wait for HTTP /join" >&2
    echo "" >&2
    echo "  generate via host: lk token create --room <ROOM> --identity test-user-001 \\" >&2
    echo "                                       --join --valid-for 240h" >&2
    exit 1
  fi
fi

# ---- ES8389 mic PGA 硬件增益 + DAC 扬声器衰减 ----
# 跟 board-audio-setup.sh 一致；冗余 set 一遍兜底（即便 alsactl restore 失败）。
amixer -c 0 cset numid=39 9   > /dev/null 2>&1 || true  # ADCL PGA = +27 dB
amixer -c 0 cset numid=40 9   > /dev/null 2>&1 || true  # ADCR PGA = +27 dB
amixer -c 0 cset numid=48 "$DAC" > /dev/null 2>&1 || true  # DACL
amixer -c 0 cset numid=49 "$DAC" > /dev/null 2>&1 || true  # DACR

# ---- 杀残留 ----
pkill -f BoardLoopback 2>/dev/null || true
pkill -f weston       2>/dev/null || true
sleep 1

# ---- env ----
cd /opt/livekit
export LD_LIBRARY_PATH=/opt/livekit:/usr/lib
# --http-only：故意不 export LIVEKIT_URL/TOKEN，BoardLoopback 启动时检测不到
# env 就停在 HTTP wait 模式等 POST /v1/meeting/join 投。
if [ "$HTTP_ONLY" != "1" ]; then
  export LIVEKIT_URL="$URL"
  export LIVEKIT_TOKEN="$TOKEN"
else
  unset LIVEKIT_URL
  unset LIVEKIT_TOKEN
fi
export BOARD_LOOPBACK_VIDEO_CODEC="$CODEC"
export BOARD_LOOPBACK_VIDEO_RES="$RES"
export BOARD_LOOPBACK_VIDEO_ROTATE="$ROTATE"
export BOARD_LOOPBACK_VIDEO_FIT="$FIT"
if [ "$AEC" = "1" ]; then
  export BOARD_LOOPBACK_AEC=1
  export BOARD_LOOPBACK_AEC_DELAY_MS="$AEC_DELAY_MS"
else
  unset BOARD_LOOPBACK_AEC
  unset BOARD_LOOPBACK_AEC_DELAY_MS
fi
if [ "$USE_MPP" = "1" ]; then
  export BOARD_LOOPBACK_USE_MPP=1
else
  unset BOARD_LOOPBACK_USE_MPP
fi
export BOARD_API_TOKEN="$BOARD_API_TOKEN"
export BOARD_API_PORT="$BOARD_API_PORT"

# ---- run ----
echo "==== smoke ===="
echo "  url      = $URL"
echo "  codec    = $CODEC"
echo "  res      = $RES (sd=360P30 / hd=720P30 / fhd=1080P25)"
echo "  rotate   = $ROTATE (deg, MPP hardware rotation)"
echo "  fit      = $FIT (fill=crop center / fit=letterbox / stretch=historic)"
echo "  use_mpp  = $USE_MPP"
echo "  aec      = $AEC (delay=${AEC_DELAY_MS}ms; livekit::AudioProcessingModule AEC3+NS+HPF)"
echo "  dac      = $DAC (ES8389 numid=48/49; 155=-18dB default / 191=0dB ref)"
echo "  api      = http://0.0.0.0:$BOARD_API_PORT (Bearer auth required for POST)"
echo "  http_only= $HTTP_ONLY (1=wait for HTTP /join, no auto-fire)"
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
