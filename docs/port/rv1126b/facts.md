# RV1126B 移植 Phase 0 事实表

> 由 `scripts/phase0-collect-host.sh` 与 `scripts/phase0-collect-board.sh` 生成。
> 每个 `<!-- TODO -->` 占位符待填入实测数据。

## 1. 主机侧（ATK SDK 构建环境）

- ATK SDK 发布包：`atk_dlrv1126b_linux6.1_sdk_release_v1.2.1_20260327.tar.gz`
- 顶层 manifest：`rv1126b_linux6.1_release.xml` → `release/atk-dlrv1126b_linux6.1_release_v1.1.0_20260327.xml`
- 基础 manifest：`release/rv1126b_linux6.1_release_v1.2.0_20251220.xml`（Rockchip R2 branch，tag `linux-6.1-stan-rkr7`）
- ATK 覆写 tag：`atk-dlrv1126b-release-v1.0` / `v1.1`（覆写 buildroot、device/rockchip、u-boot、rkbin、kernel-6.1、camera_engine_rkaiq、ipc_drv_ko）
- SDK 根路径（VM）：`/home/alientek/atk-dlrv1126b-sdk/`
- 同步方式：`.repo/project-objects/` 本地自带（5.1GB），`./repo.sh` 仅做本地 checkout（`repo sync -l`，无需联网）
- Sync 后工作树大小：21 GB（含所有 external/*、yocto/、rtos/、hal/）
- Sync 后顶层：`app/ buildroot/ device/ docs/ external/ hal/ kernel/ kernel-6.1/ prebuilts/ rkbin/ rtos/ tools/ u-boot/ yocto/` + linkfile `build.sh` `Makefile` `rkflash.sh`
- 主机 OS：Ubuntu 20.04.2 LTS (focal) / gcc 9.4.0-1ubuntu1~20.04.1 / 9.4.0
  - 注意：Buildroot 某些较新包需要 Python 3.9+ 或 make 4.3+；focal 自带 Python 3.8 / make 4.2.1，遇到相关报错再装 backports

### 1.1 交叉编译工具链

SDK 内共有 **两套** aarch64 工具链，用途不同：

**(a) Prebuilt ARM toolchain**（仅用于 kernel / u-boot / rkbin 构建）
- 路径：`$SDK_ROOT/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/`
- 前缀：`aarch64-none-linux-gnu-`（bare-metal 三元组，无 sysroot 绑定）
- 别名：`aarch64-rockchip1031-linux-gnu-gcc -> aarch64-none-linux-gnu-gcc`（Rockchip 贴牌符号链接，避免 Rockchip 构建脚本到处改三元组）
- GCC 版本：`10.3.1 20210621`（ARM Toolchain A-profile 10.3-2021.07）
- `-dumpmachine`：`aarch64-none-linux-gnu`
- `-print-sysroot`：`.../aarch64-none-linux-gnu/libc/`（内含 `usr/lib64/libstdc++.so.6`，**其他库全缺**）
- **不用于** 编译 livekit 用户态代码（该 sysroot 只够 kernel/u-boot 自举，不含 OpenSSL/ALSA/libudev/MPP/RGA）

**(b) Buildroot host toolchain**（livekit 交叉编译用此套）
- 路径：`$SDK_ROOT/buildroot/output/rockchip_rv1126b/host/usr/bin/`（<!-- TODO 确认 defconfig 名 -->）
- 前缀：`aarch64-buildroot-linux-gnu-`
- GCC 版本：<!-- TODO 从 phase0-host.log 填 -->
- `-dumpmachine`：<!-- TODO -->
- `-print-sysroot`：<!-- TODO -->
- `-print-libgcc-file-name`：<!-- TODO -->
- 前提：Buildroot 需先至少构建一次（`make rockchip_rv1126b_defconfig && make`）

### 1.2 Sysroot 关键库清单

| 库 | 是否存在 | .so 路径 | .pc 路径 |
|---|---|---|---|
| libssl | <!-- TODO --> |  |  |
| libcrypto | <!-- TODO --> |  |  |
| libasound | <!-- TODO --> |  |  |
| libv4l | <!-- TODO --> |  |  |
| libudev | <!-- TODO --> |  |  |
| librockchip_mpp | <!-- TODO --> |  |  |
| librga | <!-- TODO --> |  |  |
| libstdc++ | <!-- TODO --> |  |  |

### 1.3 SDK 源码位置（sync 后已确认存在 ✓）

| 路径 | tag | 用途（livekit 视角） |
|---|---|---|
| `external/mpp` | `linux-6.1-stan-rkr7.1` | H.264/H.265/AV1 硬件编解码，必用 |
| `external/linux-rga` | `linux-6.1-stan-rkr7` | 2D 硬件加速/格式转换，备用 |
| `external/rockit` | `rv1126b-linux-6.1-stan-rkr7` | Rockchip 媒体中间件（MPP 的高层封装），可选 |
| `external/gstreamer-rockchip` | `linux-6.1-stan-rkr7` | 若走 GStreamer 管线集成则需要 |
| `external/alsa-config` | `linux-6.1-stan-rkr7` | ALSA userspace 配置 |
| `external/camera_engine_rkaiq` | `atk-dlrv1126b-release-v1.0` | 摄像头 ISP 调校，livekit 客户端一般不需要 |
| `external/rknpu2` / `rknn-toolkit2` / `rknn-llm` | `linux-6.1-stan-rkr7` | NPU，livekit 不用 |
| `buildroot/` | `atk-dlrv1126b-release-v1.0` | rootfs + 用户态 toolchain 生成器 |
| `kernel-6.1/` | `atk-dlrv1126b-release-v1.1` | Linux 6.1 内核源码 |
| `u-boot/` | `atk-dlrv1126b-release-v1.0` | bootloader |

### 1.4 Rust 环境

- `rustc` 版本：未安装（脚本输出 "rustup 未安装 — 先 curl https://sh.rustup.rs | sh"）
- `aarch64-unknown-linux-gnu` 已安装：n
- 后续：Phase 1 webrtc-sys 版本锁定后再决定安装 rustup 还是 distro rustc

## 2. 板端（ATK-DLRV1126B）

### 2.1 系统信息

| 项 | 值 |
|---|---|
| `uname -m` | <!-- TODO 期望 aarch64 --> |
| 内核 | <!-- TODO 例：Linux 6.1.x --> |
| `/etc/os-release` NAME | <!-- TODO 期望 Buildroot 2024.02 或 Debian 12 --> |
| libc 类型 | <!-- TODO glibc / musl --> |
| libstdc++ 版本 | <!-- TODO 例：GLIBCXX_3.4.32 --> |

### 2.2 CPU / 内存 / 磁盘

| 项 | 值 |
|---|---|
| CPU 型号 | <!-- TODO 例：Cortex-A53 × 4 --> |
| CPU 频率 | <!-- TODO 例：1.9 GHz --> |
| 内存 | <!-- TODO 例：512 MB LPDDR4X --> |
| 根分区剩余 | <!-- TODO 例：1.2 GB --> |

### 2.3 硬件设备节点

| 节点 | 存在 | 备注 |
|---|---|---|
| `/dev/mpp_service` | <!-- TODO y/n --> |  |
| `/dev/rga` | <!-- TODO --> |  |
| `/dev/dri/card0` | <!-- TODO --> |  |
| `/dev/video*` | <!-- TODO 数量/列表 --> |  |
| `/dev/dma_heaps/cma-uncached` | <!-- TODO --> |  |
| `/dev/dma_heaps/system` | <!-- TODO --> |  |

### 2.4 板端运行时库

| 库 | 路径 | 版本（若可查） |
|---|---|---|
| librockchip_mpp | <!-- TODO --> |  |
| librga | <!-- TODO --> |  |
| libasound | <!-- TODO --> |  |
| libssl / libcrypto | <!-- TODO --> |  |

### 2.5 网络 / 时钟

- 网卡 IP：<!-- TODO -->
- 默认网关：<!-- TODO -->
- 系统时间准确：<!-- TODO y/n，若 n 需 ntpdate -->
- LiveKit server 可达性：<!-- TODO 待 Phase 5 测 -->

## 3. webrtc-sys pinned 版本（Phase 1 决策直接输入）

- libWebRTC 版本 / commit：<!-- TODO 从 client-sdk-rust/webrtc-sys/libwebrtc/build.rs 查 -->
- aarch64-linux prebuilt URL 是否存在：<!-- TODO y/n -->
- prebuilt URL（若存在）：<!-- TODO -->

## 4. Rootfs 决策

- **选定**：Buildroot
- **板级 defconfig**（`build.sh lunch` 选这个）：
  `device/rockchip/rv1126b/04_atk_dlrv1126b_mipi720x1280_defconfig`
  - 匹配硬件：ATK-DLRV1126B + 竖向长条屏 (MIPI 720×1280)
  - `RK_BUILDROOT_CFG="alientek_rv1126b"` → 映射到 `buildroot/configs/alientek_rv1126b_defconfig`
  - `RK_KERNEL_CFG="alientek_rv1126b_defconfig"`
  - `RK_KERNEL_DTS_NAME="rv1126b-alientek-mipi720x1280"`
  - `RK_UBOOT_CFG="alientek_rv1126b"` + `RK_UBOOT_SPL=y` + `RK_USE_FIT_IMG=y`
  - `RK_WIFIBT=n`（该板型无 WiFi/BT；livekit 走有线）
  - `RK_ROOTFS_PREBUILT_TOOLS=y` + `RK_ROOTFS_INSTALL_MODULES=y`
- **Buildroot defconfig 组成**（`alientek_rv1126b_defconfig`，135 行，片段式 include）：
  - 架构：`chips/rv1126b_aarch64.config` → **aarch64**，glibc（由 base/base.config 默认）
  - 媒体：`multimedia/mpp.config` + `multimedia/audio.config` + `multimedia/camera.config` + `multimedia/gst/*` (rtsp/video/audio/camera)
  - GUI 栈：`gui/weston.config`（**Wayland** 合成器）+ `gui/qt5.config` + `gui/lvgl.config` + `gpu/mesa3d.config`
  - 字体/语言：`font/chinese.config`、`BR2_PACKAGE_NOTO_SANS_SC=y`、`BR2_TARGET_LOCALTIME="Asia/Shanghai"`
  - 网络：`connman` + `dnsmasq` + `ntp` + `openssh`
  - TLS：`GNUTLS_OPENSSL=y`，OpenSSL 被 nginx/openssh 传递拉入（不需要额外声明）
  - FFmpeg：`GPL=y NONFREE=y`，启用 x264/x265 —— **商用镜像需注意 license**
  - Rockchip 偏好：`BR2_PREFER_ROCKCHIP_RGA=y`
  - Rootfs overlay：`board/alientek/atk-dlrv1126b/fs-overlay/`
  - 体积警告：包含 OpenCV4、Qt5、Samba、LIVE555、MediaMTX、nginx-rtmp、完整 Python3 —— 首次 build 估计镜像 >300MB，后续若空间紧可按需裁
- **Kernel defconfig**：`alientek_rv1126b_defconfig`（在 `kernel-6.1/arch/arm64/configs/` 下）
- **备选 rootfs**：
  - `yocto/`（manifest 已 sync，未启用）——若 Buildroot 太大或包管理不够灵活再切
  - Debian 12（manifest 中注释掉，需手工启用）——IPC 场景空间敏感不首选

## 5. 原始收集输出（附）

<details>
<summary>主机侧脚本输出</summary>

```
<!-- TODO 粘贴 scripts/phase0-collect-host.sh 的完整输出 -->
```

</details>

<details>
<summary>板端脚本输出</summary>

```
<!-- TODO 粘贴 scripts/phase0-collect-board.sh 的完整输出 -->
```

</details>
