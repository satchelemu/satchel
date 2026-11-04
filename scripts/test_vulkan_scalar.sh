#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_OUT="${SATCHEL_VULKAN_SCALAR_BUILD_ROOT:-/tmp/satchel-vulkan-scalar}"
TASK_MVK=/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK
TASK_FLAGS=(-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
mkdir -p "$TASK_OUT/original-mutations"
xcrun clang -std=c11 "${TASK_FLAGS[@]}" -c "$TASK_ROOT/Tools/ScalarCompiler/ScalarCompiler.c" -o "$TASK_OUT/compiler.o"
xcrun clang++ -std=c++17 "${TASK_FLAGS[@]}" -Wno-missing-field-initializers -I "$TASK_MVK/include" -I "$TASK_ROOT/Tools/GuestGraphics" "$TASK_ROOT/Tools/VulkanBackend/VulkanBackend.cpp" "$TASK_ROOT/Tools/VulkanBackend/ScalarTest.cpp" "$TASK_OUT/compiler.o" "$TASK_MVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a" -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework IOSurface -framework IOKit -framework AppKit -o "$TASK_OUT/test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_OUT/test" "$TASK_OUT/original-mutations"
for TASK_SPV in "$TASK_OUT/original-mutations"/*.spv; do spirv-val --target-env vulkan1.1 "$TASK_SPV"; done
