#!/usr/bin/env bash
# Phase 0: 主机侧（ATK SDK 构建机）事实收集
# 用法：
#   export SDK_ROOT=~/atk-dlrv1126b-sdk   # 指向 Rockchip RV1126B Linux6.1 SDK 根
#   bash scripts/phase0-collect-host.sh | tee /tmp/phase0-host.txt
#
# 然后把输出贴到 docs/port/rv1126b/facts.md

set -u
SDK_ROOT="${SDK_ROOT:-$HOME/atk-dlrv1126b-sdk}"

echo "========================================"
echo "Phase 0 主机侧侦察 — $(date -Iseconds)"
echo "SDK_ROOT=$SDK_ROOT"
echo "========================================"

if [ ! -d "$SDK_ROOT" ]; then
  echo "ERROR: SDK_ROOT 目录不存在：$SDK_ROOT"
  echo "先解压 atk_dlrv1126b_linux6.1_sdk_release_v*.tar.gz，并设置 SDK_ROOT 变量"
  exit 1
fi

cd "$SDK_ROOT"

echo ""
echo "=== [1] SDK 版本 ==="
for f in .repo/manifests/RV1126B_Linux6.1_SDK_Note.md \
         docs/en/RV1126B/RV1126B_Linux6.1_SDK_Note.md; do
  if [ -f "$f" ]; then
    echo "File: $f"
    head -5 "$f"
    break
  fi
done
if [ -d .repo/manifests ]; then
  echo "当前 manifest:"
  ls -la .repo/manifests/default.xml 2>/dev/null
fi

echo ""
echo "=== [2] 工具链 ==="
TOOLCHAIN="$SDK_ROOT/buildroot/output/rockchip_rv1126b/host/usr/bin"
if [ ! -d "$TOOLCHAIN" ]; then
  echo "WARNING: Buildroot 工具链未构建，尝试 U-Boot/Kernel 预置工具链"
  TOOLCHAIN="$SDK_ROOT/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin"
fi
echo "PATH=$TOOLCHAIN"
ls -la "$TOOLCHAIN"/*-gcc 2>/dev/null | head -5

GCC_CANDIDATES=(
  "$TOOLCHAIN/aarch64-buildroot-linux-gnu-gcc"
  "$TOOLCHAIN/aarch64-linux-gcc"
  "$TOOLCHAIN/aarch64-none-linux-gnu-gcc"
)
for GCC in "${GCC_CANDIDATES[@]}"; do
  if [ -x "$GCC" ]; then
    echo "--- $GCC ---"
    "$GCC" --version | head -1
    echo "dumpmachine: $("$GCC" -dumpmachine)"
    echo "sysroot: $("$GCC" -print-sysroot)"
    echo "libgcc: $("$GCC" -print-libgcc-file-name)"
    break
  fi
done

echo ""
echo "=== [3] Sysroot 关键库 ==="
SYSROOT=$("$GCC" -print-sysroot 2>/dev/null)
if [ -n "$SYSROOT" ] && [ -d "$SYSROOT" ]; then
  echo "SYSROOT=$SYSROOT"
  for lib in libssl libcrypto libasound libv4l libudev librockchip_mpp librga libstdc++; do
    hit=$(find "$SYSROOT" -name "${lib}*" 2>/dev/null | head -1)
    printf "  %-20s  %s\n" "$lib" "${hit:-(missing)}"
  done
else
  echo "(sysroot 未确定，跳过)"
fi

echo ""
echo "=== [4] pkg-config ==="
for pc_dir in "$SYSROOT/usr/lib/pkgconfig" \
              "$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig" \
              "$SYSROOT/usr/share/pkgconfig"; do
  if [ -d "$pc_dir" ]; then
    echo "--- $pc_dir ---"
    ls "$pc_dir" 2>/dev/null | grep -iE 'ssl|crypto|alsa|v4l|udev|rockchip|mpp|rga' | sort
  fi
done

echo ""
echo "=== [5] MPP / Rockit 源码位置 ==="
for p in external/mpp external/rockit external/linux-rga; do
  if [ -d "$p" ]; then
    echo "FOUND: $SDK_ROOT/$p"
    ls "$p" | head -5
  else
    echo "MISSING: $SDK_ROOT/$p"
  fi
done

echo ""
echo "=== [6] Rust target ==="
if command -v rustup >/dev/null 2>&1; then
  rustup --version
  rustc --version
  echo "installed targets:"
  rustup target list --installed
else
  echo "rustup 未安装 — 先 curl https://sh.rustup.rs | sh"
fi

echo ""
echo "=== 收集完成 ==="
