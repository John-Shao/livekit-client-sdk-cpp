# Toolchain for Rockchip RV1126B (Cortex-A53, aarch64, Buildroot 2024.02)
#
# Usage:
#   source scripts/env-rv1126b.sh                 # exports ATK_TOOLCHAIN_ROOT/SYSROOT, etc.
#   cmake -S . -B build-rv1126b \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rv1126b-aarch64-linux-gnu.cmake \
#     -DCMAKE_BUILD_TYPE=Release
#
# Prereq: scripts/env-rv1126b.sh has been sourced — sets
#   ATK_TOOLCHAIN_ROOT (e.g. .../buildroot/output/alientek_rv1126b/host)
#   ATK_SYSROOT        (e.g. $ATK_TOOLCHAIN_ROOT/aarch64-buildroot-linux-gnu/sysroot)

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{ATK_TOOLCHAIN_ROOT})
  message(FATAL_ERROR
    "ATK_TOOLCHAIN_ROOT not set. Run 'source scripts/env-rv1126b.sh' before invoking cmake.")
endif()
if(NOT DEFINED ENV{ATK_SYSROOT})
  message(FATAL_ERROR
    "ATK_SYSROOT not set. Run 'source scripts/env-rv1126b.sh' before invoking cmake.")
endif()

set(ATK_TOOLCHAIN_ROOT $ENV{ATK_TOOLCHAIN_ROOT})
set(ATK_SYSROOT        $ENV{ATK_SYSROOT})

set(CMAKE_SYSROOT        ${ATK_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${ATK_SYSROOT})

set(_prefix aarch64-buildroot-linux-gnu)
# Buildroot lays out toolchain bins under host/usr/bin (not host/bin)
set(_bindir ${ATK_TOOLCHAIN_ROOT}/usr/bin)

set(CMAKE_C_COMPILER   ${_bindir}/${_prefix}-gcc)
set(CMAKE_CXX_COMPILER ${_bindir}/${_prefix}-g++)
set(CMAKE_AR           ${_bindir}/${_prefix}-ar)
set(CMAKE_RANLIB       ${_bindir}/${_prefix}-ranlib)
set(CMAKE_STRIP        ${_bindir}/${_prefix}-strip)
set(CMAKE_LINKER       ${_bindir}/${_prefix}-ld)
set(CMAKE_OBJCOPY      ${_bindir}/${_prefix}-objcopy)
set(CMAKE_OBJDUMP      ${_bindir}/${_prefix}-objdump)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Cortex-A53: armv8-a + CRC. Crypto extensions（AES/PMULL/SHA）板上有但
# 部分批次工具链未默认开 +crypto，先按下不表，需要时再加。
set(CMAKE_C_FLAGS_INIT   "-march=armv8-a+crc")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a+crc")

# bindgen 用 libclang 不读 GCC 的 sysroot —— 这里冗余 export 一下，
# 即使开发者忘 source env 脚本，CMake 也能保证 cargo build.rs 看到。
# Buildroot sysroot 是 flat 布局（无 multi-arch 子目录），不加 aarch64-linux-gnu/。
set(ENV{BINDGEN_EXTRA_CLANG_ARGS}
    "--sysroot=${ATK_SYSROOT} -I${ATK_SYSROOT}/usr/include")
