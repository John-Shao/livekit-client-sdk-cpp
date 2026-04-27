# 第三方参考资料的权威位置

我们在调试时偶尔会拉取 Rockchip MPP / RGA 的 demo 源码或 webrtc-prebuilt 头文件
当文档读。这些**不入仓**（人家的版权、原始位置就在 SDK 里、入仓既冗余又会过
时）。需要时按下面的路径从 VM scp 到本地 `C:\Users\<USER>\AppData\Local\Temp\
mpp-ref\` 或类似 scratch 目录，用完丢弃。

## VM (`rv1126b-vm`) 上的标准位置

### Rockchip MPP（编解码 API）

```
$HOME/atk-dlrv1126b-sdk/external/mpp/inc/                MPP 公共头
  ├── rk_mpi.h                  主入口（mpp_create / mpp_init / MppApi）
  ├── rk_mpi_cmd.h              control() ioctl 常量
  ├── mpp_buffer.h              buffer group / ref counting
  ├── mpp_frame.h               MppFrame
  ├── mpp_packet.h              MppPacket
  ├── mpp_meta.h                metadata (KEY_OUTPUT_PACKET 等)
  ├── mpp_err.h                 MPP_RET 错误码
  ├── rk_type.h                 基础类型 (RK_U32 / MppCtx 等)
  ├── rk_venc_cfg.h             MppEncCfg API
  ├── rk_venc_cmd.h             编码器配置常量
  └── rk_venc_rc.h              rate control 类型
$HOME/atk-dlrv1126b-sdk/external/mpp/test/               官方 demo
  ├── mpi_enc_test.c            编码 sample（我们的 encoder.cpp 范本）
  ├── mpi_dec_test.c            解码 sample（我们的 decoder.cpp 范本）
  └── ...
```

链接位置（buildroot sysroot）：

```
$HOME/atk-dlrv1126b-sdk/buildroot/output/alientek_rv1126b/host/
  aarch64-buildroot-linux-gnu/sysroot/usr/lib/librockchip_mpp.so
```

`scripts/env-rv1126b.sh` 已自动 export `ROCKCHIP_MPP_INCLUDE` 和
`ROCKCHIP_MPP_LIB`，无需手动配置。

### Rockchip RGA（2D 硬件加速器）

```
$HOME/atk-dlrv1126b-sdk/external/linux-rga/
  ├── im2d_api/im2d.h           im2d 用户态 API（推荐）
  ├── include/RgaApi.h          老 API（兼容用）
  └── samples/cvtcolor_demo/src/rga_cvtcolor_demo.cpp   色彩转换 sample
```

板上 runtime: `/usr/lib/librga.so.2.1.0`

### libwebrtc 头（webrtc-prebuilt）

```
$HOME/webrtc-prebuilt/linux-arm64-release/include/api/
  ├── video/i420_buffer.h
  ├── video/nv12_buffer.h
  ├── video/video_frame.h
  ├── audio/builtin_audio_processing_builder.h
  ├── audio/audio_processing.h            APM Config struct
  ├── peer_connection_interface.h
  └── ...
```

`scripts/env-rv1126b.sh` 已 export `LK_CUSTOM_WEBRTC` 指向
`$HOME/webrtc-prebuilt/linux-arm64-release/`。

## 快速 scp 模板

读 MPP API 时常用：

```sh
# 拉一个或几个文件到本地 scratch，用完丢
scp rv1126b-vm:'$HOME/atk-dlrv1126b-sdk/external/mpp/inc/{rk_mpi.h,mpp_buffer.h}' /tmp/refs/
scp rv1126b-vm:'$HOME/atk-dlrv1126b-sdk/external/mpp/test/mpi_enc_test.c' /tmp/refs/

# webrtc 头
scp rv1126b-vm:'$HOME/webrtc-prebuilt/linux-arm64-release/include/api/audio/audio_processing.h' /tmp/refs/
```

## 板上 (`rv1126b-board`) 的相关文件

```
/dev/mpp_service              MPP 内核驱动
/dev/rga                      RGA 内核驱动
/usr/lib/librockchip_mpp.so.0 MPP 用户态 (2.4 MB)
/usr/lib/librga.so.2.1.0      RGA 用户态 (231 KB)
/usr/bin/mpi_enc_test         MPP 自带编码冒烟工具
/usr/bin/mpi_dec_test         MPP 自带解码冒烟工具
```

板上 `mpi_enc_test` / `mpi_dec_test` 可直接跑，作为 MPP 是否正常的 baseline 验证。
