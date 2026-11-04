#!/bin/zsh
set -euo pipefail
while true; do
  signer_busy=0
  for signer_pid in ${(f)$(pgrep -f xcodebuild || true)}; do
    if [[ "$(ps -p "$signer_pid" -o comm= 2>/dev/null)" == */xcodebuild ]]; then signer_busy=1; fi
  done
  if (( signer_busy == 0 )); then break; fi
  print 'Waiting for the other xcodebuild to finish.'
  sleep 15
done
