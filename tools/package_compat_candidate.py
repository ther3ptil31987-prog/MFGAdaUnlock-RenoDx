"""Seal the local RC2 compatibility candidate; no installation or publication."""
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

BASELINE = 'd3053148c1f4eec0a25042cb3277784092168620'
RC1 = 'e4ec4476b6e4263692818dd92d4740d55f3e73f5ba7b40174c0df5819556baff'
OPTIONAL = {'provider_identity', 'engineering_manifest', 'release_1_4_5_manifest'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--previous-package', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    evidence = root / 'build/rc1-compat-triage'
    build = root / 'build/tests'

    def git(*options):
        return subprocess.check_output(['git', *options], cwd=root, text=True).strip()

    if git('status', '--porcelain'):
        raise ValueError('Commit the reviewed source before sealing')
    if args.output.exists() or Path(str(args.output) + '.zip').exists():
        raise ValueError('Output already exists; preserve sealed artifacts')
    subprocess.run(['git', 'merge-base', '--is-ancestor', BASELINE, 'HEAD'], cwd=root, check=True)
    previous = json.loads((args.previous_package / 'manifest.json').read_text())
    if previous['source_commit'] != BASELINE or sha(args.previous_package / 'renodx-mfgunlock.addon64') != RC1:
        raise ValueError('RC1 rollback identity differs')
    assets = {
        'README.md': root / 'docs/local-candidate-1.5.0-rc.2.md',
        'engineering-report.md': root / 'docs/research/compatibility-rc2-2026-10-21.md',
        'rollback/renodx-mfgunlock-RC1.addon64': args.previous_package / 'renodx-mfgunlock.addon64',
        'evidence/analysis.log': evidence / 'analysis-rc2.log',
        'evidence/installed-inventory.json': evidence / '20261021-013856/inventory.json',
        'evidence/diagnostic-settings-restored.json': evidence / 'diagnostic-settings/restoration.json',
    }
    for directory in ('notices', 'helper', 'rollback'):
        for path in (args.previous_package / directory).rglob('*'):
            if path.is_file():
                assets[f'{directory}/{path.relative_to(args.previous_package / directory).as_posix()}'] = path
    binaries, validation, compiler = {}, {}, {}
    for config in ('Release', 'Debug'):
        name = config.lower()
        addon = build / f'xemfg/{config}/renodx-mfgunlock-native.addon64'
        if not addon.read_bytes().startswith(b'MZ') or b'1.5.0-rc.2' not in addon.read_bytes():
            raise ValueError('Candidate identity differs')
        for identifier, source in ((9010, root / 'src/addons/mfgunlock/xemfg/dependency_catalog.json'),
                                   (9011, root / 'tools/Repair-XeDependencies.ps1')):
            if resource_bytes(addon, identifier) != source.read_bytes():
                raise ValueError('Stale embedded dependency resource')
        digest = sha(addon)
        binaries[name] = digest
        cases = ET.parse(evidence / f'ctest-rc2-{name}.xml').getroot().findall('.//testcase')
        failures = [c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None]
        skipped = {c.attrib['name'] for c in cases if c.find('skipped') is not None}
        if failures or skipped != OPTIONAL or len(cases) != 131:
            raise ValueError(f'{config}: incomplete regression: failures={failures}, skips={skipped}, total={len(cases)}')
        text = (evidence / f'LastTest-rc2-{name}.log').read_text(encoding='utf-8')
        receipts = {}
        for case in cases:
            case_name = case.attrib['name']
            section = re.search(r'^\d+/\d+ Testing: ' + re.escape(case_name) +
                                r'\r?\n(.*?)(?=^\d+/\d+ Testing: |\Z)', text, re.M | re.S)
            if not section:
                raise ValueError('Missing complete test log: ' + case_name)
            for line in section[1].splitlines():
                if 'Evidence:' not in line:
                    continue
                receipt = Path(line.split('Evidence:', 1)[1].strip()) / 'result.json'
                if not receipt.is_file():
                    continue
                data = json.loads(receipt.read_text())
                tested = data.get('inputs', {}).get('renodx-mfgunlock-native.addon64')
                if tested is not None and tested != digest:
                    raise ValueError('Stale addon receipt: ' + case_name)
                if data.get('exit', 0) != 0 or data.get('passed', True) is False:
                    raise ValueError('Failed integration: ' + case_name)
                receipts[case_name] = sha(receipt)
                for filename in ('result.json', 'ReShade.log', 'MFGAdaUnlock-XeMFG.log'):
                    if (receipt.parent / filename).is_file():
                        assets[f'evidence/{name}/{case_name}/{filename}'] = receipt.parent / filename
        for route in ('XeLL', 'Reflex'):
            if f'xemfg_native_tail_sleep_{route}' not in receipts:
                raise ValueError('Missing late-sleep regression: ' + route)
        validation[name] = dict(total=len(cases), passed=len(cases) - len(skipped),
                                skipped=sorted(skipped), failed=[], integration_receipts=receipts)
        assets['renodx-mfgunlock.addon64' if config == 'Release' else 'debug/renodx-mfgunlock.addon64'] = addon
        assets[f'symbols/{name}/renodx-mfgunlock-native.pdb'] = build / f'xemfg/{config}/renodx-mfgunlock-native.pdb'
        for suffix in ('xml', 'log'):
            assets[f'evidence/{name}/ctest.{suffix}'] = evidence / f'ctest-rc2-{name}.{suffix}'
        assets[f'evidence/{name}/LastTest.log'] = evidence / f'LastTest-rc2-{name}.log'
        assets[f'evidence/{name}/build.log'] = evidence / f'build-rc2-{name}-final.log'
        compiler[name] = compiler_inputs(build / 'xemfg/mfgunlock_native_xemfg.vcxproj', config)
    # Retain failed/manual explorations distinctly. They are never counted as
    # successful regressions merely because stdout includes an earlier PASS.
    explorations = {}
    for directory in sorted(evidence.iterdir()):
        if not directory.is_dir() or directory.name in ('20261021-013856', 'diagnostic-settings'):
            continue
        for receipt in directory.glob('*/result.json'):
            data = json.loads(receipt.read_text())
            key = receipt.relative_to(evidence).as_posix()
            explorations[key] = {'sha256': sha(receipt), 'exit': data.get('exit'),
                                 'tested_addon': data.get('inputs', {}).get('renodx-mfgunlock-native.addon64')}
            for filename in ('result.json', 'ReShade.log', 'MFGAdaUnlock-XeMFG.log'):
                if (receipt.parent / filename).is_file():
                    assets[f'evidence/explorations/{receipt.parent.relative_to(evidence).as_posix()}/{filename}'] = receipt.parent / filename
        if (directory / 'latest-session.json').is_file():
            assets[f'evidence/rc1-gameplay/{directory.name}.json'] = directory / 'latest-session.json'
    for path in evidence.glob('*.log'):
        assets[f'evidence/history/{path.name}'] = path
    required = {
        'rc2-plague-matching-overlap': lambda d: d.get('exit') == 0 and d.get('inference_verified') and d.get('shutdown_completed') and d.get('overlap_tail_sleep_requested'),
        'rc2-tail-6x-XeLL': lambda d: d.get('exit') == 0 and d.get('tail_sleep_verified') and d.get('latency_output') == 'XeLL',
        'rc2-tail-6x-Reflex': lambda d: d.get('exit') == 0 and d.get('tail_sleep_verified') and d.get('latency_output') == 'Reflex',
        'rc1-tail-final-negative': lambda d: d.get('exit') == 1 and 'tail wait did not reach the following source frame' in d.get('stderr', ''),
    }
    for name, predicate in required.items():
        valid = []
        for receipt in (evidence / name).glob('*/result.json'):
            data = json.loads(receipt.read_text())
            expected = RC1 if name.startswith('rc1-') else binaries['release']
            if predicate(data) and data.get('inputs', {}).get('renodx-mfgunlock-native.addon64') == expected:
                valid.append(receipt)
        if not valid:
            raise ValueError('Missing final-binary evidence: ' + name)
    for name, path in assets.items():
        if not path.is_file():
            raise ValueError('Missing asset: ' + name)
    args.output.mkdir(parents=True)
    for name, path in assets.items():
        destination = args.output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
    sources = {p: sha(root / p) for p in git('ls-files').splitlines() if (root / p).is_file()}
    manifest = dict(identity='MFG Unlock 1.5.0-rc.2 (local)', source_commit=git('rev-parse', 'HEAD'),
                    baseline_commit=BASELINE, branch=git('branch', '--show-current'),
                    addon_sha256=binaries['release'], debug_addon_sha256=binaries['debug'],
                    binaries_sha256=binaries, source_inputs_sha256=sources, compiler_inputs=compiler,
                    precompiled_inputs=json.loads((root / 'build/xemfg-inputs/input-provenance.json').read_text()),
                    validation=validation, explorations=explorations, public_release=False, installed=False,
                    runtime_dlls_bundled=False, ui_composition_default=False, dlssg_xell_active=False,
                    rc2_gameplay_validated=False, paired_presentmon_benchmark=False,
                    intermittent_boot_crashes_resolved=False, black_flag_device_removal_resolved=False,
                    intel_derived_program_distribution_review='unresolved; local artifact only',
                    assets=[dict(name=p.relative_to(args.output).as_posix(), bytes=p.stat().st_size, sha256=sha(p))
                            for p in sorted(args.output.rglob('*')) if p.is_file()])
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    (args.output / 'SHA256SUMS.txt').write_text(''.join(sha(p) + '  ' + p.relative_to(args.output).as_posix() + '\n'
                                                    for p in sorted(args.output.rglob('*')) if p.is_file()), encoding='utf-8')
    archive = Path(str(args.output) + '.zip')
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
        for path in sorted(args.output.rglob('*')):
            if path.is_file():
                zipped.write(path, path.relative_to(args.output).as_posix())
    with zipfile.ZipFile(archive) as zipped:
        if zipped.testzip() or any(n.lower().endswith('.dll') for n in zipped.namelist()):
            raise ValueError('Invalid archive or unexpected bundled DLL')
        for asset in manifest['assets']:
            if hashlib.sha256(zipped.read(asset['name'])).hexdigest() != asset['sha256']:
                raise ValueError('Archive asset mismatch')
    print(json.dumps(dict(output=str(args.output), zip_sha256=sha(archive),
                          binaries=binaries, source_commit=manifest['source_commit']), indent=2))


if __name__ == '__main__':
    main()
