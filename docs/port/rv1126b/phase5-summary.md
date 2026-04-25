# Phase 5 工作总结 — 板端首次联调

> **状态**：5a smoke 已通过 ✅，5b 等待 LiveKit server 端 token
> 起始日期：2026-04-25
> 分支：`port/rv1126b-phase-0-recon`
> 关联文档：[plan.md §Phase 5](plan.md) · [phase4-summary.md](phase4-summary.md) · [oplog.md](oplog.md)

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
