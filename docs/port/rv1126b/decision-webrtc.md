# Phase 1 决策：libWebRTC 来源

> 决策日期：2026-04-24
> 决策人：John-Shao
> 关联计划：[plan.md §Phase 1](plan.md)

## 结论

**走路径 ①：直接使用 LiveKit 官方 prebuilt（`webrtc-linux-arm64-release.zip`）**

无需任何 WebRTC 相关源码改动，也不需要配置 `LK_CUSTOM_WEBRTC`。`webrtc-sys/build.rs` 在 target = `aarch64-unknown-linux-gnu` 时会自动下载并展开此 zip。

## 决策依据

### (a) WebRTC 版本锚点

文件 [`client-sdk-rust/webrtc-sys/build/src/lib.rs`](../../../client-sdk-rust/webrtc-sys/build/src/lib.rs) 硬编码：

```rust
pub const WEBRTC_TAG: &str = "webrtc-7af9351";
```

下载 URL 生成逻辑（`download_url()`）：

```rust
format!(
    "https://github.com/livekit/rust-sdks/releases/download/{}/{}.zip",
    WEBRTC_TAG,  // webrtc-7af9351
    format!("webrtc-{}", webrtc_triple())  // webrtc-linux-arm64-release
)
```

其中 `webrtc_triple()` 对我们的目标展开为 `linux-arm64-release`（`target_arch()` 第 57 行将 `aarch64 → arm64`）。

### (b) Prebuilt 存在性验证

```bash
$ curl -s https://api.github.com/repos/livekit/rust-sdks/releases/tags/webrtc-7af9351 \
  | jq -r '.assets[] | select(.name=="webrtc-linux-arm64-release.zip") | "\(.name) \(.size)"'
webrtc-linux-arm64-release.zip 165522664
```

- 文件大小：**165,522,664 B ≈ 158 MB**
- `HEAD` 请求返回 `200`，CDN 正常重定向到 `release-assets.githubusercontent.com`
- 发布渠道：<https://github.com/livekit/rust-sdks/releases/tag/webrtc-7af9351>

### (c) 交叉编译硬依赖已满足

`webrtc-sys/build.rs` 在 Linux 分支里硬 `unwrap()` 了 pkg-config probe（第 174-176 行）：

```rust
for lib_name in ["glib-2.0", "gobject-2.0", "gio-2.0"] {
    pkg_config::probe_library(lib_name).unwrap();
}
```

即 sysroot 里必须有这仨的 `.pc` 文件。Buildroot sysroot 实测：

| 库 | 版本 | .pc 路径 | .so 路径 |
|---|---|---|---|
| glib-2.0 | **2.76.1** | `usr/lib/pkgconfig/glib-2.0.pc` | `usr/lib/libglib-2.0.so.0.7600.1` |
| gobject-2.0 | **2.76.1** | `usr/lib/pkgconfig/gobject-2.0.pc` | `usr/lib/libgobject-2.0.so.0.7600.1` |
| gio-2.0 | **2.76.1** | `usr/lib/pkgconfig/gio-2.0.pc` | `usr/lib/libgio-2.0.so.0.7600.1` |

## 待 Phase 2 验证（但不阻塞）

1. **Prebuilt 的嵌入式 sysroot ABI 对齐**：`add_gio_headers()` 会从 `webrtc-linux-arm64-release.zip` 内嵌的 `include/build/linux/debian_bullseye_arm64-sysroot/usr/include/glib-2.0/` 取 glib 头文件 —— 该 sysroot（Debian Bullseye）的 glib 版本可能与我们的 Buildroot sysroot（2.76.1）不同。Chromium bullseye 分支通常用 glib 2.66.x。如果编译时出现 glib ABI 错误，可能需要强制用 Buildroot 头或给 prebuilt 打补丁
2. **Desktop capturer 惰性加载**：`add_lazy_load_so(builder, "desktop_capturer", ["drm", "gbm", "X11", "Xfixes", "Xdamage", "Xrandr", "Xcomposite", "Xext"])`（第 178 行）。板端无 X11，但这些是 `dlopen` 延迟加载（通过 `.so.init.c` / `.so.tramp.S` stub），**不走真正链接**。运行时调用到 `desktop_capturer` 相关 API 时才会尝试加载，我们的 IPC 用例不应触发
3. **WEBRTC_USE_X11 宏**：`webrtc.ninja` 定义里可能含 `WEBRTC_USE_X11=1`（用于 desktop_capturer）。读 `webrtc_defines()` 时会传给 cxx_build；预期影响面仅限 desktop_capturer 编译单元，非关键路径。首次全量编译出问题再细查

## 开放项

- **`client-sdk-rust` submodule 固定版本**：当前 clone 的 HEAD 是 upstream `main` 的 `fd3df87`（2026-04-24 拉的 20 commit 浅克隆）。livekit-sdk-cpp **0.3.3** 具体要求哪个 client-sdk-rust commit 未在本仓库 `.gitmodules` 或 CMakeLists.txt 里硬锚。
  - 做法 A：先不提交 submodule gitlink，待 Phase 3 CMake 实际编译通过后再回填
  - 做法 B：查阅 upstream livekit/livekit-sdk-cpp 0.3.3 release 时的 client-sdk-rust 版本（GitHub release notes 或 CI 配置）→ 显式 pin
  - 暂取做法 A，避免过早锁定。

## 备用路径（如 ① 失败）

- **路径 ②**：从 Chromium CI 拿 Google 官方 prebuilt，设 `WEBRTC_LIB_PATH` + `WEBRTC_INCLUDE_PATH`（即 `LK_CUSTOM_WEBRTC`）让 `build.rs` 跳过下载。耗时 1 天左右
- **路径 ③**：源码构建。参考 <https://webrtc.googlesource.com/src>，主机 x86_64 编出 aarch64 静态库 → 塞进 `LK_CUSTOM_WEBRTC`。耗时 1-3 天，磁盘 30GB+
