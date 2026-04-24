# LiveKit C++ SDK (client-sdk-cpp-0.3.3) 移植到 Rockchip RV1126B Linux 方案

## 一、背景与目标

**起因**：需要把 `livekit-sdk-cpp-0.3.3`（位于 `D:\workspace\Meeting\livekit-sdk-cpp-0.3.3`，已推送到 `github.com/John-Shao/livekit-sdk-cpp-0.3.3`）从 x86_64 Linux 移植到 **正点原子 ATK-DLRV1126B** 开发板运行。

**目标硬件**（RV1126B 全功能版，数据源：Rockchip RV1126B Linux6.1 SDK v1.2.0 / ATK 官方资料）：
- CPU：4 核 Cortex-A53 @ **1.9 GHz**（NPU 1.05 GHz）
- 架构：**64 位 ARMv8-A（aarch64）**
- 内存：DDR3/DDR4 / LPDDR3/LPDDR4/LPDDR4X
- 内核：**Linux 6.1**（可选 RT 补丁 6.1.141）
- U-Boot：2017.9
- Rootfs 三选一：**Buildroot 2024.02（默认）** / Debian 12 bookworm / Yocto 5.0
- 硬件编解码：Rockchip MPP + RGA + IOMMU + DMA-BUF
- 硬编能力：H.264/H.265 4K30（编+解），JPEG 4K30 解；**硬件完全不支持 VP8/VP9**
- 多媒体中间件：Rockit v2.46.0（wrap MPP，提供 VI/VENC/VDEC/VPSS/VO 等通道化模块）

> **重要**：RV1126B 与原版 RV1126（32 位 Cortex-A7）**架构不同**。本方案针对 B 版本的 aarch64，不能套用网上大量 RV1126 armhf 教程。

**最终形态**：音视频 + 数据通道全量功能。第一版先跑软件编解码（A53 × 4 @ 1.9 GHz 足以支撑 480p/720p），后续用 Rockchip MPP 硬件编解码器做 720p30 优化（目标 < 40% CPU）。**由于硬件不支持 VP8/VP9**，LiveKit server 必须强制 codec preference 为 H.264 或 H.265。

**项目结构要点（已踏勘）**：
- C++ 层是薄壳，真正承载实现的是 `client-sdk-rust/livekit-ffi` 产出的 `liblivekit_ffi.so`
- Rust 侧通过 `webrtc-sys` 包装 Google libWebRTC，`build.rs` 自动下载对应 target 的预编译二进制
- 构建系统已有 `RUST_TARGET_TRIPLE` 参数化机制（当前仅 macOS 使用），Linux aarch64 分支需要新增
- `cmake/protobuf.cmake:139` 已有 abseil x86 指令过滤机制，guard 中已包含 `aarch64`，复用无需扩展
- 最小可验证目标：`HelloLivekitSender` / `HelloLivekitReceiver`（只依赖核心 `livekit` 库，不需要 SDL3）

## 二、关键风险

aarch64 是 Rust / libWebRTC / Debian 的一等公民，原方案中"armv7 libWebRTC 预编译缺失"这个项目级风险**已不存在**。剩下都是标准交叉编译工程细节。

### 主要风险（按严重性）
- **webrtc-sys 对 Linux aarch64 的 prebuilt 是否已发布**：需要查 `client-sdk-rust/webrtc-sys/libwebrtc/build.rs` 的 target 映射表（LiveKit 自建的 libwebrtc 分发，也可能对 aarch64 Linux 有发布空档期；若无则走源码构建，A53 + 64 位环境下顺利度远高于 armv7）
- **Rootfs 分叉影响依赖获取策略**：Buildroot（默认）要 `make menuconfig` 勾选包后重建，迭代慢；Debian 12 可直接 `apt install ...:arm64`。选错 rootfs 会让 Phase 4 反复返工。
- **Codec 协商**：RV1126B 硬件不支持 VP8/VP9，LiveKit server 必须配置 codec preference 强制 H.264。若对端发 VP8/VP9 会走 A53 软件解码，拖 CPU。
- **MPP 编码器输入必须是 dmabuf/ion/drm**：WebRTC 的 `I420Buffer`（CPU 分配）不能直接喂给 MPP，需要 MppBuffer 中转或直接从 V4L2/DMA-BUF 输入——Phase 6 集成时影响帧缓冲路径设计。
- **MPP `encode_put_frame` 是阻塞调用**：等硬件释放输入帧才返回，WebRTC 编码线程需独立，否则卡 PeerConnectionFactory。
- **bindgen 不遵循 `CC` 的 sysroot**：仍需单独设 `BINDGEN_EXTRA_CLANG_ARGS`（aarch64 下同样存在）

## 三、分阶段实施方案

### Phase 0 — 信息归档与版本核对（0.5 天）

本阶段把已知事实固化到 `docs/port/rv1126b/facts.md`。基于 Rockchip SDK 文档已预填的部分：

**已知（从 Rockchip SDK doc 预填）**：
- Rootfs 默认：**Buildroot 2024.02**；可选 Debian 12 bookworm / Yocto 5.0
- 切换 rootfs：`RK_ROOTFS_SYSTEM=debian ./build.sh`（或 `buildroot` / `yocto`）
- Buildroot 工具链路径：`<SDK>/buildroot/output/rockchip_rv1126b/host/usr/bin/`
- Triple：`aarch64-buildroot-linux-gnu`（同时有 `aarch64-linux-*` 软链）
- GCC：**13.4.0**（无 GLIBCXX 旧 ABI 顾虑）
- 环境配置：`source buildroot/envsetup.sh rockchip_rv1126b` 自动导出 PATH、CROSS_COMPILE 等
- **便携式工具链打包**：`./build.sh bmake:sdk` 产出 `buildroot/output/*/images/aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz`，解压即可脱离 SDK 树独立使用（本项目推荐）
- MPP 源码：`<SDK>/external/mpp/` （github.com/rockchip-linux/mpp 镜像）
- Rockit 源码：`<SDK>/external/rockit/`
- Rockit headers：`<SDK>/external/rockit/mpi/sdk/include/rk_comm_video.h` 等
- MPP demo/模板：`<SDK>/external/mpp/test/mpi_enc_test.c` 和 `mpi_dec_test.c`
- **prebuilts 下的 `aarch64-none-linux-gnu` 是 bare-metal 风格，只能编 u-boot/kernel，不能编用户态程序**
- SDK 整体构建：`./build.sh all`（仅模块）或 `./build.sh`（含 update.img 打包）

**主机侧 —— 在 ATK SDK 根目录一键收集**（复制到 `scripts/phase0-collect-host.sh`）：
```bash
#!/usr/bin/env bash
# Phase 0: 主机侧事实收集
set -e
SDK_ROOT="${SDK_ROOT:-$HOME/atk-dlrv1126b-sdk}"
cd "$SDK_ROOT"

echo "=== SDK 版本 ==="
cat .repo/manifests/RV1126B_Linux6.1_SDK_Note.md 2>/dev/null | head -5 || echo "(SDK Note not found)"

echo "=== 工具链 ==="
TOOLCHAIN="$SDK_ROOT/buildroot/output/rockchip_rv1126b/host/usr/bin"
echo "PATH: $TOOLCHAIN"
"$TOOLCHAIN/aarch64-buildroot-linux-gnu-gcc" --version | head -1
"$TOOLCHAIN/aarch64-buildroot-linux-gnu-gcc" -dumpmachine
"$TOOLCHAIN/aarch64-buildroot-linux-gnu-gcc" -print-sysroot

echo "=== Sysroot 关键库 ==="
SYSROOT=$("$TOOLCHAIN/aarch64-buildroot-linux-gnu-gcc" -print-sysroot)
for lib in libssl libcrypto libasound libv4l libudev librockchip_mpp; do
  printf "%-20s " "$lib"; find "$SYSROOT" -name "${lib}*" 2>/dev/null | head -1
done

echo "=== pkg-config ==="
ls "$SYSROOT/usr/lib/pkgconfig/" 2>/dev/null | grep -E 'ssl|alsa|v4l|udev|rockchip|rga' | sort

echo "=== Rust target ==="
rustup target list --installed | grep aarch64 || echo "(not installed)"
```

**板端 —— 通过 ssh/串口跑**（复制到 `scripts/phase0-collect-board.sh`）：
```bash
#!/bin/sh
# Phase 0: 板端事实收集
echo "=== CPU 架构 ==="
uname -a
cat /proc/cpuinfo | head -10

echo "=== OS ==="
cat /etc/os-release

echo "=== 内存/磁盘 ==="
free -h
df -h /

echo "=== 硬件设备节点 ==="
ls /dev/mpp_service /dev/rga /dev/dri/ /dev/video* /dev/dma_heaps/ 2>&1

echo "=== MPP 库 ==="
find /usr /opt -name "librockchip_mpp*" 2>/dev/null
find /usr /opt -name "librga*" 2>/dev/null

echo "=== 网络 ==="
ip addr show | grep -E 'inet |link/' | head -6
# TLS 时钟校准：如需要，手动 date -s 或 ntpdate pool.ntp.org
date
```

**主机侧查 webrtc-sys pinned 版本**（阻塞 Phase 1 决策）：
```bash
cd ~/livekit/livekit-sdk-cpp-0.3.3/client-sdk-rust/webrtc-sys
grep -E 'version|WEBRTC_SDK_VERSION|http.*download' libwebrtc/build.rs | head -20
grep -rE '"aarch64.*linux"|linux.*aarch64' libwebrtc/build.rs | head -20
```

**Go 标准**：
- `docs/port/rv1126b/facts.md` 填充完整
- rootfs 选定（默认 Buildroot，除非需要 Debian 的 apt 生态）
- sysroot 绝对路径记录
- webrtc-sys 对 aarch64-linux 的 prebuilt URL 已知（存在/缺失）

---

### Phase 1 — libWebRTC 来源决策（1 小时，通常直接选 ①）

在 `docs/port/rv1126b/decision-webrtc.md` 记录。

**决策树**：
1. **①（大概率）webrtc-sys/build.rs 映射表含 aarch64-unknown-linux-gnu** → 直接用 LiveKit 官方 prebuilt，Phase 2 无需任何 WebRTC 相关改动
2. **② webrtc-sys 未配置 aarch64 Linux** → 检查是否能手动用 Google 官方 prebuilt（从 Chromium CI 产物拿），配置 `WEBRTC_LIB_PATH` / `WEBRTC_INCLUDE_PATH` 让 build.rs 跳过下载
3. **③（兜底）自行源码构建** → 在 x86_64 主机上交叉编译，耗时 hours 级（aarch64 target 比 armv7 顺利很多）

A53 性能足够不触发 MPP 急切需求 → 即使走 ③ 也可以并行推进 Phase 2-5，不阻塞主干。

---

### Phase 2 — Rust 交叉编译工具链（0.5–1 天）

**步骤**：

1. **安装 Rust target**（主机一次性）：
   ```bash
   rustup target add aarch64-unknown-linux-gnu
   rustup target list --installed | grep aarch64  # 验证
   ```

2. **创建 `client-sdk-rust/.cargo/config.toml`**（完整文件内容）：
   ```toml
   [target.aarch64-unknown-linux-gnu]
   linker = "aarch64-buildroot-linux-gnu-gcc"
   ar = "aarch64-buildroot-linux-gnu-ar"
   rustflags = [
     "-C", "link-arg=--sysroot=/opt/atk-rv1126b-sysroot",
     "-C", "target-feature=+neon,+crc",
   ]

   [env]
   PROTOC = "/usr/bin/protoc"  # 主机版 protoc
   ```
   > `linker` 只需要命令名，`source scripts/env-rv1126b.sh` 后 PATH 中可找到。

3. **创建 `scripts/env-rv1126b.sh`**（完整文件内容）：
   ```bash
   #!/usr/bin/env bash
   # LiveKit RV1126B 交叉编译环境
   # Usage: source scripts/env-rv1126b.sh

   # === 以下两行改成你机器上的实际路径 ===
   export ATK_TOOLCHAIN_ROOT="${ATK_TOOLCHAIN_ROOT:-$HOME/atk-rv1126b-toolchain}"
   export ATK_SYSROOT="${ATK_SYSROOT:-$ATK_TOOLCHAIN_ROOT/aarch64-buildroot-linux-gnu/sysroot}"

   # PATH 前置工具链
   export PATH="$ATK_TOOLCHAIN_ROOT/bin:$PATH"

   # Rust cross-compile 环境
   TARGET=aarch64-unknown-linux-gnu
   PREFIX=aarch64-buildroot-linux-gnu

   export CC_aarch64_unknown_linux_gnu="$PREFIX-gcc"
   export CXX_aarch64_unknown_linux_gnu="$PREFIX-g++"
   export AR_aarch64_unknown_linux_gnu="$PREFIX-ar"

   # pkg-config 指向目标 sysroot
   export PKG_CONFIG_ALLOW_CROSS=1
   export PKG_CONFIG_SYSROOT_DIR="$ATK_SYSROOT"
   export PKG_CONFIG_PATH="$ATK_SYSROOT/usr/lib/pkgconfig:$ATK_SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig"

   # bindgen 使用 libclang，不遵循 gcc 的 sysroot，必须单独告知
   export BINDGEN_EXTRA_CLANG_ARGS="--sysroot=$ATK_SYSROOT -I$ATK_SYSROOT/usr/include -I$ATK_SYSROOT/usr/include/aarch64-linux-gnu"

   # protoc 必须是 host 版
   export PROTOC="${PROTOC:-/usr/bin/protoc}"

   # 如果 Phase 1 选 ③ 本地构建 libWebRTC：
   # export WEBRTC_LIB_PATH=/path/to/local/webrtc/lib
   # export WEBRTC_INCLUDE_PATH=/path/to/local/webrtc/include

   echo "[env-rv1126b] TOOLCHAIN=$ATK_TOOLCHAIN_ROOT"
   echo "[env-rv1126b] SYSROOT=$ATK_SYSROOT"
   echo "[env-rv1126b] $($PREFIX-gcc --version | head -1)"
   ```

4. **若需便携工具链**（从 ATK SDK 打包一次，拷到 LiveKit 构建机）：
   ```bash
   # 在 ATK SDK 根目录
   ./build.sh bmake:sdk
   # 产物：buildroot/output/rockchip_rv1126b/images/aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz
   # 拷到 LiveKit 构建机并解压
   tar xzf aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz -C $HOME
   export ATK_TOOLCHAIN_ROOT=$HOME/aarch64-buildroot-linux-gnu_sdk-buildroot
   ```

5. **冒烟验证**（livekit-api 不依赖 webrtc-sys，作为最早验证）：
   ```bash
   cd ~/livekit/livekit-sdk-cpp-0.3.3/client-sdk-rust
   source ../scripts/env-rv1126b.sh
   cargo build --target aarch64-unknown-linux-gnu -p livekit-api
   file target/aarch64-unknown-linux-gnu/debug/liblivekit_api.rlib 2>/dev/null \
     || file target/aarch64-unknown-linux-gnu/debug/deps/liblivekit_api-*.rlib | head -1
   ```

**Go 标准**：`cargo build` 成功，产物 `file` 输出含 `ELF 64-bit LSB ..., ARM aarch64`；无链接/bindgen 错误。

---

### Phase 3 — C++ 端交叉编译（1–2 天）

**步骤**：

1. **新建 `cmake/toolchains/rv1126b-aarch64-linux-gnu.cmake`**（完整文件内容）：
   ```cmake
   # Toolchain for Rockchip RV1126B (Cortex-A53, aarch64, Buildroot 2024.02)
   # Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rv1126b-aarch64-linux-gnu.cmake ...
   # Prerequisite: env var ATK_TOOLCHAIN_ROOT and ATK_SYSROOT set (see scripts/env-rv1126b.sh)

   set(CMAKE_SYSTEM_NAME      Linux)
   set(CMAKE_SYSTEM_PROCESSOR aarch64)

   if(NOT DEFINED ENV{ATK_TOOLCHAIN_ROOT})
     message(FATAL_ERROR "ATK_TOOLCHAIN_ROOT not set; source scripts/env-rv1126b.sh first")
   endif()
   set(ATK_TOOLCHAIN_ROOT $ENV{ATK_TOOLCHAIN_ROOT})
   set(ATK_SYSROOT        $ENV{ATK_SYSROOT})

   set(CMAKE_SYSROOT       ${ATK_SYSROOT})
   set(CMAKE_FIND_ROOT_PATH ${ATK_SYSROOT})

   set(_prefix aarch64-buildroot-linux-gnu)
   set(CMAKE_C_COMPILER   ${ATK_TOOLCHAIN_ROOT}/bin/${_prefix}-gcc)
   set(CMAKE_CXX_COMPILER ${ATK_TOOLCHAIN_ROOT}/bin/${_prefix}-g++)
   set(CMAKE_AR           ${ATK_TOOLCHAIN_ROOT}/bin/${_prefix}-ar)
   set(CMAKE_STRIP        ${ATK_TOOLCHAIN_ROOT}/bin/${_prefix}-strip)

   set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
   set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
   set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
   set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

   # Cortex-A53 是 armv8-a + CRC + 可选 crypto（部分批次未开 crypto，先不加）
   set(CMAKE_C_FLAGS_INIT   "-march=armv8-a+crc")
   set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a+crc")

   # 传递给 webrtc-sys / bindgen
   set(ENV{BINDGEN_EXTRA_CLANG_ARGS}
       "--sysroot=${ATK_SYSROOT} -I${ATK_SYSROOT}/usr/include -I${ATK_SYSROOT}/usr/include/aarch64-linux-gnu")
   ```

2. **`CMakeLists.txt:166-180`**（扩展 RUST_TARGET_TRIPLE 的 Linux 分支）：
   - 当 `CMAKE_SYSTEM_NAME STREQUAL "Linux"` 且 `CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64"` → `RUST_TARGET_TRIPLE = "aarch64-unknown-linux-gnu"`
   - 顺手支持 `armv7` 以便未来其他板型复用

3. **`CMakeLists.txt:186-194`**（GCC_LIB_DIR 探测交叉友好化）：
   - `CMAKE_CROSSCOMPILING` 为真：用 `${CMAKE_C_COMPILER} -print-libgcc-file-name` 的 dirname 替代文件系统猜测
   - Native 路径保留原逻辑

4. **`cmake/protobuf.cmake:139`**：**无需修改**（现有 guard `APPLE AND arm64|aarch64` → 扩展为 `CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"` 即可全覆盖，一行改动）

5. **`CMakeLists.txt:196-234`**（`run_cargo.cmake` 模板）：
   - 交叉编译时 **不要** 设置 `RUSTFLAGS`（避免覆盖 `.cargo/config.toml` 的 linker）
   - 保留 `PROTOC`、`LD_LIBRARY_PATH` 传递

6. **`CMakeLists.txt:539`**（OpenSSL `find_package`）：无需改（自动遵循 `CMAKE_FIND_ROOT_PATH`）

**构建命令**：
```bash
cmake -S . -B build-rv1126b \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rv1126b-aarch64-linux-gnu.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIVEKIT_BUILD_EXAMPLES=ON \
  -DLIVEKIT_BUILD_TESTS=OFF
cmake --build build-rv1126b -j
```

**Go 标准**：`liblivekit.so`、`HelloLivekitSender/Receiver` 均为 aarch64 ELF。`readelf -A` 显示 `Tag_CPU_arch: v8`。

---

### Phase 4 — 目标平台依赖库（0.5–2 天，视 rootfs 选择）

**路径 A：Buildroot（默认，推荐给 IPC 类产品）**

通过 `make menuconfig` 在 Buildroot 配置里勾选下列包，然后 `make` 重建 sysroot：

| Buildroot 选项 | 用途 |
|---|---|
| `BR2_PACKAGE_OPENSSL` + `BR2_PACKAGE_OPENSSL_BIN` | OpenSSL 运行库 + 头文件 |
| `BR2_PACKAGE_ALSA_LIB` + `BR2_PACKAGE_ALSA_UTILS` | ALSA 用户态库 |
| `BR2_PACKAGE_LIBV4L` | V4L2 辅助库 |
| `BR2_PACKAGE_SYSTEMD`（或 `BR2_PACKAGE_EUDEV`） | libudev（cpal 可能需要） |
| `BR2_PACKAGE_ROCKCHIP_MPP` | 板级 MPP 库（Rockchip 自家包） |
| `BR2_PACKAGE_ROCKCHIP_RGA`（或 `BR2_PACKAGE_LIBRGA`） | RGA 2D 加速库（Phase 6 零拷贝色彩转换用） |
| `BR2_PACKAGE_ROCKIT` | Rockit 多媒体中间件（若 Phase 6 改用 Rockit 路线再勾；本方案不用） |

重建后新 sysroot 在 `<SDK>/buildroot/output/rockchip_rv1126b/host/usr/aarch64-buildroot-linux-gnu/sysroot/`，`.pc` 文件在 `sysroot/usr/lib/pkgconfig/`。

**便携部署**：使用 `./build.sh bmake:sdk` 打包一份 `aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz` 到 LiveKit 项目的独立构建机器，避免交叉编译环境和 ATK 完整 SDK 树耦合。

**路径 B：Debian 12 bookworm**

选 Debian rootfs 时（Rockchip 适配过的 bookworm），可直接用 apt：
```bash
# 在主机侧用 qemu-user 或直接板端 apt
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install libssl-dev:arm64 libasound2-dev:arm64 libv4l-dev:arm64 libudev-dev:arm64
# librockchip-mpp-dev 通常在 Rockchip 的 deb 仓库或从源码 install 进 sysroot
```

**关键检查**：`Cargo.lock` 中 `ring` / `rustls` / `openssl-sys` 激活情况：
- 若用 `ring`（常见）→ 不需要系统 OpenSSL
- 若用 `openssl-sys` → 需要 sysroot 里 `libssl-dev`（Buildroot 则 `BR2_PACKAGE_OPENSSL=y`）

**Go 标准**：`cargo build -p livekit-ffi --target aarch64-unknown-linux-gnu` 链接通过，产物 `.so` 架构正确。

---

### Phase 5 — 板端首次联调（1–2 天）

**部署**：
- `scp liblivekit_ffi.so liblivekit.so HelloLivekitSender HelloLivekitReceiver` 到板子 `/opt/livekit/`
- 运行：`LD_LIBRARY_PATH=/opt/livekit RUST_LOG=debug SPDLOG_LEVEL=debug ./HelloLivekitSender ...`

**预期故障（按概率）**：
1. **板端缺运行时 so**：`ldd` 检查 → 补齐
2. **TLS / 时钟**：板子未同步时间 → cert 校验失败 → `date -s` 或配 NTP
3. **GLIBCXX 版本不匹配**：若工具链 libstdc++ 比板子新 → 把工具链的 `libstdc++.so.6` 复制到 `/opt/livekit/` 并用 `LD_LIBRARY_PATH` 优先加载
4. **ALSA 设备**：hello 示例合成音频，一般不依赖麦克风

**冒烟升级**：把 `src/tests/integration/test_sdk_initialization.cpp` 交叉编译后在板端跑，作为 CI smoke。

**Go 标准**：板↔笔电一次 30 秒音频通话（Opus + 数据通道）无崩溃完成。

---

### Phase 6 — Rockchip MPP 硬件编解码器接入（可选优化，2–4 周）

A53 × 4 @ 1.9 GHz 软件编解码对 480p30 / 720p15 已可用，MPP 集成从"必须"降为"性能优化"。何时开始看 Phase 5 压测结果。

**API 层次选择**（三选一，本方案选 MPP 直接集成）：

| 层级 | 位置 | 优点 | 缺点 | 本方案取舍 |
|---|---|---|---|---|
| MPP (`rk_mpi`) | `<SDK>/external/mpp/` | 开源、帧级粒度与 `webrtc::VideoEncoder` 对齐、零拷贝可控 | 需要手管 `MppBufferGroup` / `MppFrame` 生命周期 | ✅ 采用 |
| Rockit (`rk_mpi_venc/vdec`) | `<SDK>/external/rockit/` | 中间件封装好，含 VI/VPSS/VO 完整链 | 通道化 API 不适配 WebRTC 帧级拉推、部分闭源 | ❌ |
| gst-rkmpp | Debian rootfs 预装 | 一行 pipeline 能跑 | 依赖 gstreamer runtime、延迟控制差 | ❌ |

**集成路径**：

1. **C++ 侧实现 MPP 编解码器**：
   - `client-sdk-rust/webrtc-sys/cpp/hw_codec/rockchip_mpp_encoder.{h,cc}` 与 `rockchip_mpp_decoder.{h,cc}`
   - 继承 `webrtc::VideoEncoder` / `webrtc::VideoDecoder`
   - 调用 `librockchip_mpp` 的 `mpp_init` / `encode_put_frame` / `encode_get_packet` 等（见 `<SDK>/external/mpp/test/mpi_enc_test.c` 为模板）

2. **关键 MPP 约束（来自 Rockchip MPP Developer Guide）**：
   - **输入必须是 dmabuf/ion/drm**：`encode_put_frame` 不接 CPU 分配的缓冲。需把 WebRTC 传入的 `I420Buffer` 先拷到 `MppBuffer`（用 `MppBufferGroup` 池化复用，避免每帧 malloc）。条件允许时直接从 V4L2 DMA-BUF 输入实现零拷贝。
   - **`encode_put_frame` 是阻塞调用**：WebRTC 编码线程必须独立于 PeerConnection 主循环，否则信令卡死。建议每个 MPP 实例一条独立的编码工作线程。
   - **H.264 输出固定 Annex B**（带 `00 00 00 01` 起始码）：与 WebRTC RTP 打包器兼容（WebRTC 内部会做 NAL length-prefix 转换，无需手工剥离）。
   - **rate control**：配置 `rc:mode=CBR` + `rc:bps_target` 与 WebRTC 自适应码率衔接；WebRTC 带宽估计输出通过 `SetRates()` 回调传给 MPP 的 `MPP_ENC_SET_CFG`。
   - **GOP 控制**：`rc:gop` 建议 2 × fps（30 fps → 60），配合 WebRTC 的 keyframe request 信号。

3. **RGA 零拷贝色彩空间转换**：
   - MPP 要求输入 NV12，WebRTC 内部用 I420 → 用 **RGA**（`/dev/rga`）做 I420 → NV12，零 CPU 占用
   - `librga` 在 Rockchip SDK 里自带，API 见 `<SDK>/external/linux-rga/`
   - DMA-BUF heap 选择（RV1126B 支持多 heap，写 `/dev/dma_heaps/<heap>`）：
     - `cma-uncached` —— 纯 GPU/VPU 访问首选（write-combine，无需 cache 维护）
     - `sys_uncached_heap` —— 非连续物理内存的 uncached 变体
     - `cma` —— CPU+硬件共享且容忍 cache 维护开销时用
   - LiveKit 场景推荐：WebRTC 编码器输入缓冲用 `cma-uncached`；解码输出到 CPU 处理前用 `cma` + 手动 sync

4. **摄像头采集路径（对接 WebRTC `VideoTrackSource`）**：
   - RV1126B 标准管道（Rockchip RKIPC 模式）：`VI_PIPE (V4L2/rkisp)` → `VPSS (RGA)` → `VENC (MPP)`
   - LiveKit 侧实现：自定义 `rtc::AdaptedVideoTrackSource` 子类，从 V4L2 `/dev/video*` 取帧（DMA-BUF mmap 或 `VIDIOC_DQBUF`），以 `VideoFrame` (I420 或 NV12) 喂给 `OnFrame()`
   - 若走零拷贝：`VideoFrameBuffer` 子类持有 DMA-BUF fd，WebRTC 编码器（若是 MPP）按 fd 直接读取
   - ISP 参数调优（去噪/白平衡等）在 `rkaiq` 里配置，本项目不碰，使用默认 IQ XML

5. **工厂注册**：
   - `RockchipMppVideoEncoderFactory` / `DecoderFactory`，实现 `webrtc::VideoEncoderFactory` / `webrtc::VideoDecoderFactory`
   - 在 `webrtc-sys` 的 `PeerConnectionFactoryBuilder` 接入点注入

6. **cxx-bridge 暴露**：
   - `webrtc-sys/src/hw_codec/rockchip_mpp.rs` 用 `#[cxx::bridge]` 声明启用接口
   - `livekit-ffi` Room 初始化路径加特性标志 `hw_codec_mpp`

7. **RV1126B MPP 硬编能力**（来自官方文档，已确认）：
   - 编码：**H.264 4K30、H.265 4K30**；**无 VP8/VP9**
   - 解码：**H.264 4K30、H.265 4K30、JPEG 4K30**；**无 VP9**
   - **必须与 LiveKit server 协商 H.264 / H.265**：server 端配置 codec preference，否则对端推 VP8/VP9 会走 A53 软件路径

8. **替代方案（不推荐但可作为 fallback）**：
   - GN 参数 `rtc_use_h264=true ffmpeg_branding="Chrome"` + 系统 ffmpeg 的 rkmpp 插件，控制力弱但工作量小

**Go 标准**：
- 发送端 720p30 H.264 编码 CPU < 40%（单核占用）
- 接收端同规格解码 CPU < 20%
- 端到端延迟 < 300 ms 单程（板子 → SFU → 对端）

## 四、并行与时间线

**可并行**：
- Phase 1-③（若走 WebRTC 源码构建）后台 || Phase 2/3 工具链搭建
- Phase 4 缺失依赖补齐 || Phase 3 CMake 调试
- Phase 6 编码器和解码器可分工

**用户决策点**：
- Phase 0 末尾：MPP 版本与板子 rootfs 是 Debian 还是 Buildroot —— 影响 Phase 4 策略
- Phase 1：若 webrtc-sys 无 aarch64 prebuilt，是否自建（几乎必然"是"）
- Phase 5 末尾：软件编解码性能是否满足 v1 → 决定是否立刻进 Phase 6

**单工程师全职时间线**：
- **Phase 0–5（软件编解码版跑通）：约 1.5–2 周**
- **Phase 6（MPP 硬件接入）：2–4 周（可选）**
- **合计 3.5–6 周**，首次移植按 6 周规划并预留 25% 缓冲

## 五、关键文件清单

### 需要修改（现有文件）
- `CMakeLists.txt:166-180` — `RUST_TARGET_TRIPLE` 新增 Linux aarch64/armv7 分支
- `CMakeLists.txt:186-194` — `GCC_LIB_DIR` 交叉探测改为 `-print-libgcc-file-name` 派生
- `CMakeLists.txt:196-234` — `run_cargo.cmake` 模板：交叉编译时不覆盖 `RUSTFLAGS`
- `cmake/protobuf.cmake:139` — guard 从 `APPLE AND arm64|aarch64` 放宽到 `CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"`
- `client-sdk-rust/.cargo/config.toml` — 新建/扩展：aarch64 target linker / rustflags

### 需要新建
- `cmake/toolchains/rv1126b-aarch64-linux-gnu.cmake` — CMake 交叉工具链
- `scripts/env-rv1126b.sh` — 交叉编译环境脚本
- `docs/port/rv1126b/facts.md` — Phase 0 事实表
- `docs/port/rv1126b/decision-webrtc.md` — Phase 1 决策记录
- Phase 6：`client-sdk-rust/webrtc-sys/cpp/hw_codec/rockchip_mpp_encoder.{h,cc}`、`rockchip_mpp_decoder.{h,cc}`、`webrtc-sys/src/hw_codec/rockchip_mpp.rs`

### 只读参考
- `client-sdk-rust/webrtc-sys/libwebrtc/build.rs` — 取 pinned commit 与 `WEBRTC_LIB_PATH` 覆盖机制
- `examples/hello_livekit/{sender,receiver}.cpp` — 首版验证目标
- `src/tests/integration/test_sdk_initialization.cpp` — 板端冒烟模板

## 六、端到端验证

**Phase 3 出口（宿主构建验证）**：
```bash
file build-rv1126b/liblivekit.so
# 期望：ELF 64-bit LSB shared object, ARM aarch64, ...
file build-rv1126b/examples/hello_livekit/HelloLivekitSender
# 期望：ELF 64-bit LSB executable, ARM aarch64, ...
aarch64-linux-gnu-readelf -A build-rv1126b/liblivekit.so | grep Tag_CPU_arch
# 期望：Tag_CPU_arch: v8
```

**Phase 5 出口（板端音频 + 数据通道冒烟）**：
```bash
cd /opt/livekit
LD_LIBRARY_PATH=. RUST_LOG=info ./HelloLivekitSender \
  --url wss://<livekit-server>/ --token <token> --room test_room
# 对侧在笔电/板子 B 上跑 Receiver
# 期望：30s 无断连、无 crash、日志无 ERROR
```

**Phase 6 出口（MPP 硬件编码性能基线）**：
- `top -p $(pidof HelloLivekitSender)` 观测 CPU < 40% @ 720p30 H.264 发送
- 对侧 `ffprobe` 确认分辨率/帧率/码率
- 端到端延迟双板时间戳测量，目标 < 300 ms 单程

**回滚策略**：
- 每个 Phase 修改在独立 git 分支（如 `port/rv1126b-phase-3-cmake-cross`），合入 main 前保证原 x86_64 Linux 构建不 regress
- 若有 CI：跑 x86_64 + aarch64 双目标，前者阻断合并

## 七、可执行任务清单（按顺序打钩）

### Phase 0 — 侦察（~0.5 天）
- [ ] 0.1 在 ATK SDK 根目录跑 `scripts/phase0-collect-host.sh`，输出粘到 `docs/port/rv1126b/facts.md`
- [ ] 0.2 在板端跑 `scripts/phase0-collect-board.sh`（ssh 或串口），输出粘到同一 facts.md
- [ ] 0.3 查 `client-sdk-rust/webrtc-sys/libwebrtc/build.rs` pinned 版本 + aarch64-linux prebuilt URL
- [ ] 0.4 决定 rootfs：Buildroot（默认）/ Debian 12 / Yocto 5.0
- [ ] 0.5 打开分支 `port/rv1126b-phase-0-recon`，提交 `docs/port/rv1126b/plan.md` 与 `facts.md`

### Phase 1 — WebRTC 决策（~1 小时）
- [ ] 1.1 根据 build.rs 查询结果，在 `docs/port/rv1126b/decision-webrtc.md` 记录决策（① LiveKit prebuilt / ② 官方 Chromium prebuilt / ③ 源码自建）
- [ ] 1.2 若选 ③：在独立构建机/容器 clone webrtc 源码，后台启动 aarch64 cross build

### Phase 2 — Rust 交叉（~0.5–1 天）
- [ ] 2.1 `rustup target add aarch64-unknown-linux-gnu`
- [ ] 2.2 若用便携工具链：`./build.sh bmake:sdk` 在 ATK SDK 里打包 → 拷贝到 LiveKit 构建机解压
- [ ] 2.3 新建 `scripts/env-rv1126b.sh`（本方案第 Phase 2 步骤 3 有完整内容）
- [ ] 2.4 新建 `client-sdk-rust/.cargo/config.toml`（第 Phase 2 步骤 2）
- [ ] 2.5 `source scripts/env-rv1126b.sh && cargo build --target aarch64-unknown-linux-gnu -p livekit-api` 通过
- [ ] 2.6 `file` 验证产物是 `ARM aarch64` → 提交 Phase 2 分支

### Phase 3 — C++ CMake 交叉（~1–2 天）
- [ ] 3.1 新建 `cmake/toolchains/rv1126b-aarch64-linux-gnu.cmake`（第 Phase 3 步骤 1）
- [ ] 3.2 编辑 `CMakeLists.txt:166-180`：扩展 `RUST_TARGET_TRIPLE` 的 Linux aarch64/armv7 分支
- [ ] 3.3 编辑 `CMakeLists.txt:186-194`：`GCC_LIB_DIR` 用 `-print-libgcc-file-name` 替代文件系统猜测
- [ ] 3.4 编辑 `cmake/protobuf.cmake:139`：guard 放宽到 `CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"`
- [ ] 3.5 编辑 `CMakeLists.txt:196-234`：交叉编译时不设 `RUSTFLAGS`
- [ ] 3.6 跑 `cmake -S . -B build-rv1126b -DCMAKE_TOOLCHAIN_FILE=...` 配置通过
- [ ] 3.7 `cmake --build build-rv1126b -j` 构建 `liblivekit.so` + `HelloLivekitSender/Receiver`
- [ ] 3.8 `file` + `readelf -A` 验证 aarch64 + ARMv8 → 提交 Phase 3 分支

### Phase 4 — 依赖库补齐（~0.5–2 天）
- [ ] 4.1 跑 `cargo build -p livekit-ffi --target aarch64-unknown-linux-gnu` 看缺哪些 `.pc`
- [ ] 4.2 Buildroot 路径：`make menuconfig` 勾 `BR2_PACKAGE_OPENSSL` / `ALSA_LIB` / `LIBV4L` / `ROCKCHIP_MPP` / `ROCKCHIP_RGA` 等 → `make` 重建 sysroot
- [ ] 4.3 Debian 路径：`sudo dpkg --add-architecture arm64 && sudo apt install libssl-dev:arm64 ...` 到 sysroot
- [ ] 4.4 重跑 cargo build 无链接错误 → 提交 Phase 4 分支

### Phase 5 — 板端首调（~1–2 天）
- [ ] 5.1 `scp` 4 个 binary 到板子 `/opt/livekit/`
- [ ] 5.2 `ldd` 检查板端缺运行时 so，补齐
- [ ] 5.3 板子 `date -s` 同步时间，确保 TLS 不失败
- [ ] 5.4 跑 `test_sdk_initialization` 冒烟（不需要 LiveKit server）
- [ ] 5.5 配 LiveKit sandbox/自建 server（**codec preference 强制 H.264**）
- [ ] 5.6 板↔笔电 HelloLivekitSender/Receiver 跑 30s 音频 + 数据通道
- [ ] 5.7 压测软件 VP8/H.264 编码 CPU 占用，决定是否进 Phase 6 → 提交 Phase 5 分支

### Phase 6 — MPP 硬编（~2–4 周，可选）
- [ ] 6.1 读 `<SDK>/external/mpp/test/mpi_enc_test.c` 与 `Rockchip_Developer_Guide_MPP_EN.pdf` 第 3.4-3.5 节
- [ ] 6.2 在 `webrtc-sys/cpp/hw_codec/` 新建 `rockchip_mpp_encoder.{h,cc}`
- [ ] 6.3 实现 `webrtc::VideoEncoder` 接口（`InitEncode` / `Encode` / `SetRates` / `Release`）
- [ ] 6.4 实现 MppBufferGroup 池化 + 独立编码工作线程
- [ ] 6.5 实现 RGA I420→NV12 转换（走 librga，零拷贝）
- [ ] 6.6 新建 `rockchip_mpp_decoder.{h,cc}`，类似套路实现 `webrtc::VideoDecoder`
- [ ] 6.7 新建 `webrtc-sys/src/hw_codec/rockchip_mpp.rs`（cxx-bridge）
- [ ] 6.8 在 `webrtc-sys` 工厂接入点注入 MPP 工厂
- [ ] 6.9 `livekit-ffi` 加 `hw_codec_mpp` feature flag
- [ ] 6.10 板端 720p30 H.264 压测，CPU < 40% → 达标提交 Phase 6 分支

### 合入 main（最终）
- [ ] 所有 Phase 分支合并回 `port/rv1126b-integration`
- [ ] 补 CI（x86_64 + aarch64 双目标）
- [ ] 验证 x86_64 Linux 原生构建不 regress
- [ ] `git rebase -i main` 整理提交历史 → merge to main
