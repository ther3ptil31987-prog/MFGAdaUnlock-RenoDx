# SPDX-License-Identifier: MIT
# Runs NVIDIA's Streamline_Sample (built by tools/sl_sample.py) with ReShade
# and the native XeMFG addon, then classifies the session log with the same
# triage analyzer used for field sessions. A real Streamline runtime, real
# DLSS-G option flow and Reflex/PCL markers: no synthetic interposer.
import argparse, json, os, pathlib, shutil, subprocess, sys, time
root = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(root.parent / 'tools'))
import xemfg_triage

ap = argparse.ArgumentParser()
ap.add_argument('--bin', type=pathlib.Path, required=True, help='sample _bin folder with StreamlineSample.exe')
ap.add_argument('--addon', type=pathlib.Path, required=True)
ap.add_argument('--reshade', type=pathlib.Path, required=True)
ap.add_argument('--sdk-fg', type=pathlib.Path, required=True)
ap.add_argument('--sdk-ll', type=pathlib.Path, required=True)
ap.add_argument('--seconds', type=int, default=30)
ap.add_argument('--expect', default='interpolating')
a = ap.parse_args()
b = a.bin.resolve()
if not (b / 'StreamlineSample.exe').is_file():
    print('SKIP: sample not built:', b); raise SystemExit(0)
for src, name in ((a.addon, 'renodx-mfgunlock-native.addon64'), (a.reshade, 'dxgi.dll'),
                  (a.sdk_fg, 'libxess_fg.dll'), (a.sdk_ll, 'libxell.dll')):
    shutil.copy2(src, b / name)
(b / 'MFGAdaUnlock.ini').write_text('[Backend]\nSelected=XeMFG\n[XeFG]\nMultiplier=4\n', encoding='ascii')
(b / 'ReShade.ini').write_text('[GENERAL]\nNoDebugInfo=1\n[OVERLAY]\nTutorialProgress=4\n', encoding='ascii')
log = b / 'MFGAdaUnlock-XeMFG.log'
if log.exists():
    log.unlink()
env = dict(os.environ, MFG_SAMPLE_DLSSG='1')
process = subprocess.Popen([str(b / 'StreamlineSample.exe'), '-width', '1280', '-height', '720'], cwd=b, env=env)
try:
    time.sleep(a.seconds)
finally:
    process.kill(); process.wait(30)
lines = log.read_text(encoding='utf-8', errors='replace').splitlines() if log.exists() else []
result = xemfg_triage.analyze(lines)
versions = [json.loads(l) for l in lines if '"native_streamline_version"' in l]
print(json.dumps({'triage': result, 'streamline': versions[-1] if versions else None}, indent=2))
raise SystemExit(0 if result.get('cause') == a.expect else 1)
