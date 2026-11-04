# Horizon service coverage

This overlay extends the pinned NX2Apple host at `bbc3b73784751d2a706932dc69fd67627e3445d4`.
It follows the session and graphics overlays and never edits the dependency checkout.

## Coverage and priority

The engine primarily replaces named SDK functions. A function named `GetUserCount`
is not an implementation of the `acc:u0` wire protocol. Its only pinned raw HIPC service
is `olsc:u`, with a wildcard zero-success reply. The 883 declared SDK hooks are listed
in `coverage/engine-sdk.tsv`, with exact source locations, reasons and conservative
stub/policy classifications. Kernel SVC cases are in `coverage/engine-svc.tsv`.
The SDK table includes the wrapper macros for SSL, sockets, Palma and inline keyboards.
Graphics declaration tables that forward an API are not Horizon service commands.

`coverage/ryujinx-services.tsv` lists every named registration found in the two requested
Ryujinx source trees. `coverage/ryujinx-commands.tsv` lists numeric command attributes,
aliases, inherited commands and unnamed child interfaces. The latter use `(object)`
because they are obtained through parent commands, not directly from SM. Horizon
registrations record their server source; the server manager selects the concrete object.
The `sdk_candidates` column includes method-name cross-references and explicitly
reviewed SDK aliases. It does not claim raw IPC ABI support. Generic names can match multiple unrelated SDK hooks. A blank means
no exact name match, not proof that no differently named SDK function implements it.
Reference stub markers distinguish Ryujinx's own partial implementations.

The ranking is an engineering priority estimate, not a measured market-wide call rate.
The three allowed games have no missing-service observations in the retained baseline
boot logs. That evidence cannot rank unseen calls by frequency. The strongest remaining
evidence is the engine's SDK takeover notes and Ryujinx's command contracts:

| Rank | Service surface and command IDs | Evidence and pinned gap | This round |
| --- | --- | --- | --- |
| 1 | `set`: 0, 1, 2, 3, 4, 5, 6, 8 | Language selection is early startup behavior. `hle_sdk16.c` explains startup aborts for missing settings connections and Odyssey's older language enum behavior. Ryujinx `ISettingsServer` defines old and new list limits. No raw service in the pin. | All eight ported. Quest flag is an explicit once-logged reference stub. |
| 1 | `acc:u0`: 0, 1, 2, 3, 4, 50, 51, 141 | Local user selection precedes save mounting. `hle_account.c` documents Odyssey's aborting preselected-user call. Ryujinx `ApplicationServiceServer` defines null UID errors, complete-entry lists and application registration permission. No raw service in the pin. | All eight ported against the same existing local UID. Online-required selection retains Ryujinx's once-logged offline policy. |
| 2 | `acc:u0` 5 and profile object 0, 1, 10, 11 | Profile object creation and image loading follow user discovery. The SDK image hook fails. | Round 5 adds the factory and commands 0/1. Images 10/11 remain missing. |
| 2 | `time:u` 0, 1, 2, 4, 5, 50, 100, 101, 102, 200; `ISystemClock` 0, 1, 2, 3; `ISteadyClock` 0, 2, 3, 101, 102, 200, 201 | Clock access and permission checks during startup. The engine already has working SDK clocks but no equivalent child IPC objects. | Round 4: 21 commands, including permission-denied setters and unavailable network-clock results. |
| 2 | `fsp-srv`, `am` objects, `hid`, audio, `nvdrv`, `vi` | Boot, save, input and presentation paths are universal needs. Many SDK hooks exist. Full command IDs are in the table. | Unchanged. Their stateful objects and native handles still need individual ports. |
| 3 | `nifm:u` 4, 5; `IGeneralService` 5, 12, 15, 18 | Internet profile, IP, configuration and connectivity checks. The engine disables guest BSD networking. | Round 4: six commands. Real child creation and the reference no-interface error path, without claiming working guest networking. |
| 2 | friends, `prepo:u`, `aoc:u` | Boot and menu capability checks, DLC discovery and waitable notifications. | Round 6 contracts below. `pctl` and `bcat:u` remain unchanged. |
| 4 | Administrative, debug, hardware and other object commands | No observed calls in the allowed baseline logs. | Unchanged. |

The current raw HIPC transport still enters through the SDK's `nn::sf::hipc` hooks.
Direct SVC SM connections and raw control messages are not newly implemented. Existing
SDK control hooks provide pointer-buffer size, duplicate sessions and domain conversion.
Do not label every Ryujinx row as an actual game blocker: many callers use SDK takeovers.

## Implemented behavior

* `set` returns the configured `DM_LANG` and the existing host region policy. Language
  conversion preserves all 18 Ryujinx entries, rejects out-of-range indices with the
  settings result, and honors old 15-entry and new 18-entry lists and buffer capacities.
* `acc:u0` exposes the engine's one local user. Null UIDs fail, unknown UIDs return false,
  lists clear unused entries and never emit a partial UID. Application user registration
  is forbidden. No online account or additional local identity is invented.
* `time:u` returns local/user, network and steady clock child objects. User/local time,
  context and steady time call the same engine functions as SDK clients. This preserves
  `DM_FIXED_TIME`, context stability and clock source IDs across both paths. Network
  clocks remain uninitialized and report the reference error; automatic correction and
  network accuracy are false. Initial year is the reference setting, 2019. Time setters
  enforce the user service's read-only permissions. The host steady clock has zero test
  and internal offsets, no RTC reset and a successful setup result.
* `nifm:u` creates a general service in both old and new forms. Its current profile, IP,
  IP configuration and connection status commands return `NoInternetConnection`, matching
  Ryujinx's offline branch and the pinned engine's disabled guest networking. They never
  expose a host IP address or write a successful fabricated network profile.
* Child objects work as moved session handles or domain IDs. There are at most 64 open
  handles and 32 objects per domain. Duplicate handles share the domain and its lifetime,
  including conversion performed after duplication. Closing an object invalidates it;
  closing the last handle retires the whole domain. Capacity exhaustion returns an error,
  and a failed reply releases any newly allocated object. Session reset clears all owners.
  Unknown child commands preserve interface-specific once-only diagnostics.
* Unknown commands return CMIF `UnknownCommandId` rather than fabricated success. Existing
  `olsc:u` compatibility replies remain zeroed and are now visibly marked as stubs.
* Session and domain headers, descriptors and lengths are checked before access. Native
  buffer writes use `vm_write`, available in both installed Apple SDKs, so an unmapped or read-only destination returns a
  failure instead of a host dereference fault. Request payloads are never dumped.
* `ServiceIPC service=... command=... status=... caller=module+offset` records each distinct
  service, full 32-bit command and diagnostic kind once per session, across duplicate
  handles and threads. The first caller is retained. A failed connection has command
  `4294967295`, meaning no command was sent. Unknown host callers use `host+0x0`.
  The bounded table retains the first 1024 distinct diagnostics and stops logging when full.
* The app resets `services.log` when launching and loads it before the last 256 KiB of
  `host.log`. These lines appear in the existing Game report view and JSON export.
  Service and caller labels are sanitized. Guest arguments, addresses outside a known
  module, raw message bytes, keys and paths are never included in these new diagnostics.

## Reproduce

Run from the worktree, with outputs outside it:

```sh
python3 -B NativeHost/services/inventory.py
ANYGAME_OUT="$HOME/SatchelBuild/anygame/services" SATCHEL_OUT="$HOME/SatchelBuild/anygame/services" python3 -B NativePrepare/guard.py python3 -B NativeHost/services/check.py
ANYGAME_OUT="$HOME/SatchelBuild/anygame/services" SATCHEL_OUT="$HOME/SatchelBuild/anygame/services/app" python3 -B NativePrepare/guard.py python3 -B NativeApp/build.py --unsigned
```

The guard waits for heavy work, applies nice 10, stops owned processes before the
10 GiB RSS cap, and preserves 35 GiB free disk. Build drivers use two compiler jobs.
No command installs or launches on the phone.

Round 4 adds 27 command contracts, for 43 ported inventory rows. Synthetic checks exercise
every added command in both session and domain mode, both network clock factories,
duplicate lifetime, closed objects, capacity exhaustion and concurrent handle ownership.
The native adapter check links the pinned `hle_time.c` and compares IPC time and context
against its SDK functions. The Mac boot driver requires the original frame 300 and 1200
checkpoints within 65 seconds for all three allowed titles, with baseline and overlay hosts.
These are boot checks, not proof of visible gameplay or a complete Horizon implementation.

Remaining time gaps include shared-memory and operation-event handles, time-zone objects,
snapshots and privileged service variants. Round 6 adds NIFM request objects and copied event handles. Direct SVC SM connections remain outside this SDK transport overlay.
The child inventory rows explicitly apply only to the `time:u` and `nifm:u` parents.

## Attribution

Command semantics and descriptor layouts are adapted from Ryujinx 1.3.3:
`Settings/ISettingsServer.cs`, `SystemState/SystemStateMgr.cs`,
`Account/Acc/IAccountServiceForApplication.cs`, `Account/Acc/ApplicationServiceServer.cs`,
`Settings/ResultCode.cs`, `Account/ResultCode.cs`, `Ipc/IpcBuffDesc.cs`,
`Ipc/IpcRecvListBuffDesc.cs`, `Ipc/IpcMessage.cs`, `Services/IpcService.cs`,
`Time/IStaticServiceForGlue.cs`, `Time/IStaticServiceForPsc.cs`,
`Time/StaticService/ISystemClock.cs`, `Time/StaticService/ISteadyClock.cs`,
`Time/Clock/SteadyClockCore.cs`, `Time/Clock/StandardSteadyClockCore.cs`,
`Time/Types/TimePermissions.cs`, `Time/ResultCode.cs`, `Settings/NxSettings.cs`,
`Nifm/IStaticService.cs`, `Nifm/StaticService/IGeneralService.cs` and `Nifm/ResultCode.cs`
under `src/Ryujinx.HLE/HOS` (service paths relative to `Services`).
The MIT notice is in `RYUJINX-LICENSE.txt` and copied into the app's native notices.
No firmware, GPL reference, game code or game data is used in the overlay or tests.

## Round 5 account profiles

The next source-ranked gap is `acc:u0` command 5 (`GetProfile`), followed by
`IProfile` commands 0 and 1 (`Get` and `GetBase`). These add three inventory rows,
for 46 total. No allowed-game run has yet supplied an empirical frequency ranking.
The profile factory rejects null and unknown users using Ryujinx results. Replies
use the existing engine UID and `DM_NICK` nickname policy, a 56-byte ProfileBase,
and bounded 128-byte UserData. The reference's fixed icon UserData remains marked
as a stub. The local profile's modification timestamp is zero because no editable
profile save exists. Image and online-account commands remain unknown.

Sources: `Account/Acc/AccountService/IProfile.cs`,
`Account/Acc/AccountService/ProfileServer.cs`, and
`Account/Acc/ApplicationServiceServer.cs` from the same MIT reference.
Synthetic checks cover both transport modes, malformed UIDs and buffers, duplicate
lifetime, full reply lengths, nickname values and native adapter dispatch.

## Round 6 broad boot and menu compatibility

Implemented: 45 additional command contracts, bringing the overlay to 91 inventory
rows. Ranking remains source-based, not a claim about measured game call frequency.
The inventory now resolves Horizon server-manager filenames with their `.cs` suffix
and imports from the manager. This maps friends and prepo roots to their registered
ports and expands the full reference inventory to 1,662 rows.

| Surface | New commands | Behavior |
| --- | --- | --- |
| `aoc:u` | 0 through 10, 50 | Package DLC count and pagination, base ID, reference prepare/mount-check stubs, lost-content error, quiet list-change event |
| `friend:u` | 0, 1 | Friend and per-user notification objects, null-user rejection |
| Friend object | 0, 1, 10100, 10101, 10120, 10121, 10400, 10420, 10421 | Signaled completion event, cancel stub, empty friend/blocked lists and reference availability stubs |
| Notification object | 0, 1, 2 | Quiet event, clear, reference queue-empty error |
| NIFM general object | 4 | Versioned request factory |
| NIFM request object | 0, 1, 2, 3, 4, 11, 21 | Error state, network-unavailable result, two quiet events, cancel/submit/confirmation stubs, unavailable applet info |
| `prepo:u` | 10100 through 10105, 10200, 10300, 10400, 40100, 40101 | Discarded report stubs, per-object transmission/agreement state and stable session ID |

SendSyncRequest uses the existing big-stack entry so first-time DLC directory
scans do not overflow small guest stacks. Caller diagnostics preserve the original
guest return address. AOC reads the same cached, sorted, deduplicated index as the pinned SDK hooks, from
`DM_AOC_DIR`, with the same `DM_AOC_OWNED=0` policy and session reset. It does not
read DLC contents. Explicit queries for a different application return no owned
content. List counts must fit the receive buffer; only complete indices are written.
The base ID uses the existing package convention, program ID plus `0x1000`. Packages
do not currently carry a custom NACP AddOnContentBaseId into this host API. Prepare
and mount-status check remain the reference's explicitly logged no-op stubs. The
package list is fixed during a run, so its event stays unsignaled.

Copy-handle replies now use real readable events in the existing kernel handle
table, including two-handle replies and domain replies. Each service object owns
one handle per event and each client receives an independent copy. Client closure,
domain-object closure, last-session closure, duplicate sessions, reset, failed reply
encoding and handle exhaustion preserve reference counts. Allocation exhaustion
returns a CMIF error instead of aborting the host. The kernel reset runs after the
service reset, releasing any remaining client copies.

Friends completion follows the reference's initially signaled event. Notifications
stay empty offline and never signal. Friend and blocked lists are reference stubs,
not connected account data. NIFM follows the reference's offline Error state but
returns `NoInternetConnection` from GetResult instead of the reference's unconditional
success. Submit does not establish networking or signal an invented connection.
Prepo reports are discarded without dereferencing, logging or transmitting their
payloads. The reference SaveReport methods also ignore ProcessPlayReport's returned
validation result. Transmission status reflects only the reference's local flags,
not successful delivery. Administrative prepo ports remain unimplemented.

Additional MIT sources, relative to the pinned source trees:

* HLE: `HOS/Services/Ns/Aoc/IAddOnContentManager.cs`, `Ns/Aoc/ResultCode.cs`,
  `Nifm/StaticService/IRequest.cs`, and `Nifm/StaticService/IGeneralService.cs`.
* Horizon: `Sdk/Friends/Detail/Ipc/ServiceCreator.cs`, `FriendService.cs`,
  `NotificationService.cs`, `Sdk/Friends/FriendResult.cs`,
  `Sdk/Sf/RawDataOffsetCalculator.cs`, and `Prepo/Ipc/PrepoService.cs`.

The round 6 synthetic suite exercises every new command in session and domain mode,
including partial and oversized DLC lists, foreign application IDs, null users,
CMIF alignment, event copy lifetime, failed replies, allocation exhaustion, domain
close and reset. A separate native support test lifts the generated event helpers,
actual kernel table declarations and pinned DLC scan into a synthetic fixture, then
checks real reference counting and table exhaustion. The adapter links these same
helpers and checks copied events through the actual SDK transport hooks. This is
separate from real-game boot validation and does not claim phone execution.

Reproduce this round from the worktree, with the existing guard and two build jobs:

```sh
python3 -B NativeHost/services/inventory.py
ANYGAME_OUT="$HOME/SatchelBuild/anygame/round6" SATCHEL_OUT="$HOME/SatchelBuild/anygame/round6" python3 -B NativePrepare/guard.py python3 -B NativeHost/services/check.py
ANYGAME_OUT="$HOME/SatchelBuild/anygame/round6" SATCHEL_OUT="$HOME/SatchelBuild/anygame/round6" python3 -B NativePrepare/guard.py python3 -B NativeHost/services/build_mac.py
ANYGAME_OUT="$HOME/SatchelBuild/anygame/round6" SATCHEL_OUT="$HOME/SatchelBuild/anygame/round6" python3 -B NativePrepare/guard.py python3 -B NativeHost/services/run_mac.py
ANYGAME_OUT="$HOME/SatchelBuild/anygame/round6" SATCHEL_OUT="$HOME/SatchelBuild/anygame/round6/app-build" python3 -B NativePrepare/guard.py python3 -B NativeApp/build.py --unsigned
```

The Mac runs use only the three existing allowed packages under
`~/SatchelBuild/anygame`, fresh task-owned saves, and filtered host diagnostics.
They retain the original frame 300 and 1200 requirements within 65 seconds for
both baseline and overlay builds. No phone action is performed by these commands.

Next service gap: account profile images and account application-manager objects,
followed by time-zone/shared-memory objects and stateful filesystem IPC. Those
require their own backing data or handle contracts and remain unchanged here.

Round 6 validation status (2026-11-02): inventory reproduction, Python syntax and
whitespace checks passed. The compiled synthetic suite, both Mac host builds, all
six automated boot runs and the canonical unsigned Release app build passed under
the original guards. No implementation fix was needed during resumed validation.
All boot runs retained frames 300 and 1200 before the 65-second limit, with no
fatal, crash or missing-service diagnostics. The app also passed preparation-symbol,
shader-import ownership and registry checks. Signing and phone execution were not
performed.

The boot pass measures frame progress, not visible gameplay. Odyssey and TOTK have
black frame-1200 captures in both hosts, with matching missing vertex/compute shader
diagnostics. Castlevania reaches its language menu, with intermittent missing text:
the first pair showed text only in the baseline capture, while a guarded repeat
showed text only in the service-host capture. Both repeated boots passed. Earlier
`services2` captures also show the issue, predating round 6. These rendering
limitations were recorded and left unchanged. Exact commands, measurements, retained artifacts
and phone test steps are in `~/SatchelBuild/anygame/report.md`.

## Round 7 missing-command summaries and system objects

The native failure path now maintains `missing-services.json` beside `host.log` in
`Documents/NativeRuntime/<title>/`. Launch clears the file. Teardown clears the
in-memory counters but preserves the completed title's file for Prepare diagnostics.
Entries contain only
`service`, `interface`, `command`, `method` (null if unknown), `firstSeenFrame` and
`count`. The key is the service port, interface and full 32-bit command ID, shared
across duplicate handles, moved children and domain objects. Failed connections use
command `4294967295`. Counts saturate at UInt64.max; the first 1,024 distinct keys
are retained. Each update atomically replaces the bounded JSON snapshot. File-system
failure never changes the guest's result. An unknown command still returns CMIF
UnknownCommandId. Successful reference stubs stay in `services.log`. Explicit Audio NotImplemented
and applet Stubbed failures also enter the JSON summary, preserving their original
error results.

The method dictionary is generated from the coverage inventory into
`coverage/MissingServiceNames.inc`. It contains source labels only. Runtime requests,
arguments, buffers, storage contents, addresses and game-derived data are excluded.
The frame is the existing host presentation counter, not proof of a visible game
frame. Service failures before presentation have frame zero. The JSON reader limits
size and entries, validates labels, and feeds the existing Game report view and
export. Prepare now offers the same Game diagnostics view. Session package and
attempt-time guards still prevent displaying another package's runtime report.

The 86 new contracts bring the ported inventory to 177 rows.
The new command set is declared in `round7.json`. Each declared contract must be
exercised in both session and domain mode by the sanitizer-backed suite. This is
source-ranked work, not a measured frequency ranking. The three overlay round 7
boot checks observed no missing service calls; the summary's failure behavior is
validated with synthetic requests.

| Surface | Commands and scope |
| --- | --- |
| `appletOE`, application proxy | Open application proxy; get common state, self controller, library creator and application functions |
| `appletAE`, library proxy | OpenLibraryAppletProxy 200/201; get common state, self controller and library creator |
| IApplicationFunctions | Preselected-user launch storage, empty user channel, terminate-result state, running acknowledgment, media/crash-report flags, capture-unavailable query, quiet GPU/invitation/notification/health events |
| ISelfController | Balanced fatal sections, exit-lock reference stubs, restart/display flags, user-active stub, launchable and suspended-tick events |
| ICommonStateGetter | Boot-mode stub, sleep-lock stub and quiet event, VR/backlight flags, HDCP stub and global platform region |
| Library applets and storage | Foreground error dismissal and keyboard cancellation, state/output events, single cancellation output, bounded shared storage and accessors |
| `vi:u` | Application-only display factory, reference fixed 1280 by 720 resolution, enable stub and validated scaling-mode conversion |
| `audren:u`, AudioDevice | Both device factories and commands 15 through 20 with the reference NotImplemented result; no invented device-notification handles |
| `pctl` | Both factories, stereo permission/reset queries and unrestricted local-policy queries |
| `time:u` | Timezone factory, denied setter and the reference's firmware-free catalogue fallback |

Storage has a 64 KiB per-object cap. Accessors retain shared contents after the parent
closes. Read and write use bounded Mach copies, including pointer and map-alias
transports. Input storage objects are validated before use; moved sessions are
released after dispatch, while domain IDs retain caller ownership. Closing, reset,
allocation failure and reply failure release owned storage and event handles.
Keyboard cancellation returns the reference's 0x7d8-byte standard output, result 1
and no entered text. Error dismissal emits no output data. Inline/background keyboards
and other applet IDs fail with AppletLaunchFailed. Transfer-memory storage, layers,
Binder and indirect display handles remain unsupported. These stubs do not display
a native keyboard or error dialog. The remaining flags describe the reference's local
state. They do not record video, capture screenshots, change iOS sleep or implement VR.

The missing timezone firmware path in Ryujinx marks the manager initialized without
setting its total count (zero), while the glue location cache falls back to one UTC
entry. The port preserves both contracts. It does not fabricate a timezone archive.
Time conversions remain the existing host-local SDK takeover.

### SDK exclusions reviewed this round

`coverage/ryujinx-commands.tsv` now includes reviewed nonidentical SDK names as well
as exact method-name candidates. A candidate still is not raw IPC support.

* AudioDevice auto-select list/name/volume commands delegate to the original commands,
  covered by `hle_audren.c`'s device hooks. Notification queries correspond to its
  AcquireAudioDeviceNotificationForInput/Output and switch-notification hooks.
* AudioOut open, append, release and notification setup stay in `hle_audio.c`. Auto
  variants use the same SDK operations. Raw played-sample count, contains and flush
  commands remain unported. No dummy audio object or synthetic playback success is
  introduced.
* AudioRenderer is opened through `hle_audren.c`'s CPU/manual SDK renderer. Its edge
  getters/setters and updates belong to that SDK object, not a new raw IPC object.
  The pinned host supplies the render thread and output. Raw renderer IPC remains
  unported; a future observed raw caller needs an actual backend object, not zero replies.
* `hle_codec.c` redirects HardwareOpusDecoder and HardwareOpusMultiStreamDecoder to
  the SDK software implementations, including sizing, initialization, reset and decode.
  Those hooks are retained. Raw hwopus sessions and newer wire variants remain unported.
* `hle_oe.c` already implements the notification queue and event, operation/performance
  notification switches, focus suspension, screenshot and recording switches, copyright
  visibility, idle extension, auto-sleep sections and album/record flags. Those 15
  nonidentical-name rows are marked as SDK exclusions. The raw overlay
  does not create a second notification queue or competing sleep/idle state.
* Timezone WithMyRule conversions use the same ToCalendarTime/ToPosixTime SDK hooks.
  Focus, operation/performance mode, screenshot orientation and existing account/save
  commands with applicable SDK hooks are unchanged.
* ILibraryAppletAccessor Start/GetResult were reviewed despite nonempty candidate
  columns: Start matched `movie::Decoder::Start`; GetResult matched account, friends
  and network-clock async objects. Those are unrelated interfaces, so they do not
  answer the library applet contracts added here. IStorage's Fs interface is also
  distinct from the applet IStorage and is not marked ported. Storage-accessor
  GetSize matched the unrelated EC shop AsyncResponse hook; Read/Write matched SSL
  connection hooks. None answers an applet storage object. No networking is added.

Additional MIT sources: `Am/AppletOE/ApplicationProxyService/IApplicationProxy.cs`,
`Am/AppletOE/ApplicationProxyService/ApplicationProxy/IApplicationFunctions.cs`,
`Am/AppletAE/IAllSystemAppletProxiesService.cs`, the common/self/library-creator
objects under `Am/AppletAE/AllSystemAppletProxiesService/SystemAppletProxy`,
`LibraryAppletCreator/ILibraryAppletAccessor.cs`, `Am/AppletAE/IStorage.cs`,
`IStorageAccessor.cs`, `Storage/StorageHelper.cs`, the Error and SoftwareKeyboard
applets, `Vi/IApplicationRootService.cs`, `Vi/RootService/IApplicationDisplayService.cs`,
`Pctl/IParentalControlServiceFactory.cs`, its child service, and Time's Glue/Psc timezone
services, TimeZoneManager and TimeZoneContentManager. Audio contracts come from
Horizon's `Sdk/Audio/Detail/AudioDevice.cs` and `AudioRendererManager.cs`.
The original MIT notice remains in `RYUJINX-LICENSE.txt` and the app notices.

Run the round 6 commands above with `round7` replacing `round6`. The canonical app
cache fingerprint includes both new runtime includes and the generated method table.
Round 7 build, boot measurements and final readiness are recorded in
`~/SatchelBuild/anygame/report.md`. No phone execution is performed by this worker.

The final sanitizer suite passes all 86 contracts in both transports, including
teardown retention and fresh-launch summary counts. All three allowed titles reach
frames 300 and 1200 on the baseline and overlay Mac hosts with no crash or fatal
diagnostics. Castlevania reaches its language menu with the existing intermittent
text issue. Odyssey and TOTK retain the baseline's black captures and missing-shader
diagnostics. These checks establish boot progress, not visible gameplay.
The guarded canonical unsigned Release app build also passes. Its built metadata
retains `com.dell.satchel`, the Satchel display name and both landscape orientations.
Signing, provisioning and phone execution remain the orchestrator's checks.

## Round 8 files, controllers and time

Implemented: 96 transport contracts in `round8.json`, including the distinct
permission-bearing clock children. These add 77 inventory rows, bringing ported
coverage to 254 of 1,662 rows. The inventory records 125 reviewed SDK exclusions
for this round. Ranking follows the source inventory, not measured call frequency.
`round8-sdk.json` records nonidentical SDK names. Filesystem and HID matches are
restricted to their own SDK source file so unrelated Read, Write and GetSize
methods cannot hide missing contracts.

| Surface | Added behavior |
| --- | --- |
| `fsp-srv` | Archive-filesystem open returns PathNotFound for absent package archives; account/device save open and read-only open validate current program, user, space, type, rank and index |
| IFileSystem | Free and total host-volume space, real file timestamps, pointer and map-alias path descriptors, missing timestamp paths and invalid-path errors |
| `hid` | Reference six-axis activation and configuration stubs, accelerometer parameters/play mode, passthrough and home-button flags, GC ERM vibration stubs, console sensor activation, fusion-strength state, USB configuration and touch configuration stubs |
| `time:a`, `time:s`, `time:r` | User/local/network/steady/timezone factories with reference access permissions, RTC query or reference NotImplemented, correction state and update time, initial-year query |
| Clock children | Shared local-clock adjustments and contexts, repair-only steady offsets, read-only or uninitialized network errors, mismatch/overflow errors |
| Timezone children | Firmware-free glue catalogue behavior, denied setters, admin setter returns TimeZoneNotFound without a rule archive, PSC count zero and reference NotImplemented catalogue query |

Save queries share the existing SDK mounts and pending save transactions. The only
raw save types opened here are the current user's account save and the current
program's device save. Foreign programs/users, system saves, raw cache/temporary
attributes, nonzero save IDs, ranks and indices return TargetNotFound. Raw mutation,
file/directory objects and storage objects remain unsupported. Read-only factory
objects expose queries only; this round adds no raw mutation command. Archive
open does not interpret a guest string as a host path. Timestamp queries resolve
through the existing mount, reject traversal, and open components without following
symlinks. Space queries validate the path but query
the mounted filesystem itself, including before a leaf exists. SDK free-space
queries accept registered bare mount names as well as qualified paths. They do not
log paths or contents. RomFS image space/timestamp queries return UnsupportedOperation rather than invented volume or timestamp data.

The pinned SDK's GetFreeSpaceSize returned a fixed 256 MiB, even for missing mounts.
That demonstrably wrong query is replaced with the same mounted-volume lookup used
by raw IPC. Mount-table exhaustion now returns a resource error under the existing
SDK locks instead of aborting. This avoids a race between an IPC capacity check
and an SDK mount on another thread. Free space means available host-volume bytes, not a save quota. All
other file/directory/storage read, write, size and enumeration hooks stay with the
SDK. Its save creation/extension, cache/temporary mounts and cache list, path-length
attributes, and Program/Data storage SDK equivalents (MountRom, MountAddOnContent,
MountSystemData) remain in place. No duplicate raw archive or DLC backend is added.

The SDK also continues to own controller styles and assignment, handheld activation
and Joy-Con hold modes, vibration devices/values and controller sensor samples.
Palma still reports no connected device through its existing SDK failure hooks.
Gesture initialization/samples remain with the host recognizer, which reports none
in console mode. The new raw HID commands are explicit reference stubs or local
configuration state. They do not claim an additional controller, USB device, motion
sample or delivered vibration. Duplicate handles share state; separate HID service
objects have independent reference state.

Time changes affect the guest session only. They never set the host clock. SDK
current-time reads, contexts, realtime and monotonic timespecs, raw IPC and snapshots
share the new state. The underlying reference `SteadyClockCore.GetCurrentTimePoint`
adds test and internal offsets separately after converting them to seconds. The
port preserves that behavior, including negative offsets. Network clocks remain
uninitialized. Automatic correction stores the flag and update time without
inventing a network clock. Snapshot creation propagates clock mismatch errors.
The session reset clears adjustments and correction state. Existing SDK calendar
conversions still use host timezone rules, not a fabricated firmware archive.

Additional MIT sources from Ryujinx 1.3.3: `Fs/IFileSystemProxy.cs`,
`Fs/FileSystemProxy/IFileSystem.cs`, `IFile.cs`, `IDirectory.cs`, `IStorage.cs`,
`Hid/IHidServer.cs`, `Time/IStaticServiceForGlue.cs`, `IStaticServiceForPsc.cs`,
`Time/StaticService/ISystemClock.cs`, `ISteadyClock.cs`, the Glue/Psc timezone
services, `Time/Types/TimePermissions.cs`, and clock cores under `Time/Clock`.
The LibHac SaveDataAttribute layout was checked against the reference's installed
assembly with a synthetic reflection program. The MIT notice is retained in
`RYUJINX-LICENSE.txt` and on the adapted runtime includes.

The synthetic suite requires every declared contract in both session and domain
mode. It links the real pinned filesystem with synthetic mounts, exercises pending
save transactions, space and timestamps, missing paths and symlink/traversal refusal,
and checks clock permissions, SDK/IPC agreement, snapshots, resets and duplicate
ownership. Existing contract groups and malformed-message checks remain enabled.
The Mac gate additionally requires empty missing-services.json files and no unknown,
missing or UNIMPL diagnostics, alongside frames 300, 1200 and 3000 on both hosts
for at least 120 seconds. Scripted input is the default. Castlevania must commit
a save and the services variant must successfully query free space. `--idle-only`
is an explicit diagnostic option and does not verify the save route.

Reproduce with the round 6 commands above, replacing `round6` with `round8`.
Use `NativeApp/build.py --unsigned` for the canonical guarded Release build.
Final validation, measurements, limitations and phone test steps are recorded in
`~/SatchelBuild/anygame/report.md`. No phone action is performed by this worker.

The initial round 8 boot gate passed while idle, but missed a save-path regression.
Castlevania passes a bare mount name to SDK GetFreeSpaceSize. The first overlay
required a colon-qualified path and returned PathNotFound (0x202) before looking
up the mount. The game then aborted at main+0x5fcc6c, with its diagnostic Result 0.
Scripted Mac runs reproduced the same abort twice, after about 22 seconds. The
prior idle Mac gate never reached the query; scripted input does. The phone logs
do not establish which input or persisted setting advanced its run sooner.

The SDK boundary now resolves a registered bare mount name to its root. It does
not turn unknown mounts into successes or fabricate capacity. Timestamp lookup
still requires an existing entry. Synthetic checks cover bare and qualified names,
missing descendants, missing mounts, host root aliases, traversal, and timestamp
symlink refusal. No clock, HID, abort or result-handling bypass is used for the fix.

`DM_SF_TRACE=1` logs every handled round 8 IPC command and its result, plus SDK
free-space queries, as `Round8 service=... command=... result=0x...` on stderr.
It is disabled by default and logs no paths, request buffers or guest contents.
The app clears inherited DM variables before loading the verified profile. For a
phone diagnostic run, the device owner can set this existing switch in LLDB at
`satchel_host_session_enter`, after environment setup and before the game starts.
Normal phone validation does not require tracing. Exact steps and the current
validation state are in `~/SatchelBuild/anygame/report.md`.

## Round 9 measured SDK file flush

The 2026-11-04 inventory of retained reports found 43 allowed-game reports with
no missing IPC entries; seven synthetic reports were excluded. Three 330-second
Mac runs with private save copies also had no missing IPC entries or unresolved
SDK calls. Mac SDK counters measured `FlushFile` 10 times in Odyssey and eight
times in Zelda. Castlevania did not call it on this route. Higher-frequency rows
in the broad inventory include functioning audio, video and file-read code;
that heuristic classification is not proof of a stub. Clock initialization and
CPU boost policies remain unchanged. CPU boost has no SDK return value; whether
the games inspect FlushFile's Result is not established by the call counters.

The SDK `FlushFile` takeover now follows the backing-call/result propagation of
Ryujinx 1.3.3 `Fs/FileSystemProxy/IFile.cs`, command 2. Writable host files use
`fsync`, retry `EINTR`, and return the existing host failure Result on an error.
Null, closed, oversized and invalid image handles fail. Immutable image entries
and read-only files have no pending writes, so their game inputs are not synced.
A failed flush poisons the same save transaction as a failed write. Its later
Commit is refused even if the caller ignored the flush failure. A new complete
transaction can recover. Flush itself does not publish a save transaction.
This is one corrected SDK command, not a new raw IFile IPC object or a claim of
complete LibHac errno parity. The MIT notice remains in `RYUJINX-LICENSE.txt`.

`DM_SF_TRACE=1` emits payload-free `Round9 service=SDK.FlushFile command=2 result=...`
rows. Host tests exercise actual backing-file flushing, interrupted and failed
calls, invalid and read-only handles, preservation of the prior committed save,
and a subsequent successful transaction. No power-loss durability test is claimed.

The Mac harness can use `build_mac.py --source-shaders` when the retained cache
contains Metal source and reflection but no compiled libraries. It compiles that
source in memory with the options used by `NativeShaders/ShaderCompiler.swift`.
Compiler diagnostics retain numeric codes only. This optional Mac adapter is not
part of the iOS host and does not translate shaders or copy packages. The Mac
renderer remains the retained reference snapshot; the canonical unsigned app
build checks integration with the complete current overlay chain.

`run_mac.py --count-sdk` writes SDK counts at graceful exit. `--save-seed DIR`
copies `DIR/TITLE/save` into each private run and checks that the seed hashes did
not change. `--shots` can add required captures, but cannot remove frames 300,
1200 or 3000. `--keys` overrides the route for one explicitly selected title.
Every run writes its own result file, and later titles still run after one fails.
A passing frame gate requires separate visual review before claiming gameplay or
scene coverage. A missing-service count of zero cannot demonstrate a reduction;
round 9 removes the observed FlushFile no-op while retaining zero missing entries.

The after-change 330-second runs passed for all three games. The SDK call counts
matched six successful flush traces in Odyssey and eight in Zelda; Castlevania
did not call FlushFile. Save commits were four, 12 and four respectively. All
private seeds were unchanged. Visual checks confirmed the Castlevania opening
area, Odyssey's newly saved File 2 loaded as the active slot, and Zelda's loading
screen followed by gameplay and movement. The latter two are same-area reloads.
The host suite, Mac rebuild and canonical unsigned Release app build passed.
Xcode printed a Swift compile diagnostic reporting failure with exit code zero;
the driver exited zero, its artifact checks passed, and the new native flush
symbols were verified in the freshly linked app. No phone run is claimed.
