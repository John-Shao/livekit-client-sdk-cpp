# Phase 2 工作总结 — Rust 交叉编译工具链

> 完成日期：2026-04-24（首次 livekit-api smoke 25.77s，plan.md 原估 0.5–1 天）
> 分支：`port/rv1126b-phase-0-recon`
> 关联文档：[plan.md §Phase 2](plan.md) · [facts.md](facts.md) · [oplog.md](oplog.md)

## 交付物

| 产物 | 位置 | 状态 |
|---|---|---|
| 跨平台 env 脚本 | [../../../scripts/env-rv1126b.sh](../../../scripts/env-rv1126b.sh) | 一行 `source` 把 toolchain + Rust + pkg-config + bindgen + protoc 全部对齐 |
| VM 侧安装的 host 工具 | `rustup / cargo 1.95.0 / rustc / aarch64 target / protoc 3.21.12 / lld 10` | 通过 apt（protoc, lld）+ rustup one-liner |
| `client-sdk-rust` 实体 | VM `~/livekit/livekit-sdk-cpp-0.3.3/client-sdk-rust`（HEAD `fd3df87`）| 从 Windows 的拷贝 scp 过去 50M，VM 网络到 GitHub 不稳不重新 clone |
| 冒烟产物 | VM `target/aarch64-unknown-linux-gnu/debug/deps/liblivekit_api-*.rlib` | `file` 验证其内 `.o` 为 **ELF 64-bit LSB relocatable, ARM aarch64** ✓ |

## 硬事实

| 维度 | 值 |
|---|---|
| Host 机 | VM Ubuntu 20.04.2 (focal) / x86_64 |
| Rust 版本 | `rustc 1.95.0 (59807616e 2026-04-14)`，stable toolchain, **minimal profile**（省磁盘）|
| Rust 目标 triple | `aarch64-unknown-linux-gnu`（前缀交由 `CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER` 指定）|
| Buildroot prefix（GCC 侧）| `aarch64-buildroot-linux-gnu-` |
| 链接器链路 | `cargo → aarch64-buildroot-linux-gnu-gcc → ld.lld`（via `-fuse-ld=lld`，上游 .cargo/config.toml 硬要求）|
| lld 版本 | 10.0.0（Ubuntu focal apt 包） |
| protoc 版本 | PATH 里实际是 **3.21.12**（非 apt 的 3.6.1 —— 不知是哪装上来的，先享用） |
| 首次 livekit-api build 耗时 | **25.77s**（依赖已 cargo fetch 过，仅 incremental 编） |
| warning 数 | 14（upstream livekit-api 未用变量，与本 port 无关） |

## 关键决策

### (a) 不修 `client-sdk-rust/.cargo/config.toml`，只用 env 变量

上游那份已有 `rustflags = ["-C", "link-arg=-fuse-ld=lld"]` 注明 "need lld to link libwebrtc.a successfully"。我们**不污染**这个文件，通过 env 变量补齐 linker/ar：

```bash
CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-buildroot-linux-gnu-gcc
CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_AR=aarch64-buildroot-linux-gnu-ar
```

**好处**：后续 `git submodule add` 把 client-sdk-rust 固定下来时，零本地改动要保留。

### (b) `~/.cargo/env` 必须在 env-rv1126b.sh 里主动 source

踩了一坑：`ssh rv1126b-vm 'cargo build ...'` 这种非交互非登录的 shell **不读 `.bashrc`**，rustup 的 `cargo/rustc` PATH 注入因此失效 → `cargo: command not found`，但 tee 的 exit 0 把失败掩盖。

修法：env-rv1126b.sh 显式 `. $HOME/.cargo/env`（commit `a48c5ba`）。

### (c) `client-sdk-rust` 在 VM 上走 scp 不走 git clone

VM 到 github.com 的 git-https TLS 握手抖（首次 `git pull` 失败重试才成），同样的网络拉 40MB+ 的 client-sdk-rust 大概率失败或卡住。反正 Windows 这边已经 `fd3df87` clone 过，直接 `scp -r` 过去 50MB 一次搞定，HEAD 确定性一致。

## 踩的坑

1. **`cargo` not found**：.cargo/env 没被 sourced（见决策 b）
2. **`sudo` in non-interactive ssh**：`sudo apt install ... | tee` 无法做交互密码输入，所以 `sudo apt install lld / protobuf-compiler` 都改为"让用户在 VM 终端里自己跑一次"
3. **Submodule 空占位**：`.gitmodules` 里声明了 `client-sdk-rust` 但从未 commit 为 gitlink，`git submodule update --init` 无效。手动 clone + scp 解决

## 加码验证：webrtc-sys

Phase 2 plan.md Go 标准只要求 livekit-api。webrtc-sys 才是 Phase 3 的真实前置：一旦 cargo 要编 livekit-ffi → livekit → webrtc-sys，就会触发 158MB prebuilt 下载 + C++20 cxx-build + lld 链接。

首次 build.rs 下载在 67MB/158MB 卡死（国内 → github.com release-assets CDN TCP reset）。换 VM `wget -c --tries=50` 持续续传 —— 当前**后台跑**中，成功后切 `LK_CUSTOM_WEBRTC` 跳过下载重编验证。**不阻塞 Phase 2 结案**。

## Phase 3 切入建议

1. 读 [plan.md §Phase 3](plan.md)：CMake 通过 `ExternalProject`/`FetchContent` 或直接 `execute_process` 调 cargo 编 livekit-ffi
2. 先在 VM 上 `cargo build -p livekit-ffi --target aarch64-unknown-linux-gnu` 独立跑通（这是 Phase 2 的真正完结态，会连锁编 livekit + webrtc-sys）
3. 然后配 `CMakeLists.txt` 里的 `CMAKE_TOOLCHAIN_FILE` 指向 Buildroot toolchain，看 C++ 侧能否 link 起来
4. 坑点预期：bindgen 对 sysroot 头的解析 / cxx-build 的 C++20 flag / 最终链接时 libwebrtc.a 的符号是否对得上 lld

## 耗时

- apt install protobuf-compiler / lld：~1 min（用户交互）
- rustup 安装 + aarch64 target：~1 min
- 写 env-rv1126b.sh：~5 min
- 踩 cargo-not-found 坑 + 修 env 脚本：~5 min
- client-sdk-rust scp：~10s
- livekit-api cargo build：**25.77s**
- 文档（decision、facts、oplog、summary）：~15 min
- **加码 webrtc-sys 下载阻塞**：waiting, 不计入 Phase 2 结算
- **合计**（除下载等待）：~30 分钟
