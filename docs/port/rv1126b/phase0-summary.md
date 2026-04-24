# Phase 0 工作总结 — RV1126B livekit-sdk-cpp 移植

> 完成日期：2026-04-24  分支：`port/rv1126b-phase-0-recon`
> 关联文档：[plan.md](plan.md) · [facts.md](facts.md) · [oplog.md](oplog.md)

## 交付物

| 产物 | 位置 | 状态 |
|---|---|---|
| 移植计划 | [plan.md](plan.md) | 7 个 Phase 全程脚手架 |
| 事实表 | [facts.md](facts.md) | §1 主机 / §2 板端（含扩展 §2.7–2.8）/ §4 rootfs 全填，仅 §3 webrtc pinned 留给 Phase 1 |
| 操作日志 | [oplog.md](oplog.md) | 14 个已完成步骤 + Phase 1 切入建议 |
| 主机采集脚本 | [../../../scripts/phase0-collect-host.sh](../../../scripts/phase0-collect-host.sh) | 已验证 |
| 板端采集脚本 | [../../../scripts/phase0-collect-board.sh](../../../scripts/phase0-collect-board.sh) | 已验证 |
| SDK 完整同步 | VM `~/atk-dlrv1126b-sdk/`（21 GB） | `.repo/` 本地 objects + 工作树展开 |
| **Buildroot toolchain** | VM `buildroot/output/alientek_rv1126b/host/` | GCC 13.4.0 + 1.2 GB sysroot |
| **Firmware 镜像** | VM `output/firmware/` | `boot.img` / `rootfs.img` (1.3G ext2 或 460M squashfs) / `uboot.img` / `MiniLoaderAll.bin` |
| SSH 通路 | `ssh rv1126b-vm` / `ssh rv1126b-board` | 双向免密，记忆已存 |
| git 提交 | 分支 `port/rv1126b-phase-0-recon` | 8 笔 commit 已落地本地（未推 origin 前自行评估） |

## 硬事实（Phase 1+ 直接输入）

| 维度 | 值 |
|---|---|
| 目标 triple | `aarch64-buildroot-linux-gnu`（非 `aarch64-none-linux-gnu`，也非 `aarch64-unknown-linux-gnu`） |
| Toolchain | `aarch64-buildroot-linux-gnu-gcc 13.4.0` (Buildroot `-g4a1fe4ec`) |
| 板端 libc | glibc **2.41** |
| 板端 libstdc++ | `.so.6.0.32` (GCC 13 ABI) |
| 板端 OpenSSL | **3.x**（`libssl.so.3` + `libcrypto.so.3`） |
| 板端 MPP | `librockchip_mpp.so.{0,1}` + `/dev/mpp_service` + `/usr/bin/mpi_{enc,dec}*_test` |
| 板端 RGA | `librga.so.2.1.0` + `/dev/rga`（V4L2-M2M 路径） |
| 板端 GPU | **无 Mali**（仅软 Mesa + libdrm 2.124.0） |
| 板级 defconfig | `04_atk_dlrv1126b_mipi720x1280`（ATK + MIPI 720×1280 屏） |
| Buildroot defconfig | `alientek_rv1126b`（映射链已打通） |
| 硬件一致性 | toolchain 与板端 `/etc/os-release` 都是 **`-g4a1fe4ec`**，VM binary 可直接推板 |

## 重要"不做"决定

- **不走 GStreamer 视频管线** —— 板上无 Rockchip 一方 mpp 插件
- **不走 EGL/OpenGL 渲染** —— 无 Mali，改走 DRM/KMS planes
- **不选 BoringSSL 分支** —— 板上 OpenSSL 3.x 已在
- **不用 dmabuf heaps** —— `/dev/dma_heaps/` 缺失，零拷贝走 CMA 或 V4L2 DMABUF export
- **不走 prebuilt ARM GCC 10.3**（`aarch64-none-linux-gnu-`）—— sysroot 只有 libstdc++，不含用户态依赖

## 未闭环（留给 Phase 1 起手）

1. `client-sdk-rust/webrtc-sys` 的 `build.rs` 是否已有 `aarch64-unknown-linux-gnu` prebuilt URL？→ 决定走 [plan.md §Phase 1](plan.md#phase-1--libwebrtc-来源决策1-小时通常直接选-) 的 ①/②/③ 哪条分支
2. submodule `client-sdk-rust/` 当前未初始化（`ls` 空），需 `git submodule update --init --recursive` 再读
3. 板端硬件 smoke（`mpi_enc_test` + 摄像头抓帧）还没跑 —— Phase 1 起步可顺手验一下

## 耗时开销参考

- SDK repo sync (本地 checkout)：**28.8s**
- `./build.sh lunch` + 全量 `./build.sh`：**1h51min**（其中 U-Boot + Kernel ~30min、Buildroot ~1h20min、Firmware 打包 ~1min）
- 双路 SSH（VM + Board）联通 + Phase 0 采集：**~20min**
