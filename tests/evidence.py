"""Run folders under native-checks/xemfg-checks copy the addon, the test
executable, the Intel and ReShade DLLs and the host into every run (30-50 MB
each, about 4 GB per matrix). A passing run keeps only its logs, INI files and
result.json; a failing run keeps everything for investigation."""
from pathlib import Path

COPIED_BINARIES={'.dll','.exe','.asi','.addon64','.pdb'}


def prune(directory):
    for path in Path(directory).rglob('*'):
        if path.is_file() and path.suffix.lower() in COPIED_BINARIES:
            try:
                path.unlink()
            except OSError:
                pass  # still mapped by a process that has not exited yet


def finish(directory,passed):
    if passed:
        prune(directory)
    raise SystemExit(0 if passed else 1)
