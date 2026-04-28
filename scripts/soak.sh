#!/bin/sh
# Phase 8.2 长跑 soak harness（板上跑）。
#
# 部署：scp scripts/soak.sh rv1126b-board:/opt/livekit/soak.sh
#
# 用法（板子前台跑，后台 BoardLoopback 出错时 harness 主进程会立刻看到）：
#   /opt/livekit/soak.sh 4         # 跑 4 小时然后自动停 + 出报告
#   /opt/livekit/soak.sh 24        # 跑 24 小时
#   /opt/livekit/soak.sh 4 --bg    # harness 自己也后台，nohup 退出 ssh 也继续
#
# 期间任何信号（Ctrl-C / SIGTERM）会清干净 BoardLoopback 再退。
#
# 产出：
#   /tmp/smoke.log       BoardLoopback 自己的 stdout/stderr（含周期 stat 行）
#   /tmp/soak_stats.csv  每 5 min 的 (rss, vm, fd, threads, cpu)
#   /tmp/soak_report.txt 结束时的汇总报告
#
# 前置：peer（电脑浏览器 LiveKit demo）已加入房间在推视频/音频，否则 receive
# 侧（decoder / DRM render / AEC reverse）无负载，长跑暴露 bug 能力打折。
set -e

DURATION_HOURS="${1:-4}"
INTERVAL_SEC=300  # 5 分钟采一次
LOG=/tmp/smoke.log
CSV=/tmp/soak_stats.csv
REPORT=/tmp/soak_report.txt

# --- 处理 --bg：把 harness 自己也 nohup 化 ---
if [ "$2" = "--bg" ]; then
  HARNESS_LOG=/tmp/soak_harness.log
  echo "[soak] backgrounding harness, log=$HARNESS_LOG"
  nohup "$0" "$DURATION_HOURS" </dev/null >"$HARNESS_LOG" 2>&1 &
  echo "[soak] harness pid=$!"
  echo "[soak] tail -f $HARNESS_LOG"
  exit 0
fi

START_TS=$(date +%s)
END_TS=$((START_TS + DURATION_HOURS * 3600))

echo "[soak] started at $(date) — duration ${DURATION_HOURS}h"
echo "[soak] CSV    = $CSV"
echo "[soak] LOG    = $LOG"
echo "[soak] REPORT = $REPORT (final)"

# 启动 BoardLoopback（用最新默认 DAC=155, delay=300）
/opt/livekit/smoke.sh --bg

# 等 PID 出现
sleep 3
PID=$(pgrep -f BoardLoopback | head -1)
if [ -z "$PID" ]; then
  echo "[soak] FATAL: BoardLoopback failed to start (check $LOG)"
  tail -30 "$LOG" || true
  exit 1
fi
echo "[soak] BoardLoopback pid=$PID"

# Trap：harness 被打断时清掉 BoardLoopback
cleanup() {
  echo "[soak] cleanup at $(date)"
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    kill -TERM "$PID" 2>/dev/null || true
    sleep 5
    kill -KILL "$PID" 2>/dev/null || true
  fi
}
trap cleanup INT TERM

# CSV header
echo "iso,elapsed_s,rss_kb,vmsize_kb,threads,fd,cpu_pct" > "$CSV"

# 主循环：每 5 min 采一次
while [ "$(date +%s)" -lt "$END_TS" ]; do
  if ! kill -0 "$PID" 2>/dev/null; then
    echo "[soak] !!! BoardLoopback died at $(date) after $(( $(date +%s) - START_TS ))s"
    break
  fi

  TS=$(date +%s)
  ELAPSED=$((TS - START_TS))
  ISO=$(date '+%Y-%m-%dT%H:%M:%S')

  RSS=$(awk '/^VmRSS:/{print $2}' /proc/$PID/status 2>/dev/null || echo 0)
  VMSZ=$(awk '/^VmSize:/{print $2}' /proc/$PID/status 2>/dev/null || echo 0)
  THR=$(awk '/^Threads:/{print $2}' /proc/$PID/status 2>/dev/null || echo 0)
  FD=$(ls /proc/$PID/fd 2>/dev/null | wc -l)
  CPU=$(ps -p $PID -o %cpu= 2>/dev/null | tr -d ' ' || echo 0)

  echo "$ISO,$ELAPSED,$RSS,$VMSZ,$THR,$FD,$CPU" >> "$CSV"
  printf '[soak] T+%6ds  rss=%sKB  vm=%sKB  thr=%s  fd=%s  cpu=%s%%\n' \
         "$ELAPSED" "$RSS" "$VMSZ" "$THR" "$FD" "$CPU"

  sleep "$INTERVAL_SEC"
done

# 到点 / 提前死亡 → 出报告
echo "[soak] stopping BoardLoopback (sigterm) at $(date)"
if kill -0 "$PID" 2>/dev/null; then
  kill -TERM "$PID" 2>/dev/null || true
  sleep 5
  kill -KILL "$PID" 2>/dev/null || true
fi
trap - INT TERM

{
  echo "==== Phase 8.2 soak report ===="
  echo "started : $(date -d "@$START_TS" 2>/dev/null || echo "ts=$START_TS")"
  echo "stopped : $(date)"
  echo "planned : ${DURATION_HOURS}h"
  echo "actual  : $(( $(date +%s) - START_TS ))s"
  echo
  echo "---- 内存（KB）----"
  awk -F, 'NR==2{f3=$3; f4=$4} NR>1{if($3>m3)m3=$3; if($4>m4)m4=$4; l3=$3; l4=$4}
           END{
             printf "  rss    first=%s  last=%s  max=%s  delta=%s\n", f3, l3, m3, l3-f3
             printf "  vmsize first=%s  last=%s  max=%s  delta=%s\n", f4, l4, m4, l4-f4
           }' "$CSV"
  echo
  echo "---- fd / threads ----"
  awk -F, 'NR==2{f6=$6; f5=$5} NR>1{if($6>m6)m6=$6; l6=$6; l5=$5}
           END{
             printf "  fd      first=%s  last=%s  max=%s  delta=%s\n", f6, l6, m6, l6-f6
             printf "  threads first=%s  last=%s  delta=%s\n", f5, l5, l5-f5
           }' "$CSV"
  echo
  echo "---- CPU 中位数（粗算）----"
  awk -F, 'NR>1{a[NR-1]=$7; n++} END{
             # 简单 sort 选中位
             for(i=1;i<=n;i++) for(j=i+1;j<=n;j++) if(a[i]>a[j]){t=a[i];a[i]=a[j];a[j]=t}
             printf "  cpu median=%s%% (samples=%d)\n", a[int(n/2)+1], n
           }' "$CSV"
  echo
  echo "---- ALSA underrun / dropped 累计（smoke.log 周期 stat 行）----"
  grep 'alsa_underruns=' "$LOG" | tail -5 | sed 's/^/  /'
  echo
  echo "  独立 underrun 事件总数 (grep '\bunderrun\b' 计数):"
  echo "    $(grep -c '\bunderrun\b' "$LOG" 2>/dev/null || echo 0)"
  echo
  echo "---- final 行（如果 BL 自己 print 了 final 总数）----"
  grep -E '^final' "$LOG" 2>/dev/null | tail -3 | sed 's/^/  /' || true
  echo
  echo "---- ERROR / FATAL log 行 ----"
  grep -cE 'ERROR|FATAL|panic|abort' "$LOG" 2>/dev/null | sed 's/^/  count=/' || true
  echo
  echo "  样本（最多 10 行）:"
  grep -E 'ERROR|FATAL|panic|abort' "$LOG" 2>/dev/null | head -10 | sed 's/^/    /' || true
  echo
  echo "==== 详细数据 ===="
  echo "  CSV:  $CSV"
  echo "  LOG:  $LOG"
} > "$REPORT"

cat "$REPORT"

echo
echo "[soak] DONE. report=$REPORT"
