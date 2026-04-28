# Phase 7 工作总结 — codec 对称化、AEC 落地、解码侧零拷贝 ✅

> **状态**：核心目标全部完成，HD 720P30 双向通话 + AEC 在线，单核中位数从 ~100% 降到 ~58%
> 完成日期：2026-04-28
> 分支：`port/rv1126b-phase-0-recon`（superproject）/ `port/rv1126b-mpp`（client-sdk-rust）
> 关联文档：[plan.md](plan.md) · [phase5-summary.md](phase5-summary.md) · [phase6-summary.md](phase6-summary.md) · [oplog.md](oplog.md)

## 范围

Phase 6 把 codec 路径换成 MPP 硬编硬解。Phase 7 在那个基础上做了三件事：

1. **codec 对称化**：encoder 也支持 H.265，AEC 真正消回声，多 codec 全闭环
2. **延续硬件路径的性能优化**：消掉 capture/encode/decode/render 链路上各段的 NV12 memcpy
3. **若干可选项的探索**：每段都明确给出"为什么留到下个阶段"的边界

子阶段按落地顺序：

| 阶段 | 验证目标 | 结果 |
|---|---|---|
| **7.1 MPP H.265 硬编** | encoder factory 多 codec 化；板↔板 H.265 双向通话 | ✅ |
| **7.2 选择性 APM (NS + HPF)** | NS=VeryHigh + HPF 开，AGC 继续关；噪音感知减弱 | ✅ |
| **7.3 Playback via ADM** | 让 SDK 的 ADM 驱 ALSA，APM 拿 render reference 给 AEC | 🔁 attempt-and-revert |
| **7.4 APM 帧级 AEC** | 用 `livekit::AudioProcessingModule` 直接帧级 AEC | ✅ 90/100 |
| **7.5 encoder NV12 fast path** | encoder 入口检测 kNV12，跳掉 NV12→I420→NV12 round trip | ✅ -20pp CPU |
| **7.6.a decoder zero-copy** | `MppNV12Buffer` wrap MppBuffer，跳掉解码出口 stride-strip memcpy | ✅ -12pp CPU |
| **7.6.b FFI cvt_nv12 透传** | NV12→NV12 时 BufferStorage::Native 直通，跳掉 FFI memcpy | ✅ -10pp CPU |
| **7.6.c DRM dma-buf 直渲** | drmPrimeFDToHandle 跳掉 DRM dumb_buf memcpy | 🔁 attempt-and-revert |

## 7.1 — MPP H.265 硬编（done 2026-04-27）

Encoder 通用化跟 6.4 decoder 对齐。`RockchipMppH264EncoderImpl` →
`RockchipMppVideoEncoderImpl` 接 `MppCodingType`，cfg 按 codec 分支
（h264:* vs h265:* 键名），IDR / 参数集 NAL 解析按 H.264 (bits[4:0])
和 H.265 (bits[6:1]) 分别处理，QP parser 用对应 H264/H265 BitstreamParser，
`CodecSpecificInfo` 在 H.265 路径只填 codecType（这版 webrtc 的
`CodecSpecificInfoUnion` 没 H265 成员）。Encoder factory 也 advertise
H.265 + 按 `format.name` dispatch。`smoke.sh --codec h265` 实测板↔手机
720P30 双向 MPP HEVC 30 秒 904 帧稳定。

## 7.2 — 选择性 APM (NS + HPF)（done 2026-04-27）

Phase 6.3 时把整个 APM 关掉是为了不被 AGC 压软件 gain。这次重新分析：
NS / HPF 是 capture-side 单向滤波器，不需要 reverse stream，可以单开。
NS 提到 `kVeryHigh`（默认 kModerate 太弱听不出）+ HPF 开（去 80Hz 以下
低频），AGC1/AGC2 继续关（保留手动 ES8389 PGA + 8× 软件 gain）。AEC
**仍关**——根因要在 7.4 才解决。`BOARD_LOOPBACK_APM_OFF=1` 一键
回 6.3 全关 baseline 做 A/B。实测 NS 能感知到背景噪声减弱。

## 7.3 — Playback via ADM (尝试后回退 2026-04-28)

**目标**：把 BoardLoopback 自己的 ALSA writer 替换成 SDK 的 ADM 来驱
ALSA，让 APM 拿到 render reference 让 AEC 真生效。

**实现**：

- `livekit_ffi::AudioDevice` 加 ALSA 驱动：env-gated（`LIVEKIT_ADM_PLAYBACK_DEVICE`）
  开 ALSA mono、加 writer 线程 + queue 解耦避开 ALSA back-pressure
- `peer_connection_factory.cpp` 让 AEC 在 env 设了的时候自动开（mobile_mode）
- BoardLoopback 加 `BOARD_LOOPBACK_ADM_PLAYBACK=1` env：跳过自己的 AlsaPlayer，
  改让 ADM 拿数据

**踩到的坑（按出现顺序）**:

1. ADM `NeedMorePlayData` 设 stereo（`kChannels=2`），但板上 PulseAudio→
   ES8389 路径只接 mono 才稳；不下混就锯木头。改成 ALSA mono + 写入前
   做 L+R 平均下混。
2. `n_samples_out` 在这版 webrtc 的语义是**总样本数**（per-channel × channels），
   不是 per-channel — 名字误导，实际值是 960 不是 480。下标越界触发
   libstdc++ assertion。修法：clamp `min(n_samples_out, kSamplesPer10Ms)`。
3. 1s ALSA latency 超出 AEC mobile 工作范围（典型 ~64ms），即使有
   reference signal AEC 也对齐不上回声。降到 150ms + 实现 `PlayoutDelay()`
   返回 75ms。

**最终结论：架构跑通了，但 AEC 没产生感知效果**。手机端听到的回声没有
明显减少。退出还引入 segfault（MPP teardown 与新增 audio 线程清理时序冲突）。

**根因分析**：BoardLoopback 的 capture 路径走 `AudioTrackSource::capture_frame()`
→ sinks，这条路径**不进 APM 的 AEC pipeline**。AEC 需要 capture 走 ADM 的
`RecordedDataIsAvailable()` 才能与 render reference 对齐时间相关。NS 在 7.2
能起作用是因为它是 capture-side 单向滤波器，路径要求宽松；AEC 是 capture/render
双向相关算法，路径要求严格得多。

**决策**：revert 全部 7.3 改动回 7.2 baseline。不带半成品代码继续走。

## 7.4 — 帧级 AEC via livekit::AudioProcessingModule（done 2026-04-28）

**关键转折**：放弃改 SDK 内部 APM 路径，改用 SDK 已经暴露的
`livekit::AudioProcessingModule` 类做帧级 AEC。这个 wrapper 跑在
PCF 的 APM **外面**——直接在 BoardLoopback 的 ALSA 读/写路径
上调用它，AEC3 看到的数据就是真正进入扬声器和从麦克风出来的数据。

**实现**：

- 构造 `AudioProcessingModule({ echo_cancellation, noise_suppression,
  high_pass_filter })`（AGC 还是关，保留 ES8389 PGA + 8× 软件 gain
  校准）。`setStreamDelayMs(N)` 一次设好 stream delay hint。
- AudioStream 回调（远端音频要进 ALSA 之前）：复制 frame，调
  `processReverseStream` —— APM 拿到播放参考信号
- audio_thread（ALSA mic 读到 buf 之后、`captureFrame` 之前）：
  `processStream(frame)` 原地处理，发布的就是消好回声的音频
- env-gated `BOARD_LOOPBACK_AEC=1`，smoke.sh `--aec` 开关。
  `--aec-delay <ms>` 调 stream delay。

**实测打分**（双方外放对讲，ATK-DLRV1126B + PulseAudio + ES8389，**首轮 sweep
基于当时 DAC=186 -2.5 dB，喇叭近爆音**）：

| stream delay | 评分 | 现象 |
|---|---|---|
| 100ms | 0/100 | AEC 完全没消（hint 偏离实际太多，AEC 锁不住）|
| 200ms | 50/100 | 减小但持续有残余 |
| 300ms | 70/100 | 通话开始阶段漏掉的回声较多 |
| **400ms** | **90/100** | 最佳，残余只有 AEC3 收敛期前 5-10s |
| 500ms | 80/100 | 过了，回退一档 |

400ms 比典型手持机大，是因为这个声学链路堆叠：AlsaPlayer 队列
~50-300ms + PulseAudio ~50ms + ES8389 ~50ms + 房间 + ALSA capture
~100ms。**注**：这套打分是 DAC=186 条件下的最优；后来 2026-04-28 follow-up
做了 DAC × delay 二维 sweep 找到更好的全局最优 `(DAC=155, delay=300)`，回声
完全消除。详见 `## 2026-04-28 follow-up — AEC 收敛优化`。

**为什么 7.3/7.4-ADM 那两次没行**：把 capture/playback 接到 ADM 后，
LocalAudioTrack 仍然从 `AudioTrackSource` 拿数据（不从 ADM 经 APM
处理后的版本拿），所以 AEC 看不到 capture 那一端，没法做 cancel。
这次绕过 PCF 的 APM、直接在 frame 流上挂 standalone AEC，从架构上
对了。

后续：smoke.sh 把 `--aec` 改默认开（commit `f0a5ce0`），`--no-aec` 关。

## 7.5 — encoder NV12 fast path（done 2026-04-28）

**症状**：HD 720P30 编码侧 CPU 居高，profile 显示热点在 `frame.video_frame_buffer()->ToI420()`
和 `I420ToNV12` 这两个 libyuv 调用。

**根因**：V4L2 抓的是 NV12，但 encoder 入口强制 `ToI420()`，再走
`I420ToNV12` 把数据塞进 MppBuffer——白做了一次 NV12↔I420 round-trip，
720P30 ~90 MB/s 内存搬运。

**修法**：encoder 检测 buffer type，`kNV12` 路径直接 stride-strip memcpy
进 MppBuffer，`kI420` 等其他类型保留原 path。

**实测**：BoardLoopback 单核中位数 **~100% → ~80%（约 -20pp）**。

## 7.6 — decoder→DRM 零拷贝（部分完成）

**目标**：消掉解码出口到屏幕之间的三段 NV12 memcpy（解码 → FFI cvt_nv12
→ DRM dumb_buf），在 7.5 之后再砍一刀。分三段做，每段独立 commit + 测试。

### 7.6.a — decoder 出口 zero-copy（done）

新增 `MppNV12Buffer : NV12BufferInterface` wrap MppBuffer，`mpp_buffer_inc_ref`
持有强引用，下游消费完才 put 回 pool。stride 直接报 MPP 的 `hor_stride`，
下游都 honor stride 没问题。

**实测**：CPU 中位数 **80% → 68%（-12pp，比预估 3% 高，可能因为 NEON
对齐路径也跟着省了）**。30 fps 双向稳定，0 ALSA underrun。

### 7.6.b — FFI cvt_nv12 透传（done）

`to_video_buffer_info` 新增 `BufferStorage` enum 区分 `Owned(Box<[u8]>)` /
`Native(BoxVideoBuffer)`。NV12 → NV12 时直接走 Native 分支：
`proto::VideoBufferInfo` 指向源 buffer 的 pixel memory，FFI handle store 持
源 buffer 引用保活。

**实测**：CPU 中位数 **68% → 58%（-10pp）**。

### 7.6.c — DRM dma-buf 直渲（attempt-and-revert）

**目标**：BL 的 DRM 显示用 `drmPrimeFDToHandle` + `drmModeAddFB2` 直接 import
MPP buffer 的 dma-buf fd 当 framebuffer，省掉最后一次 dumb_buf memcpy。

**实现**：
- 新文件 `mpp_dmabuf_registry.{h,cpp}`：进程内 `data_ptr → (fd, hor_stride, ver_stride)` 注册表
- `MppNV12Buffer` ctor/dtor 维护注册表
- BL `renderNV12` 加 prime-fd 路径，cache 上一帧 (gem_handle, fb_id) 用于 SetPlane 后清理
- BL CMakeLists 加 `livekit_ffi` 链接（C 符号在那）

**踩坑**：cdylib 的 `--gc-sections` 把 `livekit_mpp_dmabuf_lookup` 整段裁掉
了。试过：
- `__attribute__((used, retain, visibility("default")))` —— 不够
- `cargo:rustc-link-arg=-Wl,--undefined=...` + `--export-dynamic-symbol=...` —— 不生效
- Rust extern "C" pub 声明 —— 不算"使用"，rustc 不引用

cdylib 默认只导 Rust `#[no_mangle]` 符号；C 库存档里的 extern "C" 要让它穿
透到 dynamic table，需要更深的 livekit-ffi 导出策略调整（version script、
Rust 包装函数等），不是 BL 一侧能搞定的。

**决策**：revert 全部 7.6.c 改动。**7.6.a + 7.6.b 已经从 80% 降到 58%（-22pp）**，
冲 720P30 双向通话 + AEC 已经留下舒适余量。7.6.c 预期 -3 ~ -5pp，工程成本 ≫
收益。真要做 1080P30 稳定时再正式做（让 livekit-ffi 暴露 dma-buf-aware API，
而不是侧通道）。

## 用户体验调优（散见各 commit）

- **DAC 音量**：起初默认从 -10 dB（171）调到 -2.5 dB（186）配合软件 gain 8×；
  AEC 收敛优化 follow-up 又往下压到 -18 dB（155），喇叭过响时 AEC 消不干净，
  压低喇叭让物理回声幅度变小、AEC 工作量减轻。详见 follow-up 段
- **smoke.sh `--aec` 默认开**：Phase 7.4 验过 90/100，标准跑动就让 AEC 生效

## 实测：板↔Web 720P30 双向通话（H.264 + AEC + 7.5/7.6.a/7.6.b）

```
==== smoke ==== url=wss://live.jusiai.com codec=h264 res=hd rotate=90 use_mpp=1
                aec=1 (delay=400ms)
[mpp-dec] Create(H264 ...) → MPP coding=7
[mpp-dec] context ready (H264, out=100ms)
[loopback] APM AEC enabled, delay_ms=400
[loopback] T+5s..T+270s  alsa_underruns=0 alsa_dropped=0  video=N>0
final  video=N audio=M     ← 30 fps 双向稳定
```

**CPU 单核中位数演进（HD 720P30 双向 + AEC）**:

| | CPU 中位数 | Δ | 备注 |
|---|---|---|---|
| Phase 6 收尾 | ~100% | — | encoder NV12→I420 round-trip + 解码侧 memcpy 链 |
| 7.5 后 | ~80% | -20pp | 跳掉 encoder NV12↔I420 round-trip |
| 7.6.a 后 | ~68% | -12pp | wrap MppBuffer，跳掉解码出口 memcpy |
| 7.6.b 后 | **~58%** | -10pp | FFI passthrough，跳掉 cvt_nv12 memcpy |

总计 **-42pp**（从满核到 ~58%）。同时 AEC 在线（~+15pp APM 成本，已包含在
58% 内），留出 ~40pp 的 headroom 给业务层。

## 已知未做项 / 边界

| 项 | 现状 | 影响 / 备注 |
|---|---|---|
| 7.6.c DRM dma-buf 直渲 | 尝试后回退 | 1080P30 阶段必做；前置：让 livekit-ffi 暴露 dma-buf-aware API |
| ~~AEC 收敛初期残余~~ | ✅ 已解决 (2026-04-28) | DAC × delay sweep 找到 `(155, 300)` sweet spot，回声完全消除 |
| 板上麦克风物理隔离 | 未做 | 当前板上麦克风离扬声器 < 5 cm，腔体共振是回声主要来源。机械改造或外接 mic 可降低 AEC 难度 |
| `mpp_buf_slot mismatch` warning | 未修 | H.265 simulcast 切层时偶现，不影响出图（Phase 6 遗留）|
| Buildroot 包补齐 | 未做 | `lsb-release` 进 `BR2_PACKAGE_*`；libvpx 静态链上去再瘦点 |
| LiveKit data track | 未通 | SCTP/DTLS 协商失败（jusiai NAT 路径），视频/音频 RTP 不受影响 |

## 运行手册（Phase 7 production-ready）

```bash
# 板上（ATK-DLRV1126B）：
ssh rv1126b-board /opt/livekit/smoke.sh                # MPP H.264 双向 + AEC，HD 720P30 默认
ssh rv1126b-board /opt/livekit/smoke.sh --no-aec       # 关 AEC（A/B 对照）
ssh rv1126b-board /opt/livekit/smoke.sh --aec-delay 300  # 调 AEC 延迟（默认 300ms）
ssh rv1126b-board /opt/livekit/smoke.sh --dac 155        # 调 DAC 喇叭音量（默认 155=-18dB）
ssh rv1126b-board /opt/livekit/smoke.sh --codec h265   # 板 → 对端 H.265 硬编
ssh rv1126b-board /opt/livekit/smoke.sh --res fhd      # 1080P25（CPU 头顶）
ssh rv1126b-board /opt/livekit/smoke.sh --bg           # 后台 + /tmp/smoke.log

# 一键音频校准：
ssh rv1126b-board /opt/livekit/board-audio-setup.sh    # PGA=9 (+27dB) + DAC=155 (-18dB)
ssh rv1126b-board PGA=10 /opt/livekit/board-audio-setup.sh  # 麦更敏感
ssh rv1126b-board DAC=171 /opt/livekit/board-audio-setup.sh # 喇叭抬高一档（A/B 对照）
```

## 提交清单

`port/rv1126b-mpp` 分支（client-sdk-rust 内）：

```
b622c43  Phase 7.6.b — FFI NV12 pass-through (skip cvt_nv12 memcpy)
50a0108  Phase 7.6.a — decoder zero-copy via MppBuffer wrap
f295610  Phase 7.5 — NV12 fast path in MPP encoder input
bd42f8b  Phase 7.2 — selective APM (NS VeryHigh + HPF on, AEC off)
41ae956  Phase 7.1 — MPP H.265 hardware encode
```

`port/rv1126b-phase-0-recon` 分支（superproject）：

```
e15258a  docs(port): record Phase 7.5 + 7.6.a/b done; 7.6.c attempt-and-revert
8709a68  bump client-sdk-rust to Phase 7.6.b — FFI NV12 pass-through
fa00eb6  bump client-sdk-rust to Phase 7.6.a — decoder zero-copy
a6ee72b  ops(port): bump default DAC playback volume 171 → 186 (-10 dB → -2.5 dB)
ef42840  bump client-sdk-rust to Phase 7.5 — NV12 encoder fast path
f0a5ce0  ops(port): smoke.sh — AEC default on (--no-aec to disable)
a6a73bd  docs(port): record Phase 7.4 — APM-based AEC at 90/100
c6f5551  Phase 7.4 — APM-based AEC via livekit::AudioProcessingModule
09310e8  docs(port): record Phase 7.1/7.2 done and 7.3 attempt-and-revert
e521e83  bump client-sdk-rust to Phase 7.2 — selective APM
cde4547  bump client-sdk-rust to Phase 7.1 — MPP H.265 encode
```

---

## 2026-04-28 follow-up — AEC 收敛优化

Phase 7.4 标定的 `(DAC=186, delay=400)` = 90/100，残余主要在 AEC3 收敛期前
5-10 秒。Follow-up 把"降扬声器音量"这条线走通，让物理回声幅度变小、AEC3
工作量减轻，目标是把残余压到 0。

**约束（用户硬要求）**：不能牺牲板子推送给对端的音量。即只动 DAC 不动 ADC PGA，
且 sweep 时同步监控对端听我们的音量/连续性，任何退化立刻 PASS。

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

**关键观察**：DAC 和 AEC delay 是耦合的——喇叭压得越低 → 物理回声路径越短 →
最佳 stream delay hint 也跟着往下走。这也解释了为什么 7.4 阶段单独 sweep
delay 时 400ms 是局部最优：那时 DAC=186 喇叭过响，需要长延迟兜回声；现在
DAC=155 喇叭合理，AEC 不用等那么久就能匹配上回声。

**对端验证**：(DAC=155, delay=300) 这组下，对端反馈板子推过去的语音音量
正常、不断断续续 → 推送音量未受影响，硬约束达成。

**默认值更新**：

```
DAC: 186 → 155       (smoke.sh + board-audio-setup.sh)
AEC_DELAY_MS: 400 → 300
```

**Phase 6.3 老默认 171 的来由**：当时软件 mic gain = 12×，喇叭压低补平体感。
后来软件 gain 改 8× 时 DAC 顺手抬到 186 配合，但实际上 7.4 AEC 投产后才暴露
喇叭过响是 AEC 残余的主因。现在 DAC=155 配合 8× 软件 gain：本端听对端略小
但完全可听，对端听我们正常，AEC 残余消除——三方都达到合理点。

**为什么 sweep 而不是先建模算最优**：声学回声路径是腔体 + 麦克风物理位置 +
PulseAudio 缓冲深度的非线性叠加，建模 + 测量参数比直接 sweep 成本更高，6 组
打分一小时拿到答案，工程上完胜。换其他硬件 / 改了喇叭距离都要重新 sweep。

---

## Phase 8 切入候选

按价值密度排序（任一都不强制，看后续业务诉求）：

1. ~~**AEC 收敛优化**~~ — ✅ 已完成 (2026-04-28)。降扬声器音量这条线走通，
   做了 DAC × delay 二维 sweep，找到 sweet spot `(DAC=155, delay=300)`，回声
   完全消除且对端听我们音量不退化。详见 `## 2026-04-28 follow-up — AEC 收敛优化`。
   物理隔离麦/喇叭、Rockchip rk_voice 硬件 AEC 还是后路（rk_voice 需 NDA，
   暂未拿到代码）
2. **DRM dma-buf 直渲（7.6.c v2）**：等 1080P30 真需要时再做。前置工作是把
   livekit-ffi 的 cdylib 加一条"额外导出 C 符号"的 build-script 入口（修
   ffi-node-bindings 的版本脚本或加一个公共 shim 模块）
3. ~~**Buildroot 包补齐**~~ — 调研后删除。当时怀疑两件事：(a) `lsb-release`
   进 Buildroot 解决 `os_version=Unknown`，(b) libvpx 静态链上去瘦 .so。
   实测两件都不成立：libvpx 已经在 `libwebrtc.a` 里静态进了 .so（没有再
   瘦空间）；`lsb_release` 即便手动 shim 上去，`os_info` crate 3.14.0 也
   不认 `Distributor ID: Buildroot`（它有 distro whitelist，未识别就回
   Unknown），警告消不掉。要消这条警告得 patch `os_info` 或在 livekit-api
   绕开它直接读 `/etc/os-release`，不属于 Buildroot 范畴。
4. **长跑测试**：7×24h soak（建议 /schedule agent 开起来），盯 RSS / fd / underrun 累计
5. **业务集成**：Phase 7 阶段板↔Web 通话已稳定，可以开始接产品业务逻辑（房间管理 / 多人 / 数据通道等）
