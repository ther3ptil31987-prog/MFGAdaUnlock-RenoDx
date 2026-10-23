"""Seal the local RC4; verify final receipts and preserve the RC3 rollback."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from package_local_candidate import resource_bytes
from write_build_manifest import compiler_inputs

BASELINE='5b7b075bd9c15bf02f98b90a8adc5e82d2b805bb'
ROLLBACK_SHA='0ae438091d8d05fd77a11320bca4c37cb2585cc0c053d813c9ab435e5c981aa5'
OPTIONAL={'provider_identity','engineering_manifest','release_1_4_5_manifest'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--previous-package',type=Path,required=True)
    args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    evidence=root/'build/rc4-validation'
    build=root/'build/tests'

    def git(*arguments):
        return subprocess.check_output(['git',*arguments],cwd=root,text=True).strip()

    if git('status','--porcelain'):
        raise ValueError('Commit reviewed source before sealing')
    subprocess.run(['git','merge-base','--is-ancestor',BASELINE,'HEAD'],cwd=root,check=True)
    if args.output.exists() or Path(str(args.output)+'.zip').exists():
        raise ValueError('Preserve existing sealed artifacts')
    rollback=args.previous_package/'renodx-mfgunlock.addon64'
    previous=json.loads((args.previous_package/'manifest.json').read_text(encoding='utf-8'))
    if previous['source_commit']!=BASELINE or sha(rollback)!=ROLLBACK_SHA:
        raise ValueError('RC3 rollback identity differs')
    assets={
        'README.md':root/'docs/local-candidate-1.5.0-rc.4.md',
        'engineering-report.md':root/'docs/research/xemfg-runtime-blockers-2026-10-21.md',
        'rollback/renodx-mfgunlock-RC3.addon64':rollback,
        'evidence/observed-rc3-blockers.json':evidence/'runtime-inputs/blockers.json',
        'evidence/reference-asi-2.json':evidence/'reference-asi-2.json',
    }
    for directory in ('notices','helper'):
        for file in (args.previous_package/directory).rglob('*'):
            if file.is_file():assets[f'{directory}/{file.relative_to(args.previous_package/directory).as_posix()}']=file
    validation,binaries,compilers={},{},{}
    required=('xemfg_native_zero-resource','xemfg_native_simulation-sleep','xemfg_native_jittered-motion',
              'xemfg_native_retained_legacy','xemfg_native_retained-output',
              'xemfg_native_observer_isolation','xemfg_native_stale_isolation',
              'xemfg_native_tail_sleep_XeLL','xemfg_native_tail_sleep_Reflex')
    for config in ('Release','Debug'):
        name=config.lower()
        addon=build/f'xemfg/{config}/renodx-mfgunlock-native.addon64'
        binary=addon.read_bytes()
        if not binary.startswith(b'MZ') or b'1.5.0-rc.4' not in binary:
            raise ValueError('Wrong binary identity')
        for rid,path in ((9010,root/'src/addons/mfgunlock/xemfg/dependency_catalog.json'),
                         (9011,root/'tools/Repair-XeDependencies.ps1')):
            if resource_bytes(addon,rid)!=path.read_bytes():raise ValueError('Stale embedded helper')
        binaries[name]=sha(addon)
        target='renodx-mfgunlock.addon64' if name=='release' else 'debug/renodx-mfgunlock.addon64'
        assets[target]=addon
        assets[f'symbols/{name}/renodx-mfgunlock-native.pdb']=addon.with_suffix('.pdb')
        cases=ET.parse(evidence/f'ctest-{name}.xml').getroot().findall('.//testcase')
        failed=[c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None]
        skipped={c.attrib['name'] for c in cases if c.find('skipped') is not None}
        if len(cases)!=139 or failed or skipped!=OPTIONAL:
            raise ValueError(f'Incomplete {name} suite: {len(cases)}, {failed}, {skipped}')
        log=(evidence/f'LastTest-{name}.log').read_text(encoding='utf-8')
        receipts={}
        for case in cases:
            test=case.attrib['name']
            section=re.search(r'^\d+/\d+ Testing: '+re.escape(test)+r'\r?\n(.*?)(?=^\d+/\d+ Testing: |\Z)',log,re.M|re.S)
            if not section:raise ValueError('Missing complete test log: '+test)
            for line in section[1].splitlines():
                if 'Evidence:' not in line:continue
                receipt=Path(line.split('Evidence:',1)[1].strip())/'result.json'
                if not receipt.is_file():continue
                data=json.loads(receipt.read_text(encoding='utf-8'))
                tested=data.get('inputs',{}).get('renodx-mfgunlock-native.addon64')
                if tested is not None and tested!=binaries[name]:raise ValueError('Stale binary receipt: '+test)
                if data.get('exit',0)!=0 or data.get('passed',True) is False:raise ValueError('Failed subprocess: '+test)
                receipts[test]=dict(sha256=sha(receipt),tested_addon_sha256=tested)
                for file in ('result.json','compatibility.json','active-compatibility.json','MFGAdaUnlock-XeMFG.log'):
                    path=receipt.parent/file
                    if path.is_file():assets[f'evidence/{name}/{test}/{file}']=path
        if any(test not in receipts for test in required):raise ValueError('Missing required integration receipt')
        validation[name]=dict(total=len(cases),passed=len(cases)-len(skipped),skipped=sorted(skipped),receipts=receipts)
        for file in (f'ctest-{name}.xml',f'ctest-{name}.log',f'LastTest-{name}.log'):
            assets['evidence/'+file]=evidence/file
        compilers[name]=compiler_inputs(build/'xemfg/mfgunlock_native_xemfg.vcxproj',config)

    negative={}
    expected={
        'zero-resource':'UI composition effective state mismatch',
        'simulation-sleep':'simulation-boundary sleep warmup/admission',
        'retained-legacy':'retained native recreation',
    }
    for scenario,reason in expected.items():
        receipts=sorted((evidence/f'negative-rc3-{scenario}').glob('*/result.json'))
        if len(receipts)!=1:raise ValueError('Missing unique RC3 negative control: '+scenario)
        receipt=receipts[0];data=json.loads(receipt.read_text(encoding='utf-8'))
        if (data.get('exit')!=1 or reason not in data.get('stderr','')
                or data.get('inputs',{}).get('renodx-mfgunlock-native.addon64')!=ROLLBACK_SHA):
            raise ValueError('RC3 did not reproduce the expected failure: '+scenario)
        events=[json.loads(line) for line in (receipt.parent/'MFGAdaUnlock-XeMFG.log').read_text(encoding='utf-8').splitlines() if line.strip()]
        if scenario=='zero-resource' and not any(e.get('event')=='native_tag_rejected'
                and e.get('resource_version')==0 and e.get('reason')=='metadata' for e in events):
            raise ValueError('Zero metadata negative has no actual resource rejection')
        if scenario=='simulation-sleep' and not any(e.get('event')=='native_frame_admission'
                and e.get('blocker')=='source_wait_not_completed' for e in events):
            raise ValueError('Missing source-wait rejection in negative control')
        negative[scenario]=dict(expected_failure=True,receipt_sha256=sha(receipt))
        for file in ('result.json','MFGAdaUnlock-XeMFG.log'):
            assets[f'evidence/negative-rc3-{scenario}/{file}']=receipt.parent/file
    peer=evidence/'black-flag-peer/result.json'
    peer_result=json.loads(peer.read_text(encoding='utf-8'))
    if (peer_result.get('exit')!=0 or peer_result.get('peer_inference_verified') is not True
            or peer_result.get('inputs',{}).get('renodx-mfgunlock-native.addon64')!=binaries['release']):
        raise ValueError('RenoDX peer receipt is not from the final Release')
    for file in ('result.json','ReShade.log','MFGAdaUnlock-XeMFG.log'):
        assets['evidence/black-flag-peer/'+file]=peer.parent/file
    # Preserve the jitter specialization failure as diagnosis, never a passing
    # interpolation receipt. The product's negative test verifies passthrough.
    init_receipt=evidence/'init-audit/native-20261021-155946-294277/result.json'
    init_events=evidence/'init-audit/native-20261021-155946-294277/MFGAdaUnlock-XeMFG.log'
    if '35112441167526893749578' not in init_events.read_text(encoding='utf-8'):
        raise ValueError('Missing unsupported shader diagnosis')
    assets['evidence/jitter-initialization-failure/result.json']=init_receipt
    assets['evidence/jitter-initialization-failure/MFGAdaUnlock-XeMFG.log']=init_events
    for name,path in assets.items():
        if not path.is_file():raise ValueError('Missing asset: '+name)
    args.output.mkdir(parents=True)
    for name,path in assets.items():
        target=args.output/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
    manifest=dict(identity='MFG Unlock 1.5.0-rc.4 (local)',source_commit=git('rev-parse','HEAD'),baseline_commit=BASELINE,
        branch=git('branch','--show-current'),addon_sha256=binaries['release'],debug_addon_sha256=binaries['debug'],
        binaries_sha256=binaries,rollback_sha256=ROLLBACK_SHA,validation=validation,negative_controls=negative,
        isolated_black_flag_renodx_peer_verified=True,
        compiler_inputs=compilers,source_inputs_sha256={file:sha(root/file) for file in git('ls-files').splitlines() if (root/file).is_file()},
        precompiled_inputs=json.loads((root/'build/xemfg-inputs/input-provenance.json').read_text()),
        public_release=False,installed=False,runtime_dlls_bundled=False,init_probe_bundled=False,
        gameplay_validated=False,paired_presentmon_benchmark=False,physical_latency_measured=False,
        universal_compatibility_claim=False,ui_composition_default=False,force_borderless=False,
        game_status=dict(mafia='resource-rejection correction verified offline; gameplay pending',
                         plague_tale='simulation-boundary wait verified offline; gameplay pending',
                         death_stranding_2='interpolation unsupported: jittered reconstruction program unavailable',
                         black_flag='device removal unresolved; separate HWND alpha continuity fix verified offline'),
        assets=[dict(name=file.relative_to(args.output).as_posix(),bytes=file.stat().st_size,sha256=sha(file))
                for file in sorted(args.output.rglob('*')) if file.is_file()])
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (args.output/'SHA256SUMS.txt').write_text(''.join(sha(file)+'  '+file.relative_to(args.output).as_posix()+'\n'
        for file in sorted(args.output.rglob('*')) if file.is_file()),encoding='utf-8')
    archive=Path(str(args.output)+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as zipped:
        for file in sorted(args.output.rglob('*')):
            if file.is_file():zipped.write(file,file.relative_to(args.output).as_posix())
    with zipfile.ZipFile(archive) as zipped:
        if zipped.testzip() or any(file.lower().endswith('.dll') for file in zipped.namelist()):raise ValueError('Invalid archive')
        for asset in manifest['assets']:
            if hashlib.sha256(zipped.read(asset['name'])).hexdigest()!=asset['sha256']:raise ValueError('Archive asset differs')
    print(json.dumps(dict(output=str(args.output),source_commit=manifest['source_commit'],addon_sha256=binaries,
                          zip_sha256=sha(archive)),indent=2))


if __name__=='__main__':
    main()
