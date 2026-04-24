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
- 主机 OS / GCC 版本：<!-- TODO 需要 `lsb_release -a && gcc --version` -->

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

- **选定**：Buildroot（tentative，待板端实测确认）
- **理由**：
  - ATK 发布包默认预置 `buildroot/` 目录（`使用说明.md` 明确指示用户走 Buildroot 流程）
  - 顶层 manifest `rv1126b_linux6.1_release.xml` 未启用 debian（`<include debian12-rkr2/>` 注释掉）
  - 但 sync 后 `yocto/` 顶层目录存在（基础 manifest 启用了 `yocto-scarthgap-release-r2`），Yocto 保留为备选
  - Debian 12 需要 ~500MB+ rootfs，对 NAND/eMMC 空间敏感的 IPC 类场景不首选
- 待补事实：
  - Buildroot 针对 RV1126B 的 defconfig 文件名 <!-- TODO 例 `rockchip_rv1126b_defconfig` -->
  - `device/rockchip/` 下对应的 BoardConfig <!-- TODO 例 `.BoardConfig-*.mk` -->
  - Buildroot 默认 libc 是 glibc 还是 musl <!-- TODO 看 defconfig 里 BR2_TOOLCHAIN_BUILDROOT_LIBC= -->

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
