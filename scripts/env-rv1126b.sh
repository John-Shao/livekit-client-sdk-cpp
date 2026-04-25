#!/usr/bin/env bash
# LiveKit RV1126B (ATK-DLRV1126B) 交叉编译环境
# Usage: source scripts/env-rv1126b.sh
#
# 默认指向 VM 上 ATK SDK 内就地的 Buildroot toolchain。如果把工具链
# tar 打包到其他位置，运行前 export ATK_TOOLCHAIN_ROOT 覆盖。
#
# 故意不修改 client-sdk-rust/.cargo/config.toml —— 上游那份已经给
# aarch64-unknown-linux-gnu 配好了 "-fuse-ld=lld"（链 libwebrtc.a
# 必需），本脚本只用 env 变量补上 linker / ar / sysroot 位，避免
# 改动 submodule。

# === 这两行改成你机器上的实际路径 ===
export ATK_SDK_ROOT="${ATK_SDK_ROOT:-$HOME/atk-dlrv1126b-sdk}"
export ATK_TOOLCHAIN_ROOT="${ATK_TOOLCHAIN_ROOT:-$ATK_SDK_ROOT/buildroot/output/alientek_rv1126b/host}"
export ATK_SYSROOT="${ATK_SYSROOT:-$ATK_TOOLCHAIN_ROOT/aarch64-buildroot-linux-gnu/sysroot}"

# 防呆检查
if [ ! -x "$ATK_TOOLCHAIN_ROOT/usr/bin/aarch64-buildroot-linux-gnu-gcc" ]; then
  echo "[env-rv1126b] ERROR: toolchain not found at $ATK_TOOLCHAIN_ROOT/usr/bin" >&2
  echo "[env-rv1126b] 请确认 Phase 0 的 Buildroot 构建已完成，或 export ATK_TOOLCHAIN_ROOT 覆盖" >&2
  return 1 2>/dev/null || exit 1
fi

# PATH 后置工具链 bin —— 让 system cmake/make/python3 等无前缀工具仍走系统版（Buildroot
# host/usr/bin 自带 cmake 但没编 HTTPS-libcurl，FetchContent 直接挂）；带前缀的
# aarch64-buildroot-linux-gnu-* 在系统里没有，照样从 toolchain 命中。
export PATH="$PATH:$ATK_TOOLCHAIN_ROOT/usr/bin"

# 确保 rustup 的 cargo/rustc 在 PATH（非交互 ssh 不会自动 source ~/.cargo/env）
if [ -f "$HOME/.cargo/env" ]; then
  . "$HOME/.cargo/env"
fi

# pip --user 安装的可执行（如新版 cmake）放在这里 —— focal 系统 cmake 是 3.16.3，
# 项目 CMakeLists.txt 要求 ≥ 3.20，pip install --user cmake 拿 4.x 救场。
if [ -d "$HOME/.local/bin" ]; then
  export PATH="$HOME/.local/bin:$PATH"
fi

# Buildroot toolchain 不内建 ld.lld，而 client-sdk-rust/.cargo/config.toml
# 强制 -fuse-ld=lld 来链 libwebrtc.a。把系统 /usr/bin/ld.lld 软链到 GCC 的
# exec 搜索目录（host/aarch64-buildroot-linux-gnu/bin/），让 GCC 的 collect2
# 能找到。Buildroot 重建 toolchain 后符号链接会丢，所以每次 source 幂等修一次。
_atk_lld_dst="$ATK_TOOLCHAIN_ROOT/aarch64-buildroot-linux-gnu/bin/ld.lld"
if [ -d "$(dirname "$_atk_lld_dst")" ] && [ ! -e "$_atk_lld_dst" ] && [ -x /usr/bin/ld.lld ]; then
  ln -sf /usr/bin/ld.lld "$_atk_lld_dst"
fi
unset _atk_lld_dst

PREFIX=aarch64-buildroot-linux-gnu
TARGET=aarch64-unknown-linux-gnu

# cargo 指定 linker / ar（不走 .cargo/config.toml，保持 submodule 干净）
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER="$PREFIX-gcc"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_AR="$PREFIX-ar"

# cc crate / build.rs 里的 cc 调用
export CC_aarch64_unknown_linux_gnu="$PREFIX-gcc"
export CXX_aarch64_unknown_linux_gnu="$PREFIX-g++"
export AR_aarch64_unknown_linux_gnu="$PREFIX-ar"

# pkg-config 指向目标 sysroot（Buildroot 扁平布局，无 multi-arch）
export PKG_CONFIG_ALLOW_CROSS=1
export PKG_CONFIG_SYSROOT_DIR="$ATK_SYSROOT"
export PKG_CONFIG_PATH="$ATK_SYSROOT/usr/lib/pkgconfig:$ATK_SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_LIBDIR="$ATK_SYSROOT/usr/lib/pkgconfig:$ATK_SYSROOT/usr/share/pkgconfig"

# bindgen 用 libclang，不读 gcc 的 --print-sysroot，必须单独告诉
export BINDGEN_EXTRA_CLANG_ARGS="--sysroot=$ATK_SYSROOT -I$ATK_SYSROOT/usr/include"

# protoc 必须是 host 版（Ubuntu 包就行）
export PROTOC="${PROTOC:-$(command -v protoc || echo /usr/bin/protoc)}"

# LiveKit webrtc prebuilt：若 ~/webrtc-prebuilt/linux-arm64-release/ 已存在，
# 自动设 LK_CUSTOM_WEBRTC 绕过 webrtc-sys build.rs 的 158MB 下载（在中国到
# release-assets.githubusercontent.com 基本拉不动，用本地 unzip 的副本最稳）。
# 手动覆盖：先 export LK_CUSTOM_WEBRTC 再 source 本脚本。
if [ -z "$LK_CUSTOM_WEBRTC" ] && [ -f "$HOME/webrtc-prebuilt/linux-arm64-release/lib/libwebrtc.a" ]; then
  export LK_CUSTOM_WEBRTC="$HOME/webrtc-prebuilt/linux-arm64-release"
fi

echo "[env-rv1126b] ATK_SDK_ROOT=$ATK_SDK_ROOT"
echo "[env-rv1126b] ATK_TOOLCHAIN_ROOT=$ATK_TOOLCHAIN_ROOT"
echo "[env-rv1126b] ATK_SYSROOT=$ATK_SYSROOT"
echo "[env-rv1126b] gcc: $($PREFIX-gcc --version | head -1)"
echo "[env-rv1126b] rust target: $TARGET"
echo "[env-rv1126b] protoc: $(protoc --version 2>/dev/null || echo NOT FOUND)"
[ -n "$LK_CUSTOM_WEBRTC" ] && echo "[env-rv1126b] LK_CUSTOM_WEBRTC=$LK_CUSTOM_WEBRTC"
