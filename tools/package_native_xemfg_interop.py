"""Seal the optional REFramework cooperation candidate without game changes."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from write_build_manifest import compiler_inputs


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser()
    for name in ('output','previous-addon','official-addon','intel-sdk','negative-result','reference-result'):
        p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    root=Path(__file__).resolve().parent.parent
    def git(*args):return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
    if git('status','--porcelain'):raise ValueError('Commit reviewed sources before sealing')
    if a.output.exists() or Path(str(a.output)+'.zip').exists():raise ValueError('Never overwrite a sealed candidate')
    build=root/'build/tests';addon=build/'xemfg/Release/renodx-mfgunlock-native.addon64'
    digest=sha(addon)
    if sha(a.previous_addon)!='1db596cf140ede231b35026be4341832eead893cb2d1935108d7e5c533c13374':raise ValueError('Expected sealed R1.11 rollback')
    if sha(a.official_addon)!='9715987f6323a2699dd5b7636412258a6b97a180a4d70c0f79d286714e2972e2':raise ValueError('Expected official 1.4.5 rollback')
    xml=root/'build/ctest-native-r1.12.xml'
    cases=ET.parse(xml).getroot().findall('.//testcase')
    if len(cases)!=124 or any(c.find('failure') is not None or c.find('error') is not None for c in cases):raise ValueError('Full 124-case regression report required')
    skipped=[c.attrib['name'] for c in cases if c.find('skipped') is not None]
    if len(skipped)!=3:raise ValueError('Review changed optional coverage')
    negative=json.loads(a.negative_result.read_text())
    if negative['exit']==0 or 'blocked peer released the Intel context' not in negative['stderr']:raise ValueError('Previous-build negative control missing')
    if negative['inputs']['renodx-mfgunlock-native.addon64']!=sha(a.previous_addon):raise ValueError('Negative control identity')
    reference=json.loads(a.reference_result.read_text())
    if reference['exit'] or reference['timed_out'] or not reference['shutdown_completed'] or reference['inputs']['renodx-mfgunlock-native.addon64']!=sha(a.previous_addon):raise ValueError('Reference shutdown control failed')
    assets={
        'renodx-mfgunlock.addon64':addon,
        'symbols/renodx-mfgunlock-native.pdb':build/'xemfg/Release/renodx-mfgunlock-native.pdb',
        'rollback/renodx-mfgunlock-R1.11.addon64':a.previous_addon,
        'rollback/renodx-mfgunlock-1.4.5.addon64':a.official_addon,
        'engineering-report.md':root/'docs/research/xemfg-reframework-r1.12-2026-10-20.md',
        'evidence/ctest.xml':xml,
        'evidence/ctest.log':root/'build/ctest-native-r1.12.log',
        'evidence/last-test.log':build/'Testing/Temporary/LastTest.log',
        'evidence/previous-build-negative.json':a.negative_result,
        'evidence/previous-build-shutdown.json':a.reference_result,
        'notices/MFGAdaUnlock-MIT.txt':root/'LICENSE',
        'notices/MFG-Unlocker-MIT.txt':root/'src/addons/mfgunlock/xemfg/vendor/LICENSE.dashdogy.txt',
        'notices/PROVENANCE.md':root/'src/addons/mfgunlock/xemfg/vendor/PROVENANCE.md',
        'notices/MinHook-LICENSE.txt':root/'src/addons/mfgunlock/xemfg/vendor/third_party/minhook/LICENSE.txt',
        'notices/Intel-program-data.txt':root/'src/addons/mfgunlock/xemfg/vendor/XEFG-PROGRAMS-NOTICE.txt',
        'notices/Intel-headers-MIT.txt':a.intel_sdk/'inc/MIT.txt',
        'notices/Intel-SDK-LICENSE.txt':a.intel_sdk/'LICENSE.txt',
    }
    receipts={}
    # CTest truncates successful JUnit stdout at 1024 bytes. Match the same
    # case in the complete log so its final Evidence line is preserved.
    complete=(build/'Testing/Temporary/LastTest.log').read_text(encoding='utf-8')
    for case in cases:
        name=case.attrib['name']
        if not name.startswith('xemfg_native_ref_'):continue
        section=re.search(r'^\d+/\d+ Testing: '+re.escape(name)+r'\r?\n(.*?)(?=^\d+/\d+ Testing: |\Z)',complete,re.M|re.S)
        text=section[1] if section else case.findtext('system-out','')
        directories=[Path(line.split('Evidence:',1)[1].strip()) for line in text.splitlines() if 'Evidence:' in line]
        if len(directories)!=1:raise ValueError('Missing final integration receipt '+name)
        directory=directories[0];result=json.loads((directory/'result.json').read_text())
        if result['exit'] or not result['retirement_verified'] or result['inputs']['renodx-mfgunlock-native.addon64']!=digest:raise ValueError('Stale/failed receipt '+name)
        receipts[name]=result
        for file in ('result.json','MFGAdaUnlock-XeMFG.log','ReShade.log'):
            if (directory/file).is_file():assets['evidence/'+name+'/'+file]=directory/file
    if len(receipts)!=8:raise ValueError('Expected eight real-SDK integration scenarios')
    a.output.mkdir(parents=True)
    for name,source in assets.items():
        dest=a.output/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
    (a.output/'README.txt').write_text(
        'MFG Unlock 1.4.5 + XeMFG R1.12 - LOCAL TEST BUILD\n\n'
        'Optional REFramework V1 pre-retire cooperation, module leases and safe rejection/quarantine.\n'
        'Legacy versions remain on the previous path; the known old hook-monitor build receives a targeted warning.\n'
        '121 regression tests passed; 3 optional tests skipped. Synthetic REFramework fixtures with real Intel SDK/ReShade, not gameplay certification.\n'
        'New handoff with actual REFramework/gameplay is UNVERIFIED. No new FPS, pacing or physical latency claim.\n'
        'No REFramework/Intel runtime DLLs, ASI or OptiScaler bundled. UI Composition remains optional.\n'
        'Close the game, back up its addon and replace ONLY renodx-mfgunlock.addon64 in an existing native ReShade setup.\n'
        'Keep ReShade, RenoDX, Intel runtimes and all settings. No third-party overlay settings are changed.\n'
        'If retirement is refused, the old resources are preserved and a process restart is required.\n'
        'Rollback: close the game and restore its backup or an addon from rollback/. Install only one MFG addon.\n'
        'Read engineering-report.md for implementation details and limits.\n',encoding='utf-8')
    summary=dict(total=len(cases),passed=len(cases)-len(skipped),skipped=skipped,failed=0,
        real_sdk_scenarios=list(receipts),real_reframework_gameplay_verified=False,previous_negative_reproduced=True,
        final_suite_normal_driver_ipc=True,reference_shutdown_passed=True)
    (a.output/'evidence/validation-summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    manifest=dict(identity='Native ReShade XeMFG R1.12',source_commit=git('rev-parse','HEAD'),branch=git('branch','--show-current'),
        base_commit=git('rev-parse','2043384'),official_1_4_5_commit='0fe65579d287581107babe267ee3e48c9135bf8a',
        addon_sha256=digest,file_version='1.4.5.27',real_reframework_gameplay_verified=False,
        optiscaler_dependency=False,runtime_dlls_bundled=False,diagnostics_default=False,
        reframework_v1_optional=True,legacy_monitor_fix_included=False,quarantine_requires_restart=True,
        source_inputs_sha256={str(f.relative_to(root)).replace('\\','/'):sha(f) for folder in ('src','tests','tools','docs')
            for f in sorted((root/folder).rglob('*')) if f.is_file() and '__pycache__' not in f.parts},
        compiler_inputs=compiler_inputs(build/'xemfg/mfgunlock_native_xemfg.vcxproj'),
        precompiled_inputs=json.loads((root/'build/xemfg-inputs/input-provenance.json').read_text()),
        assets=[dict(name=f.relative_to(a.output).as_posix(),sha256=sha(f),bytes=f.stat().st_size) for f in sorted(a.output.rglob('*')) if f.is_file()])
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (a.output/'SHA256SUMS.txt').write_text(''.join(sha(f)+'  '+f.relative_to(a.output).as_posix()+'\n' for f in sorted(a.output.rglob('*')) if f.is_file()),encoding='utf-8')
    archive=Path(str(a.output)+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for f in sorted(a.output.rglob('*')):
            if f.is_file():z.write(f,f.relative_to(a.output).as_posix())
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:raise ValueError('Archive CRC check failed')
        for item in manifest['assets']:
            if hashlib.sha256(z.read(item['name'])).hexdigest()!=item['sha256']:raise ValueError('Archive hash mismatch '+item['name'])
    print(json.dumps(dict(output=str(a.output),addon_sha256=digest,archive_sha256=sha(archive),validation=summary),indent=2))


if __name__=='__main__':main()
