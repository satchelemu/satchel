#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_OUT="${SATCHEL_NVN_COMPUTE_BUILD_ROOT:-/tmp/satchel-nvn-compute}"
TASK_CPU="${SATCHEL_CPU_ARCHIVE:-/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a}"
TASK_MVK=/tmp/satchel-vulkan-backend/macos/MoltenVK/MoltenVK
TASK_SRC="$TASK_ROOT/Tools/GuestGraphics"
TASK_VULKAN="$TASK_ROOT/Tools/VulkanBackend"
TASK_INCLUDES=(-I "$TASK_SRC" -I "$TASK_VULKAN" -I "$TASK_ROOT/Tools/ScalarCompiler" -I "$TASK_ROOT/Tools/CPUProbe")
TASK_FLAGS=(-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
mkdir -p "$TASK_OUT/host" "$TASK_OUT/faults" "$TASK_OUT/ios"
xcrun clang++ -std=c++17 "${TASK_FLAGS[@]}" -Wno-missing-field-initializers "${TASK_INCLUDES[@]}" -I "$TASK_MVK/include" -c "$TASK_VULKAN/VulkanBackend.cpp" -o "$TASK_OUT/host/backend.o"
xcrun clang -std=c11 "${TASK_FLAGS[@]}" "${TASK_INCLUDES[@]}" -c "$TASK_ROOT/Tools/ScalarCompiler/ScalarCompiler.c" -o "$TASK_OUT/host/scalar.o"
TASK_PRODUCTION=(GuestDispatch GuestPools GuestTransfers GuestVulkanPools GuestVulkanTransfers GuestVulkanCompute GuestCompute GuestPrograms GuestNVNComputeDiagnostic)
for TASK_NAME in "${TASK_PRODUCTION[@]}" NVNComputeTest NVNComputeDiagnosticTest TransferDispatchTest; do
 xcrun clang -std=c11 "${TASK_FLAGS[@]}" "${TASK_INCLUDES[@]}" -c "$TASK_SRC/$TASK_NAME.c" -o "$TASK_OUT/host/$TASK_NAME.o"
done
for TASK_NAME in GuestTransfers GuestCompute; do
 TASK_EXTRA=(-Dsatchel_cpu_probe_writev=nvn_test_writev)
 if [[ "$TASK_NAME" == GuestCompute ]]; then TASK_EXTRA=(-Dsatchel_vulkan_scalar_submit=nvn_test_submit -Dsatchel_vulkan_buffer_destroy=nvn_test_destroy); fi
 xcrun clang -std=c11 "${TASK_FLAGS[@]}" "${TASK_INCLUDES[@]}" -include "$TASK_SRC/NVNComputeFaults.h" "${TASK_EXTRA[@]}" -c "$TASK_SRC/$TASK_NAME.c" -o "$TASK_OUT/faults/$TASK_NAME.o"
done
for TASK_TEST in NVNComputeTest NVNComputeDiagnosticTest TransferDispatchTest; do
 TASK_OBJECTS=("$TASK_OUT/host/backend.o" "$TASK_OUT/host/scalar.o" "$TASK_OUT/host/$TASK_TEST.o")
 for TASK_NAME in "${TASK_PRODUCTION[@]}"; do
  TASK_DIR=host
  if [[ "$TASK_TEST" == NVNComputeTest && ( "$TASK_NAME" == GuestTransfers || "$TASK_NAME" == GuestCompute ) ]]; then TASK_DIR=faults; fi
  TASK_OBJECTS+=("$TASK_OUT/$TASK_DIR/$TASK_NAME.o")
 done
 xcrun clang++ "${TASK_FLAGS[@]}" "${TASK_OBJECTS[@]}" "$TASK_CPU" "$TASK_MVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a" -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework IOSurface -framework IOKit -framework AppKit -o "$TASK_OUT/$TASK_TEST"
 ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_OUT/$TASK_TEST"
done
for TASK_NAME in "${TASK_PRODUCTION[@]}"; do
 xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -target arm64-apple-ios17.0 -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" "${TASK_INCLUDES[@]}" -c "$TASK_SRC/$TASK_NAME.c" -o "$TASK_OUT/ios/$TASK_NAME.o"
 if [[ "$TASK_NAME" != GuestCompute && "$TASK_NAME" != GuestPrograms ]]; then xcrun libtool -static -o "$TASK_OUT/ios/libSatchel$TASK_NAME.a" "$TASK_OUT/ios/$TASK_NAME.o"; fi
done
xcrun libtool -static -o "$TASK_OUT/ios/libSatchelGuestComputeOwner.a" "$TASK_OUT/ios/GuestCompute.o" "$TASK_OUT/ios/GuestPrograms.o"
