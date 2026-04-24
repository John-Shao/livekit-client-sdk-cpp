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

### [待办] 运行 `repo sync -l` 并触发 Phase 0 收集

```bash
cd ~/atk-dlrv1126b-sdk
.repo/repo/repo sync -l -j$(nproc)       # 替代 ./repo.sh 的 -j2
ls                                       # 期望: app/ device/ external/ kernel-6.1/ u-boot/ buildroot/ prebuilts/ tools/ rkbin/ docs/
du -sh .

cd ~/livekit/livekit-sdk-cpp-0.3.3
export SDK_ROOT=~/atk-dlrv1126b-sdk
./scripts/phase0-collect-host.sh 2>&1 | tee phase0-host.log
```

预期结果与 fallback 说明：

| 脚本段 | 预期 |
|---|---|
| `[1] SDK 版本` | 命中 `.repo/manifests/RV1126B_Linux6.1_SDK_Note.md` |
| `[2] 工具链` | WARNING: Buildroot 工具链未构建 → fallback 到 prebuilt 打印版本，这是正常的 |
| `[3] Sysroot` | 跳过（需先跑 buildroot） |
| `[5] MPP/Rockit/RGA` | sync 完应全部命中 |
| `[6] Rust` | 未装 → "rustup 未安装"，现在不急 |

收完日志贴回对话，继续 Phase 0 的板端部分与 rootfs/toolchain 决策。
