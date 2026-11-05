# Install and keep Satchel installed

Satchel is a sideloaded iPhone app. You install it using your own Apple development
account. Use SideStore, AltStore Classic, or Xcode. This guide does not cover AltStore
PAL. There is no public Satchel download or hosted source yet: build the IPA yourself
(see [BUILDING.md](../BUILDING.md)) or get one from someone who did, then save it in
Files on your iPhone.

## Before you start

The current build declares iOS 26.0 as its minimum. The tested project target is an
iPhone Duo on iOS 27.1, in landscape. A minimum version in the installer does not mean
every phone has been tested or has enough memory to play games.

Prepare accepts an Apple Development identity from the same team that signed the app you
installed, whichever team that is. Only the maintainer's account has been tried so far.
The app also requests increased memory, extended virtual addressing, App Groups and
Keychain access. Free-account installation, these capabilities and prepared-game
loading have not been verified together. Do not buy an account expecting that to fix
this limit. Installation alone is not proof that games can play.

You enter account details only in the installer or Xcode yourself. Never send an Apple
Account password, signing identity, certificate or passphrase to a helper. Satchel's
console keys and its Apple signing identity are different things.

## Your Apple certificate and account are at risk

Prepare uses your own Apple Development certificate to sign game code that is neither
yours nor Satchel's. Apple's developer agreement lets you sign apps for your own devices,
but it does not describe this use, and Apple can revoke a certificate or close a developer
account at its discretion, without warning and without appeal. If that happens, every
app you signed with that certificate stops opening, including Satchel and your prepared
games, and a paid membership is not refunded.

Use a developer account you can afford to lose. Do not use the account your employer,
your business or your published apps depend on. A free account has the same exposure,
plus the seven day limit. Satchel never sends your certificate anywhere, but the
signature itself is visible to Apple whenever the phone checks it.

## SideStore

1. On your computer and iPhone, follow the official [SideStore prerequisites](https://docs.sidestore.io/docs/installation/prerequisites) and [installation guide](https://docs.sidestore.io/docs/installation/install). Set up its local VPN, trust the developer in Settings, and enable Developer Mode when instructed.
2. Connect to Wi-Fi and turn on LocalDevVPN. Open SideStore and refresh SideStore itself before installing Satchel.
3. In My Apps, tap the add button and select Satchel.ipa from Files. Review the requested permissions and finish installation. Keep the same account for later refreshes.
4. Open Satchel. Refresh it and SideStore from My Apps before either expires. SideStore needs Wi-Fi and its local VPN for installation and refresh. Initial setup and recovery can require the computer again.

SideStore's [FAQ](https://docs.sidestore.io/docs/faq) explains its account limits and
recovery. If pairing stops working after an iOS update, follow its pairing recovery
instructions. Do not delete Satchel to troubleshoot a refresh.

## AltStore Classic

1. Follow the official [AltStore Classic setup](https://faq.altstore.io/altstore-classic/altserver). Install AltServer on your Mac or PC and use it to install AltStore on the iPhone. Complete trust and Developer Mode prompts yourself.
2. Keep AltServer running. Connect the phone by USB or use the same Wi-Fi network.
3. Open AltStore, go to My Apps, tap the add button and select Satchel.ipa from Files. Review permissions and install.
4. Use Refresh All in My Apps while AltServer is reachable. Check that Satchel's expiry counter actually advances. Background refresh can fail, so check it regularly.

See [AltStore's app and refresh guide](https://faq.altstore.io/altstore-classic/your-altstore).

## Adding a source later

When a maintainer supplies a real source URL, open Browse, Sources, then add that URL
in SideStore or AltStore Classic. Select Satchel from the source to install or update.
The locally generated source.json uses example.invalid placeholders until a maintainer
configures real URLs. Those placeholders cannot install anything. Use the local IPA
route above for now. The [local packaging tools](../NativeApp/Distribution/README.md)
produce a reproducible unsigned IPA, hash manifest and source with the existing
screenshots. The source requests both memory entitlements, but a source declaration
does not grant them. Sideloading tools must re-sign using your own certificate and
preserve the required capabilities. Automatic preservation by either store remains
unverified. This project has not hosted or uploaded a source or IPA.

## Xcode on a Mac

This route needs the project's existing native build dependencies. A clean Mac is not
yet a one-command setup. Ask the build owner for a configured checkout and follow
NativeApp/README.md there. Generating project.yml alone does not build the full app.

1. Have the build owner run NativeApp/build.py with the unsigned option. Open the generated app/Satchel.xcodeproj in that build output.
2. In Xcode Settings, Accounts, add your account yourself. Select the Satchel target and your own team under Signing & Capabilities. The build resolves the team at signing time; if Xcode rejects a capability or identifier, stop and report the error to the maintainer. Do not remove capabilities just to make signing pass.
3. Select the connected iPhone and the Satchel scheme. Set the Run action's build configuration to Release. Enable Developer Mode on the phone if prompted, then build and run when signing succeeds.
4. To renew an Xcode install, build and run again with the same team and bundle identifier before expiry. Keep com.dell.satchel and update the installed app in place.

Apple describes [Personal Teams](https://developer.apple.com/help/account/basics/about-your-developer-account)
and [adding your account in Xcode](https://help.apple.com/xcode/mac/current/en.lproj/devaf282080a.html).

## The seven-day limit

A free account's development install normally lasts seven days. Refresh before that
deadline, including the sideloading store itself. Free accounts also have a three-app
active limit, including the store, and an App ID limit. A paid membership may allow
longer validity, but the actual profile and certificate dates still govern. These
installer limits are described in the [SideStore FAQ](https://docs.sidestore.io/docs/faq).

If Satchel expires, iOS can refuse to open it. Refresh or reinstall over the existing
app using the same account and installer. If the store expired too, repair it using
its computer setup instructions first. Do not uninstall Satchel: uninstalling removes
its local library and saves. Before changing accounts or installers, export saves and
keep your original game dumps. An installer may rewrite identifiers or access groups,
so changing installers is not a verified data migration.

## Satchel's signature warning

Satchel checks readable signing metadata when the library opens or returns to the
foreground. It warns when expiry is within three days or has already passed.

An app warning means the earlier readable deadline from the installed provisioning
profile and the running app's signer certificate is nearing expiry. The date is read
from that installed signature, never estimated as seven days after installation. Refresh
Satchel in your installer, or rebuild and install with Xcode. A prepared-game warning
means that game's signing certificate is nearing expiry. Renew your own signing
identity in Prepare, then prepare the affected game again. Re-import the original
game and update files if they were cleaned up. Refreshing Satchel does not re-sign
prepared games.

The warning is informational. It does not refresh anything or send a scheduled
notification. It cannot warn you after iOS stops opening the app. If neither deadline
is readable, the app produces no warning, so no banner is not a guarantee of validity.
Revocation can also invalidate a signature before its recorded expiry date. After
refresh, reopen Satchel to read the newly installed signature. If the banner remains, compare the installer's actual deadline; refreshing
the app cannot fix an earlier prepared-game certificate deadline. The maintainer's
[refresh acceptance journey](distribution-validation.md) lists the remaining phone checks.

## First launch

First launch opens the home screen. Install and refresh help is in Settings > About.
Import your own prod.keys, then your base XCI, NSP, XCZ or NSZ from Files or iCloud
Drive. Download cloud files fully before importing when possible; the importer also
reports iCloud download progress. Add matching updates from the library. Keep
original dumps outside Satchel; Prepare can clean up imported copies after verification.

Prepare needs your own Apple Development signing identity as well as console keys.
The identity must belong to the team that signed the installed app. You must obtain and
import the identity yourself, entering its passphrase only on the phone. Installing
with a store does not automatically supply this identity to Prepare.

Allow room for the imported files, extracted game and temporary preparation files.
There is no single verified storage requirement for every game. Wait for preparation
and verification to finish before Play. Unsupported games can still fail. Use the
game's save export before removing the app, and pair a controller in iPhone Settings,
Bluetooth if you prefer it to touch controls.
