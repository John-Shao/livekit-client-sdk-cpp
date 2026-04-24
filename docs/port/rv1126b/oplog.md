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

### [待办] 并行推进两条线

**线 A**：触发首次完整构建（取得 Buildroot host toolchain + target rootfs）—— **耗时数小时**，建议 `tmux` + 后台跑。

```bash
cd ~/atk-dlrv1126b-sdk
./build.sh lunch                            # 选 rv1126b → 04_atk_dlrv1126b_mipi720x1280
# 或直接指定（如果脚本支持）
./build.sh 04_atk_dlrv1126b_mipi720x1280    # 走 build.sh → mk-all.sh
# 首次 build 命令（等 lunch 完了再跑）
nohup ./build.sh 2>&1 > build-$(date +%Y%m%d).log &
tail -f build-*.log
```

构建产物关键路径（构建后 facts.md §1.1 可填）：
- Buildroot host 工具链：`buildroot/output/rockchip_rv1126b/host/usr/bin/aarch64-buildroot-linux-gnu-*`
- Target sysroot：`buildroot/output/rockchip_rv1126b/host/aarch64-buildroot-linux-gnu/sysroot/`
- 镜像：`output/firmware/` 或 `rockdev/`

**线 B**：板端 Phase 0 采集（先烧一版出厂固件到板子，再跑 `phase0-collect-board.sh`）。如果板子已有可用系统，直接进线 B 不等线 A。

```bash
# 把脚本推到板子（示例，用 adb 或 scp）
adb push scripts/phase0-collect-board.sh /tmp/  # 或 scp root@<board-ip>:/tmp/
# 板端执行
ssh root@<board-ip> 'bash /tmp/phase0-collect-board.sh' | tee phase0-board.log
```

---

**下一步决策点**（跑之前告诉我）：

1. 线 A 的完整 `./build.sh` 首次构建要占 50–80GB 磁盘、2–6h 墙钟时间；如果只想拿 toolchain/sysroot 不出整镜像，可以仅 `./build.sh buildroot` 起一轮 buildroot target build（仍要 1h+）
2. 线 B 前提是板子能跑一个系统（出厂 + 串口或网口能连）——你现在板子是**空**的还是有 ATK 出厂固件？
