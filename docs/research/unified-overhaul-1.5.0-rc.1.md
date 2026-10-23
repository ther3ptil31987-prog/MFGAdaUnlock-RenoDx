# Unified engineering candidate 1.5.0-rc.1

Baseline: `a065fffcdcee694f13c88d9449038d0e595fb91f`, branch
`feature/1.4.5-xemfg-reframework-r1.12`. The unified DLSS-G/XeMFG worktree was
clean before source changes, so no empty checkpoint was created. The dirty RD9
worktree is excluded. R1.12 and official 1.4.5 remain rollback references.

This is a local candidate only. No push, tag, GitHub release, distribution or
automatic game installation is authorized. Existing game files remain intact.

## Architecture and ownership

- The single addon selects DLSS-G or XeMFG once at startup. Changing backend is
  a saved preference requiring restart, never a live ownership transfer.
- DLSS-G retains NVIDIA's proxy/pacer, Streamline options, native Reflex,
  validated 1.4.5 payloads and engineering lifetime policy.
- XeMFG intercepts D3D12 factory creation and Streamline inputs, owns an outer
  DXGI identity and an Intel public proxy/context. ReShade renders on the final
  output. Source Present serialization and per-output input serialization are
  distinct. SDK-owned queues/fences are not replaced by addon timers.
- FrameStore entries belong to a source token/viewport and output epoch.
  Volatile inputs use Intel ONLY_NOW copies recorded on the producer list.
  AddRef alone does not preserve texture contents. Optional HUD inputs may aid
  interpolation with composition off; composition remains an explicit opt-in.
- Retirement drains source calls, invalidates the epoch, performs optional
  REFramework pre-retire cooperation without bridge locks, then destroys Intel
  ownership. Refused handoffs quarantine both proxy and SDK context.
- Window mutations run on the HWND owner thread. The new display controller
  must distinguish DXGI exclusive state from window geometry and preserve
  native borderless and foreign changes.
- Dependency loading admits exact Intel file identities. Dependency inspection
  and repair must run off the render thread. An external helper may apply only
  after review, game exit, identity recheck, backup and journal creation.

## Validation boundaries

Automated fixtures and real SDK isolated rendering are distinct from gameplay.
No game compatibility, image quality, displayed pacing or physical latency
benefit is established by compilation. Gameplay and paired performance results
will be listed explicitly as unverified unless actually collected.

Intel public headers are MIT; Intel runtime/program data has separate terms.
No additional authorization was supplied. This candidate does not resolve the
license review required for public distribution of derived runtime programs.

## Confirmed implementation findings

| Finding and earliest observable failure | Change | Evidence boundary |
|---|---|---|
| Borderless applied styles/position before deciding whether the desired state was already satisfied | Combine DXGI fullscreen observation, border styles, client and monitor bounds, per-monitor DPI coordinates and HWND generation; compatible windows are a no-op | Real Win32 owner-thread fixture; game display transitions still need validation |
| Automatic retries could retake a window after another component changed it | Latch relinquished ownership; only an explicit new request may retry | Repeated requests and foreign-style fixture |
| A removed HUD tag cleared local bits after Intel had already accepted it; that cannot retract the SDK tag | Mark the source batch invalid and recover on a new valid frame | Real SDK/ReShade removed-tag regression; no image-quality conclusion |
| An upstream Streamline tag/constant error could leave earlier tags in an otherwise enabled source batch | Invalidate the matching frame, preserve the original error, log a bounded diagnostic and resume only with a valid new frame; latch the legacy frame association before calling the original API | A frozen pre-fix binary fails the new regression; the corrected binary passes with real XeFG/ReShade |
| Resource addresses alone were insufficient identities for admitted optional textures | Retain canonical IUnknown allocation leases in the bounded FrameStore, including quarantine on failed SDK retirement | Reference-count/epoch tests and existing retirement integration fixtures; content lifetime still uses producer-copy/UNTIL_PRESENT contracts |
| Settings were published before several separate INI writes had all succeeded | Stage the complete requested set and atomically replace the INI; publish only after success | Locked-destination, missing-directory and unchanged-file tests |
| Unknown booleans/latency preferences could silently become an active choice | Validate choices, retain safe defaults, explain invalid settings | ABI/enumeration rejection and configuration checks; no silent composition opt-in |
| Old control/status consumers could interpret a changed structure | Version 5 plus size/version rejection; old control ABI remains rejected | Real SDK harness checks both old request/status rejection and current ABI |
| NDEBUG was unconditional in addon build definitions | Configuration-specific NDEBUG; Debug keeps assertions and symbols | Debug/Release builds; release-layout STL views remain explicitly asserted |

## Dependency system

The trusted catalog fixes Intel commit
`de0fb9c1c510661c571164e1418ceca8101dab69`, XeFG 1.3.1.78 and XeLL 1.3.2.10.
XeLL 1.3.0.5 remains admitted and is not replaced merely for being older.
The helper pins source paths and SHA-256 independently of the catalog supplied
to it, and also checks downloaded DLL versions. The addon hashes files on an
optional background worker and identifies mapped paths separately from disk
copies. Mapped does not prove that a feature uses that module.

No file hashing, downloading or installation runs inside the rendering callback.
Rendering reads cached status. Download and interactive review run in an external
PowerShell process extracted from embedded addon resources. The helper validates
the game PID, creation time and executable, reviews root/_storage_ targets, and
waits for the approved process to exit. A newly running game blocks installation.
Other mapped locations and reparse paths require manual handling.

Before replacing anything it rechecks the reviewed hashes, stages files, verifies
backups and writes a durable journal. Each file replacement is atomic; a failure
attempts rollback of the whole set. Interrupted journals support explicit
rollback. Foreign changes abort rather than being overwritten. The transaction
uses per-file filesystem operations, not a filesystem-wide atomic transaction;
another program racing the same files remains a reason to stop and re-review.
Permission and process/module-inspection failures are reported. The helper never
terminates a game, disables an overlay, updates libxess.dll, or executes a DLL.

Transaction functions are tested using fixture files, including wrong downloads,
locked targets, partial failure, new files, changed files and interrupted state.
The four pinned original files were also downloaded using PowerShell's actual
HTTPS client; all hashes and both DLL versions matched the catalog. No downloaded
DLL was executed or installed. The interactive dialog, wait-for-game-exit flow
and privileged real-game installation remain separate, unverified steps.

## DLSS-G Reflex → XeLL status: partially implemented, activation blocked

The shared marker policy validates sleep-before-markers, paired starts/ends,
alias deduplication, complete source frames, sequence wrap, owner thread and
epoch invalidation. Admission requires 32 consecutive complete frames. XeMFG
uses its shared ordering checks; admission alone never authorizes a wait.

The [Intel XeLL guide](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xell_developer_guide_english.md)
requires GPU activity to be finished before changing sleep mode. A live
warmup-to-active switch over NVIDIA-owned queues has no demonstrated quiescence
contract in this addon. The [DLSS-G guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.16/docs/ProgrammingGuideDLSS_G.md)
also requires its Reflex integration. Consequently this candidate exposes the
unavailable state honestly and preserves native Reflex. It does not suppress
NVIDIA's sleep or forward guessed markers to an extra SDK context. A native
DLSS-G gameplay/SDK experiment and a safe initialization boundary are still
required before enabling that route. XeMFG's existing Reflex/XeLL selection
continues independently.

## Existing game issues: independent assessment

| Issue | Evidence retained from R1.12 work | This candidate's status |
|---|---|---|
| TLOU2 does not interpolate / ReShade sometimes not hooked | Failure may precede this bridge's input/swapchain ownership; no new runtime session is available | Unverified; no claim that changing model or pacing fixes loader activation |
| Alan Wake 2 does not interpolate with FG on | Final output creation alone did not establish complete frame inputs and an active feature | Unverified; use selected viewport/token and SDK admission diagnostics in a new session |
| Capcom Steam Overlay boot/recreation conflict | Optional REFramework pre-retire cooperation and conservative quarantine already exist in R1.12 | Preserved and exercised with synthetic peers plus real SDK; real Steam/REFramework combinations remain unverified |
| Requiem/Onimusha periodic stalls | The user observed improvement after onehoon REFramework; earlier logs corroborated a changed hook-monitor behavior | Correlation retained; no new causal proof or universal stall fix. No waits/fences removed |
| Control/Pragmata input compatibility | Earlier user sessions reported generation after R1.9.4/R1.9.5 fixes | Preserved baseline behavior, not new certification of this RC |

## Performance, compatibility and remaining risks

No new GPU reconstruction pass, readback, history, busy wait, cap or pacer is
introduced. Optional HUD tags add bounded allocation references; their cost is
not a measured FPS improvement. The display controller polls DXGI only while
requested, at most approximately once per second, at a live callback boundary.
Diagnostic counters remain off the main UI; CPU/GPU timing stays opt-in.

Three alternating 30-second A/B pairs, displayed p95/p99, base FPS, physical
latency and SDR/HDR pixel/alpha validation have **not** been collected for this
candidate. Source Present intervals are CPU submissions, not scanout. No FPS,
latency or smoothness benefit is announced. No game installation was replaced
to manufacture runtime evidence.

Unknown Intel pairs fail closed. FP16/scRGB outputs, unsupported APIs and missing
source data retain their existing conservative behavior. UI format/size checks
cannot establish pixel encoding, alpha convention or visual correctness. Older
REFramework builds without the cooperative ABI still use the legacy path;
their private hook monitor is not repaired by this addon. A refused retirement
requires restart and preserves outstanding ownership/resources.

Debug keeps `_DEBUG`, assertions and debug CRT, with `_ITERATOR_DEBUG_LEVEL=0`
for the profiled foreign STL views; their size assertions remain enabled. This
is an ABI requirement, not a claim of binary reproducibility. MSVC compiler and
linker read-set hashes accompany the local package.

## Validation record

The local package records final Debug/Release CTest XML, full LastTest logs,
integration receipt hashes, compiler inputs, symbols, UI previews and rollback
binaries. The final outcome and commit are recorded in its manifest. Initial
failed runs are retained separately rather than overwritten by the final pass.

The initial run used stale V4 assertions in the harness after the deliberate V5
ABI bump. It also exceeded the visible-recreation time limit while still making
progress in its fourth cycle; this is not evidence of an identified shutdown
deadlock. A separate sealed R1.12 visible-recreation control completed all four
cycles and shutdown. A later run was contaminated by a separately running game:
the GPU was already at 98% utilization before our visible test. After the user
closed it, idle utilization fell to 4–5% and the candidate's unchanged 60-second
visible test passed in 15.37 seconds. No driver service or power policy changed.

The window regression initially counted asynchronous desktop position messages
as addon writes. It now checks synchronous style writes, ownership, pending
state, error and actual rectangle. Ten consecutive runs passed. This changes
the attribution of the fixture, not the no-op/foreign-ownership requirement.
Both earlier failed suite receipts are retained. The final candidate must pass
the full regression before sealing; success does not certify gameplay.

### Final automated results

Environment: Windows x64, MSVC 19.44 / VS 2022, RTX 4070 SUPER, driver 617.42.
The following completed with the final product binaries, after freeing the GPU:

| Verification | Actual result | Scope |
|---|---|---|
| Release build and complete CTest suite | 126 passed, 3 optional skipped, 0 failed; 345.31 s | 129 registered cases, including real XeFG/Tensor inference, ReShade, synthetic Streamline inputs, retirement and failure injection |
| Debug build and complete CTest suite | 126 passed, 3 optional skipped, 0 failed; 405.39 s | Assertions and debug CRT enabled, same coverage; foreign STL layout explicitly fixed |
| Canonical Intel XeFG 1.3.1.78 + XeLL 1.3.2.10, XeLL output | Passed real SDK smoke, 4x, three recreation cycles and shutdown | Reflex input translated to XeLL, native waits=0, XeLL waits=48; conflicting/removed HUD tags and upstream failure bypass verified |
| Same canonical pair, Reflex output | Passed real SDK smoke, 4x, three recreation cycles and shutdown | Reflex input/output, native waits=48, XeLL waits=0; no competing XeLL sleep |
| Window controller static analysis | MSVC `/analyze /W4` passed without diagnostics | Native window controller only; no claim of whole-project sanitizer coverage |
| Borderless attribution fixture | Ten consecutive runs passed | Owner-thread Win32 tests; does not certify game fullscreen transitions |
| Official Intel download verification | 4/4 hashes and both DLL versions matched | Actual HTTPS client; no game files changed. Later SDK smokes executed copies in isolated test directories |
| Embedded helper/catalog | Both Debug and Release exactly match source | Resource-only PE mapping; no DllMain during inspection |
| UI previews | 13 final screenshots produced and layout inspected | Synthetic status, large font, navigation, expanded sections, tooltips, multiplier and repair state; not gameplay capture |

The three optional tests are `provider_identity`, `engineering_manifest` and
`release_1_4_5_manifest`; their explicit offline/historical inputs were not
configured for this native candidate run. They are skips, not successful tests.
Each configuration has 50 integration receipts linked to its final addon hash.
The two additional canonical-pair receipts and the failed pre-fix negative
control are retained separately. Neither the occluded SDK fixture's CPU timing
nor its SDK output counts is a displayed-pacing benchmark.

Release addon SHA-256:
`e4ec4476b6e4263692818dd92d4740d55f3e73f5ba7b40174c0df5819556baff`.
Debug addon SHA-256:
`cb92b52016321775a993a46b7b615bbedce8b81cb83127af6ffef5512af84835`.
Both carry product version `1.5.0-rc.1`; PE file version is `1.5.0.1`.

### Phase disposition and local delivery

| Work | Disposition | Remaining boundary |
|---|---|---|
| Baseline/checkpoint and architecture audit | Completed | One unified clean worktree; baseline `a065fffcdcee694f13c88d9449038d0e595fb91f`, dirty RD9 excluded |
| HUD admission/lifetime and input failure handling | Implemented and regression-tested | Pixel/alpha/HDR correctness and visual improvement unverified in games |
| Intel dependency inspection and assisted repair | Implemented; transaction/download tests passed | Interactive real-game install, permissions and game-exit flow still unverified |
| Shared UI, configuration and Borderless | Implemented and fixture-tested | Real multi-monitor/game/overlay display transitions unverified |
| DLSS-G Reflex to XeLL | Partial; activation blocked | No safe demonstrated mode-switch/quiescence contract over NVIDIA-owned queues; native Reflex retained |
| Reliability and compatibility | Confirmed defects corrected; baseline paths exercised | Existing game-specific failures are not declared fixed; see compatibility matrix above |
| Debug/Release artifacts and regression evidence | Completed locally | No public release, game installation or redistribution approval |
| Gameplay/performance certification | Unverified | No three-pair PresentMon comparison, HDR pixel check or physical latency measurement |

Implementation commits, in order: `51a00bb` (Borderless), `082e03b` (HUD
identity), `c9c70c4` (atomic settings/ABI), `fa3e21f` (marker policy),
`ed08446` (retired allocation leases), `3f2cad5` (upstream failure bypass),
`2ba8d58` (reviewed Intel repair and unified controls). The final documentation
and packaging commit, complete source hashes, compiler read sets and artifact
hashes are recorded in `manifest.json`, avoiding a self-referential commit SHA
inside this report.

The local output is `out/1.5.0-rc.1/` and the sibling ZIP in the original
workspace root. Release is `renodx-mfgunlock.addon64`; Debug is kept separately
under `debug/`, with PDBs under `symbols/`. The package contains the changelog,
this report, optional external repair helper, notices, evidence, previews and
sealed R1.12/official 1.4.5 rollback addons. It bundles no Intel runtime DLLs.
Existing game installations and settings were not changed.
