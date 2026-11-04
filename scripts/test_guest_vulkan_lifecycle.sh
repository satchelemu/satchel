#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_CPU_ARCHIVE="${SATCHEL_CPU_ARCHIVE:-/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a}"
TASK_OUT="/tmp/satchel-guest-dispatch/staging"
TASK_MVK="/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK"
mkdir -p "$TASK_OUT"
# Run after the checksum-verifying Vulkan build and coordinate a frozen CPU archive.
for TASK_SOURCE in GuestDispatch LifecycleTest; do
  xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$TASK_ROOT/Tools/CPUProbe" -I "$TASK_ROOT/Tools/GuestGraphics" -I "$TASK_ROOT/Tools/VulkanBackend" \
    -c "$TASK_ROOT/Tools/GuestGraphics/$TASK_SOURCE.c" -o "$TASK_OUT/$TASK_SOURCE.o"
done
xcrun clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I "$TASK_MVK/include" -I "$TASK_ROOT/Tools/GuestGraphics" \
  "$TASK_ROOT/Tools/VulkanBackend/VulkanBackend.cpp" "$TASK_OUT/GuestDispatch.o" "$TASK_OUT/LifecycleTest.o" \
  "$TASK_CPU_ARCHIVE" \
  "$TASK_MVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a" \
  -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework IOSurface -framework IOKit -framework AppKit \
  -o "$TASK_OUT/lifecycle-test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_OUT/lifecycle-test"
xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -target arm64-apple-ios17.0 \
  -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" -I "$TASK_ROOT/Tools/CPUProbe" \
  -c "$TASK_ROOT/Tools/GuestGraphics/GuestDispatch.c" -o "$TASK_OUT/GuestDispatch-ios.o"
xcrun libtool -static -o "$TASK_OUT/libSatchelGuestDispatch.a" "$TASK_OUT/GuestDispatch-ios.o"
