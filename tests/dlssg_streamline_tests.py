"""Plan 6.1: run the DLSS-G backend against the Streamline fixture in its own
directory, never in a game. ReShade loads the addon with [Backend]
Selected=DLSSG, a fixed 4x multiplier and DLSS preset K (1.6, plan B)."""
import argparse
import ctypes
import datetime
import json
from pathlib import Path
import shutil
import subprocess
import evidence

ap=argparse.ArgumentParser()
for name in ('directory','addon','exe','fixture','reshade','ngx_fixture'):
    ap.add_argument('--'+name.replace('_','-'),type=Path,required=True)
args=ap.parse_args()
d=args.directory/('dlssg-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
d.mkdir(parents=True)
for src,name in {args.addon:'renodx-mfgunlock-native.addon64',args.exe:args.exe.name,
                 args.fixture:'sl.interposer.dll',args.reshade:'dxgi.dll',
                 args.ngx_fixture:'_nvngx.dll'}.items():
    shutil.copy2(src,d/name)
(d/'MFGAdaUnlock.ini').write_text('[Backend]\nSelected=DLSSG\n',encoding='ascii')
(d/'ReShade.ini').write_text('[GENERAL]\nNoDebugInfo=1\n[OVERLAY]\nTutorialProgress=4\n'
    '[RenoDX.MFGUnlock]\nEnabled=1\nForceMultiplier=4\nDlssSuperResolutionPreset=11\nDlssRayReconstructionPreset=5\n',encoding='ascii')
ctypes.windll.kernel32.SetErrorMode(3)
result=subprocess.run([str(d/args.exe.name)],cwd=d,capture_output=True,text=True,timeout=120,creationflags=0x08000000)
log=(d/'ReShade.log').read_text(encoding='utf-8',errors='replace') if (d/'ReShade.log').exists() else ''
verified=('PASS: DLSS-G backend' in result.stdout and 'mfgunlock: DLSS-G backend' in log
    and 'wrapped slDLSSGSetOptions' in log and 'wrapped slDLSSSetOptions' in log)
(d/'result.json').write_text(json.dumps(dict(exit=result.returncode,stdout=result.stdout,stderr=result.stderr,verified=verified),indent=2))
print(result.stdout,end='');print(result.stderr,end='');print('Evidence:',d)
evidence.finish(d,result.returncode==0 and verified)
