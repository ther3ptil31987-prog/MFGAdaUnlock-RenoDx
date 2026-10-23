"""Collect exact CTest receipts and complete the admitted XeLL runtime matrix."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser()
p.add_argument('--revision',required=True,choices=['R1.9.2','R1.9.3','R1.9.4','R1.9.5'])
p.add_argument('--output',type=Path,required=True)
p.add_argument('--alternate-ll',type=Path,required=True)
p.add_argument('--legacy-streamline',type=Path)
a=p.parse_args()
root=Path(__file__).resolve().parent.parent
build=root/'build/tests'
addon=build/'xemfg/Release/renodx-mfgunlock-native.addon64'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
digest=sha(addon)
cases=ET.parse(root/f'build/ctest-native-{a.revision.lower()}.xml').getroot().findall('.//testcase')
if len(cases)!={'R1.9.2':94,'R1.9.3':95,'R1.9.4':97,'R1.9.5':101}[a.revision] or any(c.find('failure') is not None for c in cases):
    raise ValueError('Complete passing CTest report required')
by_name={c.attrib['name']:c for c in cases}
last_test=(build/'Testing/Temporary/LastTest.log').read_text(encoding='utf-8')
def receipt(name):
    text=by_name[name].findtext('system-out','')
    found=re.findall(r'Evidence: ([^\r\n]+)',text)
    if not found:
        # CTest truncates successful JUnit stdout at 1024 bytes. Its full
        # LastTest log remains authoritative and contains the same run header.
        section=re.search(r'^\d+/\d+ Testing: '+re.escape(name)+r'\r?\n(.*?)(?=^\d+/\d+ Testing: |\Z)',last_test,re.M|re.S)
        if section: found=re.findall(r'Evidence: ([^\r\n]+)',section[1])
    if len(found)!=1: raise ValueError('Missing unambiguous evidence: '+name)
    directory=Path(found[0].strip())
    result=json.loads((directory/'result.json').read_text())
    if result['exit'] or result['inputs']['renodx-mfgunlock-native.addon64']!=digest:
        raise ValueError('Stale or failed evidence: '+name)
    return dict(directory=str(directory),result_sha256=sha(directory/'result.json'))
sdk={f'{route}-{m}x-61aeeab7':receipt(f'xemfg_native_{"reshade" if route=="XeLL" else "reflex"}_{m}x')
     for route in ('XeLL','Reflex') for m in (2,3,4,6)}
compat={layout:receipt('xemfg_native_runtime_'+layout) for layout in ('storage-only','preloaded-storage','mutated-storage','missing')}
streamline={f'{route}-{order}':receipt(f'xemfg_real_streamline_{route}_{order}')
            for route in ('XeLL','Reflex') for order in ('early','late')}
cache={m[1]:m[2] for line in (build/'CMakeCache.txt').read_text().splitlines()
       if (m:=re.match(r'([^:#]+):[^=]+=(.*)',line))}
common=[sys.executable,str(root/'tests/xemfg_native_tests.py'),'--directory',str(build/'native-checks'),
        '--addon',str(addon),'--exe',str(build/'xemfg/Release/xemfg_native_smoke.exe'),
        '--fixture',str(build/'xemfg/Release/sl.interposer.dll'),'--reshade',cache['XEMFG_OFFLINE_RESHADE'],
        '--sdk-fg',cache['XEMFG_OFFLINE_FG']]
for route,ll,layout,label,target in (
    ('XeLL',a.alternate_ll,'adjacent','XeLL-4x-d2030dcd',sdk),
    ('Reflex',a.alternate_ll,'adjacent','Reflex-4x-d2030dcd',sdk),
    ('Reflex',Path(cache['XEMFG_OFFLINE_LL']),'preloaded-storage','preloaded-storage-Reflex',compat)):
    run=subprocess.run(common+['--sdk-ll',str(ll),'--latency-output',route,'--runtime-layout',layout],cwd=root,text=True,capture_output=True,timeout=150)
    if run.returncode: raise ValueError(run.stdout+run.stderr)
    directory=Path(re.findall(r'Evidence: ([^\r\n]+)',run.stdout)[0].strip())
    target[label]=dict(directory=str(directory),result_sha256=sha(directory/'result.json'))
    print(label+': passed',flush=True)
summary=dict(addon_sha256=digest,ctest=dict(total=len(cases),failed=0,
    skipped=sum(c.find('skipped') is not None for c in cases),passed=sum(c.find('skipped') is None for c in cases)),
    sdk_cases=sdk,compat_cases=compat,streamline_cases=streamline,
    capcom_flags=receipt('xemfg_native_capcom_flags'),
    gameplay_validated=False,displayed_pacing_measured=False,physical_latency_measured=False)
if a.revision in ('R1.9.3','R1.9.4','R1.9.5'): summary['retirement_case']=receipt('xemfg_native_retire_source_sleep')
if a.revision in ('R1.9.4','R1.9.5'):
    summary['producer_case']=receipt('xemfg_native_producer_shadow')
    summary['visible_case']=receipt('xemfg_real_streamline_visible')
if a.revision=='R1.9.5':
    summary['legacy_cases']={f'{kind}-{route}':receipt(f'xemfg_{kind}_legacy_{route}')
        for kind in ('native','real_streamline') for route in ('XeLL','Reflex')}
    if not a.legacy_streamline: raise ValueError('Explicit Control Streamline directory required')
    summary['control_streamline_cases']={}
    for route in ('XeLL','Reflex'):
        run=subprocess.run([sys.executable,str(root/'tests/xemfg_streamline_recreate.py'),
            '--directory',str(build/'native-checks'),'--addon',str(addon),
            '--exe',str(build/'xemfg/Release/xemfg_native_smoke.exe'),
            '--reshade',cache['XEMFG_OFFLINE_RESHADE'],'--sdk-fg',cache['XEMFG_OFFLINE_FG'],
            '--sdk-ll',cache['XEMFG_OFFLINE_LL'],'--streamline',str(a.legacy_streamline),
            '--generate','--early-addon','--legacy-factory','--latency-output',route],cwd=root,text=True,capture_output=True,timeout=85)
        if run.returncode: raise ValueError(run.stdout+run.stderr)
        directory=Path(re.findall(r'Evidence: ([^\r\n]+)',run.stdout)[0].strip())
        summary['control_streamline_cases'][route]=dict(directory=str(directory),result_sha256=sha(directory/'result.json'))
        print('Control Streamline '+route+': passed',flush=True)
a.output.mkdir(parents=True,exist_ok=True)
(a.output/'validation-summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print('Validation:',a.output/'validation-summary.json')
