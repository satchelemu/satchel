#!/bin/zsh
# Device-owner control helper. This script sends commands only when invoked.
set -euo pipefail
[[ $# -ge 2 ]] || { print -u2 'Usage: ctl.zsh TITLE_ID reset|send|shot|pull [arguments]'; exit 2; }
title="${1:u}"
[[ ${#title} == 16 && "$title" != *[^0-9A-F]* ]] || { print -u2 'Invalid title ID'; exit 2; }
shift
local_root="$HOME/SatchelBuild/games/control/$title"
mkdir -p "$local_root"
control="$local_root/ctl.txt"
runtime="Documents/NativeRuntime/$title"
device='00008160-001A30E02E600036'
bundle='com.dell.satchel.dev'
copy_from() {
    xcrun devicectl device copy from --device "$device" --domain-type appDataContainer --domain-identifier "$bundle" --source "$1" --destination "$2"
}
upload() {
    xcrun devicectl device copy to --device "$device" --domain-type appDataContainer --domain-identifier "$bundle" --source "$control" --destination "$runtime/ctl.txt"
}
case "$1" in
    reset) : > "$control" ;;
    send) shift; for line in "$@"; do print -r -- "$line" >> "$control"; done; upload ;;
    shot)
        name="${2:-frame}"
        [[ "$name" != *[^A-Za-z0-9_]* ]] || { print -u2 'Use letters, digits or underscore for screenshot names'; exit 2; }
        print -r -- "shot shot-$name.png" >> "$control"
        upload
        sleep 3
        copy_from "$runtime/shot-$name.png" "$local_root/$name.png"
        ;;
    pull) copy_from "$2" "$3" ;;
    *) print -u2 'Unknown control action'; exit 2 ;;
esac
