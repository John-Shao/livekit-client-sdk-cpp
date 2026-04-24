# Phase 1 工作总结 — libWebRTC 来源决策

> 完成日期：2026-04-24（~30 分钟，plan.md 原估 1 小时）
> 分支：`port/rv1126b-phase-0-recon`
> 关联文档：[plan.md §Phase 1](plan.md) · [decision-webrtc.md](decision-webrtc.md) · [facts.md §3](facts.md) · [oplog.md [15]](oplog.md)

## 交付物

| 产物 | 位置 | 状态 |
|---|---|---|
| 决策文档 | [decision-webrtc.md](decision-webrtc.md) | 完整决策链 + 依据 + 备用路径 |
| `facts.md` §3 填完 | [facts.md](facts.md) | webrtc-sys 版本、prebuilt URL、pkg-config 依赖 |
| `client-sdk-rust` submodule 实体 | `./client-sdk-rust/`（HEAD `fd3df87`） | 本地 clone，**未固定 gitlink**（Phase 3 编通后再 pin） |
| oplog [15] | [oplog.md](oplog.md) | 记录 clone BUG 绕法 + pkg-config 验证步骤 |

## 硬事实

| 维度 | 值 |
|---|---|
| webrtc-sys 锚点 | `WEBRTC_TAG = "webrtc-7af9351"`（[`webrtc-sys/build/src/lib.rs:31`](../../../client-sdk-rust/webrtc-sys/build/src/lib.rs#L31)）|
| 架构映射 | `aarch64 → arm64`（第 57 行） |
| Prebuilt URL | <https://github.com/livekit/rust-sdks/releases/download/webrtc-7af9351/webrtc-linux-arm64-release.zip> |
| Prebuilt 大小 | **165,522,664 B ≈ 158 MB**（GitHub API 确认） |
| HTTP 校验 | `curl -sIL` 返回 200，重定向到 `release-assets.githubusercontent.com` |
| 下载触发点 | `webrtc-sys/build.rs:105` `webrtc_sys_build::download_webrtc().unwrap()`，仅当 `webrtc_dir()` 不存在时下载 |
| 自定义覆写机制 | 环境变量 `LK_CUSTOM_WEBRTC=/path/to/libwebrtc`（绕过下载）|
| pkg-config 硬依赖 | `glib-2.0` / `gobject-2.0` / `gio-2.0`，`unwrap()` 在 build.rs:174-176 |
| Sysroot 实测版本 | 三者均为 **2.76.1**（Buildroot 里自带，`.pc` + `.so` 齐全） |

## 决策

**走路径 ①：直接使用 LiveKit 官方 prebuilt。**

- **无需**自建 libWebRTC
- **无需**设置 `LK_CUSTOM_WEBRTC`
- `webrtc-sys/build.rs` 自动处理：target = `aarch64-unknown-linux-gnu` 时下载 `webrtc-linux-arm64-release.zip`，展开到 `scratch::path("livekit_webrtc")/livekit/linux-arm64-release-webrtc-7af9351/`

**排除的路径**：

- ② Chromium CI prebuilt — 现 ① 可用，无需
- ③ 源码构建 — 现 ① 可用，无需

## Phase 2 阻塞的遗留风险（非阻塞 Phase 1，**放 Phase 2 验证**）

1. **Prebuilt 内嵌 sysroot 的 glib ABI**：`webrtc-sys/build.rs:430-454 add_gio_headers()` 从 prebuilt 的 `include/build/linux/debian_bullseye_arm64-sysroot/usr/include/glib-2.0/` 取头。Chromium bullseye 分支 glib 通常 2.66.x，我们 Buildroot 2.76.1。理论上 glib 的 API 稳定，实际若出现 ABI 问题，用 Buildroot 头覆盖即可
2. **desktop_capturer 惰性加载的 X11 系列 `.so`**：板端无 X11，但 `add_lazy_load_so` 走 `.so.init.c` + `.so.tramp.S` stub 路径，不真正链接；运行时不触发调用就无事。IPC 用例不该触发
3. **`WEBRTC_USE_X11` 等 ninja defines**：`webrtc.ninja` 里可能含，会被 cxx_build 作为预处理宏。影响面预期仅 desktop_capturer 编译单元

## Phase 2 起手步骤（从 plan.md §Phase 2 摘）

```bash
# 在 VM 上
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
rustup target add aarch64-unknown-linux-gnu
rustup target list --installed | grep aarch64   # 验证

# 在 Windows 仓库里
# 1. 写 client-sdk-rust/.cargo/config.toml：指定 linker + sysroot
# 2. 写 scripts/env-rv1126b.sh：export 环境变量
# 3. 首次试编：cargo build -p webrtc-sys --target aarch64-unknown-linux-gnu
```

## 耗时

- Submodule clone（含 "initial ref transaction" BUG 绕法）：~3 分钟
- 读 `build.rs` + `build/src/lib.rs` 定位常量：~5 分钟
- GitHub API 查 prebuilt 大小：~10 秒
- VM 查 sysroot pkg-config：~10 秒
- 写三份文档（decision / facts / oplog）+ commit：~15 分钟
- **合计 ~30 分钟**
