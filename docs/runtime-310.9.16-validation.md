# DLSS-G 310.9.16 / Streamline 2.14.16 compatibility

Checked on October 16, 2026. This is offline compatibility evidence; in-game
Dynamic MFG, image-quality and performance validation remains pending.

The Dynamic version gate now accepts DLSS-G 310.9.1 and 310.9.16, and
Streamline 2.14.1 and 2.14.16. Other patch versions and nonzero revisions remain
rejected. Loaded-module checks, the provider's Dynamic capability bit, D3D12
requirements and the Star Wars Outlaws exception remain in force.

NVIDIA's [Streamline 2.14.16 release](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.16)
and [DLSS 310.9.16 release](https://github.com/NVIDIA/DLSS/releases/tag/v310.9.16)
supplied the inspected files. The new `sl_dlss_g.h` is byte-identical to the
project's existing header. NVIDIA's [Dynamic MFG guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.16/docs/ProgrammingGuideDLSS_G.md#63-enabling-dynamic-multi-frame-generation)
still requires the runtime capability query and documents driver 595.41 and
D3D12 as prerequisites.

The new provider has PE timestamp `0x6AB69977` and image size `7618560`.
Its `Kernel_BlendCandidatesFused` parameter block grew from 240 to 248 bytes.
The UI flag load moved from parameter offset 220 to 128, and the native
forward/inverse RGB destination triplets exchanged registers. The new exact
profile preserves this layout while retaining the existing quality program.
Normalized Blackwell PTX is 39833 bytes with FNV-1a64 `4e730b28bfb4e935`.
Metadata, full PTX identity, parameter signature and unique descriptor checks
must all pass before this patch can apply. Legacy profiles retain their
original identity and output layout.

Validation used an RTX 4070 SUPER with driver 617.42:

- All 32 tests passed in a separate compatibility snapshot of release 1.4.3.
- The five focused runtime, hook, pacing, Reflex and geometry tests passed in
  the working branch.
- Loaded-module version tests passed against NVIDIA's production DLLs.
- Offline provider patch/restore probes passed on 310.9.1 and 310.9.16,
  including the blend rewrite, framework kernels and temporal fallback.
- The complete release quality shader emitted by C++ passed NVIDIA driver JIT
  compilation on both providers: 48 registers and 288 bytes of shared memory.
  The offline Python blend builder also passed full-kernel JIT for both.
- Streamline 2.14.16 retains one exact frame-count clamp signature, with a
  compiled bound of five generated frames.

Release 1.4.4 is built from the existing 1.4.3 source snapshot and
release flags, preserving its Local Stable geometry and V2 Compatibility
inpaint. The original release directory is preserved. Local build logs and manifests
are under `build/release-1.4.4/`. No NVIDIA DLLs are included in the
package.

Inspected `nvngx_dlssg.dll` SHA-256:
`8d59d49b36410dd6b62fef2503a5871d347415bd85d943fa0848563a95dd50c7`.
