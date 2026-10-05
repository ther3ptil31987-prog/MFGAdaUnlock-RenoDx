"""Report code, shared-memory and register footprints of generated cubins.

This intentionally reads only the local generated headers. It does not copy or
emit NVIDIA payloads, and remains useful on machines without CUDA developer
tools installed.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from build_thin_geometry_variants import fingerprint_elf  # noqa: E402


ARRAY = re.compile(
    r"static const unsigned char (?P<name>[A-Za-z0-9_]+)\[\] = \{"
    r"(?P<body>.*?)\n\};",
    re.DOTALL,
)
BYTE = re.compile(r"0x([0-9a-fA-F]{2})")


def arrays(path: Path) -> dict[str, bytes]:
    text = path.read_text(encoding="ascii")
    return {
        match.group("name"): bytes(
            int(value, 16) for value in BYTE.findall(match.group("body"))
        )
        for match in ARRAY.finditer(text)
    }


def inspect_thin(path: Path) -> list[tuple[str, int, int, int, int]]:
    text = path.read_text(encoding="ascii")
    blobs = arrays(path)
    rows = []
    for name, mechanism in re.findall(
        r"sizeof\((kThinGeometryCubin\d+)\),\s*\1,\s*\"([^\"]+)\"",
        text,
    ):
        blob = blobs[name]
        code, shared, registers = fingerprint_elf(blob)
        rows.append((mechanism, len(blob), code, shared, registers))
    return rows


def inspect_blackwell(path: Path) -> list[tuple[str, int, int, int, int]]:
    text = path.read_text(encoding="ascii")
    blobs = arrays(path)
    rows = []
    pattern = re.compile(
        r"sizeof\((kCubin[A-Za-z0-9_]+)\),\s*\1,\s*\"([^\"]+)\""
    )
    for name, mechanism in pattern.findall(text):
        blob = blobs[name]
        code, shared, registers = fingerprint_elf(blob)
        rows.append((mechanism, len(blob), code, shared, registers))
    return rows


def print_rows(title: str, rows: list[tuple[str, int, int, int, int]]) -> None:
    print(title)
    print("mechanism,cubin_bytes,text_bytes,shared_bytes,registers")
    for row in rows:
        print(",".join(str(value) for value in row))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=ROOT / "src" / "addons" / "mfgunlock",
    )
    args = parser.parse_args()
    thin = args.source_dir / "thin_geometry_cubins.generated.hpp"
    blackwell = args.source_dir / "blackwell_cubins.generated.hpp"
    if not thin.exists() or not blackwell.exists():
        raise SystemExit("generated cubin headers are unavailable")
    print_rows("thin-geometry variants", inspect_thin(thin))
    print()
    print_rows("Blackwell baseline replacements", inspect_blackwell(blackwell))


if __name__ == "__main__":
    main()
