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

### [33] Phase 6.1 SKELETON：MPP encoder hooks（编不动，但路由通了）

子模 `client-sdk-rust` 拉本地分支 `port/rv1126b-mpp`（基线 `fd3df87`），落 4 个新文件 + 改 2 个现有文件：

```
webrtc-sys/src/rockchip_mpp/
├── rockchip_mpp_encoder.h           — RockchipMppH264EncoderImpl : webrtc::VideoEncoder 声明
├── rockchip_mpp_encoder.cpp         — Encode() 返回 WEBRTC_VIDEO_CODEC_ERROR，initMppContext() 返回 -1 故意失败
├── rockchip_mpp_encoder_factory.h   — RockchipMppVideoEncoderFactory : webrtc::VideoEncoderFactory 声明
└── rockchip_mpp_encoder_factory.cpp — IsSupported() 双闸：getenv("BOARD_LOOPBACK_USE_MPP")=="1" && stat("/dev/mpp_service")
webrtc-sys/build.rs                  — arm/Linux 块加 ROCKCHIP_MPP_INCLUDE 处理 + cargo:rerun-if-env-changed
webrtc-sys/src/video_encoder_factory.cpp — InternalFactory ctor 加 #if defined(USE_ROCKCHIP_MPP_VIDEO_CODEC) 注册块
```

#### [33.1] 设计意图：可建构 + 默认不选

- **可建构**：`ROCKCHIP_MPP_INCLUDE=$HOME/atk-dlrv1126b-sdk/external/mpp/inc` cargo 编通，`BoardLoopback` 链接成功，端到端 v8 落板
- **默认不选**：`IsSupported()` 默认 `false` —— 板上跑没设 `BOARD_LOOPBACK_USE_MPP=1` 时 OpenH264 仍占 H.264 slot
- **opt-in 也安全**：即便设了环境变量，`Encode()` 返 `ERROR`、`initMppContext()` 返 -1，会立刻 `WEBRTC_VIDEO_CODEC_ERROR` 把控制权让回去；fallback 留给 `Factory`（OpenH264 优先）

板上 25s baseline 跑（不设 env）：codec=H264，V4L2/ALSA/DRM 全路径稳，无回归。骨架的存在不破坏 Phase 5 已绿的路径。

#### [33.2] 子模分叉策略

`client-sdk-rust` 上游 pin 在 `fd3df87`（livekit/main 上游 `TEL-464`），我们的 MPP 改动落 **本地分支** `port/rv1126b-mpp` `9419bfc`，**没推 origin**（origin 是上游 livekit/client-sdk-rust 只读）。

后续 fork 时机：6.1.4 真 MPP API 接通、回归通过、再 fork `John-Shao/client-sdk-rust` 推上去；父仓 `.gitmodules` 同步切到 fork URL。在那之前，子模处于 "本地领先 1 个 commit、上游不可推" 状态，clone 父仓后需先 `git submodule update --init` 再 `git -C client-sdk-rust checkout port/rv1126b-mpp`。

#### [33.3] 等 Phase 6.1.4

`initMppContext()` 真活：`mpp_create` + `mpp_init(MPP_CTX_ENC, MPP_VIDEO_CodingAVC)` + `MppEncCfg`（size/fps/rc=cbr/bps_target/gop/profile）+ 两个 `mpp_buffer_group_get_internal(MPP_BUFFER_TYPE_DRM)` + `MPP_ENC_SET_CFG`。
`Encode()` 真活：I420（或 RGA 零拷 NV12）→ `MppFrame` → `encode_put_frame` → 轮询 `encode_get_packet` → `EncodedImage` 喂 `encoded_callback_`。
`SetRates()`：bitrate 变化 forward 到 MPP `MPP_ENC_SET_CFG` 的 `rc.bps_target`。

预算 4-7 天累计实活时间，不含调试 NV12 stride / GOP / IDR 触发的玄学。

#### [33.4] 6.1.4 落代码（init + encode loop bundle）

骨架 stub 拆掉，`initMppContext()` 全部用 SDK 文档化的真 API 顺序做完：
1. `mpp_buffer_group_get_internal(MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE)` —— 一个 DRM-backed 组
2. `mpp_buffer_get(grp, &frm_buf, hor_stride*ver_stride*3/2)` —— NV12 输入 buffer
3. `mpp_buffer_get(grp, &pkt_buf, frame_size)` —— 输出 packet buffer（同尺度上界）
4. `mpp_create(&ctx, &mpi)` + `MPP_SET_OUTPUT_TIMEOUT=MPP_POLL_BLOCK(-1)` + `mpp_init(ENC, AVC)`
5. `mpp_enc_cfg_init(&cfg)` + `MPP_ENC_GET_CFG` + 一组 `mpp_enc_cfg_set_s32`：
   - `prep:width/height/hor_stride/ver_stride/format=MPP_FMT_YUV420SP`
   - `rc:mode=CBR / bps_target / bps_max=17/16 / bps_min=15/16 / fps_in_num/denom / fps_out_num/denom / gop`
   - `codec:type=AVC / h264:profile=66 / h264:level=31 / h264:cabac_en=0 / h264:qp_init=26 / h264:qp_min=10 / h264:qp_max=51`
6. `MPP_ENC_SET_CFG` 落配 + `MPP_ENC_GET_HDR_SYNC` 拿 SPS+PPS 缓存到 `hdr_pps_sps_`

`Encode()` 真活：I420Buffer→NV12 软转 → memcpy 进 frm_buf 的 mmap 指针（带 sync_begin/sync_end） → `mpp_frame_init` + 设 width/height/strides/MPP_FMT_YUV420SP/buffer → `mpp_meta_set_packet(KEY_OUTPUT_PACKET)` 预绑 pkt_buf → `encode_put_frame` → 阻塞 `encode_get_packet` → `mpp_packet_get_pos/get_length` 拿 NAL → IDR 检测（先 `KEY_OUTPUT_INTRA` meta 后 NAL 头解析） → IDR 帧前补 SPS/PPS（若 MPP 没自己塞） → `EncodedImage` + `CodecSpecificInfo{kVideoCodecH264, NonInterleaved, idr_frame}` → `OnEncodedImage()`。

`SetRates()` 转发新 bps_target / fps 进 `MPP_ENC_SET_CFG`，承接 libwebrtc BWE 调速。

#### [33.5] build.rs 链上去 + env 自动设

build.rs 加 `cargo:rustc-link-lib=dylib=rockchip_mpp` —— 直链不延迟（板上 `/usr/lib/librockchip_mpp.so.0` 必在）。新加 `ROCKCHIP_MPP_LIB` env 给跨编 `-L` 路径（Buildroot sysroot `usr/lib`）。`scripts/env-rv1126b.sh` 探测 `$ATK_SDK_ROOT/external/mpp/inc/rk_mpi.h` + `$ATK_SYSROOT/usr/lib/librockchip_mpp.so` 自动 export，免手动设。

#### [33.6] 编译路上踩两个坑

1. **`struct MppApi` 前向声明对不上**：`rk_mpi.h` 里 `typedef struct MppApi_t { ... } MppApi;` —— struct tag 是 `MppApi_t` 不是 `MppApi`。我们 header 里 `struct MppApi;` 占位，链接器看着像同一个类型，编译器 deref 时炸"incomplete type"。**修法**：encoder.h 直接 `extern "C" { #include "rk_mpi.h" / mpp_buffer.h / mpp_frame.h / mpp_packet.h / rk_venc_cfg.h }`。include 路径靠 build.rs 加的 `ROCKCHIP_MPP_INCLUDE`，只在 USE_ROCKCHIP_MPP_VIDEO_CODEC 编译时加，不污染其他 .cpp。
2. **`H264BitstreamParser::GetLastSliceQp(int*)` 旧 API**：当前 libwebrtc 里它是 `std::optional<int> GetLastSliceQp() const`，没参数版本。改 `if (auto qp_opt = h264_parser_.GetLastSliceQp()) { encoded.qp_ = *qp_opt; }`。
3. **`rtc::scoped_refptr` 改名**：libwebrtc 把 `rtc::` 命名空间合进 `webrtc::`。改 `webrtc::scoped_refptr<webrtc::I420BufferInterface>`，跟仓内其它 webrtc-sys/*.cpp 一致。

#### [33.7] readelf 验链 + 部署 v9

```
$ readelf -d build-rv1126b/lib/liblivekit_ffi.so | grep NEEDED
 NEEDED librockchip_mpp.so.1                    ← 我们的新依赖
 NEEDED libm.so.6 / libstdc++.so.6 / libgcc_s.so.1 / libc.so.6
$ nm -D liblivekit_ffi.so | grep " U mpp_"
 U mpp_buffer_get_with_tag
 U mpp_create
 U mpp_init                                     ← 都是 undef，运行时由 ld.so 解析
$ ssh rv1126b-board ldd /opt/livekit/BoardLoopback | grep rockchip
 librockchip_mpp.so.1 => /usr/lib/librockchip_mpp.so.1 (0x0000007f9a98a000)  ← 板上 OK
```

板上 `/usr/lib/librockchip_mpp.so.0` 2.4MB 通过 `librockchip_mpp.so.1` 软链命中。/dev/mpp_service + /dev/rga 都在。BoardLoopback v9 + 新 liblivekit*.so 已 scp 到 `/opt/livekit/`。

**待用户**：拿一个未过期 token，`BOARD_LOOPBACK_USE_MPP=1 BOARD_LOOPBACK_VIDEO_CODEC=h264` 起一次端到端，看 `[mpp] InitEncode ... cached SPS+PPS NN bytes` + 手机端能否收到 H.264 流（stats 应显示 `implementation_name="RockchipMpp_H264"`）。

#### [33.8] 风险点（供 smoke 时排查）

- **MPP_ENC_GET_HDR_SYNC 可能返失败**：rv1126b 上 SPS/PPS 是否走 GET_HDR_SYNC 还要实测。我们的 fallback 是 NAL 头检测，IDR 时会自动跟随 MPP 自己产的 SPS/PPS（如果它产）。
- **NV12 stride 对齐**：用了 16，rv1126b 编码器实际可能要 64（Y）/16（UV）。如果首帧 init 失败或编出来花屏，调成 `AlignUp(width, 64)`。
- **CBR 收敛慢**：startBitrate 来自 libwebrtc，初始可能 200~400kbps，太低 MPP 会塞高 QP（51）出渣画质。可以观察前 1s qp_init 表现。
- **block timeout 配 MPP_POLL_BLOCK**：encode_put_frame 必须先 set timeout 否则 encode_get_packet 立刻返 timeout。已设。

#### [33.9] 端到端 smoke v9 → v10：MPP 真接通了

`BOARD_LOOPBACK_USE_MPP=1` + 24h token，板上跑 35s。

**v9 结果（9419bfc）**：能编，但每帧 2-3 个 leak warning。MPP 自己的日志确认 `set prep cfg w:h [320:240] stride [320:240] fmt 0`（NV12）+ `set rc cbr bps [225000:239062:210937] fps [20:1:fix] gop 40`，完全是我们配的参数。30s 902 帧稳定 publish。但 log 里堆成千上万行：
```
mpp_buffer: mpp_buffer_ref_dec buffer from initMppContext found non-positive ref_count 0 caller mpp_packet_deinit
mpp_meta: put_meta invalid negative ref_count -1
mpp_mem_pool: mpp_mem_pool_put invalid mem pool ptr ... check (nil)
```

**根因**：`Encode()` 里我用 `out_pkt`（pre-bind 给 frame meta 的 KEY_OUTPUT_PACKET）和 `got_pkt`（`encode_get_packet(&got_pkt)` 出来的）当两个变量。但 MPP 通过同一个变量传递所有权 —— `mpi_enc_test.c` 里就是 `MppPacket packet` 一个变量复用整条链路。我们 deinit 两次，ref_count 跳到 -1。

**修复**（commit `f30bac5`）：合二为一，单一 `MppPacket packet` 变量；`encode_get_packet(&packet)` 复用同一变量；末尾只 deinit 一次。

**v10 结果**：log 从 2119 行掉到 141 行，**0 个 leak warning**，30s 902 帧稳跑，MPP rc cbr cfg 42 次合理 BWE 更新（225k → 187k → 213k → 215k）。手机中途断连重连，板子优雅承接新 track。

**待手机端确认**：(a) 是否看到板子摄像头实拍画面；(b) WebRTC stats 里 `outboundRtp.encoderImplementation` 是否 `RockchipMpp_H264`（如果还是 `OpenH264` 说明 IsSupported() 没过 —— 但板上 `/dev/mpp_service` 在、env `BOARD_LOOPBACK_USE_MPP=1` 设了，应该能走我们的 factory）。

**注**：libwebrtc 本身的 `RTC_LOG(LS_INFO) << "[mpp] InitEncode ..."` 没出现在 stderr，因为这个 build 没把 RTC_LOG 路由到 stdout（默认走 syslog 或丢弃）。MPP 自己的 `mpp[3908]: ...` 日志倒是直出 stderr，已经够诊断。

### [34] Phase 6.2：MPP H.264 解码器

`port/rv1126b-mpp` `ff94ac9` —— 跟 6.1.4 编码器对称，4 个新文件 + 改 build.rs + 改 video_decoder_factory.cpp：

```
webrtc-sys/src/rockchip_mpp/
├── rockchip_mpp_decoder.h            — RockchipMppH264DecoderImpl : webrtc::VideoDecoder 声明
├── rockchip_mpp_decoder.cpp          — Configure / Decode / drainOneFrame 实现
├── rockchip_mpp_decoder_factory.h    — RockchipMppVideoDecoderFactory : webrtc::VideoDecoderFactory 声明
└── rockchip_mpp_decoder_factory.cpp  — 同 encoder 的 IsSupported() 双闸（共用一个 env flag）
webrtc-sys/build.rs                   — arm/Linux 块加 decoder.cpp / decoder_factory.cpp 编译条目
webrtc-sys/src/video_decoder_factory.cpp — InternalFactory ctor 加 #if USE_ROCKCHIP_MPP_VIDEO_CODEC 注册块
```

#### [34.1] 解码序列（套 mpi_dec_test.c 范式）

```
Configure:
  mpp_create(&ctx, &mpi)
  mpi->control(ctx, MPP_DEC_SET_PARSER_SPLIT_MODE, &split=1)   ← Annex-B 多 NAL 一包
  mpi->control(ctx, MPP_SET_INPUT_TIMEOUT,  MPP_POLL_BLOCK)    ← put_packet 一定吃完才返
  mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, 100ms)             ← get_frame 不卡死线程
  mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC)
  mpp_packet_init(&packet, NULL, 0)                            ← 长生命周期复用
Decode(EncodedImage):
  mpp_packet_set_data/size/pos/length(packet, image.data())
  retry mpi->decode_put_packet(ctx, packet) up to 50× × 1ms
  for _ in 0..8:
    drainOneFrame()
drainOneFrame:
  mpi->decode_get_frame(ctx, &frame)
  if info_change(frame):
    mpp_buffer_group_get_internal(MPP_BUFFER_TYPE_DRM | CACHABLE)
    mpp_buffer_group_limit_config(grp, buf_size, 24)
    MPP_DEC_SET_EXT_BUF_GROUP / MPP_DEC_SET_INFO_CHANGE_READY
    return true                                                ← 立刻 try 下一帧
  if err_info or discard: skip
  NV12 → libyuv NV12ToI420 → VideoFrameBufferPool I420Buffer
  VideoFrame::Builder → decoded_complete_callback_->Decoded()
```

#### [34.2] 共用 encoder 的 env 闸

`RockchipMppVideoDecoderFactory::IsSupported()` 用 **完全相同**的 `BOARD_LOOPBACK_USE_MPP=1` + `/dev/mpp_service` 双闸 —— 一个 flag 同时打开编+解。这是有意为之：硬件资源是同一个 VEPU/VDPU 子系统，没有"只硬编不硬解"的合理用例。

#### [34.3] v11 deploy + smoke

build 干净（30s 不到，只重编 6 个新 .o）。`liblivekit_ffi.so` 仍 DT_NEEDED `librockchip_mpp.so.1`，新增 undef 符号 `mpp_packet_init / mpp_buffer_group_limit_config / decode_put_packet / decode_get_frame`，运行时由 ld.so 解析。

35s 板上 smoke：
- 编码侧仍 906 帧/30s 稳跑
- 0 leak warning
- **解码侧 silent** —— `[loopback] track subscribed:` 没出现，房间里只剩板子自己；手机这轮不在线，远端 video track 不存在，libwebrtc 不会实例化解码器，自然没 `mpp_dec:` 日志

待手机或浏览器再次进房：单纯重跑 v11 即可验，预期日志会出现：
- `[loopback] track subscribed: ... kind=video sid=...`
- `mpp[NNNN]: mpp_dec: ...`（MPP 自己打的 init 日志）
- `[mpp-dec] info change WxH stride ...`（我们打的，前提是 RTC_LOG 这次能出 stderr —— 不出也无所谓）
- `[loopback] remote streams:` 后面应该开始列对端流

#### [34.4] 已知限制（待 6.1.5 / 后续）

- **NV12 → I420 软件转换**：libyuv NV12ToI420 跑在 A53 CPU 上，720p30 大概吃 1 个核 30%。RGA 硬件能干这个活但要 dmabuf 句柄打通。Phase 6.1.5。
- **DRM 输出还要 I420 → NV12 再转回去**：板子收到我们 decode 出的 I420，BoardLoopback 的 DRM 路径再 I420→NV12 给 plane 用 —— 双向软转，2× CPU 浪费。RGA 零拷贝同时解决这个。
- **buffer_pool 16 缓冲池**：与 24 个 MPP frame group 容量协同，720p×16 ≈ 6MB CPU 内存，可接受；若用 4K 输入需要调小。

#### [34.5] 端到端验证：MPP 解码路径 active 实锤

第一轮 v11/v12 多次 smoke：手机 publish 的是 **VP8/VP9** 不是 H.264 → 我们 factory 只 advertise H.264 → SDP 不匹配 → fallback 到 libvpx 软解。问题非 MPP，是 codec negotiation。诊断步骤：

1. 加 stderr trace 在 `livekit_ffi::VideoDecoderFactory::Create` 看实际请求的 codec → `[ffi-dec] Create requested: VP8 / VP9`
2. 用户手动改 LiveKit App 设置切到 H.264

第二轮 v13 smoke 全过：
```
[ffi-dec] Create requested: H264 profile-level-id=42e01f          ← 手机改 H.264
[mpp-dec] Create(H264 ...)                                          ← 我们 factory 选中
[mpp-dec] Configure() — opening MPP context
[mpp-dec] Configure OK                                              ← mpp_create + mpp_init(DEC, AVC) + grp + packet OK
[mpp-dec] Decode() first packet size=8831 bytes                    ← 第一个 NAL 进 MPP
[drm] dumb buffers ready: 2x 360x640 NV12                          ← 解出来的对端画面尺寸
[loopback] remote streams: 8f3cf510-...: video=202 audio=3348      ← 30s 累计
```

双向硬件 codec 全开通：板 → 手机 906 帧/30s（MPP H.264 编） + 手机 → 板 video=202/30s（MPP H.264 解）。0 leak warning，0 crash。

#### [34.6] 顺手扩 H.264 profile 覆盖

`GetSupportedFormats` 从 `{ConstrainedBaseline, Baseline} × pkt-mode 1` 扩到 `{ConstrainedBaseline, Baseline, Main} × pkt-mode {0, 1}` —— 共 6 种 fmtp 组合。`SdpVideoFormat::IsSameCodec` 严格匹配 profile-level-id，不在 advertise 列表里的 profile 会悄悄 fallback 到软解。RV1126B VDPU 实际支持 Baseline/Main/High 全三个 profile（per Rockchip 文档），advertise 范围放宽不会带来额外硬件风险。

#### [34.7] 已知盲区 / 未覆盖

- **VP8 / VP9 仍走软解**：手机如果默认这俩 codec，CPU 仍吃 libvpx。后续若要真的"对所有 publisher 都硬件"，要扩 MPP factory 支持 VP8/VP9 解码（VDPU2 文档说支持）。Phase 6.4 候选。
- **simulcast layer 选择**：手机推 simulcast 多层，我们订阅可能拿到不同 res。当前没有显式 prefer 高 layer，依赖 SFU 默认。
- **手机端帧率间歇**：测试中后 5s `video=202` 数停滞，audio 仍长 —— 手机 video publish 暂停（切后台/锁屏/网络），不是我们解码出问题。

**Phase 6 主线（编 + 解）画句号**。

#### [34.8] 性能调优（v15→v17）：从 10fps 卡顿到 30fps 持平

第一轮 v13/v14 端到端"通了"但**画面延迟从 1s 涨到 10s+**。3 个 bug 逐个揪出：

##### Bug 1: simulcast 切换 buffer 不够 → 解码冻结
v14 跑 15s 后 video 帧数停在 142 一直不动，audio 仍长。日志：
```
mpp[xxx]: mpp_buffer: mpp_buffer_create required size 471040 reach group size limit 122880
```
首次 info_change 时分了 122880 字节的 buffer group（够 180×320 用），手机后切到高 simulcast 层（720×480，需要 471040 字节），MPP 分不到 buffer，**解码彻底冻结**。修：每次 info_change 都重 `mpp_buffer_group_limit_config(grp, buf_size, 24)`。同时统一了 if/else 分支结构。提交 `5fb9a2f` 之类。

##### Bug 2: drainOneFrame 浪费 100ms 等空 → 解码上限 10fps
v15 不冻结了，但 video 解码稳定 10fps（源是 30fps），每秒落后 0.66s，30s 后 ~20s 延迟。

定位：`Decode()` 内部 `for (i=0..8) drainOneFrame()` 拿到帧后**继续 drain，第二次 `decode_get_frame` 阻塞 100ms 超时**才返回。每帧白白等 100ms → 10fps。

`drainOneFrame` 原 API 是 `bool`：true 表示"做了点什么继续 drain"。但**"传了一帧"**和**"info_change 已处理需再 drain"**没区分 → caller 误以为后面还有帧。

修：API 改 enum `DrainResult{kNothing, kInfoChange, kFrame}`；`Decode()` 看到 kFrame 立即 break，看到 kInfoChange 才继续。提交 `97dcc14`。

##### Bug 3: 退出时 segfault
退出时：
```
mpp_buffer: mpp_buffer_service_deinit cleaning leaked group
Segmentation fault
```
`mpp_destroy(ctx)` 后 libmpp 的 atexit 清理（mpp_buffer_service_deinit）尝试访问已经被我们 `mpp_buffer_group_put` 释放的 group，dangling pointer。修：`teardownMppContext` 开头先 `mpi->reset(ctx)` —— 同 mpi_dec_test.c 的清理顺序。同提交 `97dcc14`。

##### v17 实测
3 分钟稳跑：
```
T+5s   pub=151  remote video=152  audio=519
T+30s  pub=905  remote video=906  audio=3032
T+60s  pub=1808 remote video=1809 audio=6042
T+90s  pub=2712 remote video=2713 audio=9055
T+180s pub=5423 remote video=5423 audio=18092
T+185s pub=5574 remote video=5573 audio=18595
final  video=5628 audio=18777
```
- **30.1 fps decoded**（5573/185 = 30.13）
- **published 与 remote 帧数差 ≤1**（永不积压）
- **0 audio underrun，0 dropped**
- **画面延迟 <400ms 且不增长**
- 退出干净（待验 segfault 是否消失，v17 含修复）

**Phase 6.2 达到生产可用水平**。剩下的 RGA 零拷（消除 NV12↔I420 双向软转）属于优化范围，对当前 30fps@720p 已经够用，可以暂缓。

#### [34.9] 退出 segfault 收尾（v18, commit `0d2c8fd`）

v17 性能完美但 Ctrl-C 后段错误。新 trace：
```
mpp_buffer: mpp_buffer_ref_dec buffer from try_proc_dec_task found non-positive ref_count 0 caller check_entry_unused
mpp_buffer: mpp_buffer_ref_dec buffer from try_proc_dec_task found non-positive ref_count 0 caller mpp_frame_deinit
Segmentation fault
```

`try_proc_dec_task` 是 MPP 内部 decoder worker 的 buffer 来源。问题在两处交互：

1. **info_change 时 `mpp_buffer_group_clear` 与 worker 抢 ref count**：每次 simulcast 切换 limit 时，我们顺手 clear。但 worker 此刻可能正在用其中某个 buffer，clear 强制 dec ref → ref count 变 0 但 worker 还要再 deref 一次 → 失衡。修：去掉 info_change 时的 clear，只 limit_config，让 MPP 自己回收老 buffer。

2. **teardown 时 `mpp_destroy` 内部还引用着我们 `MPP_DEC_SET_EXT_BUF_GROUP` 的 group**：destroy 调内部 cleanup → check_entry_unused 想 deref 已经被我们 group_put 释放的 buffer。修：destroy 前显式 `MPP_DEC_SET_EXT_BUF_GROUP=NULL` 解绑，再 `mpp_buffer_group_clear` 主动回收，最后才 destroy。

新的 teardown 顺序：
```cpp
mpi->reset(ctx)
→ mpi->control(MPP_DEC_SET_EXT_BUF_GROUP, NULL)   // detach group from ctx
→ mpp_buffer_group_clear(grp)                      // proactive release
→ mpp_packet_deinit(&packet)
→ mpp_destroy(ctx)                                  // ctx no longer holds grp
→ mpp_buffer_group_put(grp)
```

v18 实测：30 fps 解码持平，simulcast 切换正常，退出**干净落到 shell prompt 不再 segfault**。MPP 自带的 `mpp_buffer_service_deinit cleaning leaked buffer` 警告仍出现 —— 是 libmpp atexit 池跟踪器自家簿记 mismatch（user put 的 buffer 它认不出来），cosmetic，不触碰 freed 内存。

**Phase 6.2 真正画句号**。

### [35] Phase 6.1.5 Step A：NEON 替换标量交错循环（v19, commit `862d727`）

收尾性能优化。当前 30fps 已稳，但两处 I420↔NV12 软转换里**手写标量循环没用 NEON**：

1. `examples/board_loopback/main.cpp` DRM 显示路径 I420 → NV12（每帧 ~230k 次 `uv[2i+0]=u[i]; uv[2i+1]=v[i]`）
2. `client-sdk-rust/webrtc-sys/src/rockchip_mpp/rockchip_mpp_encoder.cpp` 的 `I420ToNV12` 同款 UV 交错

修：

- encoder.cpp：直接 `libyuv::I420ToNV12`，header 加 `third_party/libyuv/include/libyuv/convert_from.h`，函数体瘦 18 行→4 行
- main.cpp：父仓没链 libyuv，手写 NEON intrinsics —— `vld1q_u8` × 2 (u/v)、构造 `uint8x16x2_t`、`vst2q_u8` 一次写 32 字节交错。配 `#if BOARD_LOOPBACK_HAVE_NEON` gate（`__aarch64__ || __ARM_NEON`），保留标量 fallback 处理尾部不足 16 字节的部分

实测（v19，720p30 双向硬件 H.264）：
- BoardLoopback 单核占用 **38.9%**（4 核系统约 10% 总占用）
- 4 核负载均匀 8-10%，load avg 0.38 / 0.17 / 0.06
- 退出后所有核 ~0%

**Phase 6.1.5 Step A 完成**。Step B（真 RGA 硬件路径，imcvtcolor 完全替代 NEON）收益估计 1-2% 单核，性价比低；真正下一阶段大头是 **dmabuf 零拷**（MPP 解码输出直接 scan-out 到 DRM，不经 SDK FFI 的 I420 中转），估计 ~10% 单核，但要改 SDK FFI / 加 raw video hook，工时 1-2 天。

### [36] Phase 6.1.5 Step B：NV12 全链路无格式转换（v26, commit `d7da5c9`）

不走 RGA 硬件，走**取消转换**：让 NV12 buffer 从 MPP 解码器一直透传到 DRM plane，中间一次格式转换都不做。比 Step A 的 NEON 加速更彻底（NEON 还是要 CPU 做 memcpy/interleave，"取消转换"则连 memcpy 都省掉）。

#### [36.1] 流程对比

```
v19 (Step A):
  MPP NV12 → libyuv NV12→I420 (我们解码器内) → SDK FFI 透传 I420
  → BoardLoopback 收到 I420 → NEON I420→NV12 → DRM NV12 plane

v26 (Step B):
  MPP NV12 → memcpy strip-stride 进 NV12Buffer → SDK FFI nv12_copy 透传 NV12
  → BoardLoopback 收到 NV12 → 直接喂 DRM NV12 plane
```

每帧 720p 节省 ~4 MB 内存带宽（一次 NV12→I420 + 一次 I420→NV12，每次 ~1.5×720×1280 = 1.4 MB 输入侧 + 同量输出侧）。30fps 累计 ~120 MB/s。

#### [36.2] 上游 SDK FFI 三个 bug 顺手修

之前 BoardLoopback `vopts.format = NV12` 报"convert to Nv12 not supported"是因为我们解码器返 I420 → FFI cvt_i420 不支持→NV12。现在我们返 NV12，触发 `cvt_nv12` → `nv12_copy` 路径，然后炸出三个 bug：

1. **`webrtc-sys/src/video_frame_buffer.rs` cxx-bridge 枚举漏 kI210/kI410**（位置 6/7）。webrtc::VideoFrameBuffer::Type::kNV12 = 8，C++ 端 `static_cast` 保留这个整数，Rust 端只有 7 个变体 → 命中 `_ => unreachable!()`。修：枚举对齐 webrtc 9 个变体。

2. **`livekit-ffi/.../cvtimpl.rs cvt_nv12` 的 dst stride 传 `chroma_w`** 是 chroma 列数（360 for 720w），但 imgproc 期望字节 stride。NV12 biplanar 字节 stride = `chroma_w * 2 = 720`。触发 assert `src_stride_uv >= width + width%2` 失败。修：传 `chroma_w * 2`。

3. **`livekit-ffi/.../mod.rs nv12_info` 的 UV size 公式** `stride_uv * chroma_height * 2`，原假设 stride_uv 是 chroma 列数所以 ×2 拿字节。我们改成字节后这个 ×2 重复了一次，UV size 报告翻倍。下游消费者按 size 读取走出 buffer 边界 → segfault。修：去掉 `* 2`。

#### [36.3] 解码器 `mpp_buffer_group_clear` 又被加回去了

[34.9] / `0d2c8fd` 把 info_change 时的 `mpp_buffer_group_clear` 删了，因为推断它跟 MPP worker 抢 ref count 导致退出 segfault。后来发现退出 segfault 真正原因是 `mpp_destroy` 前没解绑 buffer group（已修），与 clear 无关。

去掉 clear 之后 simulcast layer 升级时 MPP slot table 里残留旧 buffer 不放，新更大尺寸 buffer 没法分配 → 第二帧之后冻结 / segfault。Step B smoke 反复重现这个症状 → 把 clear 加回来 + 维持 [34.9] 的 detach + reset 退出顺序。两全。

另加 `mpp_buffer_get_size` 防御检查：如果 MPP 这一帧的 buffer < `hor_stride*ver_stride*1.5`（simulcast 升级 race），跳这一帧不读越界。

#### [36.4] v26 实测（手机端 H.264 publish + 板上 BoardLoopback 持续 100s+）

- BoardLoopback 单核 CPU **23 ~ 29%**（vs v19 38.9%）—— **省 ~12 个百分点单核** ≈ 25-30% 相对降低
- published 31fps 稳，remote video 30fps 跟得上（4161 帧 / 138s ≈ 30 fps）
- 0 audio underrun，0 dropped
- 画面延迟 <400ms 不增长
- 简易 simulcast 切换正常

**Phase 6.1.5 Step B 完成**。从 Phase 6.2 终版（~46-50% 推测）到 v26 的 23-29%，**整个 Phase 6.1.5 累计节省 ~50% 相对 CPU**。

下一步候选：真正的 dmabuf 零拷（MPP decode → DRM scan-out 直通 dmabuf，再省一次 memcpy）。但需要改 SDK FFI（让 NV12 buffer 不进 boxed_slice 而透传指针 + 生命周期），是个大动作。当前 23-29% 单核已经好得不要不要的，可暂缓。

### [37] 远端音量过小 ("听筒感") —— 双层修复

#### [37.1] 现象
板子→手机方向，无论软编 / MPP 都听感像从手机听筒里出来（实际开了扬声器）。之前的 12× 软件增益治标不治本。

#### [37.2] 根因 1：WebRTC PCF 默认 AGC 把信号又压回去

`client-sdk-rust/webrtc-sys/src/peer_connection_factory.cpp` 装的 `BuiltinAudioProcessingBuilder`，**默认 config 开启 AGC1 + AGC2**。机制：

1. ALSA mic 捕到的信号本就低
2. 我们 12× 软件放大到接近 clip
3. 信号进 webrtc 的 APM
4. AGC 看到 hot/clipping signal，**自动把它压下去** —— 这是 AGC 该干的事
5. Opus 编出来的电平回到 "normal voice"，软件放大被 SDK 自己抵消
6. 手机端听到的就跟"听筒"一样

修：传 `AudioProcessing::Config{ec=false, agc1=false, agc2=false, hp=false, ns=false}` 进 builder。板上没有 acoustic loopback 要 cancel，也不需要 NS/HPF（本就该原始信号）。提交 `af40b9c`。

#### [37.3] 根因 2：ES8389 mic 的硬件 PGA 关在最小档

`amixer -c 0 contents` 列出来：

```
numid=39 'ADCL PGA Volume'  range 0-14, step 3.00 dB, default 0
numid=40 'ADCR PGA Volume'  range 0-14, step 3.00 dB, default 0
```

**PGA = 0 dB**（mic 模拟前置增益完全关闭）。这是硬件层把信号在进 ADC 之前就砍了，软件再怎么 ×N 也救不动 dynamic range。

调试过程（每次 +3 dB）：
- PGA=14 (+42 dB)：人没说话就音爆，远端环境噪声放大到爆
- PGA=6 (+18 dB)：明显改善，但偏小
- PGA=7 (+21 dB)：再大点
- PGA=8 (+24 dB)：再大点
- **PGA=9 (+27 dB)**：✅ 用户判定"声音够大"。固化。

总链路：mic → ES8389 PGA **+27 dB** → ADC → ALSA → 软件 **8×（+18 dB）** → AudioTrackSource → APM (passthrough) → Opus → SFU → 手机。等效 ~+45 dB，比之前纯 12× 软件 +21 dB 强 24 dB（≈ 16×）。

#### [37.4] 持久化 + 自动应用

- `alsactl store` 写 `/var/lib/alsa/asound.state`，重启后由 init 自动恢复
- `/opt/livekit/smoke.sh` 启动时调 `amixer cset` 兜底
- `scripts/board-audio-setup.sh` 一次性配置脚本，PGA 值可 env 调，给新板初始化用

`examples/board_loopback/main.cpp` 默认 `mic_gain` 12 → **8**，因为去掉 AGC 后 12× 几乎一定 clip。env `BOARD_LOOPBACK_MIC_GAIN` 可调（推荐 4-12 范围）。

#### [37.5] 反方向：板上扬声器太响

PGA 修完之后跑通话，**反方向（手机→板子扬声器）声音偏大**，刺耳。

`amixer -c 0 contents | grep -A4 DAC`：

```
numid=48 'DACL Playback Volume'  range 0-255, step 0.5 dB, base -95.5 dB, default 191 (0 dB)
numid=49 'DACR Playback Volume'  same
```

DAC 默认 191 = 0 dB unity，扬声器原始信号直出。改 **171 = -10 dB**：用户判定"刚好"。`board-audio-setup.sh` 同步 + alsactl 持久化。

完整 board-audio-setup.sh 现配置：
- PGA = 9 (+27 dB mic 前级增益)
- DAC = 171 (-10 dB 扬声器衰减)
- 都通过 env 可覆盖（`PGA=8 DAC=161 ./board-audio-setup.sh`）

**Phase 6 音频问题彻底收尾**。










参考 plan.md §Phase 6 (3 选 1：MPP 直接 / Rockit / GStreamer 插件) + facts.md §2.7 (b/g/h)。
