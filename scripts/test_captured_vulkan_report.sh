#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-captured-report.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
xcrun swiftc -swift-version 5 -warnings-as-errors -O -g -sanitize=address \
 -import-objc-header Tools/GuestGraphics/GuestCapturedProgramDiagnostic.h \
 App/SwitchCapturedVulkanReport.swift Tests/captured_vulkan_report.swift -o "$TASK_BUILD/report-test"
ASAN_OPTIONS=halt_on_error=1 "$TASK_BUILD/report-test"
