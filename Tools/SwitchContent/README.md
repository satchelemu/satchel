# Switch package metadata inspector

This Mac-side tool reads real XCI/NSP content using LibHac 0.19.0. It is a
prerequisite for preparing a game build, not an emulator or proof that a game runs.
It opens selected files read-only and does not extract, patch, modify, or upload
them. Keys are supplied by path; key contents and library exception text are
never emitted. No game files or keys are included.

Build with .NET 10, placing output and intermediate files outside this repository:

```sh
dotnet build Tools/SwitchContent/SwitchContent.csproj -c Release \
  --artifacts-path /tmp/satchel-switch-content
```

Then run the built `SwitchContent.dll`:

```sh
dotnet /path/to/SwitchContent.dll self-test
dotnet /path/to/SwitchContent.dll inspect --keys /path/to/prod.keys \
  --base /path/to/base.xci --update /path/to/update.nsp
# For an XCI/NSP that bundles multiple applications, select a verified internal ID:
dotnet /path/to/SwitchContent.dll inspect --keys /path/to/prod.keys \
  --base /path/to/bundle.xci --application-id 01005f4027e94000 \
  --update /path/to/gallery-update.nsp
```

Output is JSON. Success is exit 0; errors are nonzero with a stable `error.code`
and bounded message. `selection` identifies one base Application and its optional
Patch using the decrypted CNMT application ID, patch ID and numeric version.
Without `--application-id`, the base must contain exactly one Application record.
The optional ID must be exactly 16 hexadecimal digits (case-insensitive), nonzero,
and match exactly one internal Application record. Unknown IDs and duplicate
matching records are rejected; the update never chooses the application implicitly.
`base.metadata` still reports all records, while `selection.application_id` identifies
the chosen application. A patch must match that application's own ID and patch ID,
even when it matches a different application in the same package.

An unrelated update, DLC used as an update, unresolved multi-application package,
missing referenced NCA, renamed content ID, or size mismatch stops inspection.
The specific exception is optional distribution DeltaFragment content (type 6)
in Patch metadata: dump tools can omit those NCAs while preserving their CNMT
records. Such omissions are reported as `omitted_delta_fragments`; Program,
Control, Data and other required content checks are unchanged. If a delta NCA is
present, its recorded size is still checked. This follows the
[nxdumptool v1.1.7 release notes](https://github.com/DarkMatterCore/nxdumptool/releases/tag/v1.1.7).
It never matches an update using the outer filename. XCI reads use the secure
partition; normal images and images with a 4096-byte key-area prefix are accepted.
NSZ/XCZ compression is not supported here.

`metadata_integrity_checked` means the metadata NCA section was read with LibHac
`IntegrityCheckLevel.ErrorOnInvalid`. Referenced content IDs and sizes are checked
against package entries. It does **not** mean Nintendo signatures were verified
or every byte of every game NCA was hash-checked; both are explicitly false in
the report. Later extraction must retain its own full content integrity checks.
The CNMT trailing digest is not recomputed: retail CNMT digests are not generally
hashes of the retail CNMT byte sequence. Internal content hashes are not treated
as an authenticity guarantee.

The inspector does not import ticket title keys because it only opens metadata
sections, not the game's rights-managed executable/data sections. Metadata that
requires an unavailable key fails explicitly. The extraction tool handles the
common tickets later. Personalized-ticket execution is not implemented here.

`self-test` uses synthetic binary PFS0/CNMT fixtures: bounded tables, truncation,
integer overflow, name termination, metadata interpretation, a matching update,
an unrelated application, explicit selection from a two-application bundle,
sound-gallery/main-game update mismatch, unknown/malformed/duplicate application
IDs, and invalid update roles/versions. These are not real-game execution tests.

API provenance: LibHac 0.19.0 NuGet XML and reflected installed assembly signatures;
NX2Apple commit `8227f80f9b381c680611303319656c4e0c806a50`,
`dm/tools/nx_extract/Program.cs`; CNMT layout from
<https://switchbrew.org/wiki/CNMT>.
