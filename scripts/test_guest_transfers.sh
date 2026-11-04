#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_CPU_ARCHIVE="${SATCHEL_CPU_ARCHIVE:-/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a}"
TASK_OUT="${SATCHEL_TRANSFER_BUILD_ROOT:-/tmp/satchel-guest-transfer-queue/direct}"
TASK_MVK="/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK"
mkdir -p "$TASK_OUT"
for TASK_SOURCE in GuestPools TransferTest; do
 xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I "$TASK_ROOT/Tools/CPUProbe" -I "$TASK_ROOT/Tools/GuestGraphics" -I "$TASK_ROOT/Tools/VulkanBackend" \
 -c "$TASK_ROOT/Tools/GuestGraphics/$TASK_SOURCE.c" -o "$TASK_OUT/$TASK_SOURCE.o"
done
xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I "$TASK_ROOT/Tools/CPUProbe" -I "$TASK_ROOT/Tools/GuestGraphics" -I "$TASK_ROOT/Tools/VulkanBackend" \
 -Dsatchel_vulkan_transfer_batch=satchel_transfer_test_batch -Dsatchel_cpu_probe_writev=satchel_transfer_test_writev \
 -c "$TASK_ROOT/Tools/GuestGraphics/GuestTransfers.c" -o "$TASK_OUT/GuestTransfers.o"
xcrun clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-missing-field-initializers \
 -fsanitize=address,undefined -fno-omit-frame-pointer -I "$TASK_MVK/include" -I "$TASK_ROOT/Tools/GuestGraphics" \
 "$TASK_ROOT/Tools/VulkanBackend/VulkanBackend.cpp" "$TASK_OUT/GuestPools.o" "$TASK_OUT/GuestTransfers.o" "$TASK_OUT/TransferTest.o" \
 "$TASK_CPU_ARCHIVE" "$TASK_MVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a" \
 -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework IOSurface -framework IOKit -framework AppKit -o "$TASK_OUT/test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_OUT/test"
xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -target arm64-apple-ios17.0 \
 -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" -I "$TASK_ROOT/Tools/CPUProbe" -I "$TASK_ROOT/Tools/VulkanBackend" -I "$TASK_ROOT/Tools/GuestGraphics" \
 -c "$TASK_ROOT/Tools/GuestGraphics/GuestTransfers.c" -o "$TASK_OUT/GuestTransfers-ios.o"
xcrun libtool -static -o "$TASK_OUT/libSatchelGuestTransfers.a" "$TASK_OUT/GuestTransfers-ios.o"
