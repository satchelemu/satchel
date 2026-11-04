#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_OUT="/tmp/satchel-vulkan-compute-staging"
TASK_MVK="/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK"
TASK_COMPILER="${SATCHEL_SHADER_HOST_DIR:-/tmp/satchel-shader-cold-staging/repro/host-production-publish}"
mkdir -p "$TASK_OUT"
xcrun clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I "$TASK_MVK/include" -I "$TASK_ROOT/Tools/GuestGraphics" \
 "$TASK_ROOT/Tools/VulkanBackend/VulkanBackend.cpp" "$TASK_ROOT/Tools/VulkanBackend/ComputeTest.cpp" \
 "$TASK_COMPILER/SatchelShaderProbe.dylib" -Wl,-rpath,"$TASK_COMPILER" \
 "$TASK_MVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a" \
 -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework IOSurface -framework IOKit -framework AppKit -o "$TASK_OUT/test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_OUT/test"
