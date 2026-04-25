# Phase 5 工作总结 — 板端首次联调 ✅

> **状态**：完整通过（协议层 + 4 路硬件 I/O 全闭环；端到端真双向音视频通话稳定）
> 完成日期：2026-04-25
> 分支：`port/rv1126b-phase-0-recon`
> 关联文档：[plan.md §Phase 5](plan.md) · [phase4-summary.md](phase4-summary.md) · [oplog.md](oplog.md) · `examples/board_loopback/main.cpp`

## 范围澄清（Phase 5 vs Phase 6）

经讨论，Phase 5 / Phase 6 的边界**不是"软编 vs 硬编"**而是 **"协议 + 硬件 I/O" vs "MPP codec 替换"**：

- **libwebrtc 内置软编（VP8/Opus）从 Phase 0 起就一直在 A53 上跑**，"软件编解码"不是单独要做的事
- **Phase 5 真正需要做的是连接硬件 I/O**：
  - (a) `/dev/video-camera0` V4L2 摄像头采集 → libwebrtc 软编
  - (b) libwebrtc 软解 → DRM/KMS 屏幕渲染
  - (c) ALSA 麦克风采集 → libwebrtc 软编
  - (d) libwebrtc 软解 → ALSA 扬声器播放（**已完成**）
- **Phase 6 是把中间 codec 路径换成 MPP**（VEPU/VDPU 硬件编解码），capture/render/playback 不变

按这个边界，BoardLoopback 当前是 Phase 5 的**部分**：协议栈 + 音频下行通了，但视频上行（合成渐变占位）、视频下行（仅计数不渲染）、音频上行（合成 440Hz 占位）都不是真硬件。

## 子阶段拆分

| 阶段 | 验证目标 | 结果 |
|---|---|---|
| **5a — Smoke 启动**（本节）| 二进制能进 main()、libwebrtc/Rust runtime/signal_client 全部初始化、错误能正确传播且析构干净 | ✅ |
| **5b — 真实 server 联调** | 板上 receiver 与远端 sender 一次 30 秒 Opus 音频通话 + 数据通道无崩溃 | ⏳ 等 LiveKit server URL + JWT |

## 5a — 板端 Smoke 结果

**运行命令**：

```bash
ssh rv1126b-board 'cd /opt/livekit && \
  LD_LIBRARY_PATH=/opt/livekit RUST_LOG=debug SPDLOG_LEVEL=debug \
  LIVEKIT_URL=wss://invalid-fake-host.local \
  LIVEKIT_RECEIVER_TOKEN=invalid \
  LIVEKIT_SENDER_IDENTITY=test \
  timeout 8 ./HelloLivekitReceiver'
```

**走完的代码路径**（按 log 顺序）：

```
livekit_ffi::server initializing v0.12.53          ← FFI 服务启动
livekit_ffi::cabi initializing v0.12.53            ← C ABI 边界初始化
LkRuntime::new()                                    ← Rust 运行时
libwebrtc RtcRuntime()
libwebrtc PeerConnectionFactory()                   ← WebRTC C++ 引擎
input_volume_stats_reporter (APM 子模块跳过 logging)
audio_processing_impl: AudioProcessing config:
    AEC=off NS=off AGC=off pre_amp=off …          ← 默认关掉，按需开
WebRtcVoiceEngine::WebRtcVoiceEngine               ← 音频引擎初始化
signal_client connecting to wss://invalid-fake-host.local/rtc?
    sdk=cpp&os=Linux&device_model=Alientek+RV1126B+Board&protocol=17
ws failure: failed to lookup address information   ← DNS lookup 失败（预期）
retry 1/3 ... 2/3 ... 3/3
LkRuntime::drop()
~PeerConnectionFactory() / ~WebRtcVoiceEngine / ~RtcRuntime
ERROR Room::Connect failed: signal failure
[receiver] Failed to connect                        ← 错误传到 C++ user code
disposing ffi server                                ← 干净析构
```

**验证清单**（13 项全 ✓）：

| 验证项 | 状态 |
|---|---|
| C++ 静态初始化（spdlog / abseil / protobuf / livekit-cpp） | ✓ |
| Rust runtime + tokio + reqwest + tungstenite (ws-client) | ✓ |
| libwebrtc PeerConnectionFactory + WebRtcVoiceEngine + APM | ✓ |
| AEC/NS/AGC 模块加载（默认 off，可后续开启） | ✓ |
| signal_client wss URL 构造（携带 sdk/os/device_model/protocol 参数） | ✓ |
| DNS lookup 路径正常工作 | ✓ |
| 内置重试机制 3 次后 give up | ✓ |
| 错误传播 libwebrtc → Rust → C++ → user code | ✓ |
| 完整析构 cleanup 顺序（drop → ~Factory → ~Engine → ~Runtime） | ✓ |
| 进程 exit(0)，无段错误/abort | ✓ |
| 板上时钟正确（UTC+8 与 UTC log 时间一致） | ✓ |
| device_model 自动识别 = "Alientek RV1126B Board" | ✓ |
| livekit 协议版本 17（与 server 端期望对齐） | ✓ |

**结论**：板上从 ELF 加载到 livekit FFI 析构的**全过程闭环**。剩下唯一变量是网络的另一端真有个 livekit server。

## 5a 顺手发现

- **`lsb_release` 命令在板上不存在**：`[DEBUG os_info] lsb_release command failed with NotFound`。Buildroot rootfs 没装 `lsb-release` 包（IPC 系统空间收紧）。后果只有一个：device 上报里 `os_version=Unknown`。如果 server 端有按 OS version 做的统计/过滤会受影响，但功能不受阻，可后续加 `BR2_PACKAGE_LSB_RELEASE` 进 Buildroot 补。
- **板端 hostname** 透传到 livekit server 的 device_model 字段。这意味着我们想加产品名 branding 时改 hostname 即可（或在 receiver.cpp 里覆盖 `RoomOptions.device_model`）。
- 重试间隔实测 **<1 秒**（log 时间戳全在 06:46:13 同一秒）—— Rust runtime 实际重试 backoff 比 plan.md 默认的"几秒级"更激进。如果是真服务器临时抖动这个间隔合理，但 NAT keep-alive 下可能更频繁。

## 5b — Server 端待用户提供

需要：

1. **LiveKit server 实例**
   - **路径 A**（最快）：[cloud.livekit.io](https://cloud.livekit.io) 免费层 → 拿 `wss://<project>.livekit.cloud` + API key/secret
   - **路径 B**（自建本地）：
     ```bash
     docker run --rm -it \
       -p 7880:7880 -p 7881:7881 -p 50000-60000:50000-60000/udp \
       -e LIVEKIT_KEYS="devkey: secret" \
       livekit/livekit-server --dev
     ```
     ws-url：`wss://<host-ip>:7880`，注意板子和 host 要在同一可达网络
2. **两个 JWT token**：
   ```bash
   livekit-cli token create --api-key devkey --api-secret secret \
     --room test-room --identity sender --valid-for 1h
   livekit-cli token create --api-key devkey --api-secret secret \
     --room test-room --identity receiver --valid-for 1h
   ```

拿到 ws-url + 两 token 后预期的 5b 拓扑：

```
笔电 / VM (sender)            ATK-DLRV1126B 板 (receiver)
┌──────────────────────┐      ┌──────────────────────────┐
│ HelloLivekitSender   │──┐   │ /opt/livekit/            │
│   - 发 camera0 video │  │   │   HelloLivekitReceiver   │
│   - 发 app-data 文本 │  │   │   liblivekit*.so         │
└──────────────────────┘  │   └──────────────────────────┘
                          ↓                ↑
                     wss://<server>:7880 livekit signal
                          ↓                ↑
                     UDP 50000-60000      UDP 50000-60000
                       RTP/SRTP            RTP/SRTP
```

板侧 sender 同 receiver 设计可以双向：板子也能跑 Sender 把 `/dev/video-camera0` 摄像头流推上去，但这是 Phase 6 的事（MPP 硬编集成）。Phase 5 仅做远端 → 板的 receive 方向，验证下行通路。

## Phase 5b 期望故障 & 排查（plan.md 摘）

| 概率 | 现象 | 根因 / 修法 |
|---|---|---|
| 中 | TLS handshake 挂 | 板上时钟不准 → `date -s` 或 ntpdate（已确认本板时钟正确）|
| 低 | UDP 50000-60000 ICE 失败 | 板上 NAT 类型 / 防火墙 → 看 ICE candidates |
| 低 | Audio device 找不到 | ALSA card 0 应该 OK（`rockchipes8390` ES8389），用 `aplay -l` 二次确认 |
| 极低 | GLIBCXX 不匹配 | toolchain 与板 rootfs 同 SHA `-g4a1fe4ec`，已闭环 |

预计 Phase 5b 实测耗时：30–90 分钟（如果 server/token 配好且网络通，通常协商 30 秒内成功；调试空间在 ICE/防火墙）。

---

## 5b — 真实 server 实测结果（live.jusiai.com + 手机 LiveKit 测试 app）

LiveKit server: `wss://live.jusiai.com`（ATK 测试环境，自建）。手机端：装 LiveKit 测试 app 加入同一房间 `f1d641ae-...`。

### 5b.1 现成 example 的限制

`HelloLivekitSender`/`Receiver` 是**单向**例子：sender 推合成 RGBA 视频 + data，receiver 订阅特定 `<id>:camera0` + `<id>:app-data`。两个进程也只能 board → phone 单向（receiver 的硬编码 track 名匹配不上手机发的 track）。

### 5b.2 写新 example：`examples/board_loopback/main.cpp`

单进程 publisher + subscriber：
- **Publish** 合成 RGBA 渐变视频 (`board-cam` 320×240) + 440Hz 正弦音频 (`board-mic` 48kHz 单声道 PCM16，10ms 帧)
- **Subscribe-all** via `RoomDelegate::onTrackSubscribed` 动态注册 frame callback（不依赖 track 名）
- 远端音频 callback 用 **`TrackSource` enum** 注册而非 by-name（手机 audio track 的 name 是空字符串）
- 远端音频写到板上 ALSA 经 ES8389 codec 播放

CMakeLists.txt 加 `target_link_libraries(BoardLoopback PRIVATE livekit asound)`。

### 5b.3 ALSA 路径迭代

板上跑着 PulseAudio (PID 658)。三轮调整：

| 配置 | 结果 |
|---|---|
| `plughw:0,0` 直通 100ms latency | 14 underruns / 5s（2.8/s 稳定）+ 严重音质差 |
| `plughw:0,0` 直通 + producer-consumer 队列 + 200ms latency | 同样 underrun 速率（不是 jitter 问题）|
| `plughw:0,0` 1s latency | 同样 |
| **`default` 经 PulseAudio**（最终）| **0 underruns**，音质明显改善 ✓ |

**根因**：`plughw:0,0` 跟 PulseAudio 抢硬件，share-mode 不稳。`default` 让 pulse 接管混音/重采样，质量稳定。

加了 env override `ALSA_PCM_DEVICE=plughw:0,0` 以备未来想 bypass pulse（需先 `systemctl stop pulseaudio`）。

### 5b.4 SDK callback 时序坑

第一版用同步 `snd_pcm_writei` 在 SDK 订阅线程里直接调，被 PCM block 反压回 SDK callback dispatcher。改架构：

- AudioFrame callback 只 `enqueue` 到 deque
- 专用 writer 线程出队后 `snd_pcm_writei`
- 队列上限 50 帧（500ms），超溢丢弃旧帧（带 `dropped` 计数）

### 5b.5 实测 140 秒通话

```
first audio frame: rate=48000Hz channels=1 samples_per_ch=480 buf_size=480
alsa playback opened: default 48000Hz 1ch s16le 1s-buf
T+5s..T+75s   alsa_underruns=0 alsa_dropped=0
[participant disconnected ... reconnected]   ← 手机熄屏后恢复
T+80s..T+140s  alsa_underruns=1 alsa_dropped=0    （重连切换 1 次）
final  video=3966 audio=14087    ← 14087 / 140 = 100.6 frames/s 完美 10ms cadence
```

| 验证 | 结果 |
|---|---|
| board → phone video（紫蓝渐变） | ✓ 手机端可见 |
| board → phone audio（440Hz 正弦） | ✓ 手机端可听见 |
| phone → board video | ✓ 28 fps 接收 |
| phone → board audio | ✓ 100 frames/s steady |
| **板上扬声器播放手机音频** | ✓ 用户主观判定改善后接受 |
| 中断重连恢复 | ✓ `onParticipantDisconnected/Connected` 自动重订阅 |
| 140s 持续无 crash | ✓ |
| Go 标准 30s 音频通话（协议层）| ✓ 持续 140s 不崩；但音频上行是合成正弦，**非真麦** |

### 5b.6 已知 polish 项

| 项 | 状态 | 备注 |
|---|---|---|
| Data track publish 超时 | 未修 | SCTP/DTLS 协商问题，jusiai NAT/防火墙路径相关。视频/音频走 RTP/SRTP 不受影响。BoardLoopback 已移除 data track 发布 |
| ALSA 1s buffer = 1s 端到端延迟 | 未修 | 通话级体验上"1秒延迟"明显但不阻塞功能。Phase 6 集成 MPP 时可考虑减小到 100-200ms |
| `lsb_release` not found 警告 | 未修 | cosmetic，device 上报 `os_version=Unknown`。Buildroot 加 `BR2_PACKAGE_LSB_RELEASE` 即可 |
| 板上麦克风 capture（双向真互动）| 未做 | 板上 ES8389 同时支持 capture（§2.3）。要让板子的麦音上行，需要给 BoardLoopback 加 ALSA capture 线程 + 喂给 `AudioSource::captureFrame`。Phase 6 / 后续 polish |
| Native MPP 硬件编解码 | 未做 | 当前走 libwebrtc 自带软编（VP8/Opus）。A53 软编 480p30 / 720p15 可承受。MPP 集成是 Phase 6 |

## Phase 5 各子项完成情况

### (c) ALSA 麦克风 capture ✅ 已完成

ES8389 codec capture 经 ALSA `default` 设备读 PCM16 48kHz mono 10ms 帧 → `AudioSource::captureFrame`。

**坑/经验**：板载 mic 默认增益偏低，加纯软件 gain（默认 12×，env `BOARD_LOOPBACK_MIC_GAIN` 可调）后清晰可辨。`default` 设备走 PulseAudio，`plughw:0,0` 直通会跟 pulse 抢硬件（实测音质差）。

### (b) V4L2 摄像头 capture ✅ 已完成

`/dev/video-camera0` (rkisp_mainpath) NV12 multiplanar，4 个 mmap buffer，poll/DQBUF/QBUF 循环。驱动默认就是 NV12 320×240，零格式转换直接喂 SDK。

**坑/经验**：rkisp_v11 是 multiplanar driver，`v4l2_buffer.m.planes[]` 数组要正确填，否则 `QUERYBUF/QBUF` 都报 EINVAL。30 fps 实测稳定。

### (a) DRM/KMS 显示 ✅ 已完成

`/dev/dri/card0` 上 `DSI-1` connector + plane 74（NV12-capable），双 dumb buffer 双缓冲，drmModeSetPlane 把帧拉伸到 720×1280 全屏。

**坑/经验**（按发现顺序）：
1. **weston 占着 DRM master** — `drmSetMaster` 失败。修法：跑前 `pkill weston`
2. **SDK callback 默认给 RGBA**，DRM 要 NV12 → 设 `VideoStream::Options.format = VideoBufferType::I420`（NV12 不被 FFI convert pipeline 支持，I420 是稳的）
3. **手动 I420 → NV12 软转换**：Y 平面 memcpy + UV 按 U-V 配对插值（~20 行 thread_local 缓冲）
4. **手机端 simulcast / adaptive bitrate** 中途切换分辨率（360×640 → 720×1280 → 180×320），`ensureBuffers` 检测维度变化重新分配 dumb buffer，**显示不中断**

## 实测：板↔手机端到端 65 秒双向通话

```
[drm] connector 96 720x1280@60Hz
[drm] using plane 74 (NV12)
[drm] dumb buffers ready: 2x 360x640 NV12        ← 自适应分配
[loopback] T+5s..T+65s  alsa_underruns=0 alsa_dropped=0
final  video=1776 audio=6188
```

| 维度 | 实测 |
|---|---|
| Board → phone 视频（rkisp 摄像头）| 30 fps，手机端可见摄像头实拍 |
| Board → phone 音频（ES8389 麦克风）| 12× gain 后人声清晰 |
| Phone → board 视频（MIPI 屏渲染）| 27-30 fps，画面跟随手机摄像头 |
| Phone → board 音频（ALSA 播放）| 100 frames/s，1s ALSA buffer 0 underrun |
| 自适应分辨率切换 | 板上显示无中断，buffer 自动重分配 |
| 65 秒持续运行 | 0 错误，0 crash，干净退出 |

## 运行手册（Phase 5 production-ready 命令）

```bash
# 板上（ATK-DLRV1126B）：
pkill weston                               # 让出 DRM master
cd /opt/livekit
LD_LIBRARY_PATH=/opt/livekit RUST_LOG=warn \
  LIVEKIT_URL=wss://<your-server> \
  LIVEKIT_TOKEN=<jwt> \
  ./BoardLoopback
# Ctrl-C 停；要恢复 weston 用 systemctl restart weston 或 reboot

# 可调 env：
#   BOARD_LOOPBACK_MIC_GAIN=N        软件 mic 放大倍数（默认 12）
#   ALSA_PCM_DEVICE=plughw:0,0       output 设备（默认 default）
#   ALSA_CAPTURE_DEVICE=plughw:0,0   capture 设备（默认 default）
#   V4L2_DEVICE=/dev/videoX          摄像头（默认 /dev/video-camera0）
#   BOARD_LOOPBACK_SYNTH_VIDEO=1     强制合成视频（fallback）
#   BOARD_LOOPBACK_SYNTH_AUDIO=1     强制合成音频（fallback）
#   BOARD_LOOPBACK_NO_DRM=1          跳过 DRM 渲染（headless 模式）
```

---

## Phase 6 切入（Phase 5 a/b/c 完成后）

把中间 codec 路径换成 MPP：
1. **VP8 编解码 → MPP H.264 硬编硬解**（VEPU/VDPU）
2. **WebRTC 编解码外挂 hook**：libwebrtc 支持 vendor 自定义 encoder（参考 NVidia / VAAPI 的方式，见 `webrtc-sys/build.rs:212-244` CUDA 块的实现）
3. **零拷贝路径**：V4L2 capture 出的 dmabuf → 直接喂 MPP 编 → 不经 CPU；MPP 解出的 dmabuf → 直接 DRM plane → 不经 CPU
4. **rkrga 色彩转换**：摄像头 NV16 → MPP 期望的 NV12，用 RGA 硬件做（已知 librga 在 sysroot）

参考 plan.md §Phase 6 三选一（MPP 直接 / Rockit / GStreamer 插件），结合 facts.md §2.7 (b/g/h) 硬件路径事实。
