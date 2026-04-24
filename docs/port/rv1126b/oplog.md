# RV1126B 移植 · 操作记录

> 按时间顺序记录"实际执行并生效"的步骤，用于排错回溯和交接。
> 失败尝试、放弃的分支只在"备注"里一行带过，避免把主线埋掉。

---

## 2026-04-24

### [1] 仓库位置迁移（Windows 宿主机）

从 `D:\Projects\livekit-sdk-cpp-0.3.3` 迁至 `D:\workspace\Meeting\livekit-sdk-cpp-0.3.3`。

- 复制 `.claude/settings.local.json`（15 条 Rockchip PDF 读取 + pdftotext 权限），避免 Claude Code 重新审批
- 分支 `port/rv1126b-phase-0-recon` 已推到 `origin`（`github.com/John-Shao/livekit-sdk-cpp-0.3.3`）
- 全局 `~/.claude/`（计划文件、记忆）不受影响
- 备注：`.claude/settings.local.json` 在 commit `e655093` 被纳入仓库，后续考虑是否加 `.gitignore`

### [2] VM 侧拉取分支（alientek Linux）

```bash
cd ~/livekit/livekit-sdk-cpp-0.3.3
git checkout port/rv1126b-phase-0-recon
chmod +x scripts/phase0-collect-host.sh scripts/phase0-collect-board.sh
```

### [3] 定位 ATK SDK 并扁平化目录

SDK 解压位置原为 `~/atk-dlrv1126b-sdk/atk_dlrv1126b_linux6.1_sdk_release_v1.2.1_20260327/`，手工扁平化：

```bash
cd ~/atk-dlrv1126b-sdk/atk_dlrv1126b_linux6.1_sdk_release_v1.2.1_20260327
mv * ..
mv .repo ..
cd ..
rmdir atk_dlrv1126b_linux6.1_sdk_release_v1.2.1_20260327
```

最终 `SDK_ROOT=~/atk-dlrv1126b-sdk`，与 `scripts/phase0-collect-host.sh` 默认值一致，后续无需 export。

初始顶层内容：`.repo/` + `buildroot/` + `README.md` + `使用说明.md` + `repo.sh` —— **源码尚未 checkout**。

### [4] 确认 SDK 分发形态（本地 objects，无需联网）

```bash
cat repo.sh            # → .repo/repo/repo sync -l -j2
ls .repo/manifests/    # 看到 rv1126b_linux6.1_release.xml
du -sh .repo           # 5.1GB（objects 全量本地）
```

形态判定：**形态 B**（`.repo/project-objects/` 本地自带，`repo sync -l` 做纯本地 checkout，不联网）。

### [5] Windows 宿主机侧 manifest 溯源（Claude 执行）

直接从宿主机解压的 `.repo/manifests.git/` bare 仓库中读出 manifest 链（绕开 Windows 无法跟随 repo 符号链接的问题）：

- 顶层 → `rv1126b_linux6.1_release.xml` → `release/atk-dlrv1126b_linux6.1_release_v1.1.0_20260327.xml`（ATK 覆写层）
- 基础 → `release/rv1126b_linux6.1_release_v1.2.0_20251220.xml`（Rockchip R2）
- 基础层 include：`common/linux6.1/linux6.1-rkr2.xml` + `common/ipc/rkipc-r2.xml` + `common/amp/amp-riscv-4.1-r2.xml` + `include/rv1126b_doc-r2.xml`

Manifest 推断的事实已写入 [facts.md](facts.md) §1、§1.1、§1.3、§4。关键结论：

- livekit 用户态必须用 Buildroot 自构的 `aarch64-buildroot-linux-gnu-gcc`，**不能**用 `prebuilts/` 下的 `aarch64-none-linux-gnu`
- `external/mpp` tag 是 `linux-6.1-stan-rkr7.1`（比其他工程高半级）
- Rootfs 暂定 Buildroot

### [6] 运行 `./repo.sh` 本地 checkout

```bash
cd ~/atk-dlrv1126b-sdk
./repo.sh
```

- 52 个 project 在 28.8s 内完成（`-j2` 也够用，纯本地 IO）
- sync 后工作树 21GB
- 顶层新增目录：`app/ build.sh Copyright_Statement.md device/ docs/ external/ hal/ kernel/ kernel-6.1/ Makefile prebuilts/ rkbin/ rkflash.sh rtos/ tools/ u-boot/ yocto/`
- **修正**：`yocto/` 也被同步（此前误以为基础 manifest 未启用）

### [7] Phase 0 主机侧收集脚本首次成功运行

```bash
cd ~/livekit/livekit-sdk-cpp-0.3.3
export SDK_ROOT=~/atk-dlrv1126b-sdk
./scripts/phase0-collect-host.sh 2>&1 | tee phase0-host.log
```

实际结果（详见 [facts.md](facts.md) §1）：

| 段 | 实测 |
|---|---|
| `[1]` | ✓ 命中 `.repo/manifests/RV1126B_Linux6.1_SDK_Note.md` |
| `[2]` | Buildroot 未构建，fallback 到 prebuilt：`aarch64-none-linux-gnu-gcc 10.3.1 20210621`；发现额外别名 `aarch64-rockchip1031-linux-gnu-gcc` |
| `[3]` | Prebuilt sysroot 仅含 `libstdc++.so.6`，其他全部 `(missing)` —— 符合预期 |
| `[4]` | Prebuilt 无 pkg-config 目录 —— 符合预期 |
| `[5]` | ✓ `external/mpp` / `external/rockit` / `external/linux-rga` 全部命中 |
| `[6]` | `rustup` 未装 |

### [8] 定位 Buildroot / Kernel / U-Boot defconfig 映射

```bash
ls buildroot/configs/ | grep -iE 'rv1126b|rockchip'     # 16 个 RV1126B 专用
ls device/rockchip/rv1126b/                              # 6 个 ATK 板级 defconfig
cat buildroot/configs/alientek_rv1126b_defconfig         # 135 行，片段式 include
cat device/rockchip/rv1126b/04_atk_dlrv1126b_mipi720x1280_defconfig
lsb_release -a && gcc --version | head -1                # Ubuntu 20.04.2 + gcc 9.4.0
```

**硬件确认**：ATK-DLRV1126B + 竖向长条屏 720×1280 → **板级 defconfig 选 `04_atk_dlrv1126b_mipi720x1280_defconfig`**。

映射链（详见 [facts.md](facts.md) §4）：

```
device/rockchip/rv1126b/04_atk_dlrv1126b_mipi720x1280_defconfig
  → RK_BUILDROOT_CFG  "alientek_rv1126b"
  → RK_KERNEL_CFG     "alientek_rv1126b_defconfig"
  → RK_KERNEL_DTS     "rv1126b-alientek-mipi720x1280"
  → RK_UBOOT_CFG      "alientek_rv1126b" (+ SPL + FIT)
  → RK_WIFIBT=n
```

`alientek_rv1126b_defconfig` 通过片段 include 配齐：aarch64 / MPP / Weston / Mesa3D / Qt5 / LVGL / GStreamer-RTSP / FFmpeg(GPL+NONFREE) / connman / OpenSSH / 中文字体 / 亚洲/上海时区。

### [9] Host 依赖预装 + Buildroot Lunch

```bash
sudo apt update
sudo apt install -y \
  build-essential git ssh make gcc g++ libssl-dev liblz4-tool \
  expect patchelf chrpath gawk texinfo diffstat binfmt-support \
  qemu-user-static live-build bison flex fakeroot cmake \
  gcc-multilib g++-multilib unzip device-tree-compiler \
  ncurses-dev libncurses5-dev bzip2 expat bc python3-pip \
  python3-pyelftools u-boot-tools rsync cpio file \
  libgmp-dev libmpc-dev tmux
```

新装：`expat libncurses5-dev libubootenv-tool u-boot-tools tmux`；升级若干。其余已存在。

```bash
./build.sh lunch
# 菜单：Pick a defconfig → 输入 4
# 选中: 04_atk_dlrv1126b_mipi720x1280_defconfig
# 配置写入: output/.config
# Log saved at output/sessions/2026-04-24_16-30-00
```

Lunch 菜单暴露的 6 档含义已确认：

| 序号 | defconfig | 含义 |
|---|---|---|
| 1 | 01_atk_dlrv1126b_automipi_defconfig | **出厂固件配置**（编译所有 MIPI 屏 DT，启动时自动识别） |
| 2 | 02_..._automipi_amp_mcu | 多核异构，MCU 跑 amp.img |
| 3 | 03_..._automipi_ipc | IPC 应用开发，多媒体走 Rockit（非 GStreamer） |
| 4 | 04_..._mipi720x1280 | **选中** — 720×1280 MIPI 屏，多媒体走 GStreamer |
| 5 | 05_..._mipi800x1280 | 800×1280 |
| 6 | 06_..._mipi1080x1920 | 1080×1920 |

补充观察：**除 ipc 档外，其他配置多媒体默认 GStreamer 框架**（livekit 友好）。ipc 档走 Rockit（Rockchip 自家媒体栈），想对齐 Rockchip IPC 生态时再切。

### [10] 启动 Buildroot 全量构建

```bash
tmux new -s rkbuild
cd ~/atk-dlrv1126b-sdk
./build.sh 2>&1 | tee build-$(date +%Y%m%d-%H%M).log
# Ctrl-B D 脱离；tmux attach -t rkbuild 重连；tail -f build-*.log 外部看
```

运行时资源基线：418G 可用磁盘 / 8 核 / 7.7G RAM + 8G swap。

预期耗时分段（首次）：U-Boot 10-20min → Kernel 20-40min → Recovery 5-10min → **Buildroot 1.5-4h（含 dl/ 下载 3-5GB）** → Firmware 打包 5-10min。总 2-6h。

产出（构建完成后 facts.md §1.1 / §4 可全填）：
- `buildroot/output/rockchip_rv1126b/host/usr/bin/aarch64-buildroot-linux-gnu-*`（livekit 真正要用的 toolchain）
- `buildroot/output/rockchip_rv1126b/host/aarch64-buildroot-linux-gnu/sysroot/`（带 OpenSSL/ALSA/MPP/RGA 的完整 sysroot）
- `rockdev/` 或 `output/firmware/`（boot.img / rootfs.img / uboot.img / parameter.txt，板子到手后烧录用）

### [11] Build 失败补修：缺 `gettext`

首次 build 在 Buildroot 阶段失败。U-Boot + Kernel 均通过（FIT image + `output/firmware/boot.img` 已产出），错误信息：

```
Start building buildroot(2024.02)
==========================================

Your msgmerge is missing
Please install it:
sudo apt-get install gettext

ERROR: Running .../mk-rootfs.sh - build_buildroot failed!
```

**修法**（不需要从头 rebuild）：

```bash
sudo apt install -y gettext
./build.sh rootfs 2>&1 | tee -a build-<原日期>.log   # 续跑 rootfs 阶段
```

**教训**：之前 [9] 的 apt 清单漏了 `gettext`，Buildroot 2024.02 的 host dep 检查会专门校验 `msgmerge`。后续若仍有缺包按错误提示逐个补即可。

### [12] 线 B — 板端 Phase 0 采集

板子到位后联调 SSH 免密 + 跑采集脚本。

```bash
# 1) 给板子建专用 ed25519 密钥（别和 VM/GitHub 那把混）
ssh-keygen -t ed25519 -N "" -f ~/.ssh/id_rv1126b_board -C "claude-code@rv1126b-board ..."
# 配置 ~/.ssh/config Host alias: rv1126b-board → 192.168.10.236 (root)

# 2) 用户一次性手动推公钥到板子（PowerShell 窗口，输入密码 root）
type $env:USERPROFILE\.ssh\id_rv1126b_board.pub | ssh root@192.168.10.236 \
  "mkdir -p /root/.ssh && chmod 700 /root/.ssh && cat >> /root/.ssh/authorized_keys && chmod 600 /root/.ssh/authorized_keys"

# 3) 推脚本 + 采数据（此后全免密）
scp scripts/phase0-collect-board.sh rv1126b-board:/tmp/
ssh rv1126b-board 'sh /tmp/phase0-collect-board.sh' > phase0-board.log
```

板端侦察结果已整理进 [facts.md](facts.md) §2（§2.1–2.6），原始输出折叠在 §5。

**关键发现（影响后续 Phase 1–5 决策）**：

| 发现 | 影响 |
|---|---|
| Rootfs 标记 `RK_BUILD_INFO="alientek_rv1126b"` | 出厂固件与 VM 当前在构建的**完全同款**，VM 产物可直接烧板验证 |
| libstdc++.so.6.0.32 → **GCC 13.x ABI** | 线 A 产出的 Buildroot toolchain ABI 必须对齐；Buildroot 2024.02 默认是 GCC 13，符合 |
| OpenSSL **3.x** 已在 rootfs 中 | webrtc-sys 链接 boringssl vs openssl 的分叉可倒向 openssl 路径 |
| `/dev/dma_heaps/` **缺失** | libwebrtc 零拷贝层如假设 dmabuf heaps 需改 Rockchip CMA/ION 接口，Phase 2 关注点 |
| Swap=0 + 1.9G RAM + 4.6G 空闲 `/` | livekit + weston + qt5 + ffmpeg 运行时需 profile 内存与 rootfs 余量 |

SSH 连接信息已存记忆 `reference_vm_ssh.md` 的姐妹篇理论上该补一条 board 的 —— 见下一 commit。

### [13] 线 B 扩展扫描（Phase 1 前置）

在基础采集之外再做 10 项 livekit-相关深挖（GStreamer 插件 / V4L2 摄像头格式 / ALSA codec / Weston 状态 / libc 精确版本 / listening 端口 / MPP 测试工具 / GPU-Mesa / 外网 DNS / 内核 CONFIG 抽样）。

```bash
ssh rv1126b-board 'bash -s' < <scan-script> > phase0-board-extended.log
```

结果整理进 [facts.md](facts.md) §2.7（10 大项结论） + §2.8（Phase 1 决策总表 6 条）。原始 log 本地保留，未入 git。

**几条决定性结论**（直接塑形 Phase 1 蓝图）：

| 问题 | 先验假设 | 实测颠覆/确认 |
|---|---|---|
| 是否走 GStreamer 做视频管线？ | 可能走 | **不走** —— 板上无 Rockchip GStreamer 插件，直接 `librockchip_mpp` |
| 是否能用 Mali EGL 渲染？ | 可能能 | **不能** —— 无 `libmali*`，走 DRM/KMS planes |
| webrtc-sys 选 OpenSSL 还是 BoringSSL？ | 待定 | **OpenSSL 3.x**（板上 `libssl.so.3`） |
| 零拷贝通路？ | 可能用 dmabuf heaps | **不是** —— `/dev/dma_heaps/` 缺失，走 CMA / V4L2 DMABUF |
| `mpi_enc_test` 可否直接 smoke？ | 未知 | **可以** —— 板上 `/usr/bin/mpi_{enc,dec}*_test` 齐全 |
| 摄像头工作吗？ | 未知 | **工作** —— `rkisp_v11` + 默认 640×480 NV16 |

### [14] 线 A — Buildroot 全量构建完成

**耗时 1h51min**（起 16:43 终 18:34），比预期的 2–4h 快。通过 `ssh rv1126b-vm` 远程巡检（见 `reference_vm_ssh.md`）确认：

- Toolchain：`buildroot/output/alientek_rv1126b/host/usr/bin/aarch64-buildroot-linux-gnu-gcc` = **13.4.0** (Buildroot `-g4a1fe4ec`)
- 关键：目录名是 `alientek_rv1126b`（Buildroot defconfig 名），不是 `rockchip_rv1126b`（我之前脚本里猜的路径）。改 `scripts/phase0-collect-host.sh` 的 Toolchain fallback 可以把 `rockchip_rv1126b` 换成从 `device/rockchip/.chip/*.chipconfig` 读取 `RK_BUILDROOT_CFG`，不过 Phase 0 已收尾，不动了。
- Sysroot (1.2 GiB)：`libssl.so.3` / `libcrypto.so.3` / `librockchip_mpp.so.{0,1}` / `librga.so.2.1.0` / `libasound.so.2` / `libstdc++.so.6.0.32` 全在
- **ABI 闭环**：toolchain 版本 hash `-g4a1fe4ec` 与板上 `/etc/os-release` 的 `VERSION=-g4a1fe4ec` 完全相等，VM 编出的 livekit 可直接推到板上跑，无 ABI 偏移
- Firmware 产物就绪（`output/firmware/`）：`boot.img` / `rootfs.img` (→`rootfs.ext2` 1.3G) / `uboot.img` / `MiniLoaderAll.bin` / `misc.img`
- 可选 rootfs 格式（`buildroot/output/alientek_rv1126b/images/`）：`rootfs.squashfs` (460M，紧凑) / `rootfs.ext2` (1.3G) / `rootfs.cpio.gz` (463M) / `rootfs.tar` (1.1G)

`facts.md` §1.1 (toolchain)、§1.2 (sysroot) 已填完最终值。

### Phase 0 正式收尾

所有 §1 主机侧、§2 板端、§4 rootfs 决策事实已固化。剩余 §3 webrtc-sys pinned 版本留给 Phase 1 决策时查代码时填（需读 `client-sdk-rust/webrtc-sys/libwebrtc/build.rs`）。

### [Phase 1 切入建议]

1. 硬件 smoke：VM 推板端用 `mpi_enc_test` + 摄像头采帧 → 验证 H.264 硬编能 work
2. 测 DRM/KMS plane overlay（无 Mali 无 EGL 路径），用板上现成 libdrm 写最小渲染 demo
3. 读 `client-sdk-rust/webrtc-sys/libwebrtc/build.rs` 查 pinned libWebRTC commit，确认 aarch64 prebuilt URL 是否存在
4. 若无 prebuilt：自建 libWebRTC aarch64 分支，用 §1.1 toolchain 编
