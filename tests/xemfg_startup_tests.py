"""Isolate real SDK/loader checks in child processes; no files in the game change."""
import argparse
import ctypes
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import evidence

ap=argparse.ArgumentParser()
ap.add_argument('--mode', choices=['loader','early-lifecycle','redirect-lifecycle','optiscaler-present','optiscaler-game-first','optiscaler-live-native'],required=True)
ap.add_argument('--directory',type=Path,required=True)
ap.add_argument('--plugin',type=Path,required=True)
ap.add_argument('--executable',type=Path,required=True)
ap.add_argument('--sdk-fg',type=Path,required=True)
ap.add_argument('--sdk-ll',type=Path,required=True)
ap.add_argument('--host',type=Path,required=True)
ap.add_argument('--foreign-fixture',type=Path)
args=ap.parse_args()
d=args.directory.resolve()/(args.mode+'-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
d.mkdir(parents=True)
shutil.copy2(args.sdk_fg,d/'libxess_fg.dll')
shutil.copy2(args.sdk_ll,d/'libxell.dll')
shutil.copy2(args.executable,d/args.executable.name)
(d/'MFGAdaUnlock.ini').write_text('[Backend]\nSelected=XeMFG\n',encoding='ascii')
env=os.environ.copy()
if args.mode in ('loader','optiscaler-present','optiscaler-game-first','optiscaler-live-native'):
    (d/'plugins').mkdir()
    shutil.copy2(args.plugin,d/'plugins/MFGAdaUnlock.XeMFG.asi')
    shutil.copy2(args.host,d/'dxgi.dll')
    fake=args.host.parent/'fakenvapi.dll'
    if fake.exists(): shutil.copy2(fake,d/'fakenvapi.dll')
    framegen=('Enabled=true\nFGInput=dlssg\nFGOutput=xefg\n' if args.mode!='loader'
              else 'Enabled=false\nFGInput=nofg\nFGOutput=nofg\n')
    (d/'OptiScaler.ini').write_text('[Plugins]\nPath='+str(d/'plugins')+
        '\nLoadAsiPlugins=true\nLoadReshade=false\n[FrameGen]\n'+framegen+
        '[Upscalers]\nDx12Upscaler=dlss\n[Log]\nLogToFile=true\nLogLevel=2\n',encoding='ascii')
else:
    shutil.copy2(args.host,d/'xemfg_host_fixture.dll')
    shutil.copy2(args.plugin,d/'MFGAdaUnlock.XeMFG.asi')
    env['MFGAXE_FIXTURE_EARLY']='1' if args.mode=='early-lifecycle' else '0'
    env['MFGAXE_FIXTURE_REDIRECT']='1' if args.mode=='redirect-lifecycle' else '0'
ctypes.windll.kernel32.SetErrorMode(3) # Inherited by this child; no interactive crash dialog.
extra=[]
if args.mode=='optiscaler-live-native':
    if not args.foreign_fixture: raise ValueError('The native guard requires an unadmitted DLL caller')
    shutil.copy2(args.foreign_fixture,d/'xemfg_foreign_fixture.dll')
    extra=['--foreign-live']
elif args.mode=='optiscaler-game-first': extra=['--game-live']
command=[str(d/args.executable.name)]+extra
r=subprocess.run(command,cwd=d,env=env,
                 capture_output=True,text=True,timeout=40,creationflags=0x08000000)
(d/'result.json').write_text(json.dumps({'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr},indent=2),encoding='utf-8')
print(r.stdout,end='')
print(r.stderr,end='')
print('Evidence:',d)
evidence.finish(d,not r.returncode and 'PASS:' in r.stdout)
