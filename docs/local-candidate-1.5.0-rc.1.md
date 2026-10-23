# MFG Unlock 1.5.0-rc.1 — local candidate

This candidate continues R1.12 (`a065fff`) and the healthy official 1.4.5
DLSS-G implementation. It does not incorporate the discarded low-base-FPS
DLSS-G model experiments. No public release, tag, push or automatic game
installation is performed.

## Changes for players

- Force Borderless preserves an already compatible borderless window. It
  distinguishes game-controlled and addon-controlled windows, and refuses
  ambiguous/exclusive live conversions. DLSS-G and XeMFG have separate saved
  preferences; both default off.
- Safer optional HUD inputs: changing/removing an already admitted texture
  invalidates that generated batch, while the following valid frame can
  recover. Allocation identities live until their frame is retired.
  UI Composition remains off by default and never silently enables itself.
- Intel files and repair panel: exact versions/hashes, loaded paths, optional
  older compatible XeLL, and a separate review/install helper. Original Intel
  downloads are pinned. The helper never closes the game or replaces a loaded
  DLL; it waits for exit, verifies the reviewed files, backs them up, installs
  atomically per file and records a rollback journal.
- Settings save together before becoming active. Failed saves do not publish
  a requested state as applied. Invalid latency/boolean choices use safe
  defaults with an explanation. Backend and latency-output changes still
  require restart.
- Compact ReShade cards, aligned controls, tooltips, quiet diagnostics and a
  link to the maintained GitHub repository. ReShade continues to own Home and
  keyboard/mouse input; no new GUI library or input hook is introduced.
- Debug and Release are distinct builds with symbols. Debug keeps assertions
  enabled while retaining the release STL layout required by the profiled
  Intel model views.

## Installation and rollback

The candidate is not installed automatically. For an existing native ReShade
setup, close the game and back up its addon before replacing only
`renodx-mfgunlock.addon64` from the package root. Keep only one MFG addon. Do not
install the Debug addon alongside Release. Preserve ReShade, RenoDX, all INIs,
Intel runtime files and overlay settings. Restart the process after any addon
replacement; hot reload is not supported.

To return, close the game and restore its prior addon, the supplied R1.12
rollback, or official 1.4.5. No INI reset is required.

For Intel repair, open **Diagnostics → Intel files and repair → Review install
/ repair**. When startup cannot find Intel files, the repair panel also appears
on General. Review the exact target files and hashes in the external helper.
Confirmation may include backing up an unknown version; it never implies that
the unknown version is compatible. Cancel leaves the game files intact.

The helper downloads only original XeFG 1.3.1.78, XeLL 1.3.2.10 and their
notices from the pinned Intel commit. Compatible XeLL 1.3.0.5 is preserved.
It does not install XeSS Super Resolution (`libxess.dll`). Existing root and
`_storage_` copies are reviewed separately; a mapped DLL elsewhere requires
manual repair. This is file admission, not a guarantee of game compatibility.

Transactions are stored below `.mfg-dependency-backups` in the game directory.
After an interrupted install, keep the game closed and use the journal:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\helper\Repair-XeDependencies.ps1 `
  -GameDirectory "C:\Path\Game" -GameExecutable "C:\Path\Game\game.exe" `
  -RollbackJournal "C:\Path\Game\.mfg-dependency-backups\TRANSACTION\journal.json"
```

The helper asks before restoring and refuses to overwrite files changed since
installation. Keep its console/journal when reporting an interrupted repair.

## Boundaries

This candidate does not promise higher FPS, better displayed pacing, lower
physical latency or a universal fix for HUD/model artifacts. The changes add
no reconstruction shader, blur, history buffer, custom GPU wait or replacement
motion vectors.

Native Reflex remains the DLSS-G latency output. The shared marker translator
and 32-frame admission policy are tested, but DLSS-G → XeLL activation remains
unavailable pending a safe GPU-quiescence/mode-switch contract and runtime
validation. XeMFG's existing selectable Reflex/XeLL output is preserved.

Gameplay, HDR alpha correctness, physical latency and three-pair PresentMon
comparisons remain unverified for this candidate. Read the engineering report
and compatibility matrix before treating isolated SDK tests as game evidence.

Intel runtime/program data retains its own terms; MIT applies to the project's
own code. Public distribution of derived runtime programs remains subject to
the unresolved license review. No Intel DLL, OptiScaler, ASI or REFramework is
bundled as a new dependency.
