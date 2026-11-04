#!/bin/zsh
# Builds the simulator UI preview into ~/SatchelBuild/redesign/preview.
# Pass "dev" to compile the native UI paths too (into preview-dev).
# Pass "game" for the game screen with the native menu bridge and pause button (into
# preview-game), plus the dev screens; it needs a display build's output in SATCHEL_DISPLAY_OUT
# (overlays, engine).
# Refuses to start while another xcodebuild runs (the Mac crashed on parallel builds).
set -euo pipefail
here=${0:A:h}
out=$HOME/SatchelBuild/redesign/preview
conditions='DEBUG SATCHEL_UI_PREVIEW_MINIMAL'
spec=$here/project.yml
display=${SATCHEL_DISPLAY_OUT:-$HOME/SatchelBuild/display2}
if [[ ${1:-} == dev ]]; then out=$HOME/SatchelBuild/redesign/preview-dev; conditions='DEBUG'; fi
if [[ ${1:-} == game ]]; then out=$HOME/SatchelBuild/redesign/preview-game; conditions='DEBUG PREVIEW_GAME'; spec=$here/project-game.yml; fi
if pgrep -x xcodebuild >/dev/null; then echo "Another xcodebuild is running; try again later." >&2; exit 1; fi
mkdir -p $out
repo=${here:h:h}
sed -e "s|__REPO__|$repo|g" -e "s|__DISPLAY__|$display|g" $spec > $out/project.yml
xcodegen generate --spec $out/project.yml --project $out --quiet
nice -n 10 xcodebuild -quiet -project $out/SatchelPreview.xcodeproj -scheme SatchelPreview \
  -configuration Debug -destination 'generic/platform=iOS Simulator' -derivedDataPath $out/DerivedData -jobs 2 SWIFT_ACTIVE_COMPILATION_CONDITIONS="$conditions" build
echo "Built: $out/DerivedData/Build/Products/Debug-iphonesimulator/SatchelPreview.app"
