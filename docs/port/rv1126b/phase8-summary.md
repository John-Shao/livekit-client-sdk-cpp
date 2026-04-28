# Phase 8 工作总结 — 产品级调优 + 长跑稳定性 + 业务集成（进行中）

> **状态**：8.1 已完成（AEC 收敛优化），其余候选项排队中
> 启动日期：2026-04-28
> 分支：`port/rv1126b-phase-0-recon`
> 关联文档：[plan.md](plan.md) · [phase7-summary.md](phase7-summary.md) · [oplog.md](oplog.md)

## 范围

Phase 7 把 720P30 双向通话 + AEC 落到 production-ready：HD 720P30 单核中位数
~58%，AEC 90/100 残余在收敛期前 5-10s。Phase 8 **不追加新功能**，目标是把
Phase 7 产出**调优到产品级**，并为业务集成铺路。

子阶段（按价值密度 / 落地顺序）:

| 阶段 | 验证目标 | 状态 |
|---|---|---|
| **8.1 AEC 收敛优化** | 把 AEC3 收敛期残余从 5-10s 压到 0 | ✅ 完成 |
| **8.2 长跑测试 (24-72h soak)** | RSS / fd / underrun 累计稳定 | ⏳ 待启动 |
| **8.3 DRM dma-buf 直渲 v2** | 7.6.c retry — 1080P30 真要时再做 | ⏳ 视需要 |
| **8.4 业务集成** | 房间管理 / 多人 / 数据通道接产品逻辑 | ⏳ 视需要 |

## 8.1 — AEC 收敛优化（done 2026-04-28）

**起点**：Phase 7.4 标定的 `(DAC=186, delay=400)` = 90/100，残余主要在 AEC3
收敛期前 5-10s。本阶段把"降扬声器音量"这条线走通——压低物理回声幅度，
让 AEC3 工作量减轻，目标把残余压到 0。

**约束（用户硬要求）**：不能牺牲板子推送给对端的音量。即只动 DAC 不动 ADC PGA，
且 sweep 时同步监控对端听我们的音量/连续性，任何退化立刻 PASS——宁可忍受
少量回声残余，也不削推送音量。

**Sweep 矩阵**（每组同样的双向对话脚本，对端外人评判）:

| DAC (numid=48/49) | dB | AEC delay | 现象 |
|---|---|---|---|
| 186 | -2.5 | 400 | 板子音爆（旧默认，喇叭过响） |
| 181 | -5 | 400 | 回声明显 |
| 176 | -7.5 | 400 | 回声明显 |
| 171 | -10 | 400 | 回声明显 |
| 171 | -10 | 350 | 低弱回声 |
| 165 | -13 | 350 | 低弱回声 |
| 165 | -13 | 300 | 开头有回声 |
| 161 | -15 | 300 | 开头有低弱回声 |
| **155** | **-18** | **300** | **回声消除** ✅ |

**关键观察 — DAC 和 AEC delay 是耦合的**：喇叭压得越低 → 物理回声路径越短
→ 最佳 stream delay hint 也跟着往下走。这也解释了为什么 7.4 阶段单独
sweep delay 时 400ms 是局部最优：那时 DAC=186 喇叭过响，需要长延迟兜回声；
现在 DAC=155 喇叭合理，AEC 不用等那么久就能匹配上回声。

**对端验证**：(DAC=155, delay=300) 这组下，对端反馈板子推过去的语音音量
正常、不断断续续 → 推送音量未受影响，硬约束达成。

**默认值更新**:

```
DAC: 186 → 155       (smoke.sh + board-audio-setup.sh)
AEC_DELAY_MS: 400 → 300
```

**Phase 6.3 老默认 171 的来由**：当时软件 mic gain = 12×，喇叭压低补平体感。
软件 gain 后改 8× 时 DAC 顺手抬到 186 配合，但实际上 7.4 AEC 投产后才暴露
喇叭过响是 AEC 残余的主因。现在 DAC=155 配合 8× 软件 gain：本端听对端略小
但完全可听，对端听我们正常，AEC 残余消除——三方都达到合理点。

**为什么 sweep 而不是先建模算最优**：声学回声路径是腔体 + 麦克风物理位置 +
PulseAudio 缓冲深度的非线性叠加，建模 + 测量参数比直接 sweep 成本更高，
6 组打分一小时拿到答案，工程上完胜。换其他硬件 / 改了喇叭距离都要重新 sweep
（声学路径耦合，不通用）。

## 8.2 — 长跑测试（pending）

**目标**：7×24h（最少 24h，目标 72h）板↔板或板↔Web soak，盯：

- RSS 不漏（看 `cat /proc/$PID/status | grep VmRSS`）
- fd 不漏（`ls /proc/$PID/fd | wc -l`）
- ALSA underrun / dropped 累计（smoke.sh 周期 stat 行）
- 视频帧数稳定 30 fps
- AEC 残余跟开头对照（声学路径短期飘）

**启动方案**：建议用 `/schedule` agent 在低负载窗口跑，每 1h 写一次状态行
到 `/tmp/soak.log`，结束后 grep 出 RSS / fd 走势 + underrun 累计。

**前置**：

- smoke.sh `--bg` + `--tail` 已经能后台跑 + 看日志
- BoardLoopback 当前已经能稳定 5 分钟 ~270s 不出 underrun (Phase 7.4 验证)
- 缺：周期性 stat dumper（可加到 main.cpp 的 main loop，或外面 wrapper 脚本）

## 8.3 — DRM dma-buf 直渲 v2（pending）

7.6.c v1 attempt-and-revert 见 [phase7-summary.md](phase7-summary.md)。retry 前置：

- livekit-ffi 的 cdylib 暴露一条"额外导出 C 符号"的 build-script 入口
  （现有 `--gc-sections` 把 C 静态库里的 extern "C" 符号裁掉）
- 真正出现 1080P30 业务需求（HD 720P30 ~58% 单核已经够用了）

预期收益 -3 ~ -5pp CPU。**不要主动做**——等业务真要 1080P30 时再来。

## 8.4 — 业务集成（pending）

Phase 7 阶段板↔Web 通话已稳定，可以开始接产品业务逻辑：

- 房间管理（创建 / 加入 / 离开 / 列表）
- 多人通话（>2 人 simulcast 行为复核）
- 数据通道（LiveKit data track 当前 SCTP/DTLS 协商失败，jusiai NAT 路径要 server 配合排查）
- 业务侧 UI / 状态机

**前置**：跟产品确认 MVP 功能集，再决定切入点。

## 已知排除项

- ~~**Buildroot 包补齐**~~ — Phase 7 follow-up 时调研后删除。`lsb_release` 进
  Buildroot 解决不了 `os_version=Unknown`（os_info crate 有 distro whitelist，
  Buildroot 不在内）；libvpx 已经静态进了 `libwebrtc.a`，没有再瘦空间。详见
  [phase7-summary.md](phase7-summary.md) 排除项段。
- **Rockchip rk_voice 硬件 AEC** — 需 NDA，未拿到代码 / 库。8.1 实测软件 AEC
  在合理 (DAC, delay) 配置下已能消除回声，rk_voice 暂不迫切。如果未来有更
  恶劣声学环境（如车载 / 远场 / 多人会议室），再去找厂家拿。
- **板上麦克风物理隔离** — 当前板上 mic 离扬声器 < 5 cm，腔体共振是回声主因。
  8.1 的 DAC 压低本质上是用电学手段抵消物理耦合；如果未来要彻底解决，机械
  改造或外接 mic 是更彻底但成本高得多的路线。

## 提交清单（持续更新）

`port/rv1126b-phase-0-recon`（superproject）:

```
a83917b  feat(port): AEC convergence sweep — sweet spot (DAC=155, delay=300)
```

`port/rv1126b-mpp`（client-sdk-rust）: 暂无（Phase 8.1 全部在 superproject 范围内）。
