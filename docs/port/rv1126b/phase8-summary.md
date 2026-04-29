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
| 8.4.2-MVP | onActiveSpeakersChanged + 动态 setSubscribed | ⏳ 下一步 |
| 8.4.3 | 音频软混音器（pull 模型） | ⏳ |
| 8.4.4-MVP | HTTP API 极简版 + Bearer auth | ⏳ |
| 8.4.2-完整 | hold-down 600ms + 切换体验优化 | ⏳ |
| 8.4.4-完整 | Daemon 化 + 完整 lifecycle | ⏳ |
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

### 8.4 后续 subphase

详见上面表格 + 计划文件。下一步从 8.4.2-MVP 起。

## 已知未做项 / 边界

| 项 | 现状 | 影响 / 备注 |
|---|---|---|
| **MPP shutdown leaked group/buffer** | 未修，2026-04-28 8.2 soak 退出时观察到 | smoke.log 末尾打印 `mpp_buffer_service_deinit cleaning leaked group` + `cleaning leaked buffer`。**不影响运行**（7505s 0 underrun 0 ERROR），但说明 MPP buffer 内部 ref-count 有边角情况（可能跟 7.6.a `MppNV12Buffer` 的 inc_ref/put 配对有关）。短期容忍；如果未来观察到长跑后期 buffer pool 实际耗尽 / 性能退化再追。Phase 6.2 "clean shutdown" 工作没覆盖到 MPP 内部清理。|
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
