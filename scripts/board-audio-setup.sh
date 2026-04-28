#!/bin/sh
# 一次性板上 ALSA mixer 配置：拉 ES8389 mic PGA + 调 DAC playback 到合适音量
# + 持久化到 /var/lib/alsa/asound.state（重启自动恢复）。
#
# 用法：把脚本 scp 到板子，root 跑一次：
#   scp scripts/board-audio-setup.sh rv1126b-board:/tmp/
#   ssh rv1126b-board /tmp/board-audio-setup.sh
#
# BoardLoopback 的 smoke.sh 也在每次启动时再 set 一次 PGA 兜底。
#
# 调音参数：
#
#   PGA  (mic 模拟前置增益, numid=39/40)
#     范围 0-14, step +3 dB (0=0dB, 14=42dB)
#     默认 9 = +27 dB，板载 mic 实测远端能听清正常说话音量
#     嫌响→ env PGA=8 ./board-audio-setup.sh
#     嫌轻→ env PGA=10 ./board-audio-setup.sh
#
#   DAC  (扬声器 playback 音量, numid=48/49)
#     范围 0-255, step 0.5 dB, base -95.5 dB
#     191 = 0 dB (unity), 171 = -10 dB, 151 = -20 dB
#     默认 186 = -2.5 dB
#     嫌响→ env DAC=181 ./board-audio-setup.sh   (-5 dB)
#     嫌轻→ env DAC=191 ./board-audio-setup.sh   (0 dB, unity)
set -e

PGA=${PGA:-9}
DAC=${DAC:-186}

amixer -c 0 cset numid=39 "$PGA" > /dev/null  # ADCL PGA Volume (mic +N*3 dB)
amixer -c 0 cset numid=40 "$PGA" > /dev/null  # ADCR PGA Volume

amixer -c 0 cset numid=48 "$DAC" > /dev/null  # DACL Playback Volume
amixer -c 0 cset numid=49 "$DAC" > /dev/null  # DACR Playback Volume

# 持久化到 /var/lib/alsa/asound.state，重启后由 init 自动恢复
alsactl store

PGA_DB=$((PGA * 3))
# DAC 是 0.5 dB step + base -95.5 dB；shell 里没浮点，用 *5 / 10 - 955/10 近似
DAC_DB=$(( (DAC * 5 - 955) / 10 ))
echo "[board-audio-setup] mic  PGA = $PGA  (+${PGA_DB} dB analog pre-ADC)"
echo "[board-audio-setup] spk  DAC = $DAC  (~${DAC_DB} dB playback)"
echo "[board-audio-setup] persisted to /var/lib/alsa/asound.state"
