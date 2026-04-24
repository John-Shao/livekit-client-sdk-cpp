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

采集时间：2026-04-24T18:36:40+08:00，采集方式：`ssh rv1126b-board` + `scripts/phase0-collect-board.sh`（见 §5）。

### 2.1 系统信息

| 项 | 值 |
|---|---|
| `uname -a` | `Linux ATK-DLRV1126B 6.1.141 #28 SMP Mon Mar 23 15:01:49 CST 2026 aarch64 GNU/Linux` |
| 内核编译时间 | 2026-03-23 15:01 CST |
| `/etc/os-release` | Buildroot 2024.02（`VERSION=-g4a1fe4ec`，`ID_LIKE="buildroot"`） |
| `RK_BUILD_INFO` | `alientek@alientek Mon Mar 23 15:03:55 CST 2026 - alientek_rv1126b` —— 出厂镜像就是用 `alientek_rv1126b_defconfig` 编的 |
| libc | **glibc**（`/lib/libc.so.6` 1.5MB + `/lib/ld-linux-aarch64.so.1`） |
| libstdc++ | `libstdc++.so.6.0.32` → **GCC 13.x ABI**（后续 Buildroot host toolchain 要与此对齐） |

### 2.2 CPU / 内存 / 磁盘

| 项 | 值 |
|---|---|
| CPU | **ARM Cortex-A53**（`CPU part 0xd03`），4 核（dmesg/ps 显示 mpp_worker 0-2 + 主核 = 4） |
| CPU features | `fp asimd evtstrm aes pmull sha1 sha2 crc32 cpuid` —— **硬件 AES/PMULL/SHA 指令**可用，TLS 加速 |
| BogoMIPS | 48（启动态，不代表峰值） |
| 内存 | 1.9 GiB（空闲 1.5 GiB，buff/cache 312 MiB） |
| Swap | **0**（无交换分区，运行期不能假设虚拟内存） |
| 根分区 | `/dev/root` on `/`，5.9 GiB（已用 1.1 GiB / 19%，剩 4.6 GiB） |

### 2.3 硬件设备节点

| 节点 | 存在 | 备注 |
|---|---|---|
| `/dev/mpp_service` | ✓ | c 240:0，root:video rw |
| `/dev/rkvenc*` / `/dev/rkvdec*` | ✗ | RV1126B 不暴露独立 venc/vdec 节点，统一走 mpp_service |
| `/dev/rga` | ✓ | c 10:124 |
| `/dev/dri/card0` | ✓ | c 226:0 |
| `/dev/dri/renderD128` | ✓ | c 226:128（Mesa render node） |
| `/dev/video-camera0` | ✓ | symlink → `/dev/video23` |
| `/dev/video0..video16` | ✓ | ISP 流水线节点，合计 10+ 个 |
| `/dev/dma_heaps/` | ✗ | **缺失**——libwebrtc 零拷贝路径若预期 `/dev/dma_heaps/{cma-uncached,system}` 需改走 Rockchip CMA allocator 或 `ION` 老接口 |

### 2.4 板端运行时库

| 库 | 路径 | 版本 |
|---|---|---|
| librockchip_mpp | `/usr/lib/librockchip_mpp.so.{0,1}` | 同时存在 .0 和 .1 两个 soname（兼容 shim） |
| librga | `/usr/lib/librga.so.2.1.0` → `.so.2` | RGA API 2.x |
| librockit | **未安装** | 预期，因板级选了 GStreamer 档（非 `ipc`） |
| libasound | `/usr/lib/libasound.so` + `alsa-lib/*.so`（含 bluealsa/samplerate） | ALSA 用户态完整 |
| libssl / libcrypto | `/usr/lib/libssl.so.3` / `libcrypto.so.3` | **OpenSSL 3.x** ✓ |

### 2.5 网络 / 时钟

- 网卡：`wlan0` 有 IP `192.168.10.236/24`（注：板级 defconfig 设了 `RK_WIFIBT=n` 但 `alientek_rv1126b_defconfig` 里 include 了 `wifibt/wireless.config` 拉了 WiFi 用户态；RK_WIFIBT=n 只影响内核驱动/固件打包）
- 未启用以太网口（板子或没插网线，或 DTS 没配）
- 默认网关：`192.168.10.1 dev wlan0`
- DNS：`nameserver ::1 / 127.0.0.1`（connman 本地 stub）
- 系统时间：`Fri Apr 24 18:36:43 CST 2026` —— **正确**（与 NTP 或 RTC 已同步），TLS 证书校验不会挂
- LiveKit server 可达性：未测（Phase 5 再确认）

### 2.6 运行态进程

板子 idle 1h22m，无用户态 MPP/RockIPC/rockit 进程运行，仅以下内核线程：
- `mpp_worker_0/1/2`（MPP 内核端工作线程）
- `irq/64-rga2`（RGA2 IRQ 处理）

**注意**：§2.7 扩展扫描显示用户态实际有若干服务已在跑（rkaiq_3A_server、nginx、vsftpd、connmand、rpcbind 等），见下。

### 2.7 扩展扫描（Phase 1 决策输入）

采集方式：`ssh rv1126b-board 'bash -s'` + 临时扫描脚本，时间 2026-04-24T18:55；原始 192 行输出保存在 `phase0-board-extended.log`（git 未追踪）。下面只保留结论。

**(a) GStreamer**
- `gst-launch-1.0` 可用；`gst-inspect-1.0` 中**无 Rockchip HW-accel 一方插件**（`mppvideodec`/`mppvideoenc`/`rgasrc`/`kmssink`/`waylandsink` 均不存在）
- 仅 `video4linux2` + `libav`（ffmpeg 后端）+ 基础 typefind
- 结论：livekit 视频编解码走 `librockchip_mpp` 直接调用，不依赖 GStreamer

**(b) V4L2 / 摄像头**
- 摄像头已接入且活跃：`rkisp_mainpath`（`/dev/video-camera0` → `video23`）
- 默认采集格式：**640×480 NV16** (4:2:2)，可通过 `v4l2-ctl --set-fmt` 调高
- 驱动：`rkisp_v11` 3.2.0，Media 6.1.141
- ISP 拓扑：`rkaiisp` + `rkisp-statistics` + `rkcif-mipi-lvds*`（11 个 cif 节点）
- 用户态 AE/AWB/AF 服务 `rkaiq_3A_server` 已在 `127.0.0.1:4894` 监听

**(c) ALSA 音频**
- Card 0：`rockchipes8390` + **ES8389 codec**（硬件 IC）
- 同一 card 同时支持 playback + capture（headset codec 架构）
- 结论：livekit audio in/out 可直接用 `plughw:0,0`，无需 PulseAudio

**(d) Weston / 显示**
- `/usr/bin/weston` 已安装，但**未自启**（无 systemd service；`/proc/fb` 显示 `rockchipdrmfb`）
- 决策点：livekit 要么起 Weston 合成，要么直接 KMS+DRM planes 自管显示 —— Phase 3 渲染方案待定

**(e) libc 精确版本**
- **glibc 2.41**（Buildroot 2024.02 stable，2025 Copyright）
- 后续 VM toolchain `aarch64-buildroot-linux-gnu-gcc` 的 sysroot 必须对齐 glibc 2.41，否则运行时 `GLIBC_2.xx` 符号找不到

**(f) 监听端口（`ss -tlnp`）**
- `0.0.0.0:22` sshd
- `0.0.0.0:80` nginx（出厂 IPC web UI）
- `0.0.0.0:21` vsftpd（FTP —— 安全上建议后续关）
- `0.0.0.0:111` rpcbind + rpc.mountd/statd（NFS client）
- `127.0.0.1:53` connmand DNS stub
- `127.0.0.1:4894` `rkaiq_3A_server`
- 与 livekit 默认端口（7880 ws、RTP/UDP 动态）**无冲突**

**(g) MPP / RGA 测试二进制**（`/usr/bin/` 下齐全）
- `mpi_enc_test` / `mpi_enc_mt_test` / `mpi_rc2_test` —— 硬件编码 smoke test
- `mpi_dec_test` / `mpi_dec_mt_test` / `mpi_dec_multi_test` / `mpi_dec_nt_test` —— 硬件解码 smoke test
- **Phase 1 可以先用这些做硬件 smoke，再开始 livekit 集成**

**(h) GPU / Mesa**
- `libEGL.so.1` + `libGLESv{1_CM,2}.so` + `libgbm.so.1.0.0` + `libdrm.so.2.124.0` 都在
- **无 `libmali*`** —— RV1126B 当前档位**无 Mali 3D GPU**（或驱动未装），Weston 只能走软渲 (llvmpipe) 或纯 2D VOP 合成
- 结论：Phase 3 本地预览**优先走 DRM/KMS plane 直给**，避免 GLES 软渲染吃 CPU

**(i) 外网 / DNS**
- `1.1.1.1` ping RTT 75ms（WiFi → 公网）
- `www.livekit.io` DNS 能解析（connman stub + 上游 DNS OK）
- TLS 证书校验所需的系统时间准确性已在 §2.5 确认

**(j) 内核 CONFIG 抽样**（`/proc/config.gz` 可读）
- `CONFIG_V4L2_MEM2MEM_DEV=y` ✓
- `CONFIG_VIDEO_ROCKCHIP_ISP=y` + V1X/V21/V30/V32/V35/V39 全版本
- `CONFIG_VIDEO_ROCKCHIP_ISPP=y`（ISP 后处理）
- `CONFIG_VIDEO_ROCKCHIP_RGA=y`（V4L2-M2M RGA）
- `CONFIG_ROCKCHIP_RGA=n` 但 `CONFIG_VIDEO_ROCKCHIP_RGA=y`：`/dev/rga` (10:124) 来自 V4L2-M2M 路径
- `CONFIG_ROCKCHIP_MPP_SERVICE=y`（mpp_service 驱动）
- `CONFIG_ROCKCHIP_DRM_TVE=y`（TV 编码器，本板无用）

### 2.8 扩展扫描 → Phase 1 影响总表

| 决策点 | 基于事实 | 倾向结论 |
|---|---|---|
| 视频编解码通路 | GStreamer 无 mpp 插件；`/usr/bin/mpi_enc_test` 齐全 | 直调 `librockchip_mpp`，不绕 GStreamer |
| 渲染通路 | 无 Mali GPU；DRM + libdrm + KMS 可用 | DRM/KMS planes 直接 overlay，免 EGL |
| 音频通路 | ES8389 单卡双向 | ALSA `plughw:0,0`，无需 PulseAudio |
| TLS | OpenSSL 3.x on board + glibc 2.41 | webrtc-sys 选 OpenSSL 分支（非 BoringSSL） |
| 零拷贝 | 无 `/dev/dma_heaps/`，有 `/dev/rga` + MPP + V4L2-M2M | Rockchip CMA 或 V4L2 DMABUF export，不用 dmabuf heaps |
| 相机 | rkisp_v11 + rkaiq_3A_server running | 走标准 V4L2 capture，不需额外 ISP 初始化 |

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
<summary>板端脚本输出（MAC 地址已掩码）</summary>

```
Phase 0 板端侦察 — 2026-04-24T18:36:40+08:00

=== [1] CPU / 架构 ===
Linux ATK-DLRV1126B 6.1.141 #28 SMP Mon Mar 23 15:01:49 CST 2026 aarch64 GNU/Linux
processor	: 0..3 (Cortex-A53, CPU part 0xd03, features: fp asimd aes pmull sha1 sha2 crc32)

=== [2] OS / Rootfs ===
NAME=Buildroot VERSION=-g4a1fe4ec ID=buildroot VERSION_ID=2024.02
RK_BUILD_INFO="alientek@alientek Mon Mar 23 15:03:55 CST 2026 - alientek_rv1126b"
libc: /lib/ld-linux-aarch64.so.1 (198 KB) + /lib/libc.so.6 (1.5 MB)
libstdc++: /usr/lib/libstdc++.so.6.0.32

=== [3] 内存 / 磁盘 ===
Mem: 1.9Gi total, 1.5Gi free, 312Mi buff/cache; Swap: 0
/dev/root on /: 5.9G size, 1.1G used, 4.6G avail (19%)

=== [4] 硬件设备节点 ===
MPP:   /dev/mpp_service (240:0)  [no rkvenc/rkvdec nodes]
RGA:   /dev/rga (10:124)
DRM:   /dev/dri/card0 (226:0), /dev/dri/renderD128 (226:128)
V4L2:  /dev/video-camera0 -> video23; /dev/video0..video16 (10+ nodes)
DMA-BUF heaps: MISSING

=== [5] 用户态库 ===
librockchip_mpp: /usr/lib/librockchip_mpp.so.{0,1}
librga:          /usr/lib/librga.so.2.1.0, .so.2
librockit:       (not installed)
libasound:       /usr/lib/libasound.so + alsa-lib/{bluealsa,samplerate}
libssl/libcrypto: /usr/lib/lib{ssl,crypto}.so.3

=== [6] 网络 ===
wlan0: 192.168.10.236/24, MAC xx:xx:xx:xx:xx:xx
default gw 192.168.10.1 via wlan0
DNS: ::1, 127.0.0.1 (connman stub)

=== [7] 时钟 ===
Fri Apr 24 18:36:43 CST 2026 (correct)

=== [8] 进程 ===
只有 mpp_worker_0/1/2 和 irq/64-rga2 内核线程；无用户态媒体/网络应用
```

```
<!-- TODO 粘贴 scripts/phase0-collect-board.sh 的完整输出 -->
```

</details>
