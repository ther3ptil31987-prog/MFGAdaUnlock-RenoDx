# SPDX-License-Identifier: MIT
"""Generate the xvk program table and its RCDATA script.

1.7 (plan F): input is the folder written by tools/extract_xvk_programs.py
(<dxil-sha256>.spv, programs.json, SHA256SUMS.txt). Output, in the build tree:
embedded_xvk_programs.generated.h (xvk::embedded::bindings and ::programs, the
layout of vendor/xvk/xvk_embedded.h), embedded_xvk_programs.rc (one RCDATA
per SPIR-V module, ids from 7400) and vk_input_copy_spv.h (the Vulkan host's
input-copy shader, xess_vulkan::input_copy::shader0..2). Every file is verified against the manifest;
nothing is written to the source tree.

Usage: python -I tools/generate_xvk_programs.py <programs-dir> <output-dir>
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

FIRST_RESOURCE_ID = 7400
INPUT_COPY_SHA256 = (
    "f6f48f06d01134d9596b117e0c98b4a1a098325ab5b67268eedee8554955b7ca",
    "112a67e0df42b09edab40be77f505d58f5b2d3b357bda64a45f145fb247ce0d3",
    "2e34c018e32f1a5fd819e93e2e46cf31c454312c8d3f53ae22d491d2e1470fbc",
)
TYPES = {"STORAGE_BUFFER": 0, "UNIFORM_BUFFER": 1, "SAMPLED_IMAGE": 2, "STORAGE_IMAGE": 3, "SAMPLER": 4}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("programs", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    manifest_text = (args.programs / "SHA256SUMS.txt").read_text(encoding="ascii")
    manifest = {}
    for line in manifest_text.splitlines():
        if line.strip():
            digest, name = line.split(maxsplit=1)
            manifest[name.lstrip("*")] = digest.lower()
    programs = json.loads((args.programs / "programs.json").read_text(encoding="utf-8"))
    rows, header_programs, rc = [], [], ["#include <windows.h>", "LANGUAGE LANG_NEUTRAL, SUBLANG_NEUTRAL"]
    for index, program in enumerate(programs):
        name = program["dxil_sha256"] + ".spv"
        path = args.programs / name
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if manifest.get(name) != digest or digest != program["spv_sha256"] or len(data) != program["size"]:
            raise SystemExit(f"xvk program does not match its manifest: {path}")
        bindings = [(b["binding"], TYPES[b["type"]]) for b in program["bindings"]]
        if any(b.get("set", 0) for b in program["bindings"]) or len({b for b, _ in bindings}) != len(bindings):
            raise SystemExit(f"unsupported binding layout: {path}")
        first = len(rows)
        rows.extend(bindings)
        resource = FIRST_RESOURCE_ID + index
        rc.append(f'{resource} RCDATA "{path.resolve().as_posix()}"')
        header_programs.append(f'    {{"{program["dxil_sha256"]}", {resource}, "{digest}", {len(data)}, '
                               f'{program["push_constant_bytes"]}, {first}, {len(bindings)}}},')
    manifest_hash = hashlib.sha256(manifest_text.encode("ascii")).hexdigest()
    header = ["#pragma once", '#include "xvk_embedded.h"', "namespace xvk::embedded {",
              f'inline constexpr char manifestSha256[] = "{manifest_hash}";',
              "inline constexpr BindingRow bindings[] = {"]
    header += [f"    {{{b}, {t}}}," for b, t in rows] or ["    {0, 0},"]
    header += ["};", "inline constexpr ProgramAsset programs[] = {"] + header_programs + ["};", "}"]
    copy = ["#pragma once", "#include <cstdint>", "namespace xess_vulkan::input_copy {"]
    for variant, pinned in enumerate(INPUT_COPY_SHA256):
        data = (args.programs / f"vk_input_copy_{variant}.spv").read_bytes()
        if hashlib.sha256(data).hexdigest() != pinned or len(data) % 4:
            raise SystemExit(f"input-copy shader {variant} does not match its pinned hash")
        words = struct.unpack(f"<{len(data) // 4}I", data)
        copy.append(f"inline constexpr uint32_t shader{variant}[] = {{")
        copy += ["    " + ",".join(f"0x{w:08x}u" for w in words[at:at + 8]) + "," for at in range(0, len(words), 8)]
        copy.append("};")
    copy.append("}")
    args.output.mkdir(parents=True, exist_ok=True)
    for name, lines in (("embedded_xvk_programs.generated.h", header), ("embedded_xvk_programs.rc", rc),
                        ("vk_input_copy_spv.h", copy)):
        text = "\n".join(lines) + "\n"
        target = args.output / name
        if not target.exists() or target.read_text(encoding="utf-8") != text:
            target.write_text(text, encoding="utf-8")
    print(f"Embedded {len(programs)} verified xvk SPIR-V programs")


if __name__ == "__main__":
    main()
