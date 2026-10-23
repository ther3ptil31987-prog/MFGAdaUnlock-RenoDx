MFGAdaUnlock - Native ReShade XeMFG R1.7.4

One ReShade addon: DLSS-G from official 1.4.5, or Intel XeMFG with XeLL,
the validated MFG model port, NVIDIA Tensor route and output pacing.
The Streamline -> XeFG bridge is new MIT code. OptiScaler and an ASI loader
are not required. Intel runtime DLLs remain external runtime requirements.

This is a local candidate, not a new published release. Initial target:
NVIDIA + DirectX 12 + Streamline, starting with Cyberpunk 2077.
It does not implement every OptiScaler feature, input backend or game workaround.

INSTALL (game closed):
  powershell -NoProfile -ExecutionPolicy Bypass -File Install-NativeXeMFG.ps1 -GameDirectory "<game exe directory>" -PackageDirectory "<this directory>"

The installer preserves the game RenoDX addon and presets, saves the current
DXGI loader, MFG addon, companion and affected INIs, installs ReShade as the
DXGI loader, and removes only the backed-up competing MFG files.
The installer expects an existing addon-enabled ReShade installation and the
validated Intel pair. It does not download or replace vendor runtime DLLs.

Intel XeFG: libxess_fg.dll 1.3.1.78, SHA-256
ec5e0c65e075570c6ede72618bb666d0be0c2e10b2ea9762c0fe8cb8e375ab27
XeLL: libxell.dll 1.3.0.5 or validated 1.3.2.10 (manifest lists identities).

In ReShade's MFG Unlock panel:
- General, Quality, Latency and Diagnostics are shared across the two backends.
  Controls are compact and aligned. Hover a control, its label or (?) for help.
  Diagnostics shows readable states; raw codes/counters are in Copy diagnostic
  report. Narrow windows use a page selector and stacked controls.
- Select DLSS-G or XeMFG. Restart the game after changing the backend.
- XeMFG multiplier is 2x-6x. Enable frame generation in the game's menu.
- The panel reports effective FG state, model and CUDA/Tensor acceleration.
  Compute capability, layer/launch counts and fallbacks remain in the copied report.
- Quality > UI Composition is unchecked by default. Off lets XeFG interpolate
  the HUD. Enable it only for HUD interpolation artifacts; composition requires
  HUD-less input and updates the HUD at the real frame rate. The preference is
  saved as [XeFG] UIComposition=0/1 and applied at the next source Present.
  A missing HUD-less input keeps interpolation active without erasing the choice.
- Driver flip metering can be toggled live. A request is committed by the
  presenter at the next generated batch, not in the middle of one. Status
  distinguishes NVAPI availability, queue readiness, driver acceptance,
  independent-flip feedback, refusal and SDK fallback. This is driver feedback,
  not a measurement of monitor scanout or proof of Blackwell display hardware.
- Force Borderless is optional (off by default). Window changes run on the
  HWND owner's message thread. The panel shows the actual style/monitor coverage,
  pending requests and Win32 errors. Unchecking restores the preceding style and
  placement only while they are still owned by this controller. Exclusive
  fullscreen requests are redirected to windowed mode while force is selected.
- Latency input: Auto or Reflex (Streamline). Output: Auto or XeLL. Auto resolves
  to this same implemented route. Both preferences persist in MFGAdaUnlock.ini.
  These selectors do not add native XeLL/NVAPI input adapters. Reflex output with
  XeMFG is not offered: XeFG requires XeLL. Use the DLSS-G backend for native Reflex.
- Active latency labels are based on recent received/successful sleep calls,
  with marker counters and the native Reflex-disable acknowledgement.
- Source FPS limits from Reflex are passed to XeLL; no second Reflex sleep.

Current output formats: SDR RGBA8/BGRA8 and HDR10 RGB10A2.
FP16/scRGB output, Vulkan, DX11, non-Streamline inputs, arbitrary multiadapter/
multiviewport setups and fullscreen-exclusive FG are not implemented.
InvertedDepth and JitteredMotion initialization flags default to 1 and 0;
incompatible actual constants disable FG instead of silently changing inputs.

VALIDATION:
Real ReShade + Intel model inference and Tensor launches passed isolated tests
at 2x, 3x, 4x and 6x, with both tagging APIs, volatile inputs, incomplete-input
bypass, FG disable, resize and context destruction. R1.7.4 full suite: 66 passed,
3 optional checks skipped. Synthetic/occluded windows do not validate display
cadence, real-game input conventions, HDR appearance, FPS or physical latency.
The X1.3 gameplay approval belongs to the previous OptiScaler transport.
R1 opened Cyberpunk but did not activate generation: admission incorrectly
required RenderSubmitEnd before Present. R1.1 allows these phases to overlap,
as XeLL permits, and keeps forwarding the game's actual END timestamp.
All four native inference tests passed again with END after Present, while
still checking incomplete-input bypass. A subsequent Cyberpunk session also
confirmed the late END markers, 2x/3x/4x/6x SDK output, Tensor execution and
accepted driver metering. The user also reports image and responsiveness
remain good versus X1.3. This is initial functional and subjective approval;
controlled performance, blind image equivalence, monitor cadence and physical
latency have not been measured.

R1.2 adds the controls above and preserves the R1.1 inference/input path. Its
isolated real-SDK tests cover the explicit latency preferences, unsupported
output rejection, live flip requests and window apply/restore. A separate
Win32 test covers cross-thread requests, reentrant resize/retirement, hidden
window visibility and foreign ownership. R1.1 gameplay approval does not by
itself validate the new window controls in every game.

R1.3 redesigns the player-facing panels and corrects marker locking in the
native latency bridge. Late markers from another frame/thread can arrive
while XeLL sleeps, and identical sleep settings are no longer sent every
frame. Context ownership and reset epochs protect outstanding sleep calls.
These are tested code changes, not a measured input-to-photon improvement.
The source comparison and remaining differences versus dashdogy/OptiScaler
are in interface-and-latency.md. The package includes synthetic-status PNG
renders of the actual XeMFG widgets; they are layout evidence only.

R1.4 compacts the shared DLSS-G/XeMFG style and overview, adds contextual help,
fixes clipped latency selector labels and removes live counter lists from XeMFG
Diagnostics. The ReShade font and other addons' styles are preserved. Frame
generation, CUDA execution, XeLL scheduling and metering behavior are unchanged
from R1.3. On NVIDIA, the metering used here and by dashdogy is NVIDIA driver
flip metering; XeFG's own pacing remains the fallback. See compact-interface.md.

R1.5 removes the unconditional UI-composition enable and adds the opt-in control
above. Requested and SDK-acknowledged states are distinct; failed changes bypass
that generated batch and retry instead of announcing success. Existing APIs for
native control settings/status now use version 2, rejecting older layout padding.
Pacing was audited against the pinned MIT source, saved game logs and numerical
tests. No pacing formulas, GPU waits or driver deadlines were changed. The bridge
log no longer labels all active output as metered. The detailed findings and
measurement limits are in ui-composition-and-pacing.md.

ROLLBACK (game closed):
  powershell -NoProfile -ExecutionPolicy Bypass -File Restore-MFGBackup.ps1 -BackupDirectory "<backup printed by installer>"

This restores the exact preceding setup, including working X1.3 + OptiScaler
when that was the installed version. rollback/ also contains official 1.4.5.
Do not hot reload: model, provider, hooks and callbacks have process lifetime.

No GPL OptiScaler implementation is included. Preserve notices/ when sharing.

R1.6 audits the frame pipeline and removes synchronous file I/O from logging
producers and per-frame map nodes. It validates XeLL START/END pairs, uses native
D3D12 devices for Intel contexts and retires viewport selection with its output.
Diagnostics > Performance capture (advanced) adds a bounded CPU timing window,
disabled by default. It does not measure displayed FPS, GPU execution or physical
input latency. CPU profiling adds work; leave it off for performance benchmarks.
R1.6 reports are ENGINEERING_AUDIT.md, PACING_ANALYSIS.md, BENCHMARK_PLAN.md
and CHANGELOG.md under history/r1.6/ in the current package.
No pacing formulas, waits, quality/model or image inputs changed in R1.6.
Controlled gameplay/display cadence/physical-latency gains are NOT TESTED.
SleepMode GPU-idle coordination and concurrent lifecycle remain open audit items.
The historical R1.6 package included R1.5 for an identical-runtime A/B.

R1.7 connects a manual ReShade runtime to Intel's final output when ReShade
cannot automatically hook the SDK's native private queue. Effects and overlay
run after interpolation; Home remains the ReShade key. Resize/retirement are
covered by real-SDK tests; actual Home/focus behavior still needs gameplay check.
Quality > HUD input safety validates optional textures without enabling UI
Composition. Correct inputs are preserved; this is not a new HUD interpolation
model or a guarantee that every crosshair artifact disappears.
General > Advanced pacing comparison > Use Intel SDK timing (test) is OFF by
default. ON preserves Intel's original deadlines and disables driver metering
at the next generated group; OFF restores the preceding driver/adaptive policy.
No new wait, FPS cap, multiplier reduction or controller is added. This is an
experimental A/B option; no displayed-cadence gain is claimed yet. See
pacing-overlay-hud.md for sources, tests, limitations and the validation protocol.
Native control ABI is version 3. The current package includes R1.6 rollback;
historical R1.6 engineering reports are under history/r1.6/.

R1.7.1 recovers complete overlay-shortcut taps that occur between output
updates. It observes this game's existing window message thread and uses
ReShade's configured binding and public open_overlay API. Native requests
consume their own taps, avoiding double toggle. Text/widget capture, disabled
bindings, focus loss, resize and bounded-queue overflow are handled. No global
hotkey or input-device registration is added. See overlay-input-fix.md.
Real-SDK tests now require normal and short Home taps, including FG-off/resize.
The R1.7 package and its user-reported Home failure remain a separate baseline.
Gameplay Home/focus confirmation remains pending; this is not a pacing or
HUD-quality change. Callback GUI bookkeeping can add CPU work and needs the
same controlled benchmark before a performance claim. R1.7 and R1.6 rollbacks
are included; retain the exact installation backup for the preceding setup.

R1.7.2 makes the Multiplier/Backend/Renderer readouts passive, in both wide
and compact layouts, for DLSS-G and XeMFG. Only actual settings receive
keyboard/gamepad navigation; the readout containers cannot take mouse focus.
Wide/compact tests navigate into actual controls and exclude the readouts.
This fixes the blue navigation highlight shown in the user screenshot; it
does not establish that all intermittent Home failures have the same cause.
See passive-card-focus.md. R1.7.1 is also included for exact rollback.

R1.7.3 extends lost-tap recovery to children of the render window on its own
message thread. A real-ReShade/Intel test reproduced one missing transition
in sealed R1.7.2 and now requires held and complete child taps without double
toggle. An unrelated top-level window remains excluded. Shortcut-specific
receipts (64 bounded records per runtime) distinguish missing delivery,
modifier mismatch, native handling, widget/text capture and focus loss. No
typed text or other-key history is recorded. The user confirmed passive cards
fixed in R1.7.2, but Home still sometimes failed. Child routing is a proven
coverage defect; it is not yet established as the cause of every game failure.
See overlay-routing.md. R1.7.2 is included for rollback. Pacing, models,
latency routing, HUD and input-device registrations are unchanged.

R1.7.4 adds a Windows-managed overlay shortcut while the game window is in
the foreground. It uses ReShade's configured exact key/modifier binding.
The OS RegisterHotKey API is system-wide; this addon registers only during
the game window's active lifetime and unregisters on deactivation, text editing,
disabled/rebound shortcuts or retirement. It does not install
a global keyboard observer or register raw-input devices. A conflicting or
unsupported binding retains normal ReShade/message recovery. F12 is reserved.
This route handles presses that do not reach the ordinary keyboard messages.
While registered it owns Home exactly once, including when ReShade captures
game commands; closing the overlay releases them normally. Current testing
uses real ReShade, an owned foreground window and a fixture that discards its
legacy Home messages. Cyberpunk verification is still pending. The corrected
user report is that animation continues while commands stop, not a render hang.
See window-hotkey.md; the package includes R1.7.3 rollback.
