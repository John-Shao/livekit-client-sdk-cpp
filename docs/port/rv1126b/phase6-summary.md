# Phase 6 工作总结 — MPP 硬件 codec 全闭环 ✅

> **状态**：完整通过（H.264/H.265 双向硬件 codec + VP8/VP9 软解兜底；端到端真音视频通话稳定）
> 完成日期：2026-04-27
> 分支：`port/rv1126b-phase-0-recon`（superproject）/ `port/rv1126b-mpp`（client-sdk-rust）
> 关联文档：[plan.md §Phase 6](plan.md) · [phase5-summary.md](phase5-summary.md) · [oplog.md](oplog.md) · `client-sdk-rust/webrtc-sys/src/rockchip_mpp/`

## 范围回顾（Phase 5 vs Phase 6）

延续 Phase 5 划定的边界：

- **Phase 5**：协议栈 + 硬件 I/O（V4L2 摄像头 / DRM 屏 / ALSA mic+spk）+ libwebrtc 软编（VP8 / Opus）
- **Phase 6**：把视频中间层换成 **Rockchip MPP**（VEPU/VDPU 硬件编解码），capture/render/playback 不变

Phase 6 内部的子拆分按落地顺序：

| 阶段 | 验证目标 | 结果 |
|---|---|---|
| **6.1 skeleton** | 在 webrtc-sys 注入 vendor 自定义 encoder/decoder factory，env opt-in 不破坏 fallback | ✅ |
| **6.1.4 MPP encoder** | `mpp_init(MPP_CTX_ENC, AVC)` + put_frame/get_packet 循环；输出真 H.264 NAL 给 libwebrtc | ✅ |
| **6.1.5 step A** | encoder 入口 I420→NV12 用 libyuv NEON；MPP 直接吃 NV12 | ✅ |
| **6.1.5 step B** | 拆掉 NV12→I420→NV12 round trip，端到端 NV12 直通；CPU 显著回落 | ✅ |
| **6.2 MPP decoder** | `mpp_init(MPP_CTX_DEC, AVC)` + put_packet/get_frame；info_change 握手；外部 buffer group | ✅ |
| **6.3 用户体验调优** | 分辨率预设、码率上限、传感器旋转、APM AGC、ES8389 PGA 增益 | ✅ |
| **6.4 多 codec** | H.265 硬解；VP8/VP9 走 libvpx 软解兜底；FFI cvt_i420 补 NV12 出口 | ✅ |
| **6.1.5 step C** | dmabuf 零拷贝（V4L2→MPP→DRM 全程不经 CPU） | ⏸ 推迟到性能阶段 |

## 6.1 — Encoder skeleton + opt-in 设计

`webrtc-sys` 在 PeerConnectionFactory 构造时挂入两个 vendor factory（`RockchipMppVideoEncoderFactory` / `…DecoderFactory`），`IsSupported()` 双闸门：

1. `BOARD_LOOPBACK_USE_MPP=1` env
2. `/dev/mpp_service` 节点存在

任一不满足就走 libwebrtc 自带 OpenH264/libvpx 软路径，所以 `--no-mpp` 切换零代价、零侵入 upstream。

`webrtc-sys/build.rs` 仿照 NVidia/CUDA 块加 `cfg!(feature = "rockchip-mpp")`，cc crate 拉进 `src/rockchip_mpp/` 下 4 个 .cpp，通过 `ROCKCHIP_MPP_INCLUDE/LIB` env 找头/库（在 `scripts/env-rv1126b.sh`）。

## 6.1.4 — MPP H.264 encoder 实测

`MppCtx` + `MppEncCfg` 用 `prep` / `rc` / `codec` 三组 cfg，put/get 双线程：webrtc encode_thread 每帧 push NV12 frame，pop H.264 NAL 同步交回。GoP=60、CBR、target = encoder bitrate kbps × 1000。

**踩过的坑**（按修复顺序）：

1. **Packet double-free** — `mpp_packet_init_with_buffer` 之后忘了拷贝就 `mpp_packet_deinit`，下一帧再 push 时 buffer 已被 mpi 释放。改成"长寿命 packet 重新指目标 buffer"模型，借鉴 mpi_dec_test 的 reusable handle 写法。
2. **MppEncCfgImpl ctor 无重载** — set_s32 / set_u32 类型严格，`bps_target` 必须是 RK_S32 不能 RK_U32（实测 set_u32 写不进）。

实测板 → 手机 720P30 H.264 单核占用从软编 80%+ 降到 ~30%。

## 6.1.5 step A — 入口 I420→NV12 NEON

webrtc encoder 入口给的是 I420，MPP 期望 NV12。第一版用 `webrtc::I420Buffer::ToNV12()` 走标量代码 ~17 ms/frame @720p。改成 `libyuv::I420ToNV12()`（Rust 端 `imgproc::colorcvt::i420_to_nv12`，拉 yuv-sys 的 `rs_I420ToNV12`），NEON 向量化后 ~3 ms/frame。

DRM 显示侧同样走这条路：`drm_display.cc` 的 NV12 转换从手写循环换成 imgproc 调 libyuv。

## 6.1.5 step B — NV12 zero-conversion chain

**症状**：720p30 测试时 CPU 还是 ~70%，hotspot 在 livekit-ffi 的 `cvt_i420` 和 `cvt_nv12` 两侧。

**根因**：MPP decoder 先把 NV12 解出 → 转 I420 给 libwebrtc → libwebrtc 走 `VideoFrameBufferType::kI420` 通道 → FFI 在 video_stream 里再 I420→NV12 给 DRM。一条流多了一次 NV12→I420→NV12 round trip，~1.4 MB / 帧 / 方向 / 30fps = ~42 MB/s 无谓内存搬运。

**修法**（同一 commit 内 3 处联动）：

1. **decoder 输出直接交 NV12Buffer**（不再 NV12ToI420）→ libwebrtc 走 `kNV12` 通道
2. **cxx-bridge VideoFrameBufferType 补齐** kI210 / kI410 enum 项 — 上游 libwebrtc 在 kNV12 之前新加了这两个，Rust 端漏映射，运行时 panic `unreachable!()`
3. **livekit-ffi cvt_nv12 stride 修正**：`dst_stride_uv = chroma_w * 2`（之前误用 `chroma_w` 当字节数，imgproc assert 撞上）；`nv12_info.size_uv = stride_uv * chroma_height`（之前多乘 2 over-report）

修完单核回落到 23–29% @720p30。

## 6.2 — MPP H.264 decoder 实测

`MPP_CTX_DEC` + `MPP_DEC_SET_PARSER_SPLIT_MODE=1`（Annex-B 字节流可一包多 NAL）+ `MPP_SET_INPUT_TIMEOUT=BLOCK / OUTPUT_TIMEOUT=100ms`。第一帧的 `info_change` 拿到分辨率后用 `mpp_buffer_group_get_internal(MPP_BUFFER_TYPE_DRM | CACHABLE)` 分外部 buffer group，挂到 `MPP_DEC_SET_EXT_BUF_GROUP`。

**坑表**：

| 现象 | 根因 / 修法 |
|---|---|
| `Decode()` 每包阻塞 100 ms | `decode_get_frame` bool 返回区分不出 frame vs info_change，进度上报不正确。改 enum `DrainResult{kNothing, kInfoChange, kFrame}`，info_change 时立即 retry，frame 时直接 break。|
| Simulcast 切层后画面冻 | `info_change` 时只 `mpp_buffer_group_limit_config` 但不 clear，slot table 还指向小 buffer，新大 buffer 申请触发 "reach group size limit"，**音频继续走，视频静止** —— 极隐蔽。修法：在 `info_change` 分支里先 `mpp_buffer_group_clear` 再 limit_config，让 mpi 重新分配。|
| 退出时 segfault | `mpp_destroy` 后 atexit handler `mpp_buffer_service_deinit` 清理 leaked group → use-after-free。修法：teardown 顺序改为 reset → `MPP_DEC_SET_EXT_BUF_GROUP=NULL` → packet_deinit → mpp_destroy → group_put。|
| H.264 profile 协商失败 | 只 advertise ConstrainedBaseline pkt-mode=1。修法：扩到 {Constrained Baseline, Baseline, Main} × {pkt-mode 0, 1} 共 6 项，`profile-level-id` 严格匹配（IsSameCodec），不能依赖 FFI 二次匹配 loop。|

## 6.3 — 用户体验调优

按用户反馈驱动迭代，无单独 commit phase 编号但实际是 Phase 6 的一大块：

### 视频

- **分辨率预设**：SD 480P30（1.5 Mbps）/ HD 720P30（2.5 Mbps，默认）/ FHD 1080P25（5.5 Mbps）
- **simulcast=false + 显式 max_bitrate**：libwebrtc 默认 simulcast bitrate allocator 给 720p 层只 ~30 kbps，画面糊。强制单层 + 上限码率后清晰
- **MPP encoder 硬件旋转**：`prep:rotation` 设 90/180/270，对 ATK-DLRV1126B 摄像头 sensor 物理 +90° 装配做编码前补偿。env `BOARD_LOOPBACK_VIDEO_ROTATE`（默认 90）
- **不走 RTP video orientation extension**：实测某测试机不读 `urn:3gpp:video-orientation`，靠扩展不可靠

### 音频

- **WebRTC APM 关闭**：`AudioProcessing::Config{ AEC=NS=AGC1=AGC2=HPF=off }` 注入 `BuiltinAudioProcessingBuilder`。AGC 把软件 12× gain 全压回去了，导致"听筒感"
- **ES8389 硬件 mic PGA = +27 dB**：`amixer -c 0 cset numid=39/40 9`。软件 gain × 硬件 PGA 双层增益后人声明显
- **ES8389 DAC 衰减 = -10 dB**：`numid=48/49 = 171`。PGA 调到 9 后扬声器太响，DAC 收一档刚好
- 配置写进 `scripts/board-audio-setup.sh` + `scripts/smoke.sh`（双保险）

### 运维

- **Token 三级解析**：`--token <jwt>` > `$LIVEKIT_TOKEN` env > `/opt/livekit/.token` 文件
- **smoke.sh 进版本管理**：`scripts/smoke.sh` 同步部署到板子 `/opt/livekit/smoke.sh`，不再散落
- **vendor reference 文档**：`docs/port/rv1126b/vendor-refs.md` 记录 MPP 头/示例的临时位置（不进仓库）

## 6.4 — H.265 / VP8 / VP9 多 codec

**目标**：取消"对端必须发 H.264"的脆弱依赖，对方推什么码板子都能解。

**实现**：

1. `RockchipMppH264DecoderImpl` → `RockchipMppVideoDecoderImpl`，构造接受 `MppCodingType`，`mpp_init` 用 `coding_`。`MPP_DEC_SET_PARSER_SPLIT_MODE` 仅在 AVC/HEVC 设（VP8/VP9 一包一帧不需要 split）
2. `RockchipMppVideoDecoderFactory::Create()` 按 `format.name` 分发：
   - H.264 → MPP CodingAVC
   - H.265 → MPP CodingHEVC
   - VP8 → `webrtc::CreateVp8Decoder(env)`
   - VP9 → `webrtc::VP9Decoder::Create()`
3. **livekit-ffi `cvt_i420` 补 I420→NV12 arm**（之前只有 I420→{RGBA,RGB24,I420}）。libvpx 出 I420，DRM 要 NV12，FFI 转换缺这一段时报 `failed to convert video frame to Some(Nv12)` 黑屏
4. `imgproc::i420_to_nv12` 包装 `libyuv::I420ToNV12`（NEON）

**踩过的坑**：以为 `MppCodingType` 枚举里有 VP8/VP9 就意味着支持，跑起来才看到 `mpp: unable to create dec vp8 for soc rv1126b unsupported`。MPP 是通用 API，per-SoC 的 codec 表才是真支持矩阵 — RV1126B 解码端只有 AVC + HEVC。

**实测**：

| 对端 codec | 板上路径 | 状态 |
|---|---|---|
| H.264 | MPP 硬解 | ✅ |
| H.265 | MPP 硬解 | ✅ video=102 帧 |
| VP8 | libvpx 软解 → I420 → FFI → NV12 → DRM | ✅ 出图 |
| VP9 | libvpx 软解 → I420 → FFI → NV12 → DRM | ✅（CPU 比 VP8 高一档） |

## 已知未做项（Phase 7+ 性能阶段）

| 项 | 现状 | 影响 |
|---|---|---|
| Phase 6.1.5 step C — dmabuf 零拷贝 | 未做 | HD 720P30 单核 ~100%；step C 后预期 ~50%。目前体验已可接受 |
| `mpp_buf_slot mismatch h_stride_by_pixel 960 - 768` warning | 未修 | H.265 simulcast 切层时偶现，不影响出图。需要进一步排 |
| 板上麦克回声消除 | 未做 | APM 整体关了。两端用扬声器外放时会回环。耳机/单向场景无感 |
| ALSA 1s buffer = 1s 延迟 | 未优化 | 音频通话级别明显但稳定；要 100-200ms 需要重做队列 |
| `lsb_release not found` | 未修 | cosmetic，server 上报 `os_version=Unknown` |
| LiveKit data track | 未通 | SCTP/DTLS 协商失败（jusiai NAT 路径），视频/音频 RTP 不受影响 |

## 实测：板↔手机 H.264 + H.265 + VP8 三种 codec 通话

```
==== smoke ==== url=wss://live.jusiai.com codec=h264 res=hd rotate=90 use_mpp=1
[mpp-dec] Create(H264 ...) → MPP coding=7        ← H.264 硬解路径
[mpp-dec] context ready (H264, out=100ms)
[loopback] T+5s..T+180s  alsa_underruns=0 alsa_dropped=0  video=N>0
final  video=N audio=M     ← 30 fps 双向稳定
```

| 维度 | H.264 (MPP) | H.265 (MPP) | VP8 (libvpx) |
|---|---|---|---|
| 板 → 手机 视频 | ✓ 720P30 ~30% 单核 | n/a（板不发 H.265）| n/a |
| 手机 → 板 视频 | ✓ 23-29% 单核 | ✓ 出图 | ✓ 出图，CPU 高一档 |
| 持续运行 | 3+ 分钟 0 crash | OK | OK |
| 音频 | 0 underrun | 0 underrun | 0 underrun |

## 运行手册（Phase 6 production-ready）

```bash
# 板上（ATK-DLRV1126B）：
ssh rv1126b-board /opt/livekit/smoke.sh         # 默认 H.264 / HD / rotate=90 / MPP on
ssh rv1126b-board /opt/livekit/smoke.sh --no-mpp   # 退回软编（OpenH264/libvpx）做对照
ssh rv1126b-board /opt/livekit/smoke.sh --res sd   # 480P30
ssh rv1126b-board /opt/livekit/smoke.sh --res fhd  # 1080P25
ssh rv1126b-board /opt/livekit/smoke.sh --rotate 0 # 关 sensor 旋转补偿

# 后台跑 + 看日志：
ssh rv1126b-board /opt/livekit/smoke.sh --bg
ssh rv1126b-board /opt/livekit/smoke.sh --tail

# Token：--token <JWT> > $LIVEKIT_TOKEN > /opt/livekit/.token
```

## 提交清单

`port/rv1126b-mpp` 分支（client-sdk-rust 内）：

```
86706c5  VP8/VP9 software fallback + I420→NV12 FFI conversion
b1ff3f8  Phase 6.4 — MPP H.265/VP8/VP9 hardware decode（H.265 部分）
a5dee38  MPP encoder hardware rotation via prep:rotation
af40b9c  disable WebRTC APM AGC/AEC/NS in built-in PCF
d7da5c9  Phase 6.1.5 step B — NV12 zero-conversion chain
862d727  Phase 6.1.5 step A — encoder I420→NV12 via libyuv NEON
0d2c8fd  Phase 6.2 — clean shutdown, eliminate exit segfault
97dcc14  Phase 6.2 — fix decoder hot-path stalls + simulcast freezes
db497bf  Phase 6.2 — broaden H.264 profile coverage + ops trace
ff94ac9  Phase 6.2 — Rockchip MPP H.264 decoder
f30bac5  6.1.4 fix — collapse duplicate MppPacket handle to one var
5aa4b0d  Phase 6.1.4 — real MPP encode path (init + put/get loop)
9419bfc  Phase 6.1 skeleton — Rockchip MPP H.264 encoder hooks
```

`port/rv1126b-phase-0-recon` 分支（superproject）：

```
647253b  bump client-sdk-rust to VP8/VP9 software fallback
057a1da  bump client-sdk-rust to Phase 6.4 MPP multi-codec decode
7af4302  bump SD/FHD bitrate caps for cleaner picture at edges
8b021d0  video resolution presets, max-bitrate cap, sensor rotation
65e2f09  version-control smoke.sh, document vendor reference layout
1c604c5  board speaker too loud — DAC playback default to -10 dB
323c476  outbound audio "from earpiece" — disable AGC + boost ES8389 mic PGA
074eefd  Phase 6.1.5 step B — NV12 zero-conversion chain
146feb5  Phase 6.1.5 step A — NEON I420→NV12 in DRM display + encoder
6f26aaa  Phase 6.2 — clean exit, no more libmpp atexit segfault
37ad197  Phase 6.2 perf — 30 fps decode, no latency accumulation
aa19b3e  Phase 6.2 verified — MPP H.264 decode path active end-to-end
2b4991f  Phase 6.2 — wire Rockchip MPP H.264 decoder
cb6d524  Phase 6.1.4 packet double-free in Rockchip MPP encoder
72460c8  Phase 6.1.4 — wire real Rockchip MPP H.264 encode path
62b7fc0  Phase 6.1 skeleton — bump client-sdk-rust to MPP encoder hooks
```

---

## Phase 7 进度（持续追加）

### 7.1 — MPP H.265 硬编（done 2026-04-27）

Encoder 通用化跟 6.4 decoder 对齐。`RockchipMppH264EncoderImpl` →
`RockchipMppVideoEncoderImpl` 接 `MppCodingType`，cfg 按 codec 分支
（h264:* vs h265:* 键名），IDR / 参数集 NAL 解析按 H.264 (bits[4:0])
和 H.265 (bits[6:1])分别处理，QP parser 用对应 H264/H265 BitstreamParser，
`CodecSpecificInfo` 在 H.265 路径只填 codecType（这版 webrtc 的
`CodecSpecificInfoUnion` 没 H265 成员）。Encoder factory 也 advertise
H.265 + 按 `format.name` dispatch。`smoke.sh --codec h265` 实测板↔手机
720P30 双向 MPP HEVC 30 秒 904 帧稳定。

### 7.2 — APM 选择性开启（done 2026-04-27）

NS 提到 `kVeryHigh`（默认 kModerate 太弱听不出）+ HPF 开（去 80Hz 以下
低频），AGC1/AGC2 继续关（保留手动 ES8389 PGA + 8× 软件 gain）。AEC
**仍关**——根因在 7.3 探索中说清楚了。`BOARD_LOOPBACK_APM_OFF=1` 一键
回 6.3 全关 baseline 做 A/B。实测 NS 能感知到背景噪声减弱。

### 7.4 — 帧级 AEC via livekit::AudioProcessingModule（done 2026-04-28）

**关键转折**：放弃改 SDK 内部 APM 路径，改用 SDK 已经暴露的
`livekit::AudioProcessingModule` 类做帧级 AEC。这个 wrapper 跑在
PCF 的 APM **外面**——我们直接在 BoardLoopback 的 ALSA 读/写路径
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

**实测打分**（双方外放对讲，ATK-DLRV1126B + PulseAudio + ES8389）：

| stream delay | 评分 | 现象 |
|---|---|---|
| 100ms | 0/100 | AEC 完全没消（hint 偏离实际太多，AEC 锁不住）|
| 200ms | 50/100 | 减小但持续有残余 |
| 300ms | 70/100 | 通话开始阶段漏掉的回声较多 |
| **400ms** | **90/100** | 最佳，残余只有 AEC3 收敛期前 5-10s |
| 500ms | 80/100 | 过了，回退一档 |

400ms 比典型手持机大，是因为这个声学链路堆叠：AlsaPlayer 队列
~50-300ms + PulseAudio ~50ms + ES8389 ~50ms + 房间 + ALSA capture
~100ms。其他硬件的 delay 不一样，按实际打分挑最优。

**为什么 7.3/7.4-ADM 那两次没行**：把 capture/playback 接到 ADM 后，
LocalAudioTrack 仍然从 `AudioTrackSource` 拿数据（不从 ADM 经 APM
处理后的版本拿），所以 AEC 看不到 capture 那一端，没法做 cancel。
这次绕过 PCF 的 APM、直接在 frame 流上挂 standalone AEC，从架构上
对了。

### 7.3 — Playback via ADM (尝试后回退 2026-04-28)

**目标**：把 BoardLoopback 自己的 ALSA writer 替换成 SDK 的 ADM driving
ALSA，让 APM 拿到 render reference 信号让 AEC 真生效。

**实现**：

- `livekit_ffi::AudioDevice` 加 ALSA 驱动：env-gated（`LIVEKIT_ADM_PLAYBACK_DEVICE`）
  开 ALSA mono、加 writer 线程 + queue 解耦避开 ALSA back-pressure
- `peer_connection_factory.cpp` 让 AEC 在 env 设了的时候自动开（mobile_mode）
- BoardLoopback 加 `BOARD_LOOPBACK_ADM_PLAYBACK=1` env：跳过自己的 AlsaPlayer，
  改让 ADM 拿数据
- `smoke.sh` 加 `--adm-playback` 选项

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
明显减少，加不加 `--adm-playback` 体感无差异。退出还引入了 segfault
（MPP teardown 与新增 audio 线程清理时序冲突）。

**根因分析**：BoardLoopback 的 capture 路径走 `AudioTrackSource::capture_frame()`
→ sinks，这条路径**不进 APM 的 AEC pipeline**。AEC 需要 capture 走 ADM 的
`RecordedDataIsAvailable()` 才能与 render reference 对齐时间相关。NS 在 7.2
能起作用是因为它是 capture-side 单向滤波器，路径要求宽松；AEC 是 capture/render
双向相关算法，路径要求严格得多。

**决策**：revert 全部 7.3 改动回 7.2 baseline。把 capture-via-ADM 留作
Phase 7.4 的明确范围 — 那才是让 AEC 真正生效的对的入手点。这次的探索
不是白做，验证了 ADM 可以驱 ALSA、APM 接收 render reference 路径正确，
为 7.4 铺了一半的路。

### Phase 7 后续候选

按价值密度排序：

1. **dmabuf 零拷贝**（Phase 6.1.5 step C 延伸）：V4L2 capture → MPP encode 同 dmabuf；MPP decode → DRM plane 同 dmabuf。预期单核 ~50%，腾出 CPU 做 AEC / NS
2. **AEC 收敛优化**：当前 7.4 的 90/100 残余主要在 AEC3 启动后 5-10s 收敛期内。可探索：把扬声器音量降下来（`amixer -c 0 cset name='DAC Digital Volume' 140`）减弱回声幅度；麦克风/扬声器物理隔离；或试 Rockchip 厂家的 rk_voice 硬件 AEC
3. **Buildroot 包补齐**：`lsb-release` 进 `BR2_PACKAGE_*`；libvpx 静态链上去再瘦点
4. **长跑测试**：7×24h soak（建议 /schedule agent 开起来），盯 RSS / fd / underrun 累计

参考 plan.md §Phase 7 与本文"已知未做项"表对齐。
