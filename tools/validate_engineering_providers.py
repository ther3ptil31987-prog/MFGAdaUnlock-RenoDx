"""Roundtrip known providers in isolated processes; never create an NGX feature."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--provider', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for index, provider in enumerate(args.provider):
        for mode in ['', '--refined', '--border', '--geometry-v2', '--geometry-v2-border', '--adaptive', '--adaptive-inpaint-temporal']:
            result = subprocess.run([str(args.probe), str(provider)] + ([mode] if mode else []),
                                    capture_output=True, text=True)
            name = mode.removeprefix('--') or 'base'
            output = result.stdout + result.stderr
            (args.output / f'{index}-{name}.txt').write_text(output, encoding='utf-8')
            results.append(dict(provider_sha256=hashlib.sha256(provider.read_bytes()).hexdigest(),
                provider=str(provider.resolve()), mode=name, exit_code=result.returncode,
                complete_image_restore='complete_image_restore=yes' in output,
                blackwell='blackwell_framework_supported=yes' in output,
                blend='validated_warp_blend=applied' in output))
    report = dict(scope='offline mapping/patch/restore only; no runtime feature or dispatch', results=results)
    (args.output / 'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    failures = [x for x in results if x['exit_code'] or not x['complete_image_restore'] or not x['blackwell'] or not x['blend']]
    print(f'{len(results)-len(failures)}/{len(results)} provider roundtrips passed')
    if failures:
        print(json.dumps(failures, indent=2))
        raise SystemExit(1)


if __name__ == '__main__':
    main()
