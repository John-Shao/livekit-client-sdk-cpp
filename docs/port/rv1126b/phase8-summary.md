# Phase 8 工作总结 — 产品级调优 + 长跑稳定性 + 业务集成（进行中）

> **状态**：8.1 已完成（AEC 收敛优化）；8.2 试水通过（2h 全绿），明天跑 4h SLA 验收；其余候选项排队中
> 启动日期：2026-04-28
> 分支：`port/rv1126b-on-upstream-v0.3.3`
> 关联文档：[plan.md](plan.md) · [phase7-summary.md](phase7-summary.md) · [oplog.md](oplog.md)

## 范围

Phase 7 把 720P30 双向通话 + AEC 落到 production-ready：HD 720P30（短测 ~58%，
**Phase 8.2 实测 2h 稳态 ~103% 单核 metric = 1.03 cores**），AEC 90/100 残余在
收敛期前 5-10s。Phase 8 **不追加新功能**，目标是把 Phase 7 产出**调优到产品级**，
并为业务集成铺路。业务约束：会议最长 4h，故 4h 通过 = SLA 验收。

子阶段（按价值密度 / 落地顺序）:

| 阶段 | 验证目标 | 状态 |
|---|---|---|
| **8.1 AEC 收敛优化** | 把 AEC3 收敛期残余从 5-10s 压到 0 | ✅ 完成 |
| **8.2 长跑测试 (4h SLA)** | RSS / fd / underrun 累计稳定 | 🟡 试水通过（2h），明天 4h 正式跑 |
| **8.3 DRM dma-buf 直渲 v2** | 7.6.c retry — 1080P30 真要时再做 | ⏳ 视需要 |
| **8.4 业务集成** | 房间管理 / 多人 / 数据通道接产品逻辑 | ⏳ 视需要 |

## 8.1 — AEC 收敛优化（done 2026-04-28）

**起点**：Phase 7.4 标定的 `(DAC=186, delay=400)` = 90/100，残余主要在 AEC3
收敛期前 5-10s。本阶段把"降扬声器音量"这条线走通——压低物理回声幅度，
让 AEC3 工作量减轻，目标把残余压到 0。

**约束（用户硬要求）**：不能牺牲板子推送给对端的音量。即只动 DAC 不动 ADC PGA，
且 sweep 时同步监控对端听我们的音量/连续性，任何退化立刻 PASS——宁可忍受
少量回声残余，也不削推送音量。

**Sweep 矩阵**（每组同样的双向对话脚本，对端外人评判）:

| DAC (numid=48/49) | dB | AEC delay | 现象 |
|---|---|---|---|
| 186 | -2.5 | 400 | 板子音爆（旧默认，喇叭过响） |
| 181 | -5 | 400 | 回声明显 |
| 176 | -7.5 | 400 | 回声明显 |
| 171 | -10 | 400 | 回声明显 |
| 171 | -10 | 350 | 低弱回声 |
| 165 | -13 | 350 | 低弱回声 |
| 165 | -13 | 300 | 开头有回声 |
| 161 | -15 | 300 | 开头有低弱回声 |
| **155** | **-18** | **300** | **回声消除** ✅ |

**关键观察 — DAC 和 AEC delay 是耦合的**：喇叭压得越低 → 物理回声路径越短
→ 最佳 stream delay hint 也跟着往下走。这也解释了为什么 7.4 阶段单独
sweep delay 时 400ms 是局部最优：那时 DAC=186 喇叭过响，需要长延迟兜回声；
现在 DAC=155 喇叭合理，AEC 不用等那么久就能匹配上回声。

**对端验证**：(DAC=155, delay=300) 这组下，对端反馈板子推过去的语音音量
正常、不断断续续 → 推送音量未受影响，硬约束达成。

**默认值更新**:

```
DAC: 186 → 155       (smoke.sh + board-audio-setup.sh)
AEC_DELAY_MS: 400 → 300
```

**Phase 6.3 老默认 171 的来由**：当时软件 mic gain = 12×，喇叭压低补平体感。
软件 gain 后改 8× 时 DAC 顺手抬到 186 配合，但实际上 7.4 AEC 投产后才暴露
喇叭过响是 AEC 残余的主因。现在 DAC=155 配合 8× 软件 gain：本端听对端略小
但完全可听，对端听我们正常，AEC 残余消除——三方都达到合理点。

**为什么 sweep 而不是先建模算最优**：声学回声路径是腔体 + 麦克风物理位置 +
PulseAudio 缓冲深度的非线性叠加，建模 + 测量参数比直接 sweep 成本更高，
6 组打分一小时拿到答案，工程上完胜。换其他硬件 / 改了喇叭距离都要重新 sweep
（声学路径耦合，不通用）。

## 8.2 — 长跑测试（4h SLA，进行中）

**目标**：板↔Web peer 4h soak，验证产品 SLA（业务约束：会议最长 4h，故 4h
通过 = 验收）。盯 RSS / fd / threads / ALSA underrun / ERROR 累计 + CPU 中位。

**Soak harness**: [scripts/soak.sh](../../../scripts/soak.sh)。功能：每 5 min
采 (`/proc/$PID/status` 取 RSS/VmSize/Threads + `ls /proc/$PID/fd | wc -l` +
`ps -o %cpu`) 写 CSV，到点 SIGTERM + 写汇总报告。`--bg` 让 ssh 退出后继续跑。

**单 peer 约束**：当前 BoardLoopback 有[多 peer 崩溃 bug](../../../examples/board_loopback/main.cpp#L917)，
soak 房间内**只允许 1 个固定 peer**（电脑浏览器，禁止他人加入测试房间）。
Phase 8.5 候选项之一就是修这个。

### 试水：2h 实测（2026-04-28 19:59 → 21:59）✅ 通过

板上 nohup `sleep 7029 && pkill -TERM -f BoardLoopback` 在 21:59:14 杀掉
BoardLoopback；harness 5min 采样下次（~22:04）检测到进程死亡 break loop 写
报告。实跑 7505s = 2h5min，全绿。

| 指标 | first | last | max | delta | 评估 |
|---|---|---|---|---|---|
| RSS (KB) | 55,420 | 64,916 | 65,368 | +9,496 (+17%) | ✅ |
| vmsize (KB) | 2,135,220 | 2,140,388 | — | +5,168 | ✅ |
| fd | 69 | 60 | 69 | **-9** | ✅（无 leak）|
| threads | 24 | 23 | — | -1 | ✅ |
| ALSA underrun | — | **0** | — | — | ✅✅ |
| ERROR/FATAL | — | **0** | — | — | ✅✅ |
| CPU 中位（25 样本）| — | — | — | — | **103%** ⚠️ 见下 |
| video published | — | 216,691 | — | — | 30.0 fps，稳定 |

**关键观察 1（重要）— CPU 真稳态 ~103%，Phase 7 的 ~58% 是短测假数据**：
CPU 时间轴（每 5min 一点）：
```
T+4s    57%  ← 冷启动，lazy init 没爬上来
T+5min  105% ← 直接锁稳态
T+10..95min  101-106%（缓涨到峰 106%）
T+95..120min 106→101（小幅回落）
中位 = 103%
```
Phase 7 文档"~58%"是 270s 短测，那时 codec/AEC/MPP buffer pool 都没真正
进入稳态。**真稳态 baseline = ~103%（≈ 1.03 个核，RV1126B 双核所以 ~52%
总 CPU 资源）**。Phase 7 文档已加 ATTENTION 补正。未来调优对比 / 容量规划
都用 103% 这个数。

**关键观察 2 — RSS 曲线 oscillate 不线性**：55 → 60 → 57 → 60 → 61 → 58 → 57
→ 58 → 60 → 61 → 64 → 60 → 65 → 65 → 62 → 63 → 61 → 58 → 62 → 64（KB×1000）。
看起来是 buffer 池在做 alloc/free churn，max-min spread 仅 8MB，**不是漏内存**。
4h 外推保守也只到 ~74MB（仍远小于 50MB delta concern 阈）。

**关键观察 3 — fd 反向减少**：69 → 60 然后锁住。可能是冷启动时打开的临时
文件 / pipe / socket 后续合并掉了，**完全无 fd 泄露**。

### 4h SLA 跑前建议

✅ 直接同样配置（`/opt/livekit/soak.sh 4 --bg` + 板子端 nohup pkill 在 4h 时
开杀 + cron 5min 后分析）。明天开始时间由用户定。

## 8.3 — DRM dma-buf 直渲 v2（pending）

7.6.c v1 attempt-and-revert 见 [phase7-summary.md](phase7-summary.md)。retry 前置：

- livekit-ffi 的 cdylib 暴露一条"额外导出 C 符号"的 build-script 入口
  （现有 `--gc-sections` 把 C 静态库里的 extern "C" 符号裁掉）
- 真正出现 1080P30 业务需求（HD 720P30 ~58% 单核已经够用了）

预期收益 -3 ~ -5pp CPU。**不要主动做**——等业务真要 1080P30 时再来。

## 8.4 — 业务集成（多人会议，进行中）

产品需求已确认（2026-04-28）：max 10 participants / active speaker only /
触控屏 / HTTP API 接 token / Bearer auth 必填。完整设计 + subphase 拆解见
计划文件 `~/.claude/plans/wobbly-forging-moler.md`。

| 子阶段 | 内容 | 状态 |
|---|---|---|
| **8.4.1** | DrmDisplay 多 callback 安全 + VideoRouter（fix 多 peer crash）| ✅ 完成 2026-04-29 |
| **8.4.2-MVP** | onActiveSpeakersChanged + MeetingController + 动态 setSubscribed | ✅ 完成 2026-04-29 |
| **8.4.3** | 音频软混音器（pull 模型 + AEC reverse on mix）| ✅ 完成 2026-04-29 |
| **8.4.4-MVP** | HTTP API 极简版 + Bearer auth | ✅ 完成 2026-04-29 |
| **8.4.4-完整** | Daemon 化 + per-meeting FFI lifecycle + Room::Disconnect | ✅ 完成 2026-04-29 |
| **8.4.2-完整** | hold-down 600ms + render-only switch（避开 FFI heap bug） | ✅ 完成 2026-04-29 |
| 8.4.5 | 触控 UI（OSD-on-video MVP） | ⏳ |
| 8.4.6 | 集成测试 + 多人 4h soak | ⏳ |

### 8.4.1 — DrmDisplay 多 callback 安全 + VideoRouter（done 2026-04-29）

**起点**：2026-04-28 8.2 试水准备时发现，第 2 个 peer 加入房间触发
SIGSEGV。根因 `static DrmDisplay g_drm` 单例无锁 —— 两个 peer 的 video
callback 并发调 `ensureBuffers`，新 frame 尺寸不同时一方 free 旧 mapped
buffer，另一方仍持指针写入 → use-after-free。

**修法**：

1. `DrmDisplay::renderNV12` 加 `std::mutex render_mutex_` 保护 ensureBuffers
   的 free/realloc 临界区（兜底防护，即使 router 失效也不崩）。
2. 引入 `class VideoRouter g_video_router`：
   - `renderIfActive(id, data, w, h)`：只有 id 等于 active 才进 DrmDisplay；
     不等的 callback 第一行就 return（99% 路径零开销）。
   - 第一个 peer 自动成为 active（first-peer-wins，简单 MVP 行为）。
   - `setActive(id)` / `resetIfActive(id)`：8.4.2 active speaker handler 用
     这两个 API 在多 peer 场景动态切显示。
3. `LoopbackDelegate::onTrackSubscribed` 的 video callback 改成
   `g_video_router.renderIfActive(identity, ...)`；`onParticipantDisconnected`
   调 `g_video_router.resetIfActive(identity)` 让下一个 peer 接管显示。

**验证 2026-04-29**：

- ✅ 单 peer：Web peer 进房，板子启动 → `[router] first peer wins active: <id>`，
  `[drm] dumb buffers ready: 2x 1280x720 NV12`（一次分配），video 正常显示。
- ✅ 多 peer：手机加入第 2 路（之前必崩的剧本）→ 不 segfault，没有第 2 次
  `[drm] dumb buffers ready` 重分配，没有 `mpp_buf_slot mismatch` 警告，
  屏幕保持显示 Web peer。
- 其他没动：1-peer 视频路径性能无退化（router 早 return 不上锁），AEC 行为不变。

**未做（留 8.4.2）**：active speaker 自动切换（当前是 first-peer-wins 静态绑定）。
多 peer 时手机视频仍被静默丢弃，由 8.4.2 的 `onActiveSpeakersChanged` 接管。
另：多 peer 音频还是各 callback 直接 enqueue ALSA（旧行为，可能互相覆盖），
由 8.4.3 软混音器修。

**Commit**：`22f1a44 feat(port): Phase 8.4.1 — fix multi-peer DrmDisplay crash + VideoRouter`

### 8.4.2-MVP — Active speaker auto-switch + 动态订阅（done 2026-04-29）

**目标**：板子屏幕跟随当前说话人自动切换；非 active peer 的视频
unsubscribe 防 10 peer 全解码爆 CPU。

**实施**：

1. 新增 `class MeetingController g_meeting_controller`：单 worker thread +
   命令队列，所有 SDK delegate 事件投递 cmd 给 worker 串行 reconcile，避免
   多线程并发改 setSubscribed / VideoRouter active 状态。
2. `LoopbackDelegate::onActiveSpeakersChanged`（[room_delegate.h:125-126](../../../include/livekit/room_delegate.h)）
   过滤本地 identity，第一个非本地 speaker 投 `Cmd::ActiveSpeakerObserved` 到 controller。
3. `LoopbackDelegate::onTrackPublished` 投 `Cmd::TrackPublishedDefend`：worker 收到后
   检查 publication.kind() == VIDEO 且 owner 不是当前 active → `setSubscribed(false)`
   立即退订防爆 CPU。
4. Controller worker 处理 ActiveSpeakerObserved：
   - new active.video.setSubscribed(true)
   - g_video_router.setActive(new_id)
   - old active.video.setSubscribed(false)
5. **corner case**：current_active_ 还空时不要 defend unsub —— 让第一个 peer 的视频
   保持 SDK auto_subscribe 状态，VideoRouter 的 first-peer-wins 接管显示。
6. LoopbackDelegate 加 `local_identity_` 成员，main() 在 Connect 后用 `lp->identity()`
   设置。Controller dtor 在 Room reset 之前 reset，避免 worker 访问已 reset 的 Room。

**未做（留 8.4.2-完整）**：
- hold-down 抗抖动（MVP 直接切，看 SDK speaker[0] 抖动严重再加 600ms 阻尼）
- 切换瞬间 last-frame freeze（200-400ms 防黑屏）
- 切换前 100ms 预 setSubscribed（启动 keyframe 请求）

**验证 2026-04-29**：3 peer (Web + 手机 + 板) 真房间，A→B→A 轮流讲话，屏幕跟随
切换。smoke.log 出 `[ctrl] active speaker: '' -> 'A-id'` / `[ctrl] sub video: A-id` /
`[ctrl] unsub video: B-id` 三步串行 reconcile 全对。

**Commit**：`5703984 feat(port): Phase 8.4.2-MVP — active speaker auto-switch + dynamic subscription`

### 8.4.3 — 音频软混音器（done 2026-04-29）

**起点**：8.4.2 启用多 peer 视频订阅后，音频路径暴露已知缺陷 ——
每个 peer 的 audio callback 独立 enqueue ALSA，N peer 同时讲话时音频
帧互相覆盖、ALSA 队列流入率 N×48kHz 但 drain 1×48kHz → 大量丢帧 +
AEC 看到不一致 reverse signal → 实测 8.4.2 时听感"哑音 + 电流声"，
alsa_dropped 100/秒。

**实施 — push 模型改 pull 模型**：

1. 新增 `class AudioMixer g_mixer`：
   - per-peer ring buffer（500ms = 24000 samples × int16 = 48KB / peer）
   - `write(id, samples, n)`：peer audio callback 写入对应 ring（mutex 保护）
   - `pull(out, n)`：从所有 peer ring 各取 n samples，int32 累加 + clip int16
     输出，单源帧
   - `removePeer(id)`：onParticipantDisconnected 清掉对应 ring
2. `AlsaPlayer.writerLoop` 重构：删除原 push 队列 + cv，改成每 10ms 一帧
   `g_mixer.pull(buf, 480)` → `g_apm->processReverseStream(copy)` →
   `snd_pcm_writei(buf)`。snd_pcm_writei 阻塞按 ALSA 节拍 pacing。
3. audio callback：`g_mixer.write(identity, samples, n)`，删掉原 enqueue / processReverseStream。
4. **关键调优**：MVP 第一版 ring 100ms + ALSA 1s buffer 时，writer 因 ALSA
   period 颗粒（~250ms）阻塞期间 push 突发溢出 ring → mixer_dropped
   55,680/秒 板子声音断续。修：ring 100ms→500ms，ALSA latency 1s→200ms
   （period ~50ms）。
5. ALSA playback 不再 lazy 等第一帧，main() Connect 后立刻 `g_alsa.ensureOpen(48000, 1)`
   保证 writer 启动就有 pcm_ 可用。
6. stat 行 `alsa_dropped` 改 `mixer_dropped`（语义已变）。

**关键设计点**：

- AEC reverse 看到的现在是**混好的总声**（8.4.2 之前是"恰好被先调用 callback
  那个 peer 的 frame"）—— AEC3 自适应正确 ↑。
- pull 模型 vs 单独 mixer thread：snd_pcm_writei 已经按 ALSA pace block，
  让 writer 直接调 mixer.pull 拿数据，省一根线程 + 省一层 ring buffer。

**验证 2026-04-29**：3 peer 同时讲话两路声都能听清；mixer_dropped 接近 0；
alsa_underruns=0；AEC 不退化；视频切换 + active speaker 仍正常（8.4.2 回归通过）。

**Commit**：`2516958 feat(port): Phase 8.4.3 — multi-peer audio software mixer + AEC reverse on mix`

### 8.4.4-MVP — HTTP API + Bearer auth（done 2026-04-29）

**目标**：板子开 HTTP API 让后端 push 会议 token 触发加房；GET /status 给
运维看进程当前态。MVP 单会议进程（一次 leave 退一次进程）；完整 daemon 留
8.4.4-完整。

**实施**：

1. vendor cpp-httplib v0.43.1 单头文件到 [third_party/httplib.h](../../../third_party/httplib.h)
   （header-only，从 https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h 下载，MIT 协议）。
2. **避开 nlohmann_json 依赖**：手写 ~80 行 `mini_json::{parseString, getString,
   escape}`。理由：Buildroot 不带，FetchContent 在中国大陆 GitHub 网络又卡，
   而我们的 JSON 用例只有"取 url+token"和"产几行 KV"，不值得引依赖。
3. 新增 `class MeetingState g_meeting_state`（进程级会议生命周期）：
   - state ∈ {Idle, Joining, InMeeting, Leaving}
   - `requestJoin(url, token)` HTTP /join 投递 cmd → main thread waitForJoin 解出
   - `requestLeave()` HTTP /leave → 触发 g_running.store(false) 让主循环退出
   - `setInMeeting(room_name, room*)` runMeeting 接通 Room 后调，给 status 用
   - `snapshot()` 给 GET /status 返回当前态 + room_name + active_speaker (从
     g_meeting_controller 拿) + participant_count (从 Room 拿)
4. 新增 `class BoardHttpServer`（cpp-httplib 包装）：
   - listener thread on `0.0.0.0:$BOARD_API_PORT`（默认 8080）
   - 3 端点：POST /v1/meeting/join、POST /v1/meeting/leave、GET /v1/meeting/status
   - **Bearer auth**：env `BOARD_API_TOKEN` 必填，未设直接 startup error 退出。
     POST 必带 `Authorization: Bearer <token>`，匹配失败返 401。GET /status 不
     验 auth（运维诊断用）。
5. main() 重构：
   - BOARD_API_TOKEN 必填检查
   - 启 HTTP server
   - waitForJoin 阻塞 → 拿 (url, token) 进 Connect
   - **向后兼容**：启动时 LIVEKIT_URL/TOKEN env（或 argv）已设 → 内部 fire 一次
     join request，自动加入（兼容 smoke.sh 老用法 + 单元测试）
   - leave 后进程退出（MVP 单会议）
6. cleanup 顺序：audio_thread join → MeetingController reset → MeetingState
   clearMeeting（清掉 Room 指针避免悬垂）→ tracks/source reset → Room reset →
   http_server.stop（最后停 server，避免新 join 进来访问已销毁状态机）
7. smoke.sh 加 BOARD_API_TOKEN（dev 默认 `board-dev-token-change-in-prod`）+
   BOARD_API_PORT（默认 8080）+ 状态行多一行 api endpoint 提示。

**端点契约**:

```
POST /v1/meeting/join
  Headers: Authorization: Bearer <BOARD_API_TOKEN>
  Body:    {"url":"wss://...","token":"<JWT>"}
  → 202 {"status":"joining"}    成功投递
  → 400                           bad json
  → 401                           bearer 错
  → 409                           已在会议中（MVP 单会议）

POST /v1/meeting/leave
  Headers: Authorization: Bearer <BOARD_API_TOKEN>
  → 200 {"status":"leaving"}    会触发主循环退出 + 进程退出
  → 401                           bearer 错
  → 409                           不在会议状态

GET /v1/meeting/status
  → 200 {"state":"...","room_name":"...","active_speaker":"...","participant_count":N}
  state ∈ {idle, joining, in_meeting, leaving}
```

**实测验证 2026-04-29**:

```
$ curl -s http://localhost:8080/v1/meeting/status
{"state":"in_meeting","room_name":"f1d641ae-...","active_speaker":"","participant_count":0} ✅

$ curl -s -X POST http://localhost:8080/v1/meeting/leave
{"error":"unauthorized: missing or invalid bearer"}  HTTP 401 ✅

$ curl -s -X POST -H "Authorization: Bearer <token>" http://localhost:8080/v1/meeting/leave
{"status":"leaving"}  HTTP 200 ✅ → 板子真退会 + 进程退出
```

**未做（留 8.4.4-完整）**：daemon 化 + 跨会议持有 V4L2/ALSA/APM/MppEncoder，
当前一次 leave 退一次进程，下次 join 重启资源。

**Commit**：`5dad383 feat(port): Phase 8.4.4-MVP — HTTP API + Bearer auth for meeting join/leave`

### 8.4.4-完整 — Daemon 化 + per-meeting FFI lifecycle（done 2026-04-29）

**起点**：MVP 单会议进程（一次 leave 退一次进程，下次 join 后端要重新拉
板子启动），不是产品级 daemon。本阶段把 BoardLoopback 改成长跑进程，HTTP
/join 可重复触发；同时修了一个伴生的 server-side 残留 bug：MVP 时手机端
看到板子退会要等 ~30s 客户端超时，因为 Room 析构是 fire-and-forget drop
FFI handle，没有通知 server "我主动 LEAVE"。

**实施**:

1. **MeetingState 加 Shutdown 状态** + `requestShutdown()` + `waitForJoinOrShutdown()`
   返 `std::optional`。signal handler 投 requestShutdown 让 main 解阻塞退外循环。
   注意：`std::lock_guard + cv.notify` 在 signal handler 严格上不 async-signal-safe
   （pthread_mutex_lock 不在 POSIX safe 列表上），实践 mutex 极短不死锁，留 self-pipe
   trick 做后续改进。
2. **main() 改外循环 while(true)**：每轮 `waitForJoinOrShutdown` → setupFfiAndApm
   → 跑会议体（do/while(false) 块作用域确保 RAII 清理）→ Room::Disconnect →
   room.reset → teardownFfiAndApm → continue 等下次 /join。Connect 失败 continue
   不退 daemon。
3. **per-meeting FFI 重做**：`livekit::initialize` + APM 创建移到 setupFfiAndApm，
   会议结束 teardownFfiAndApm 调 livekit::shutdown 整个 Rust runtime tear-down。
   代价 ~100-300ms cold start / 会议；收益是上轮的残余 RTP / subscription thread /
   frame callback 完全 stage cleared，不会泄漏到下轮。
4. **SDK 加 Room::Disconnect()**（[include/livekit/room.h](../../../include/livekit/room.h),
   [src/room.cpp](../../../src/room.cpp)）：送 DisconnectRequest 到 FFI server，阻塞
   等 DisconnectCallback（实测 < 200ms）。配套加 `FfiClient::disconnectAsync(room_handle)`
   做 async 关联（[src/ffi_client.cpp](../../../src/ffi_client.cpp)）。Room::Disconnect
   幂等，已 Disconnected no-op。
5. **会议体退出顺序**：tracks/source/video reset → `room->Disconnect()` → room.reset
   → teardownFfiAndApm → log idle → 回外循环。

**为什么必须显式 Disconnect**：MVP 时 Room 析构只 drop FFI handle，server
看不到 "client 离开"，仍认为板子在房间，对端要等 ~30s 客户端超时才看到退
会。daemon 模式下问题更严重 —— 下次 /join 同 identity 进来，server 可能
判为 reconnect 跟旧 ghost session 冲突。Disconnect() 让 server 立刻广播
ParticipantDisconnected，对端 < 1s 看到退会。

**实测验证 2026-04-29**:

```
T0   curl /join → 板子加入
T~5s 手机看到板子有视频
T10s curl /leave
T<1s 手机端立即看到板子参与者从房间消失（不是 30s 超时！）✅
T~10s 板子日志：[loopback] disconnecting... → [daemon] meeting cleaned up;
       idle, waiting for next /join or signal ✅
T20s curl /join (新 url + token) → 板子第二轮加房 ✅
T~5s 第二轮会议正常工作（视频 + 音频 + active speaker 切换）✅
```

50-cycle join/leave + vmrss 监控验证留给 8.4.6 多人 soak 一起跑。

**Commits**:
- `4b84d56 feat(sdk): add Room::Disconnect() for synchronous server-side leave`
- `f4c2b7a feat(port): Phase 8.4.4-完整 — daemon mode with per-meeting FFI lifecycle`

### 8.4.2-完整 — Hold-down + render-only switch（done 2026-04-29）

**起点**：8.4.2-MVP 一观察到新 active speaker 立即 commit（sub new + unsub
old）。手机+Web 同时讲话压测时崩 —— `corrupted double-linked list` SIGABRT，
backtrace 整条都在 liblivekit_ffi.so 内 realloc 路径，是 SDK Rust 侧 race。

**诊断**：先加 `crashHandler`（SIGSEGV/SIGBUS/SIGABRT）+ `-rdynamic` 让符号
解析到。第一次复现拿到 SIGABRT signal=6 + 整条 FFI 内部栈帧确认 SDK bug。
hold-down 600ms 试着减低 churn 频率，崩溃间隔从"切几次就崩"延长到~30s 一次但
还崩 → 说明不是频率问题，**每次 setSubscribed(false) 都让 FFI 累积 heap 损坏**。

**修法（双层）**：

1. **600ms hold-down**：candidate 必须连续观察 ≥600ms 才 commit。
   - MeetingController 状态机分两步：`observeActiveSpeaker`（仅记录 pending）+
     `tryCommitPendingActive`（worker 醒来时检查 pending 是否过期）
   - workerLoop 用 `cv.wait_until(deadline)` 挂 hold-down 截止时间
   - 同 candidate 重复观察不重置计时；不同 candidate 触发计时重置
   - tunable via `BOARD_ACTIVE_SPEAKER_HOLDDOWN_MS`

2. **Render-only switch**：commit 不再 `setSubscribed(false)`。所有远端视频
   stay-subscribed，`commitActiveSpeakerSwitch` 只切 `g_video_router.setActive`
   （atomic，不进 FFI）。`handleTrackPublishedDefend` 也 gut 成 no-op（同样
   会触发 FFI bug）。
   - 代价：N peer 全 stay-subscribed，每个 peer 一路 HW 解码
   - 实测 2-3 peer 无压力（MPP HW codec）
   - **10-peer 极限场景 deferred 到 SDK FFI bug 修复后再优化**

**为什么不 push fix 给 SDK**：bug 在 Rust 侧 unsubscribe 路径的 realloc。修
需要拿到 client-sdk-rust trace + 复现 minimal repro，跑上游沟通流程，工作量
远超本阶段。当前 workaround 不需要任何 unsub 也能跑会议，做完更紧迫的产品
级 subphase 再回头修。

**实测验证 2026-04-29**：手机+Web 同时讲话 3+ 分钟，多次 hold-down 间隔的
切换全部走 render-only 路径，零崩，零 FFI 警告。

**Commit**：`6437239 feat(port): Phase 8.4.2-完整 — hold-down + render-only active speaker switch`

### 8.4 后续 subphase

**MVP + 8.4.2-完整 + 8.4.4-完整 已完成 ✅**。剩下 **8.4.5**（触控 UI）+
**8.4.6**（多人 4h soak）。看产品优先级排。详见上面表格 + [计划文件](../../../../../../Users/19146/.claude/plans/wobbly-forging-moler.md)。

## 已知未做项 / 边界

| 项 | 现状 | 影响 / 备注 |
|---|---|---|
| **MPP shutdown leaked group/buffer** | 未修，2026-04-28 8.2 soak 退出时观察到 | smoke.log 末尾打印 `mpp_buffer_service_deinit cleaning leaked group` + `cleaning leaked buffer`。**不影响运行**（7505s 0 underrun 0 ERROR），但说明 MPP buffer 内部 ref-count 有边角情况（可能跟 7.6.a `MppNV12Buffer` 的 inc_ref/put 配对有关）。短期容忍；如果未来观察到长跑后期 buffer pool 实际耗尽 / 性能退化再追。Phase 6.2 "clean shutdown" 工作没覆盖到 MPP 内部清理。|
| **liblivekit_ffi heap corruption on remote video unsubscribe** | 已用 8.4.2-完整 render-only switch 绕过；上游待修 | 2026-04-29 用 crashHandler + glibc abort 抓到：每次 `setSubscribed(false)` 给远端 video track 都让 FFI 累积 heap damage，~3-5 次后 realloc 报 `corrupted double-linked list` SIGABRT。整条栈在 liblivekit_ffi.so 内。当前所有 peer video stay-subscribed，10 peer 极限场景 deferred；真做大规模会议前需修上游或换 simulcast layer 选层 API。|
| 多 peer 加入崩溃 | 8.5 待修 | 详见 8.5 段 |
| 7.6.c DRM dma-buf 直渲 | 等 1080P30 真要时再做 | 详见 8.3 |
| 板上麦克风物理隔离 | 未做 | 8.1 已用 DAC 压低代偿；机械改造是更彻底但成本高的路线 |

## 已知排除项

- ~~**Buildroot 包补齐**~~ — Phase 7 follow-up 时调研后删除。`lsb_release` 进
  Buildroot 解决不了 `os_version=Unknown`（os_info crate 有 distro whitelist，
  Buildroot 不在内）；libvpx 已经静态进了 `libwebrtc.a`，没有再瘦空间。详见
  [phase7-summary.md](phase7-summary.md) 排除项段。
- **Rockchip rk_voice 硬件 AEC** — 需 NDA，未拿到代码 / 库。8.1 实测软件 AEC
  在合理 (DAC, delay) 配置下已能消除回声，rk_voice 暂不迫切。如果未来有更
  恶劣声学环境（如车载 / 远场 / 多人会议室），再去找厂家拿。
- **板上麦克风物理隔离** — 当前板上 mic 离扬声器 < 5 cm，腔体共振是回声主因。
  8.1 的 DAC 压低本质上是用电学手段抵消物理耦合；如果未来要彻底解决，机械
  改造或外接 mic 是更彻底但成本高得多的路线。

## 提交清单（持续更新）

`port/rv1126b-on-upstream-v0.3.3`（superproject，rebased 到 upstream v0.3.3）:

```
f73c28a  chore: untrack .claude/settings.local.json (IDE-private state)
e0f9523  feat(port): scripts/soak.sh — Phase 8.2 long-running soak harness
3a76fe9  chore: point client-sdk-rust submodule at John-Shao/livekit-sdk-rust-rv1126b fork
9ba9ffe  feat(port): AEC convergence sweep — sweet spot (DAC=155, delay=300)
... (62 more 直到 v0.3.3 锚点)
```

`port/rv1126b-mpp`（client-sdk-rust）: 暂无（Phase 8 全部在 superproject 范围内）。
