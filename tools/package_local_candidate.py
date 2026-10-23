"""Seal local Debug/Release engineering artifacts; never install or publish."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile
import ctypes
from write_build_manifest import compiler_inputs

BASELINE='a065fffcdcee694f13c88d9449038d0e595fb91f'
BASELINE_ADDON='3b547caaa3e72c14e0753f5f4b6bcbfb3d0163788c0b8ab1b89343e7a35ea324'
OFFICIAL_ADDON='9715987f6323a2699dd5b7636412258a6b97a180a4d70c0f79d286714e2972e2'
OPTIONAL={'provider_identity','engineering_manifest','release_1_4_5_manifest'}

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def resource_bytes(path,identifier):
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.LoadLibraryExW.argtypes=[ctypes.c_wchar_p,ctypes.c_void_p,ctypes.c_uint32];k.LoadLibraryExW.restype=ctypes.c_void_p
    k.FindResourceW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p];k.FindResourceW.restype=ctypes.c_void_p
    k.SizeofResource.argtypes=[ctypes.c_void_p,ctypes.c_void_p];k.SizeofResource.restype=ctypes.c_uint32
    k.LoadResource.argtypes=[ctypes.c_void_p,ctypes.c_void_p];k.LoadResource.restype=ctypes.c_void_p
    k.LockResource.argtypes=[ctypes.c_void_p];k.LockResource.restype=ctypes.c_void_p
    k.FreeLibrary.argtypes=[ctypes.c_void_p]
    module=k.LoadLibraryExW(str(path.resolve()),None,0x22) # data/image resource only; no DllMain
    if not module:raise OSError(ctypes.get_last_error(),'Resource-only DLL mapping failed')
    try:
        item=k.FindResourceW(module,identifier,10)
        if not item:raise ValueError('Missing embedded dependency resource')
        size=k.SizeofResource(module,item);pointer=k.LockResource(k.LoadResource(module,item))
        if not pointer or not size:raise ValueError('Invalid embedded resource')
        return ctypes.string_at(pointer,size)
    finally:k.FreeLibrary(module)

def main():
    ap=argparse.ArgumentParser()
    for arg in ('output','baseline-addon','official-addon','intel-sdk'):
        ap.add_argument('--'+arg,type=Path,required=True)
    a=ap.parse_args();root=Path(__file__).resolve().parents[1];build=root/'build/tests'
    def git(*args):return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
    if git('status','--porcelain'):raise ValueError('Commit the reviewed source first; do not seal a dirty tree')
    if a.output.exists() or Path(str(a.output)+'.zip').exists():raise ValueError('Output exists; preserve sealed artifacts')
    if sha(a.baseline_addon)!=BASELINE_ADDON or sha(a.official_addon)!=OFFICIAL_ADDON:raise ValueError('Rollback identity differs')
    subprocess.run(['git','merge-base','--is-ancestor',BASELINE,'HEAD'],cwd=root,check=True)
    assets={
        'README.md':root/'docs/local-candidate-1.5.0-rc.1.md',
        'engineering-report.md':root/'docs/research/unified-overhaul-1.5.0-rc.1.md',
        'helper/Repair-XeDependencies.ps1':root/'tools/Repair-XeDependencies.ps1',
        'helper/dependency_catalog.json':root/'src/addons/mfgunlock/xemfg/dependency_catalog.json',
        'rollback/renodx-mfgunlock-R1.12.addon64':a.baseline_addon,
        'rollback/renodx-mfgunlock-1.4.5.addon64':a.official_addon,
        'notices/MFGAdaUnlock-MIT.txt':root/'LICENSE',
        'notices/dashdogy-MIT.txt':root/'src/addons/mfgunlock/xemfg/vendor/LICENSE.dashdogy.txt',
        'notices/MinHook-LICENSE.txt':root/'src/addons/mfgunlock/xemfg/vendor/third_party/minhook/LICENSE.txt',
        'notices/Intel-derived-programs.txt':root/'src/addons/mfgunlock/xemfg/vendor/XEFG-PROGRAMS-NOTICE.txt',
        'notices/PROVENANCE.md':root/'src/addons/mfgunlock/xemfg/vendor/PROVENANCE.md',
        'notices/Intel-runtime-LICENSE.txt':a.intel_sdk/'LICENSE.txt',
        'notices/Intel-third-party-programs.txt':a.intel_sdk/'third-party-programs.txt',
        'notices/Intel-headers-MIT.txt':a.intel_sdk/'inc/MIT.txt',
        'evidence/static-analysis.log':root/'build/analysis-1.5.0-rc.1.log',
    }
    validation={};binaries={};compilers={}
    for config in ('Release','Debug'):
        name=config.lower();addon=build/f'xemfg/{config}/renodx-mfgunlock-native.addon64'
        data=addon.read_bytes()
        if not data.startswith(b'MZ') or b'1.5.0-rc.1' not in data:raise ValueError('Candidate binary identity differs')
        if resource_bytes(addon,9010)!=(root/'src/addons/mfgunlock/xemfg/dependency_catalog.json').read_bytes():raise ValueError('Stale embedded dependency catalog')
        if resource_bytes(addon,9011)!=(root/'tools/Repair-XeDependencies.ps1').read_bytes():raise ValueError('Stale embedded helper')
        digest=sha(addon);binaries[name]=digest
        xml=root/f'build/ctest-1.5.0-rc.1-{name}.xml'
        cases=ET.parse(xml).getroot().findall('.//testcase')
        failures=[c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None]
        skipped={c.attrib['name'] for c in cases if c.find('skipped') is not None}
        if failures or skipped!=OPTIONAL or len(cases)<129:raise ValueError(f'{config}: incomplete/failed regression: {failures}, skips={skipped}')
        log=root/f'build/LastTest-rc1-{name}.log';complete=log.read_text(encoding='utf-8')
        receipts={}
        for case in cases:
            case_name=case.attrib['name']
            section=re.search(r'^\d+/\d+ Testing: '+re.escape(case_name)+r'\r?\n(.*?)(?=^\d+/\d+ Testing: |\Z)',complete,re.M|re.S)
            text=section[1] if section else case.findtext('system-out','')
            locations=[Path(line.split('Evidence:',1)[1].strip()) for line in text.splitlines() if 'Evidence:' in line]
            if not locations:continue
            if len(locations)!=1:raise ValueError('Ambiguous integration receipt '+case_name)
            receipt=locations[0]/'result.json'
            if not receipt.is_file():continue
            result=json.loads(receipt.read_text(encoding='utf-8'))
            tested=result.get('inputs',{}).get('renodx-mfgunlock-native.addon64')
            if tested is not None and tested!=digest:raise ValueError('Stale candidate integration receipt '+case_name)
            if result.get('exit',0)!=0 or result.get('passed',True) is False:raise ValueError('Failed integration receipt '+case_name)
            receipts[case_name]=sha(receipt)
            assets[f'evidence/{name}/{case_name}/result.json']=receipt
            for filename in ('ReShade.log','MFGAdaUnlock-XeMFG.log'):
                if (receipt.parent/filename).is_file():
                    assets[f'evidence/{name}/{case_name}/{filename}']=receipt.parent/filename
        if not all(f'xemfg_native_ref_{s}' in receipts for s in ('detached','not-tracked','blocked','invalid','exception','late','legacy','ambiguous')):
            raise ValueError('Missing REFramework integration coverage')
        validation[name]=dict(total=len(cases),passed=len(cases)-len(skipped),skipped=sorted(skipped),failed=[],integration_receipts=receipts)
        assets['renodx-mfgunlock.addon64' if config=='Release' else 'debug/renodx-mfgunlock.addon64']=addon
        assets[f'symbols/{name}/renodx-mfgunlock-native.pdb']=build/f'xemfg/{config}/renodx-mfgunlock-native.pdb'
        assets[f'evidence/{name}/ctest.xml']=xml
        assets[f'evidence/{name}/ctest.log']=root/f'build/ctest-1.5.0-rc.1-{name}.log'
        assets[f'evidence/{name}/LastTest.log']=log
        assets[f'evidence/{name}/build.log']=root/f'build/build-1.5.0-rc.1-{name}.log'
        compilers[name]=compiler_inputs(build/'xemfg/mfgunlock_native_xemfg.vcxproj',config)
    for name in ('ctest-1.5.0-rc.1-release-initial.log','ctest-1.5.0-rc.1-release-initial.xml','ctest-1.5.0-rc.1-release-second.log','ctest-1.5.0-rc.1-release-second.xml','ctest-rc1-focused.log','ctest-rc1-final-focused.log','negative-input-result.log','build-rc1-final-ui-release.log','build-rc1-final-ui-debug.log','build-rc1-input-fix-release.log','build-rc1-input-fix-debug.log'):
        if (root/'build'/name).is_file():assets['evidence/history/'+name]=root/'build'/name
    negative_paths=list((root/'build/rc1-negative-input').glob('native-*/result.json'))
    negative=[p for p in negative_paths if 'invalid input did not bypass FG' in json.loads(p.read_text()).get('stderr','')]
    if not negative:raise ValueError('Missing reproduced upstream-input negative control')
    assets['evidence/upstream-input-negative.json']=negative[-1]
    controls=list((root/'build/rc1-baseline-control').glob('real-streamline-*/result.json'))
    controls=[p for p in controls if (lambda r:r.get('exit')==0 and r.get('shutdown_completed') and r['inputs']['renodx-mfgunlock-native.addon64']==BASELINE_ADDON)(json.loads(p.read_text()))]
    if not controls:raise ValueError('Missing sealed-baseline visible control')
    assets['evidence/R1.12-visible-control.json']=controls[-1]
    previews=[f'page-{i}' for i in range(4)]+['large-font','navigation','navigation-compact','expanded','performance','pacing','tooltip','multiplier','dependencies']
    for name in previews:assets['ui-preview/'+name+'.png']=build/'ui-preview'/f'{name}.png'
    download_receipt=root/'build/rc1-download-smoke/result.json'
    downloads=json.loads(download_receipt.read_text(encoding='utf-8-sig'))
    if len(downloads.get('files',[]))!=4 or downloads.get('game_files_modified') or downloads.get('executed_dlls'):raise ValueError('Pinned download verification missing')
    assets['evidence/official-downloads.json']=download_receipt
    catalog=json.loads((root/'src/addons/mfgunlock/xemfg/dependency_catalog.json').read_text())
    runtime_hashes={f['name']:f['sha256'] for f in catalog['files'] if f['name'].endswith('.dll')}
    for route in ('XeLL','Reflex'):
        candidates=list((root/f'build/rc1-canonical-pair-{route}').glob('native-*/result.json'))
        valid=[]
        for receipt in candidates:
            result=json.loads(receipt.read_text(encoding='utf-8'))
            inputs=result.get('inputs',{})
            if (result.get('exit')==0 and result.get('latency_output')==route
                and result.get('runtime_selection_verified') and result.get('hud_filter_verified')
                and inputs.get('renodx-mfgunlock-native.addon64')==binaries['release']
                and all(inputs.get(name)==digest for name,digest in runtime_hashes.items())
                and f'Latency route: Reflex -> {route};' in result.get('stdout','')):
                valid.append(receipt)
        if not valid:raise ValueError('Missing verified canonical Intel pair: '+route)
        receipt=sorted(valid)[-1]
        assets[f'evidence/canonical-pair-{route}/result.json']=receipt
        assets[f'evidence/canonical-pair-{route}/test.log']=root/f'build/canonical-pair-{route}.log'
        for filename in ('ReShade.log','MFGAdaUnlock-XeMFG.log'):
            if (receipt.parent/filename).is_file():
                assets[f'evidence/canonical-pair-{route}/{filename}']=receipt.parent/filename
    for name,source in assets.items():
        if not source.is_file():raise ValueError('Missing package input: '+str(source))
    a.output.mkdir(parents=True)
    for name,source in assets.items():
        destination=a.output/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
    (a.output/'evidence/validation-summary.json').write_text(json.dumps(validation,indent=2),encoding='utf-8')
    files=subprocess.check_output(['git','ls-files'],cwd=root,text=True).splitlines()
    source_inputs={p:sha(root/p) for p in files if (root/p).is_file()}
    manifest=dict(identity='MFG Unlock 1.5.0-rc.1 (local)',source_commit=git('rev-parse','HEAD'),baseline_commit=BASELINE,
        branch=git('branch','--show-current'),addon_sha256=binaries['release'],debug_addon_sha256=binaries['debug'],
        binaries_sha256=binaries,source_inputs_sha256=source_inputs,
        compiler_inputs=compilers,precompiled_inputs=json.loads((root/'build/xemfg-inputs/input-provenance.json').read_text()),
        public_release=False,installed=False,runtime_dlls_bundled=False,optiscaler_required=False,
        ui_composition_default=False,dlssg_xell_active=False,gameplay_validated=False,
        paired_presentmon_benchmark=False,physical_input_latency_measured=False,
        intel_derived_program_distribution_review='unresolved; local artifact only',
        validation=validation,assets=[dict(name=p.relative_to(a.output).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(a.output.rglob('*')) if p.is_file()])
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (a.output/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(a.output).as_posix()+'\n' for p in sorted(a.output.rglob('*')) if p.is_file()),encoding='utf-8')
    archive=Path(str(a.output)+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p in sorted(a.output.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(a.output).as_posix())
    with zipfile.ZipFile(archive) as z:
        if z.testzip():raise ValueError('Archive CRC check failed')
        for asset in manifest['assets']:
            if hashlib.sha256(z.read(asset['name'])).hexdigest()!=asset['sha256']:raise ValueError('Archive asset mismatch')
    print(json.dumps(dict(output=str(a.output),archive_sha256=sha(archive),binaries=binaries,source_commit=manifest['source_commit']),indent=2))

if __name__=='__main__':main()
