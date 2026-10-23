"""Seal RC3 with exact final-binary receipts. No game writes or publication."""
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

BASELINE = '4e0a651b173cd321cac34999c44365f6baf5531d'
ROLLBACK_SHA = '4495326c100fe70972e32376a14bff02600c333c387c898fe07f1e5727c2cc47'
OPTIONAL = {'provider_identity', 'engineering_manifest', 'release_1_4_5_manifest'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--previous-package', type=Path, required=True)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    build, evidence = root / 'build/tests', root / 'build/rc3-validation'

    def git(*options):
        return subprocess.check_output(['git', *options], cwd=root, text=True).strip()

    if git('status', '--porcelain'):
        raise ValueError('Commit reviewed source before sealing')
    if args.output.exists() or Path(str(args.output) + '.zip').exists():
        raise ValueError('Output already exists; preserve sealed artifacts')
    subprocess.run(['git', 'merge-base', '--is-ancestor', BASELINE, 'HEAD'], cwd=root, check=True)
    previous = json.loads((args.previous_package / 'manifest.json').read_text(encoding='utf-8'))
    rollback = args.previous_package / 'renodx-mfgunlock.addon64'
    if previous['source_commit'] != BASELINE or sha(rollback) != ROLLBACK_SHA:
        raise ValueError('RC2 rollback identity differs')
    assets = {
        'README.md': root / 'docs/local-candidate-1.5.0-rc.3.md',
        'engineering-report.md': root / 'docs/research/xemfg-capability-adapters-2026-10-21.md',
        'rollback/renodx-mfgunlock-RC2.addon64': rollback,
    }
    for directory in ('notices', 'helper'):
        for path in (args.previous_package / directory).rglob('*'):
            if path.is_file():
                assets[f'{directory}/{path.relative_to(args.previous_package / directory).as_posix()}'] = path
    # Preserve the failed pre-fix run separately; it is never counted as a
    # passing final validation or silently replaced by a successful repeat.
    for path in (evidence / 'initial-debug').iterdir():
        if path.is_file() and path.suffix in ('.log', '.xml', '.json'):
            assets['evidence/initial-debug/' + path.name] = path
    validation, binaries, compilers = {}, {}, {}
    for config in ('Release', 'Debug'):
        name = config.lower()
        addon = build / f'xemfg/{config}/renodx-mfgunlock-native.addon64'
        if not addon.read_bytes().startswith(b'MZ') or b'1.5.0-rc.3' not in addon.read_bytes():
            raise ValueError('Candidate identity differs')
        for rid, source in ((9010, root / 'src/addons/mfgunlock/xemfg/dependency_catalog.json'),
                            (9011, root / 'tools/Repair-XeDependencies.ps1')):
            if resource_bytes(addon, rid) != source.read_bytes():
                raise ValueError('Stale embedded dependency resource')
        binaries[name] = sha(addon)
        cases = ET.parse(evidence / f'ctest-{name}.xml').getroot().findall('.//testcase')
        failures = [c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None]
        skipped = {c.attrib['name'] for c in cases if c.find('skipped') is not None}
        if failures or skipped != OPTIONAL or len(cases) != 135:
            raise ValueError(f'{name}: incomplete suite: {len(cases)} cases; failures={failures}; skips={skipped}')
        log = (evidence / f'LastTest-{name}.log').read_text(encoding='utf-8')
        receipts = {}
        for case in cases:
            test = case.attrib['name']
            section = re.search(r'^\d+/\d+ Testing: ' + re.escape(test) +
                                r'\r?\n(.*?)(?=^\d+/\d+ Testing: |\Z)', log, re.M | re.S)
            if not section:
                raise ValueError('Missing complete test log: ' + test)
            for line in section[1].splitlines():
                if 'Evidence:' not in line:
                    continue
                receipt = Path(line.split('Evidence:', 1)[1].strip()) / 'result.json'
                if not receipt.is_file():
                    continue
                data = json.loads(receipt.read_text(encoding='utf-8'))
                tested = data.get('inputs', {}).get('renodx-mfgunlock-native.addon64')
                if tested is not None and tested != binaries[name]:
                    raise ValueError('Stale tested binary: ' + test)
                if data.get('exit', 0) != 0 or data.get('passed', True) is False:
                    raise ValueError('Failed process: ' + test)
                receipts[test] = sha(receipt)
                for file in ('result.json', 'compatibility.json', 'ReShade.log', 'MFGAdaUnlock-XeMFG.log'):
                    if (receipt.parent / file).is_file():
                        assets[f'evidence/{name}/{test}/{file}'] = receipt.parent / file
        for test in ('xemfg_native_observer_isolation', 'xemfg_native_stale_isolation',
                     'xemfg_native_tail_sleep_XeLL', 'xemfg_native_tail_sleep_Reflex'):
            if test not in receipts:
                raise ValueError('Missing required integration receipt: ' + test)
        validation[name] = dict(total=len(cases), passed=len(cases) - len(skipped),
                                skipped=sorted(skipped), failed=[], integration_receipts=receipts)
        assets['renodx-mfgunlock.addon64' if name == 'release' else 'debug/renodx-mfgunlock.addon64'] = addon
        assets[f'symbols/{name}/renodx-mfgunlock-native.pdb'] = build / f'xemfg/{config}/renodx-mfgunlock-native.pdb'
        for file in (f'ctest-{name}.xml', f'ctest-{name}.log', f'LastTest-{name}.log', f'build-{name}.log'):
            assets[f'evidence/{file}'] = evidence / file
        compilers[name] = compiler_inputs(build / 'xemfg/mfgunlock_native_xemfg.vcxproj', config)
    negative = sorted((evidence / 'negative-rc2-observer').glob('*/result.json'))
    if len(negative) != 1:
        raise ValueError('Exactly one recorded RC2 negative is required')
    data = json.loads(negative[0].read_text(encoding='utf-8'))
    reproduced = any(reason in data.get('stderr', '') for reason in
                     ("observer options changed the game's FG request", 'observer received replacement frame state'))
    if (data.get('exit') != 1 or not reproduced
            or data.get('inputs', {}).get('renodx-mfgunlock-native.addon64') != ROLLBACK_SHA):
        raise ValueError('The RC2 negative did not reproduce observer interference')
    for path in negative[0].parent.iterdir():
        if path.is_file() and path.suffix in ('.log', '.json'):
            assets['evidence/negative-rc2-observer/' + path.name] = path
    active_support = {}
    for config in ('release', 'debug'):
        candidates = sorted((evidence / f'active-support-{config}').glob('*/result.json'))
        if len(candidates) != 1:
            raise ValueError('Missing active-frame export check: ' + config)
        receipt = candidates[0]
        result = json.loads(receipt.read_text(encoding='utf-8'))
        if (result.get('exit') != 0 or result.get('active_support_verified') is not True
                or result.get('inputs', {}).get('renodx-mfgunlock-native.addon64') != binaries[config]):
            raise ValueError('Active frame metadata was not verified: ' + config)
        active_support[config] = sha(receipt)
        for file in ('result.json', 'compatibility.json', 'active-compatibility.json', 'MFGAdaUnlock-XeMFG.log'):
            assets[f'evidence/active-support-{config}/{file}'] = receipt.parent / file
    for label, path in assets.items():
        if not path.is_file():
            raise ValueError('Missing asset: ' + label)
    args.output.mkdir(parents=True)
    for label, path in assets.items():
        target = args.output / label
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    manifest = dict(identity='MFG Unlock 1.5.0-rc.3 (local)', source_commit=git('rev-parse', 'HEAD'),
                    baseline_commit=BASELINE, branch=git('branch', '--show-current'),
                    binaries_sha256=binaries, addon_sha256=binaries['release'], debug_addon_sha256=binaries['debug'],
                    rollback_sha256=ROLLBACK_SHA, validation=validation, compiler_inputs=compilers,
                    source_inputs_sha256={f: sha(root / f) for f in git('ls-files').splitlines() if (root / f).is_file()},
                    precompiled_inputs=json.loads((root / 'build/xemfg-inputs/input-provenance.json').read_text()),
                    negative_control=dict(receipt_sha256=sha(negative[0]), expected_failure=True),
                    additional_active_support_receipts=active_support,
                    public_release=False, installed=False, runtime_dlls_bundled=False,
                    ui_composition_default=False, force_borderless=False, gameplay_validated=False,
                    paired_presentmon_benchmark=False, universal_compatibility_claim=False,
                    plague_tale_fixed=False, black_flag_device_removal_fixed=False,
                    steam_overlay_gameplay_validated=False, new_nvapi_input_adapter=False,
                    new_fidelityfx_adapter=False, intel_derived_program_distribution_review='local artifact only',
                    assets=[dict(name=f.relative_to(args.output).as_posix(), bytes=f.stat().st_size, sha256=sha(f))
                            for f in sorted(args.output.rglob('*')) if f.is_file()])
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    (args.output / 'SHA256SUMS.txt').write_text(''.join(sha(f) + '  ' + f.relative_to(args.output).as_posix() + '\n'
                                                    for f in sorted(args.output.rglob('*')) if f.is_file()), encoding='utf-8')
    archive = Path(str(args.output) + '.zip')
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
        for f in sorted(args.output.rglob('*')):
            if f.is_file():
                zipped.write(f, f.relative_to(args.output).as_posix())
    with zipfile.ZipFile(archive) as zipped:
        if zipped.testzip() or any(f.lower().endswith('.dll') for f in zipped.namelist()):
            raise ValueError('Corrupt archive or bundled runtime')
        for asset in manifest['assets']:
            if hashlib.sha256(zipped.read(asset['name'])).hexdigest() != asset['sha256']:
                raise ValueError('Archive asset mismatch')
    print(json.dumps(dict(output=str(args.output), source_commit=manifest['source_commit'],
                          zip_sha256=sha(archive), binaries_sha256=binaries), indent=2))


if __name__ == '__main__':
    main()
