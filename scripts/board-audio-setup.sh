#!/bin/sh
# 一次性板上 ALSA mixer 配置：拉 ES8389 mic 的 PGA 模拟增益 + 持久化。
#
# 用法：把脚本 scp 到板子，root 跑一次：
#   scp scripts/board-audio-setup.sh rv1126b-board:/tmp/
#   ssh rv1126b-board /tmp/board-audio-setup.sh
#
# 这一行 alsactl store 会写到 /var/lib/alsa/asound.state，重启后自动恢复。
# BoardLoopback 的 smoke.sh 也每次启动时再 set 一次兜底。
#
# 调音：
#   - PGA Volume 范围 0-14，每档 +3 dB（0=0dB, 14=42dB）。
#   - 默认 9 = +27 dB，板载 mic 实测远端能听清正常说话音量。
#   - 嫌响→调小一档：amixer -c 0 cset numid=39 8; amixer -c 0 cset numid=40 8
#   - 嫌轻→调大一档：amixer -c 0 cset numid=39 10; amixer -c 0 cset numid=40 10
#   - 改完别忘 `alsactl store` 持久化。
set -e

PGA=${PGA:-9}

amixer -c 0 cset numid=39 "$PGA" > /dev/null  # ADCL PGA Volume
amixer -c 0 cset numid=40 "$PGA" > /dev/null  # ADCR PGA Volume

# 持久化到 asound.state
alsactl store

echo "[board-audio-setup] ES8389 mic PGA = $PGA  (+$(($PGA * 3)) dB)"
echo "[board-audio-setup] persisted to /var/lib/alsa/asound.state"
