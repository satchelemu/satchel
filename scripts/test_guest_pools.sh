#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_CPU_ARCHIVE="${SATCHEL_CPU_ARCHIVE:-/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a}"
TASK_OUT="${SATCHEL_POOL_BUILD_ROOT:-/tmp/satchel-guest-pools}"
TASK_MVK="/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK"
mkdir -p "$TASK_OUT"
xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$TASK_ROOT/Tools/CPUProbe" -I "$TASK_ROOT/Tools/GuestGraphics" -I "$TASK_ROOT/Tools/VulkanBackend" \
  -Dsatchel_vulkan_buffer_write=satchel_pool_test_buffer_write -Dsatchel_vulkan_buffer_destroy=satchel_pool_test_buffer_destroy \
  -Dsatchel_vulkan_buffer_read=satchel_pool_test_buffer_read \
  -c "$TASK_ROOT/Tools/GuestGraphics/GuestPools.c" -o "$TASK_OUT/GuestPools.o"
xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$TASK_ROOT/Tools/CPUProbe" -I "$TASK_ROOT/Tools/GuestGraphics" -I "$TASK_ROOT/Tools/VulkanBackend" \
  -c "$TASK_ROOT/Tools/GuestGraphics/PoolTest.c" -o "$TASK_OUT/PoolTest.o"
xcrun clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I "$TASK_MVK/include" -I "$TASK_ROOT/Tools/GuestGraphics" \
  "$TASK_ROOT/Tools/VulkanBackend/VulkanBackend.cpp" "$TASK_OUT/GuestPools.o" "$TASK_OUT/PoolTest.o" \
  "$TASK_CPU_ARCHIVE" "$TASK_MVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a" \
  -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework IOSurface -framework IOKit -framework AppKit -o "$TASK_OUT/test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_OUT/test"
# The reusable iOS archive contains no test interception and stays staged.
xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -target arm64-apple-ios17.0 \
  -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" -I "$TASK_ROOT/Tools/CPUProbe" -I "$TASK_ROOT/Tools/VulkanBackend" -I "$TASK_ROOT/Tools/GuestGraphics" \
  -c "$TASK_ROOT/Tools/GuestGraphics/GuestPools.c" -o "$TASK_OUT/GuestPools-ios.o"
xcrun libtool -static -o "$TASK_OUT/libSatchelGuestPools.a" "$TASK_OUT/GuestPools-ios.o"
