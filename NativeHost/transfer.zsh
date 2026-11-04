#!/bin/zsh
# Printed by default. Only the device owner should invoke the --execute mode.
set -euo pipefail
host_root="$HOME/SatchelBuild/host"
package="$host_root/package/TOTK-1.4.3"
destination='Documents/PreparedGames/TOTK-1.4.3'
device='00008160-001A30E02E600036'
bundle='com.dell.satchel.dev'
[[ -f "$package/manifest.json" ]] || { print -u2 'Complete prepared package is missing.'; exit 1; }
perform() {
    if [[ "${1:-}" == '--execute' ]]; then shift; "$@"
    else shift; printf '%q ' "$@"; printf '\n'; fi
}
mode="${1:---print}"
perform "$mode" xcrun devicectl device install app --device "$device" "$host_root/DerivedData/Build/Products/Release-iphoneos/SatchelDev.app"
for item in game.dylib profile.tsv dm msl romfs.img manifest.json; do
    perform "$mode" xcrun devicectl device copy to --device "$device" --domain-type appDataContainer --domain-identifier "$bundle" --source "$package/$item" --destination "$destination/$item"
done
perform "$mode" xcrun devicectl device process launch --device "$device" "$bundle"
