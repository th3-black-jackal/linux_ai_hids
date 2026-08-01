# Cross-compile the HIDS service for 32-bit ARM (armhf) -- matches the
# Raspberry Pi OS (32-bit) userspace the training data was collected on,
# which is why FEATURE_ORDER contains geteuid32/setgroups32/statfs64/etc.
#
# Toolchain (Debian/Ubuntu host):
#   sudo apt install g++-arm-linux-gnueabihf cmake
#
# Configure + build:
#   cmake -B build-armhf -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-armhf.cmake
#   cmake --build build-armhf -j
#
# The binary is statically linked against libstdc++/libgcc so it drops into a
# minimal armhf rootfs (or runs under qemu-arm-static) without library juggling.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# Debian armhf baseline is ARMv7-A hard-float; the Pi 3B+ (Cortex-A53) is a
# superset, so this runs on the target and under qemu. Tune -mcpu=cortex-a53
# only for on-device release builds, not for portable/qemu test builds.
set(CMAKE_C_FLAGS_INIT   "-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard")

# Static C++ runtime so the binary is self-contained across rootfs variants.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
