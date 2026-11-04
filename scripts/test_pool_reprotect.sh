#!/bin/zsh
set -eu
repo_root=${0:A:h:h}
exec python3 "$repo_root/Tools/GuestGraphics/build_pool_reprotect.py" "$@"
