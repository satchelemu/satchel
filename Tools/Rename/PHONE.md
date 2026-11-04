# Fresh installation and save restoration

These steps are for the orchestrator and user after all build checks pass. The rename
worker has not listed, copied, installed, uninstalled or launched anything on the phone.
Command syntax was checked with the installed `xcrun devicectl help` commands.
Use zsh. Keep all backups outside git. Never put keys or certificate passwords in logs.

1. In each installed app, save in game, then Quit to home. A paused game is still running.
   For Castlevania (`0100BA4026E94000`), Odyssey (`0100000000010000`) and Zelda
   (`0100F2C0115B6000`), open Save data and Export save if it holds any progress.
   In Files choose On My iPhone > Satchel, create `RenameSaveExports`, and save there.
   Also keep copies in a separate iCloud folder. If the installed build lacks export,
   retain the raw backups below and resolve backup integrity before uninstalling.
   Exported ZIPs preserve the guest filename mapping in `satchel-save.json`.

2. Unlock and connect the maintainer's iPhone. Inventory both app containers. A missing app or a
   missing save is recorded as absent; an unexpected copy error is a stop condition.

```zsh
rename_device=00008160-001A30E02E600036
rename_backup="$HOME/SatchelRenameBackup-2026-11-03"
mkdir -p "$rename_backup"
chmod 700 "$rename_backup"
for rename_id in com.dell.pocketlab com.dell.pocketlab.dev; do
  mkdir -p "$rename_backup/$rename_id"
  xcrun devicectl device info files --device "$rename_device" \
    --domain-type appDataContainer --domain-identifier "$rename_id" \
    --subdirectory Documents/NativeRuntime --recurse \
    --json-output "$rename_backup/$rename_id/runtime-inventory.json"
done
```

Copy exports from each installed app that has the folder:

```zsh
rename_id=com.dell.pocketlab
xcrun devicectl device copy from --device "$rename_device" \
  --domain-type appDataContainer --domain-identifier "$rename_id" \
  --source Documents/RenameSaveExports \
  --destination "$rename_backup/$rename_id/RenameSaveExports"
```

Repeat with `rename_id=com.dell.pocketlab.dev` if present. For each title that has a
committed save, also copy the raw directory. Use the actual title ID from the inventory:

```zsh
rename_title=0100F2C0115B6000
mkdir -p "$rename_backup/$rename_id/$rename_title"
xcrun devicectl device copy from --device "$rename_device" \
  --domain-type appDataContainer --domain-identifier "$rename_id" \
  --source "Documents/NativeRuntime/$rename_title/save" \
  --destination "$rename_backup/$rename_id/$rename_title/save"
```

Repeat for all three titles in both apps where present. Copy `SaveBackups` and the
committed `save_device` sibling using the same command if the inventory shows them.
Do not restore the transaction siblings `save.work`, `save.new`, `save_device.work`
or `save_device.new`. Do not copy caches, prepared dylibs, preferences or credentials
into the new app. Raw devicectl copies are supplementary: preservation of the
`org.d2rmobile.guest-name` extended attribute has not been verified. Prefer the app's
validated ZIP import for ordinary saves, and resolve any device-save metadata needs
before removing the only original.

Verify ZIP integrity on the Mac without printing save contents:

```zsh
python3 - "$rename_backup" <<'PY'
from pathlib import Path
import hashlib, json, sys, zipfile
count = 0
for path in Path(sys.argv[1]).rglob('*.zip'):
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None, 'ZIP CRC failed'
        manifest = json.loads(archive.read('satchel-save.json'))
        assert manifest['titleID'].upper() in {'0100BA4026E94000', '0100000000010000', '0100F2C0115B6000'}
        count += 1
print('Verified save ZIPs:', count)
assert count > 0, 'No ZIPs found: confirm absence of saves or resolve backup before uninstalling'
PY
```

Compare the count to the titles/apps that actually contain progress. Retain both
apps' exports separately if their progress differs. The app's importer later verifies
each file against the manifest's SHA-256 and title binding. Do not remove the old app
until the relevant backups are complete and readable on the Mac and in iCloud.

3. List installed applications and identify leftover UI test runners:

```zsh
xcrun devicectl device info apps --device "$rename_device" \
  --json-output "$rename_backup/installed-apps.json"
python3 - "$rename_backup/installed-apps.json" <<'PY'
import json, re, shlex, sys
ids = set()
def visit(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if key in {'bundleIdentifier', 'bundleID'} and isinstance(child, str): ids.add(child)
            visit(child)
    elif isinstance(value, list):
        for child in value: visit(child)
visit(json.load(open(sys.argv[1])))
for name in sorted(ids):
    if re.fullmatch(r'com\.dell\.pocketlab\..*uitests\.xctrunner', name, re.I):
        print('xcrun devicectl device uninstall app --device 00008160-001A30E02E600036 ' + shlex.quote(name))
PY
```

Review and run the printed runner commands. This includes, if installed,
`com.dell.pocketlab.uitests.xctrunner`,
`com.dell.pocketlab.pocketlabnavigationuitests.xctrunner` and
`com.dell.pocketlab.native.touchprobe.uitests.xctrunner`.
Then uninstall the two old apps, skipping a command only if its app is absent:

```zsh
xcrun devicectl device uninstall app --device "$rename_device" com.dell.pocketlab
xcrun devicectl device uninstall app --device "$rename_device" com.dell.pocketlab.dev
```

4. Provision the new IDs with automatic signing for your team.
   Use `CODE_SIGN_STYLE=Automatic`, `DEVELOPMENT_TEAM=$SATCHEL_TEAM`, and
   `-allowProvisioningUpdates` in the final signed Release build. The ordinary
   `NativeApp/build.py` signed path already uses automatic provisioning. Its
   `--unsigned` result cannot be installed. The user's Xcode account must have
   permission to register new App IDs and associate required capabilities. Verify
   the resulting profile grants the memory and virtual-address entitlements used
   by the current app and the new Keychain groups. Existing build paths still
   declare `group.com.dell.satchel.migration` and the corresponding new shared
   Keychain group; these do not grant access to any old app's credentials.

   `com.dell.satchel` is the main App ID, with `.dev` for the optional dev build.
   Each actually installed experiment/preview and signed UI-test runner uses its
   own new identifier. Do not register every string in the rename map: Keychain
   service names, dispatch queues, error domains and UTIs are not App IDs.
   There can be a registration limit: Apple's free Personal Team allows up to
   10 App IDs, expiring after seven days. Registering all previews, tests and
   experiments at once can exceed that allowance. The membership and current
   quota of this team were not queried. Do not promise that uninstalling apps
   releases a registration quota. Build only the main app first; if provisioning
   rejects an ID, the account owner must resolve the account restriction or wait
   for its stated expiry before continuing.

   Sources: [Apple account limits](https://developer.apple.com/help/account/basics/about-your-developer-account),
   [registering an App ID](https://developer.apple.com/help/account/identifiers/register-an-app-id),
   [automatic signing controls](https://developer.apple.com/help/account/access/automatic-signing-controls).

5. After a signed build and verification, the orchestrator installs the new app:

```zsh
cd '~/Documents/Repo - Personal/satchel'
ANYGAME_OUT="$HOME/SatchelBuild/rename/final-guard" SATCHEL_OUT="$HOME/SatchelBuild/rename/final" \
  nice -n 10 python3 -B NativePrepare/guard.py python3 -B NativeApp/build.py
rename_app="$HOME/SatchelBuild/rename/final/app/DerivedData/Build/Products/Release-iphoneos/Satchel.app"
codesign --verify --deep --strict "$rename_app"
xcrun devicectl device install app --device "$rename_device" "$rename_app"
```

The user opens Satchel and imports `prod.keys`, then the signing certificate/identity.
The user enters the certificate passphrase themselves on the phone. The worker and
orchestrator must never request, handle or log it. Re-import each of the three games
and its update from the allowed iCloud folders, then run Prepare again. Old prepared
packages have the old marker ABI and must not be reused. Re-pair controllers only
if iOS asks; app preferences start fresh.

6. Copy the backed-up export folder into the new app:

```zsh
xcrun devicectl device copy to --device "$rename_device" \
  --domain-type appDataContainer --domain-identifier com.dell.satchel \
  --source "$rename_backup/com.dell.pocketlab/RenameSaveExports" \
  --destination Documents/RenameSaveExports --remove-existing-content false
```

Use each prepared game's Save data > Import save and select its ZIP from On My iPhone
> Satchel > RenameSaveExports. Quit the game first. Choose the intended app's backup
when both old apps held different progress. The importer restores name metadata and
checks title ID, SHA-256, CRC, bounds and archive structure.

For a raw committed save with independently verified metadata, restore its contents
before first play, with Satchel fully stopped and that new title's save destination
empty. This command is also the route for a committed device-save directory if one
was present and its metadata has been verified:

```zsh
rename_title=0100F2C0115B6000
rename_save=save
xcrun devicectl device copy to --device "$rename_device" \
  --domain-type appDataContainer --domain-identifier com.dell.satchel \
  --source "$rename_backup/com.dell.pocketlab/$rename_title/$rename_save" \
  --destination "Documents/NativeRuntime/$rename_title/$rename_save" \
  --remove-existing-content false
```

Set `rename_save=save_device` only for that backed-up committed sibling. If the new
app already contains progress, export it first and use the app's atomic ZIP restore
instead of merging raw directory trees. Finally load each game's save and check its
progress on the phone. Keep the external backups until all three restores are
confirmed. The rename worker's Mac validation is not phone or gameplay evidence.
