# RV1126B 移植 Phase 0 事实表

> 由 `scripts/phase0-collect-host.sh` 与 `scripts/phase0-collect-board.sh` 生成。
> 每个 `<!-- TODO -->` 占位符待填入实测数据。

## 1. 主机侧（ATK SDK 构建环境）

- ATK SDK 版本：<!-- TODO 例：rv1126b_linux6.1_release_v1.2.0_20251220 -->
- SDK 根路径：<!-- TODO 例：/home/johnshao/atk-dlrv1126b-sdk -->
- 主机 OS / GCC 版本：<!-- TODO 例：Ubuntu 22.04, gcc 11.4.0 -->

### 1.1 交叉编译工具链

| 项 | 值 |
|---|---|
| 路径（bin） | <!-- TODO 例：$SDK_ROOT/buildroot/output/rockchip_rv1126b/host/usr/bin --> |
| 前缀 prefix | <!-- TODO 例：aarch64-buildroot-linux-gnu --> |
| GCC 版本 | <!-- TODO 例：13.4.0 --> |
| `-dumpmachine` | <!-- TODO 例：aarch64-buildroot-linux-gnu --> |
| `-print-sysroot` | <!-- TODO --> |
| `-print-libgcc-file-name` | <!-- TODO --> |

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

### 1.3 SDK 源码位置

- MPP：<!-- TODO 例：$SDK_ROOT/external/mpp -->
- Rockit：<!-- TODO -->
- linux-rga：<!-- TODO -->

### 1.4 Rust 环境

- `rustc` 版本：<!-- TODO -->
- `aarch64-unknown-linux-gnu` 已安装：<!-- TODO y/n -->

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

- **选定**：<!-- TODO Buildroot / Debian 12 / Yocto 5.0 -->
- **理由**：<!-- TODO -->

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
