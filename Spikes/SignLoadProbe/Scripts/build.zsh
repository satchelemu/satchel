#!/bin/zsh
set -euo pipefail
probe_root=${0:A:h:h}
xcodegen generate --spec "$probe_root/project.yml"
nice -n 10 xcodebuild -jobs 2 -project "$probe_root/SignLoadProbe.xcodeproj" -scheme SignLoadProbe \
  -configuration Release -destination 'id=00008160-001A30E02E600036' \
  -derivedDataPath "$HOME/SatchelBuild/spike2/DerivedData" -allowProvisioningUpdates build
