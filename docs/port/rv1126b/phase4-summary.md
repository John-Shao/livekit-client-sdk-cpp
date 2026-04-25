# Phase 4 工作总结 — 目标平台依赖闭环

> 完成日期：2026-04-25（远低于 plan.md 原估 0.5–2 天 —— 30 分钟，因为 Phase 0 早就把板上 rootfs 跟 Buildroot toolchain 锁到同 SHA `-g4a1fe4ec`）
> 分支：`port/rv1126b-phase-0-recon`
> 关联文档：[plan.md §Phase 4](plan.md) · [phase3-summary.md](phase3-summary.md) · [oplog.md](oplog.md)

## 交付

不需要修 Buildroot defconfig 也不需要重编 sysroot —— **板上 rootfs 已经覆盖 Phase 3 产物的全部 NEEDED 依赖**。Phase 4 实际是一次依赖审计 + 板端 dry-run。

## 依赖审计（`readelf -d`）

`build-rv1126b/lib/liblivekit.so` 的 NEEDED：

| 库 | 板上 | 备注 |
|---|---|---|
| `liblivekit_ffi.so` | 部署时同推（来自 client-sdk-rust release target） | 项目自产 |
| `libssl.so.3` | `/usr/lib/libssl.so.3` ✓ | OpenSSL 3.x |
| `libcrypto.so.3` | `/usr/lib/libcrypto.so.3` ✓ | OpenSSL 3.x |
| `libstdc++.so.6` | `/usr/lib/libstdc++.so.6.0.32` ✓ | **完全同 minor 版本**（与 Buildroot toolchain 同源） |
| `libm.so.6` | `/lib/libm.so.6` ✓ | glibc 2.41 |
| `libgcc_s.so.1` | `/usr/lib/libgcc_s.so.1` ✓ | GCC 13 runtime |
| `libc.so.6` | `/lib/libc.so.6` ✓ | glibc 2.41 |
| `ld-linux-aarch64.so.1` | `/usr/lib/ld-linux-aarch64.so.1` ✓ | dynamic linker |

`liblivekit_ffi.so` 的 NEEDED：仅上表中的 5 个（`libstdc++ / libm / libgcc_s / libc / ld-linux`）。`HelloLivekit{Sender,Receiver}` 的 NEEDED 一致 + `liblivekit.so`。

**关键观察**：`webrtc-sys` 把所有 WebRTC 相关 deps（abseil, glib, gio, gobject, X11, drm, gbm 等）以及 libwebrtc.a 自身都做了**静态链入** + **lazy `dlopen` stub** 处理 —— 所以最终运行时 NEEDED 极简，不需要在板上单独装 glib / X11 / drm 这些库。这是 Phase 1 决策路径 ① 的副产品。

## 板端 dry-run

**部署**（VM 没配 → 板子的免密 ssh，走 Windows 中转 stream）：

```bash
for f in liblivekit.so HelloLivekitSender HelloLivekitReceiver; do
  ssh rv1126b-vm "cat ~/livekit/livekit-sdk-cpp-0.3.3/build-rv1126b/{lib,bin}/$f" \
    | ssh rv1126b-board "cat > /tmp/livekit-dryrun/$f"
done
ssh rv1126b-vm 'cat ~/livekit/livekit-sdk-cpp-0.3.3/client-sdk-rust/target/aarch64-unknown-linux-gnu/release/deps/liblivekit_ffi.so' \
  | ssh rv1126b-board 'cat > /tmp/livekit-dryrun/liblivekit_ffi.so'
```

总传输 ~31 MB（板上分区 5.9G/4.6G 空闲，完全 OK）。

**验证 ld.so**：

```
$ ssh rv1126b-board 'cd /tmp/livekit-dryrun && LD_LIBRARY_PATH=. ldd ./HelloLivekitReceiver'
linux-vdso.so.1 (0x...)
liblivekit.so => /tmp/livekit-dryrun/./liblivekit.so (0x...)
libstdc++.so.6 => /lib/libstdc++.so.6 (0x...)
libm.so.6 => /lib/libm.so.6 (0x...)
libgcc_s.so.1 => /lib/libgcc_s.so.1 (0x...)
libc.so.6 => /lib/libc.so.6 (0x...)
/lib/ld-linux-aarch64.so.1 (0x...)
liblivekit_ffi.so => /tmp/livekit-dryrun/./liblivekit_ffi.so (0x...)
libssl.so.3 => /lib/libssl.so.3 (0x...)
libcrypto.so.3 => /lib/libcrypto.so.3 (0x...)
```

**所有 NEEDED 解析全绿**，没有 `not found` 一行。

**验证 main() 进入**：

```
$ ssh rv1126b-board 'cd /tmp/livekit-dryrun && LD_LIBRARY_PATH=. ./HelloLivekitReceiver --help'
Usage: HelloLivekitReceiver <ws-url> <receiver-token> <sender-identity>
  or set LIVEKIT_URL, LIVEKIT_RECEIVER_TOKEN, LIVEKIT_SENDER_IDENTITY
EXIT=0
```

二进制启动、解析 args、打印 usage、`exit(0)`。说明：
- ELF 加载器对 aarch64 binary 接受
- 所有动态链接库 resolve 成功
- C++ static 初始化执行成功（包括 spdlog / abseil / protobuf 的全局变量）
- Rust 运行时初始化成功（`liblivekit_ffi.so` 的 ctor）
- C++ → Rust FFI cxx-bridge 没有 ABI 错配
- `main()` 进入并 return 干净

**等于 livekit-sdk-cpp 在 ATK-DLRV1126B 板上能完整 load 起来。**

## Phase 4 plan.md Go 标准

> `cargo build -p livekit-ffi --target aarch64-unknown-linux-gnu` 链接通过，产物 `.so` 架构正确。

Phase 2 加码验证、Phase 3 集成 build 都已早早跨过这条线。Phase 4 加做的板端 dry-run 是更强的"可执行"验证（不仅 link 过、还能在目标硬件上 launch）。

## 不需要的 Buildroot 改动

plan.md §Phase 4 列出的 Buildroot defconfig 加包清单（`BR2_PACKAGE_OPENSSL` / `ALSA_LIB` / `LIBV4L` / `EUDEV` / `ROCKCHIP_MPP` / `ROCKCHIP_RGA` / `ROCKIT`）—— 这些在 ATK 出厂 `alientek_rv1126b_defconfig` 里**全部已开**（详见 [Phase 0 facts.md §2.4 §2.7](facts.md)），sysroot 里全部 `.pc` 都有。我们 Phase 3 cross-compile 时 link 的库直接从 sysroot 拿，板上同款。

## Phase 5 切入

板子已有 ATK 出厂固件（同 SHA `-g4a1fe4ec` rootfs，dry-run 已证可用），**不必再烧固件**。直接用 livekit server 跑端到端：

1. 起 livekit server（Cloud 或自建 K8s）拿 ws-url + token
2. `LIVEKIT_URL=wss://... LIVEKIT_RECEIVER_TOKEN=... ./HelloLivekitReceiver`
3. 看 RTC 协商 + ICE + DTLS-SRTP 媒体协商
4. 然后是音视频实际数据流（`/dev/video-camera0` MIPI 摄像头 → MPP H.264 → 上行；下行 → MPP 解码 → DRM/KMS 渲染）

参考 plan.md §Phase 5、§Phase 6（MPP 硬编集成）。
