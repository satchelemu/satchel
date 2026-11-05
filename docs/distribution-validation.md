# Distribution and refresh acceptance

The Mac checks package an unsigned app, validate source metadata and re-sign a
throwaway copy ad hoc. They do not prove that an installer can keep Satchel's
capabilities or that Apple grants them to another account. No phone actions are part
of the packaging check.

## Expiry implementation and synthetic journey

The home screen checks on appearance and after becoming active. Production reads
the installed bundle's embedded provisioning profile and the CMS signer certificate
in its executable's code signature. The earlier readable expiry controls the banner.
It never adds seven days to installation time, consults the currently imported
Prepare identity for the app deadline, or changes device time. If neither date can
be read, validity remains unknown. Revocation is not predicted.

The banner appears within three days, includes the recorded date and explains how
to refresh without uninstalling. Prepared-game reminders use each game's own signer
certificate. Refreshing the app does not renew a prepared game.

`Tests/Onboarding/run.py` builds and runs synthetic profile/CMS tests on the Mac,
including app executable parsing, earlier certificate/profile selection, a new
seven-day deadline clearing the app reminder and an unchanged game's reminder.
The certificates are disposable synthetic fixtures, not user identities.

Simulator journeys `testSimulatorSignatureExpiry` and
`testSimulatorSignatureExpiryRefresh` use explicit synthetic dates. They check an
upcoming warning, the three-day threshold's outside case, expiry, background/return
and a relaunch with a refreshed seven-day deadline. The refreshed fixture must leave
the library and Settings usable. `testSettingsInstallHelpBothLandscapes` checks the
bundled guide in both landscape rotations. These journeys do not sign an app or
connect to a real installer.

## Orchestrator phone steps (not run by workers)

1. Back up saves. Use a separately prepared test device or the orchestrator's approved
   in-place installation procedure. Do not uninstall the existing app or change its
   bundle identifier to troubleshoot signing.
2. Review the IPA hash against `Satchel.manifest.json`. Install with the user's own
   SideStore or AltStore Classic account, supplying the requested entitlements where
   supported. The unsigned template is not automatically applied by every installer.
   Keep credentials and identity details off logs and reports.
3. Open Satchel in both landscape orientations. In Settings > Device check, require
   increased memory and extended virtual addressing to be present in the running
   signature. If an installer strips either, stop: do not remove the requirement to
   make the install pass. Record the installer version and entitlement presence only.
4. Compare the warning's deadline with the installed profile and app signer expiry
   using private signing tools. The earlier deadline must win. On an installation
   within three days of expiry, the home banner must appear on launch and after
   background/return. Do not change the phone clock to simulate expiry.
5. Refresh Satchel and the store with the same account. Verify the installer's expiry
   advances. Relaunch Satchel and confirm the banner clears when the new app deadline
   is more than three days away. Open the same library entry and verify save data
   remains. If the deadline does not advance, report the installer error verbatim
   after removing account details.
6. With the user's matching Prepare identity already available, prepare an authorized
   small title and launch it. Check that any pre-existing game certificate reminder
   survives an app-only refresh. Renewing that game requires re-Prepare, not another
   app refresh. Store install success alone does not establish game loading.
7. For recovery from a naturally expired install, refresh in place. If the store also
   expired, restore the store with its documented computer setup, then refresh Satchel.
   Verify library and saves before resuming play. Never delete Satchel as recovery.

The single remaining distribution acceptance blocker is installer capability and
refresh validation on a real signed phone. Publication and clean-runner CI activation
are separate maintainer work, outside this local validation.
