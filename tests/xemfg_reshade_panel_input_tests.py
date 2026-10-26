"""Foreground keyboard check confined to an isolated test process/window."""
import argparse,configparser,datetime,hashlib,json,shutil,subprocess
import evidence
from pathlib import Path
ap=argparse.ArgumentParser()
for name in ('directory','addon','exe','reshade','config-reference','sdk-fg','sdk-ll'):
    ap.add_argument('--'+name,type=Path,required=True)
ap.add_argument('--expect-pair',choices=['accepted','rejected'],default='rejected')
args=ap.parse_args()
d=args.directory.resolve()/datetime.datetime.now().strftime('panel-%Y%m%d-%H%M%S-%f')
d.mkdir(parents=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
inputs={args.addon:'renodx-mfgunlock-native.addon64',args.exe:'panel.exe',args.reshade:'dxgi.dll',
    args.sdk_fg:'libxess_fg.dll',args.sdk_ll:'libxell.dll'}
for src,name in inputs.items():shutil.copy2(src,d/name)
# Preserve input + saved docking/font configuration, without carrying preset,
# screenshot, cache or addon directories that could write outside this fixture.
reference=configparser.ConfigParser(interpolation=None,strict=False);reference.optionxform=str
reference.read(args.config_reference,encoding='utf-8-sig')
prefs=configparser.ConfigParser(interpolation=None);prefs.optionxform=str
prefs['GENERAL']={'NoDebugInfo':'1'}
for section in ('INPUT','OVERLAY'):
    if reference.has_section(section):prefs[section]=dict(reference[section])
if not prefs.has_section('INPUT'):prefs['INPUT']={}
if not prefs.has_section('OVERLAY'):prefs['OVERLAY']={}
prefs['INPUT']['KeyOverlay']='36,0,0,0';prefs['INPUT']['InputProcessing']='2'
prefs['OVERLAY']['TutorialProgress']='4'
with (d/'ReShade.ini').open('w',encoding='utf-8') as f:prefs.write(f,space_around_delimiters=False)
(d/'MFGAdaUnlock.ini').write_text('[Backend]\nSelected=XeMFG\n')
r=subprocess.run([str(d/'panel.exe'),*(['--manual-runtime'] if args.expect_pair=='accepted' else [])],cwd=d,capture_output=True,text=True,timeout=90,creationflags=0x08000000)
events=[json.loads(line) for line in (d/'MFGAdaUnlock-XeMFG.log').read_text(encoding='utf-8').splitlines()]
rejected=any(e.get('reason')=='unreviewed_Intel_runtime_pair' for e in events)
passed=r.returncode==0 and 'opens=8 closes=8 commands=9' in r.stdout and rejected==(args.expect_pair=='rejected')
result={'exit':r.returncode,'passed':passed,'stdout':r.stdout,'stderr':r.stderr,
    'inputs':{name:sha(d/name) for name in inputs.values()},'config_reference_sha256':sha(args.config_reference),
    'runtime_pair_rejected':rejected,'expected_pair':args.expect_pair,'manual_runtime_verified':args.expect_pair=='accepted' and 'manual_runtime=1' in r.stdout,'gameplay_validated':False}
(d/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(r.stdout,end='');print(r.stderr,end='');print('Evidence:',d)
evidence.finish(d,passed)
