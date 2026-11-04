#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_OUT="/tmp/satchel-vulkan-compute-staging/diagnostic"
TASK_MVK="/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK"
TASK_COMPILER="${SATCHEL_SHADER_HOST_DIR:-/tmp/satchel-shader-cold-staging/repro/host-production-publish}"
mkdir -p "$TASK_OUT"
for TASK_SOURCE in GuestComputeDiagnostic ComputeDiagnosticTest; do
 xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I "$TASK_ROOT/Tools/GuestGraphics" -I "$TASK_ROOT/Tools/VulkanBackend" -I "$TASK_ROOT/Tools/MaxwellShaderCompiler" \
 -c "$TASK_ROOT/Tools/GuestGraphics/$TASK_SOURCE.c" -o "$TASK_OUT/$TASK_SOURCE.o"
done
xcrun clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I "$TASK_MVK/include" -I "$TASK_ROOT/Tools/GuestGraphics" "$TASK_ROOT/Tools/VulkanBackend/VulkanBackend.cpp" \
 "$TASK_OUT/GuestComputeDiagnostic.o" "$TASK_OUT/ComputeDiagnosticTest.o" "$TASK_COMPILER/SatchelShaderProbe.dylib" -Wl,-rpath,"$TASK_COMPILER" \
 "$TASK_MVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a" \
 -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework IOSurface -framework IOKit -framework AppKit -o "$TASK_OUT/test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_OUT/test"
xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -target arm64-apple-ios17.0 -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" \
 -I "$TASK_ROOT/Tools/GuestGraphics" -I "$TASK_ROOT/Tools/VulkanBackend" -I "$TASK_ROOT/Tools/MaxwellShaderCompiler" \
 -c "$TASK_ROOT/Tools/GuestGraphics/GuestComputeDiagnostic.c" -o "$TASK_OUT/GuestComputeDiagnostic-ios.o"
xcrun libtool -static -o "$TASK_OUT/libSatchelGuestComputeDiagnostic.a" "$TASK_OUT/GuestComputeDiagnostic-ios.o"
