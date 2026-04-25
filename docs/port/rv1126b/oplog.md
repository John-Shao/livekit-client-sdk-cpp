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

### [15] Phase 1 — libWebRTC 来源决策

`client-sdk-rust/` submodule 在 `.gitmodules` 已声明但从未作为 gitlink commit，目录为空。手动 clone：

```bash
rmdir client-sdk-rust  # 空占位
git clone --depth 20 https://github.com/livekit/client-sdk-rust client-sdk-rust
# 注：clone 过程遇到 "initial ref transaction called with existing refs" BUG，
# objects 下完但 refs 没更新。绕法：
cd client-sdk-rust && git fetch --depth 1 origin main && git checkout -f FETCH_HEAD
```

HEAD 落在 upstream `main` 的 `fd3df87`（2026-04-24 最新）。

读 `client-sdk-rust/webrtc-sys/build/src/lib.rs` 取关键常量：

- `WEBRTC_TAG = "webrtc-7af9351"`
- `target_arch`: `aarch64 → arm64`
- `download_url`: `https://github.com/livekit/rust-sdks/releases/download/{TAG}/webrtc-{os}-{arch}-{profile}.zip`

组合出 prebuilt URL 并验证：

```bash
curl -s https://api.github.com/repos/livekit/rust-sdks/releases/tags/webrtc-7af9351 \
  | jq -r '.assets[] | select(.name=="webrtc-linux-arm64-release.zip") | "\(.name) \(.size)"'
# → webrtc-linux-arm64-release.zip 165522664   （158 MB）
```

**存在 ✓。Phase 1 结论锁定走路径 ①**（prebuilt 直接下）。详见 [decision-webrtc.md](decision-webrtc.md)。

再验证 Buildroot sysroot 的 pkg-config 硬依赖（webrtc-sys/build.rs Linux 分支 `unwrap()` 了 glib-2.0/gobject-2.0/gio-2.0 的 pkg-config probe）：

```bash
ssh rv1126b-vm 'SYSROOT=~/atk-dlrv1126b-sdk/buildroot/output/alientek_rv1126b/host/aarch64-buildroot-linux-gnu/sysroot; \
  find $SYSROOT/usr/lib/pkgconfig -name "g{lib,object,io}-2.0.pc"'
```

glib / gobject / gio 2.76.1 三兄弟 .pc + .so 全在 ✓。

**未提交 submodule gitlink**，等 Phase 3 CMake 实际编译通过后再 pin 到验证过的 commit。

### [16] Phase 2 — VM host 工具与 env 脚本

VM 装齐 Phase 2 host 依赖（用户在 VM 终端交互式跑，避免 ssh 非 tty sudo 问题）：

```bash
sudo apt install -y protobuf-compiler lld           # focal 自带 lld 10
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- \
  -y --default-toolchain stable --profile minimal
. $HOME/.cargo/env
rustup target add aarch64-unknown-linux-gnu
```

工具版本：rustc 1.95.0 / cargo 1.95.0 / lld 10 / protoc 3.21.12（PATH 实际取的非 apt 包，更新）。

写 `scripts/env-rv1126b.sh`（commit `71234d6`），核心设计：

- **不修 `client-sdk-rust/.cargo/config.toml`**（上游已有 `-fuse-ld=lld`），仅用 env 变量补 linker/ar：`CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-buildroot-linux-gnu-gcc`
- pkg-config 走 `PKG_CONFIG_SYSROOT_DIR` 指向 Buildroot sysroot
- bindgen 走 `BINDGEN_EXTRA_CLANG_ARGS=--sysroot=...`（libclang 不读 gcc 内置 sysroot）

`client-sdk-rust` 不再 git clone，直接从 Windows 仓库 scp 50MB 过去（VM 到 GitHub 的 git-https TLS 抖，复用已知好的 fd3df87 副本）。

### [17] livekit-api smoke build（plan.md Go 标准）

**首跑失败但被掩盖**：`ssh rv1126b-vm 'cargo build ...'` 中 cargo 找不到（非交互 ssh 不读 `.bashrc`，rustup 装的 `~/.cargo/bin` 不在 PATH），但 tee 管道 exit 0。

**修法**：env-rv1126b.sh 显式 `. $HOME/.cargo/env`（commit `a48c5ba`）。

**二跑通过**：

```
$ source scripts/env-rv1126b.sh && cd client-sdk-rust
$ cargo build --target aarch64-unknown-linux-gnu -p livekit-api
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 25.77s
```

抽 `.o` 验证：`ELF 64-bit LSB relocatable, ARM aarch64`。**Phase 2 plan.md Go 标准达成 ✓**。

### [18] webrtc-sys 加码 smoke：158MB prebuilt 下载三轮战

Phase 2 plan.md 没强制要求，但 webrtc-sys 是 Phase 3 livekit-ffi 必经。提前打。

**第一轮**：`cargo build -p webrtc-sys` → build.rs 内嵌的 `reqwest::blocking::get(...)` 拉 prebuilt 在 67MB/158MB **卡死 18 分钟**（国内 → `release-assets.githubusercontent.com` TCP reset，futex_wait_queue_me 无返回）。

**第二轮**：kill 卡死进程后用 VM `wget --tries=50 --continue` 拉到 130MB **wget 报 exit 0** 但文件实际不全。原因：GitHub release-assets 用**签名 URL，30 分钟过期**。wget 抓到 302 目标后一直用旧签名续传，过期后 HTTP 403 `Server failed to authenticate`，wget 把 403 当成"任务完成"。`unzip -t` 直接爆"central directory not found"。

**第三轮**（成功）：外层 shell 循环 + `wget -c <原始 github.com URL>`，每轮自动走 302 拿新签名：

```bash
while [ "$(wc -c < /tmp/.../webrtc.zip 2>/dev/null || echo 0)" -lt 165522664 ]; do
  wget --tries=20 --continue "https://github.com/livekit/rust-sdks/releases/download/webrtc-7af9351/webrtc-linux-arm64-release.zip" -O /tmp/.../webrtc.zip
  sleep 2
done
```

最终 165,522,664 B 完整，`unzip -t` 通过。展开到 `~/webrtc-prebuilt/linux-arm64-release/`（含 54MB `libwebrtc.a` + ninja + headers）。

**绕过 build.rs 内嵌下载**：env-rv1126b.sh 加自动检测（commit `c6f1263`），若 `~/webrtc-prebuilt/linux-arm64-release/lib/libwebrtc.a` 存在，自动 export `LK_CUSTOM_WEBRTC=$HOME/webrtc-prebuilt/linux-arm64-release` —— `webrtc_sys_build::custom_dir()` 直接命中本地路径，跳过 `download_webrtc()`。

**编译结果**：

```
   Compiling webrtc-sys v0.3.28 (...)
warning: webrtc-sys@0.3.28: cuda.h not found; building without ...
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 17.25s
```

0 errors，3 条无害 warning（cuda.h 缺失/upstream workspace profile/ort-tract semver 元数据）。产物 `libwebrtc_sys-*.rlib` 109 MB（内嵌 libwebrtc.a），抽 `.o` 验证 aarch64 ELF ✓。

**Phase 3 最大前置坑全部扫掉**：cxx-build C++20 / pkg-config glib-2.76.1 / lld 链接 / `LK_CUSTOM_WEBRTC` 机制 全证可用。

### [19] client-sdk-rust submodule pin

Phase 2 加码全过，`fd3df87` 是"已知编通的快照"，正式 pin 成 submodule gitlink。

`.gitmodules` 早就声明了 client-sdk-rust 但从未提交 gitlink，工作树里的 `client-sdk-rust/.git` 还是 manual clone 的真目录（不是 submodule 标准的 gitfile）。**`git submodule add -f` 会因为目录已存在失败**。正确路径：

```bash
git submodule init                                           # 把 .gitmodules 注入 .git/config
git update-index --add --cacheinfo \
  160000,fd3df87386cd0abd66fcc0e1dcc15f93235e56d2,client-sdk-rust  # 直接写 gitlink
git submodule absorbgitdirs client-sdk-rust                  # client-sdk-rust/.git → ../.git/modules/client-sdk-rust
git commit -m "chore: pin client-sdk-rust submodule to fd3df87"
git push
```

commit `b29f310`，diff 仅一行：

```
new file mode 160000
+Subproject commit fd3df87386cd0abd66fcc0e1dcc15f93235e56d2
```

之后任何 fresh clone 都能用 plan.md / README_BUILD.md 描述的标准路径：`git submodule update --init --recursive`。

### [20] GitHub Desktop 切分支误报（已纠错）

切到 `main` 分支时 GitHub Desktop 报"1 changed file: client-sdk-rust"，弹"Switch branch / Leave my changes / Bring my changes"对话框。

**根因**：`main` 的 tree 里没声明 client-sdk-rust（也没 `.gitmodules`），但工作树里物理目录还在 → git 视为 untracked。submodule 是 port 分支独有的概念。

**正确处理**：Cancel 对话框 → CLI `git checkout port/rv1126b-phase-0-recon` → submodule 立即被识别，工作树干净。

**后续避坑**：分支切换尽量走 CLI，或者全程别离开 `port/rv1126b-phase-0-recon`（main 留作上游对照即可）。

### [21] Phase 3 — CMake toolchain + 6 处 source 改动（commit `20d75c5`）

新建 `cmake/toolchains/rv1126b-aarch64-linux-gnu.cmake`：从 env 取 `ATK_TOOLCHAIN_ROOT`/`ATK_SYSROOT`，设 `CMAKE_C/CXX/AR/RANLIB/LINKER`，`-march=armv8-a+crc`，`CMAKE_FIND_ROOT_PATH_MODE_*` 全 ONLY/NEVER。

CMakeLists.txt 4 处改 + cmake/protobuf.cmake 2 处改，详见 commit message。核心是让 `RUST_TARGET_TRIPLE` 在 cross-Linux 时填 `aarch64-unknown-linux-gnu`，让 `GCC_LIB_DIR` 用 `-print-libgcc-file-name` 而不猜目录，以及 abseil flag-strip 从 `APPLE only` 扩到所有 aarch64 target。

### [22] VM 同步 submodule + libyuv 子 submodule 折腾

VM 上 `git pull` 拉到 `20d75c5`，但 client-sdk-rust 子 submodule 递归 init 失败 —— `yuv-sys/libyuv` 指向 `chromium.googlesource.com`，国内 GFW 阻断。

辗转：
- 先看 webrtc-prebuilt 内嵌：`include/third_party/libyuv/include/` 仅 headers，**无 .cc 源**
- 试 `lemenkov/libyuv` GitHub 镜像：安全钩子拦下（第三方内容编进 binary）
- 最终路径：**Windows 宿主机直连 chromium 走通**（host 网络 vs VM NAT 路径差异 —— Windows 那条路出去的 SNI/路由不被阻），git clone 完整副本，scp 到 VM。注意 scp 目标已存在空 dir 时会嵌套，需要用 `mv libyuv/* . && rmdir libyuv` 拍平
- `git checkout 917276084a49be726c90292ff0a6b0a3d571a6af`（项目固定的 SHA），52 个 .cc 源 + NEON64 源齐全 ✓

### [23] livekit-ffi 直跑 cargo —— 提前发现 libclang 缺失

`cargo build -p livekit-ffi --target aarch64-unknown-linux-gnu` 卡在 yuv-sys 的 bindgen：`Unable to find libclang`。

修：`sudo apt install -y libclang-dev clang`（Ubuntu focal 是 libclang-10）。

再跑 ✓ —— 三 crate-type 全产出（`liblivekit_ffi.a` 854M / `.rlib` 240M / `.so` 269M），抽 `.o` 验证 aarch64。

### [24] cdylib link 失败：`-fuse-ld=lld` 找不到 ld

CMake-触发的 livekit-ffi cdylib link 报 `collect2: cannot find ld`。Buildroot 的 GCC 13.4 内置 exec 路径只有 `host/aarch64-buildroot-linux-gnu/bin/ld`/`ld.bfd`，没有 `ld.lld`；upstream `client-sdk-rust/.cargo/config.toml` 硬要求 `-fuse-ld=lld`（链 libwebrtc.a 必需）。

修：往 toolchain 的 exec dir 软链 `/usr/bin/ld.lld`，并把这一步固化进 env-rv1126b.sh（每次 source 幂等）。`commit a48c5ba` → `e3cc4d5`。

### [25] CMake configure 三连击

a. **Buildroot 自带 cmake 3.28 没 HTTPS**（`Protocol "https" not supported or disabled in libcurl`）—— PATH 把 toolchain bin 放后置，无前缀工具走系统 cmake；
b. **系统 cmake 3.16.3 < 项目要求 3.20** —— `pip install --user cmake` 拿到 4.3.2，`~/.local/bin` 前置 PATH；
c. **abseil/protobuf/spdlog 从 GitHub fetch TLS reset** —— Windows 宿主机经 VPN 代理 (15236) 拉 tarball，scp 到 VM，cmake configure 用 `FETCHCONTENT_SOURCE_DIR_LIVEKIT_*` 跳过 download。

configure 通过：1250s（~21min，主要在 fetch SDL3 50M + nlohmann_json）。

### [26] protoc 双 ABI 战争

Symptoms：build 到 92% 失败，cargo `livekit-ffi/build.rs` 报 `protoc failed: /lib/ld-linux-aarch64.so.1: No such file or directory`。

根因：CMake `Protobuf_PROTOC_EXECUTABLE` 默认指向 vendored 的 aarch64 protoc，host 跑不起来。`run_cargo.cmake` 模板把这个值塞进 `ENV{PROTOC}` 给 cargo。

第一修：让 `run_cargo.cmake` 在 `CROSS_COMPILE=ON` 时**不**覆盖 ENV{PROTOC}，让 env-rv1126b.sh 设的 host PROTOC 生效。同时 `protobuf.cmake` 在 cross 模式跳过 `set FORCE`。

第一修不够：CMakeLists.txt:91-95 在 `include(protobuf)` 之后又**强行**把 `Protobuf_PROTOC_EXECUTABLE` 设回 `$<TARGET_FILE:protobuf::protoc>`，覆盖了 protobuf.cmake 里的修。同样加 `if(CMAKE_CROSSCOMPILING)` 分支。

第二关：用 `/usr/bin/protoc 3.6.1` 跑通 codegen，但**生成的 .pb.h 跟 vendored libprotobuf 25.3 runtime 自检冲突**：

```
#error This file was generated by an older version of protoc which is
#error incompatible with your Protocol Buffer headers.
```

终极修：用 vendored protobuf-25.3 源码本地编 host protoc（同源 ABI 严格匹配）。

```bash
cd ~/lk-deps/protobuf-25.3
ln -s ~/lk-deps/abseil-cpp-20240116.2 third_party/abseil-cpp     # tarball 缺 git submodule
mkdir build-host && cd build-host
cmake .. -DCMAKE_BUILD_TYPE=Release -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_BUILD_PROTOC_BINARIES=ON -Dprotobuf_ABSL_PROVIDER=module
cmake --build . --target protoc -j
```

得 `~/lk-deps/protobuf-25.3/build-host/protoc-25.3.0`（x86-64 host runnable，libprotoc 25.3）。`-DProtobuf_PROTOC_EXECUTABLE=$_path` 传给 cmake，全链路打通。

### [27] Phase 3 build 全绿

```
[100%] Built target BridgeRobot
```

产物（详见 [phase3-summary.md](phase3-summary.md)）：

- `lib/liblivekit.so` — ELF 64-bit ARM aarch64 ✓
- `bin/HelloLivekitSender` / `bin/HelloLivekitReceiver` — aarch64 dynamic ELF ✓
- 额外 `BridgeMuteCaller / Receiver / Robot` 也都通过

commit `ebbbb64`：CMakeLists.txt + cmake/protobuf.cmake + scripts/env-rv1126b.sh 三处源改动一并提交。

### [28] Phase 4 — 依赖审计 + 板端 dry-run

`aarch64-buildroot-linux-gnu-readelf -d` 扫 Phase 3 三个产物的 NEEDED：合计仅 9 个（`linux-vdso` + 7 个标准 C/C++ runtime + `libssl`/`libcrypto` 3.x），webrtc-sys 的 X11/glib/drm/gbm 等已经走静态 + lazy `dlopen` stub。详见 [phase4-summary.md](phase4-summary.md) §依赖审计。

板上覆盖 100%：

```bash
ssh rv1126b-board 'for lib in libssl.so.3 libcrypto.so.3 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6 ld-linux-aarch64.so.1; do
  P=$(find /lib /usr/lib -maxdepth 2 -name "$lib*" 2>/dev/null | head -1)
  printf "%-25s %s\n" "$lib" "$P"
done'
```

7/7 全部命中（板上 libstdc++.so.6.0.32 与 toolchain `-g4a1fe4ec` 完全同源 minor）。**不需要在 Buildroot defconfig 里加任何包**。

### [29] 板端 dry-run：scp + ldd + 启动入口

VM → 板子 ssh 没免密（之前没配），用 Windows 中转 **stream**：

```bash
for f in liblivekit.so HelloLivekitSender HelloLivekitReceiver; do
  ssh rv1126b-vm "cat ~/livekit/livekit-sdk-cpp-0.3.3/build-rv1126b/{lib,bin}/$f" \
    | ssh rv1126b-board "cat > /tmp/livekit-dryrun/$f"
done
ssh rv1126b-vm 'cat ~/livekit/livekit-sdk-cpp-0.3.3/client-sdk-rust/target/aarch64-unknown-linux-gnu/release/deps/liblivekit_ffi.so' \
  | ssh rv1126b-board 'cat > /tmp/livekit-dryrun/liblivekit_ffi.so'
```

总 31MB（lib 8.7M / ffi 22M / 二进制 ~30K），板上 `/` 还剩 4.6G 充裕。

`ldd` 在板上跑：所有 NEEDED resolve 通过，无 `not found`。`./HelloLivekitReceiver --help` 进 main() + 打印 usage + `exit(0)` —— 证 livekit C++ + Rust FFI 在 ATK-DLRV1126B 上**完整 load 通过**。

### [Phase 5 切入建议]

板子有 ATK 出厂固件（同 SHA `-g4a1fe4ec` rootfs，dry-run 已证 livekit 二进制可用），**不必烧固件**直接联调 livekit server：

1. 起一台 livekit server（livekit-cloud 或自建 K8s 部署），拿 ws-url + JWT
2. 在板上 `LIVEKIT_URL=wss://... LIVEKIT_RECEIVER_TOKEN=... ./HelloLivekitReceiver` 看 RTC 协商
3. 关注 ICE candidate 收集（板上 wlan0 192.168.10.236 内网 + STUN/TURN）、DTLS-SRTP 协商、SDP 交换
4. 实际媒体流之前先确认 control plane 能跑通
5. 媒体流（音视频实际数据） → Phase 5/6 边界，可能需要看 [facts.md](facts.md) §2.7 b/h（V4L2 摄像头 / DRM 渲染）的硬件路径整合

预计耗时：1–2 天（plan.md 原估）。如果 control plane 连不上看 STUN/TURN 配置或防火墙；如果协商通了但媒体 stuck 看 ICE candidate 类型 / SDP codec 协商。

### [30] Phase 5a — 板端 smoke 启动（fake URL）

把 dry-run 的 4 个 artifacts 从 `/tmp/livekit-dryrun/` 永久搬到 `/opt/livekit/`（plan.md §Phase 5 规定路径），加 +x。

冒烟启动验证整链路初始化（不走真 server）：

```bash
ssh rv1126b-board 'cd /opt/livekit && \
  LD_LIBRARY_PATH=/opt/livekit RUST_LOG=debug SPDLOG_LEVEL=debug \
  LIVEKIT_URL=wss://invalid-fake-host.local \
  LIVEKIT_RECEIVER_TOKEN=invalid LIVEKIT_SENDER_IDENTITY=test \
  timeout 8 ./HelloLivekitReceiver'
```

8 秒里走完 13 项验证（详见 [phase5-summary.md](phase5-summary.md) §5a 验证清单）：FFI server v0.12.53 / LkRuntime / libwebrtc PeerConnectionFactory / WebRtcVoiceEngine + APM 子模块全初始化、signal_client wss URL 构造、3 次 DNS retry、错误传播 libwebrtc → Rust → C++ → user code、完整析构 cleanup、`exit(0)`。

意外发现：

- 板上无 `lsb_release` → device 上报 `os_version=Unknown`（cosmetic，不阻塞）
- 板 hostname `ATK-DLRV1126B` 透传到 livekit server 的 `device_model` 字段
- 重试 backoff <1 秒（比 plan.md 期望更激进）

### [31] Phase 5b — 实测：jusiai server + 手机 livekit app

环境：`wss://live.jusiai.com`（ATK 测试环境，自建），手机装 LiveKit 测试 app 加入房间 `f1d641ae-e19d-4461-8a8b-2582d4036798`。

#### [31.1] 现成 example 不够用 → 写 BoardLoopback

`HelloLivekitSender`/`Receiver` 都是单向，receiver 还硬编码订阅 `<id>:camera0` + `<id>:app-data`，匹配不上手机的 default track 名。新写 `examples/board_loopback/main.cpp`：单进程同时 publish + subscribe-all（用 `RoomDelegate::onTrackSubscribed` 动态注册 callback，不依赖 track 名）。

第一次编译报"模板第 1 个参数无效"在 `std::shared_ptr<StreamStats>` —— `StreamStats` 这个名字跟 livekit headers 里的 `RtpStreamStats / OutboundRtpStreamStats` 等通过 `using namespace livekit;` ADL 撞了。重命名为 `LoopbackStats` 解决。

#### [31.2] 第一跑：连上 jusiai server + 视频双向打通

```bash
LIVEKIT_URL=wss://live.jusiai.com \
LIVEKIT_TOKEN=eyJhbGc... \
./BoardLoopback
```

板↔手机 30 秒：
- board → phone video（合成紫蓝渐变 320×240）：手机端实测看到 ✓（用户截图确认）
- phone → board video：1138 帧 / 30s 接收 ✓
- audio_frames=0：track 订阅了但 callback 没触发 → track 名是空字符串

#### [31.3] 修 audio callback：用 `TrackSource` 不用 track_name

```cpp
const TrackSource src = ev.track->source().value_or(TrackSource::SOURCE_MICROPHONE);
room.setOnAudioFrameCallback(identity, src, callback);
```

phone audio 现在能进 callback。同时加 board 端合成 440Hz 正弦音频 publish。第一次 SDK 报 `direct capture requires 10ms frames: got 960`：原来用了 20ms 帧，SDK 死板要 10ms。改 `kAudioFrameMs = 10`。

后台跑 35s：手机端实测**听到 440Hz 蜂鸣音** ✓。

#### [31.4] 加 ALSA 播放：板上"开口"

接 ES8389 codec：`#include <alsa/asoundlib.h>`、`-lasound`、`AlsaPlayer` 类用 `snd_pcm_writei` 写到 `plughw:0,0`。第一版 100ms buffer 同步 write —— 用户反馈"音质很差"，5s 报告 14 underruns（2.8/s 稳定）。

#### [31.5] 三轮调整无效 → 发现 PulseAudio

试过 200ms / 500ms / 1000ms latency 都 14 underruns/5s 不变（说明不是 jitter）。也试了 producer-consumer queue + writer thread：同样。

`aplay` 独立测 `Device or resource busy` → 查 `ps -e` 发现板上跑着 **PulseAudio (PID 658)**。`plughw:0,0` 直通跟它抢硬件，share-mode 出问题。

#### [31.6] 改用 `default` 设备 → 0 underruns

```cpp
const char *device = std::getenv("ALSA_PCM_DEVICE") ? ... : "default";
snd_pcm_open(&pcm, device, ...);
```

通过 PulseAudio 接管，**音质大幅改善**（用户判定 ✓），underrun 归 0。

#### [31.7] 实测 140 秒稳定通话

```
first audio frame: rate=48000Hz channels=1 samples_per_ch=480
alsa playback opened: default 48000Hz 1ch s16le 1s-buf
T+5s..T+75s    alsa_underruns=0 alsa_dropped=0
[participant disconnected/reconnected]            ← 手机熄屏后恢复
T+80s..T+140s  alsa_underruns=1 alsa_dropped=0    （切换瞬间 1 次）
final  video=3966 audio=14087    ← 14087/140s = 100.6/s 完美 10ms cadence
```

**Phase 5 plan.md Go 标准 30s 通话 → 4.6× 超额完成**，且：
- 手机自动重连后 SDK delegate `onParticipantDisconnected` + `onParticipantConnected` 触发，新 track 自动重订阅 ✓
- 板↔手机双向音视频都通
- 中间无 crash

### [32] Phase 5 重新定义：协议 + 4 路硬件 I/O 全闭环

经讨论 Phase 5 / Phase 6 的边界：**不是"软编 vs 硬编"，是"协议 + 硬件 I/O" vs "MPP codec 替换"**。libwebrtc 软编从 Phase 0 就在跑（免费跟着来）。Phase 5 真正缺的是 4 路硬件 I/O：

- (a) V4L2 摄像头 → publish
- (b) DRM/KMS 显示订阅到的视频
- (c) ALSA 麦克风 → publish
- (d) ALSA 扬声器 ← 订阅到的音频（[31.4-31.7] 已完成）

#### [32.1] (c) ALSA 麦克风 capture
`snd_pcm_open(default, CAPTURE)` + `snd_pcm_readi` 480 samples（10ms）+ 软件 gain（默认 12× 经实测板载 mic 最佳）。命中点：板上 PulseAudio 仍在跑，capture 也走 `default` 设备让 pulse 接管，`plughw:0,0` 直通会抢硬件出问题。

#### [32.2] (a) V4L2 摄像头 capture
`/dev/video-camera0` (rkisp_mainpath) Multiplanar NV12 320×240，4 mmap buffer + poll/DQBUF/QBUF 循环。驱动默认就是 NV12，零格式转换直接 `VideoFrame(w, h, NV12, bytes)` 喂 SDK。30 fps 实测稳定。手机端验证："手机能看到板子摄像头实拍"。

#### [32.3] (b) DRM/KMS 显示
最有戏的一项。多个坑串联：

1. **weston 占着 DRM master** → `drmSetMaster` 失败。文档化"先 `pkill weston`"
2. **SDK 默认 callback format=RGBA** → DRM NV12 plane 不接受。改 `VideoStream::Options.format = NV12` → SDK FFI 报"convert to Nv12 not supported"
3. **改 `format = I420`** → SDK 给我们 I420，自己写 ~20 行 thread_local I420→NV12 软转换（Y 平面 memcpy + UV 按 U-V 插值）
4. **simulcast adaptive bitrate**：手机中途切分辨率（360×640 → 720×1280 → 180×320），`ensureBuffers` 检测维度变化重分配 dumb buffer，显示不中断

最终：板上 MIPI 屏（DSI-1，720×1280）实时显示手机摄像头画面，全程拉伸到全屏，65s 0 underrun 0 drop 0 crash。

#### [32.4] 端到端通过

`examples/board_loopback/main.cpp` 单进程同时：
- V4L2 capture 真摄像头 → publish
- ALSA mic 真麦克风（12× gain）→ publish
- libwebrtc VP8/Opus 软编 → 上行
- 订阅手机 video/audio
- 手机视频 I420 → NV12 → DRM plane → MIPI 屏
- 手机音频 → ALSA `default` → ES8389 → 喇叭

**plan.md Phase 5 Go 标准实质达成**。

### [Phase 6 切入建议]

Phase 5 用软编（libwebrtc 内置 VP8 + Opus），A53 × 4 在 320×240 ~ 720×1280 都能撑住。Phase 6 性能优化只换 codec 路径（capture/render/playback 不变）：

1. **MPP 硬件编解码**：把 libwebrtc 内置的 VP8 软编/解换成 `librockchip_mpp` (VEPU/VDPU 走 H.264)。需要研究 webrtc-sys 的 `VideoEncoderFactory` 自定义 hook（参考 `webrtc-sys/build.rs:212-244` CUDA 块的实现，把 NVENC 那种集成方式套到 MPP）
2. **零拷贝路径**：V4L2 capture dmabuf → MPP 编（不经 CPU）；MPP 解 dmabuf → DRM plane（不经 CPU）。需要 dma-buf 句柄打通
3. **rkrga 色彩转换**：camera 默认 NV12 OK；如果未来需要 NV12 → I420 转换让软编继续工作，用 RGA 硬件而非软件
4. **数据通道**（已知 polish）：jusiai NAT/防火墙下 SCTP-DTLS 协商超时，BoardLoopback 已移除 data publish；需调 server STUN/TURN
5. **延迟优化**：当前端到端约 1-1.5 秒（ALSA 1s + RTP/SRTP jitter）。MPP 编解码延迟可比软编低，可缩到 ~500ms

参考 plan.md §Phase 6 (3 选 1：MPP 直接 / Rockit / GStreamer 插件) + facts.md §2.7 (b/g/h)。
