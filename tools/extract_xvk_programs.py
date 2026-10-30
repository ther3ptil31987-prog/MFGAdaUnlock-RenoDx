# SPDX-License-Identifier: MIT
"""Extract the XeMFG-on-Vulkan program set (xvk) from MFG Unlocker's release DLL.

1.7 (plan F): dashdogy's MFG Unlocker v1.7.0 (MIT) runs Intel's XeFG and XeLL on
Vulkan through a D3D12-on-Vulkan layer. Every compute program XeFG creates (DXIL)
is replaced by a SPIR-V translation matched by the DXIL's SHA-256. Those SPIR-V
modules are derived from Intel's DXIL and stay under Intel's terms: like the
DXIL programs of the D3D12 path, they are a local build input, never committed.

This tool only reads the DLL file (PE resources and .rdata strings); it never
loads or executes it. It writes, for each program, <dxil-sha256>.spv plus a
manifest (SHA256SUMS.txt) and a programs.json with the binding contract derived
from the SPIR-V itself (descriptor bindings and push-constant size).

Usage: python -I tools/extract_xvk_programs.py <MFGUnlocker.dll> <output-dir>
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

RT_RCDATA = 10
FIRST_RESOURCE_ID = 7400
# The release DLL this tool was written against (v1.7.0, SHA256SUMS.txt).
KNOWN_DLL_SHA256 = "e6b6e6b70a6ed3af839c9647a32fc22660ae6b2d655062c5c072246c91fc10c7"
# The Vulkan host's own input-copy shader (vk_input_copy.comp, dashdogy's MIT
# GLSL): its three variants as his generate_vk_input_shaders.py pins them. They
# are compiled into the DLL's .rdata; taken from there (no glslang 1.2.176.1).
INPUT_COPY_SHA256 = (
    "f6f48f06d01134d9596b117e0c98b4a1a098325ab5b67268eedee8554955b7ca",
    "112a67e0df42b09edab40be77f505d58f5b2d3b357bda64a45f145fb247ce0d3",
    "2e34c018e32f1a5fd819e93e2e46cf31c454312c8d3f53ae22d491d2e1470fbc",
)


def pe_sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("not a PE image")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    magic = struct.unpack_from("<H", data, pe + 24)[0]
    if magic != 0x20B:
        raise SystemExit("not a PE32+ image")
    # Data directory 2: resources.
    rva_resources = struct.unpack_from("<I", data, pe + 24 + 112 + 2 * 8)[0]
    sections = []
    first = pe + 24 + optional
    for i in range(count):
        o = first + 40 * i
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        sections.append((va, max(vsize, rsize), raw))
    return sections, rva_resources


def rva_to_offset(sections, rva):
    for va, size, raw in sections:
        if va <= rva < va + size:
            return raw + rva - va
    raise SystemExit(f"RVA 0x{rva:x} outside every section")


def pe_layout(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    image_base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    sections, _ = pe_sections(data)
    return image_base, sections


def va_to_offset(image_base, sections, va):
    return rva_to_offset(sections, va - image_base)


def rcdata(data):
    sections, root_rva = pe_sections(data)
    base = rva_to_offset(sections, root_rva)

    def entries(offset):
        named, ids = struct.unpack_from("<HH", data, base + offset + 12)
        for i in range(named + ids):
            name, target = struct.unpack_from("<II", data, base + offset + 16 + 8 * i)
            yield name, target

    out = {}
    for type_id, type_target in entries(0):
        if type_id != RT_RCDATA or not type_target & 0x80000000:
            continue
        for res_id, res_target in entries(type_target & 0x7FFFFFFF):
            if res_id & 0x80000000 or not res_target & 0x80000000:
                continue
            for _, leaf in entries(res_target & 0x7FFFFFFF):
                rva, size = struct.unpack_from("<II", data, base + leaf)
                off = rva_to_offset(sections, rva)
                out[res_id] = data[off:off + size]
    return out


# --- SPIR-V reflection: descriptor bindings and push-constant size ----------
OP_NAME, OP_DECORATE, OP_MEMBER_DECORATE = 5, 71, 72
OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_VECTOR, OP_TYPE_MATRIX = 21, 22, 23, 24
OP_TYPE_IMAGE, OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE = 25, 26, 27
OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER = 28, 29, 30, 32
OP_CONSTANT, OP_VARIABLE = 43, 59
DEC_BLOCK, DEC_BUFFER_BLOCK, DEC_BINDING, DEC_SET, DEC_OFFSET = 2, 3, 33, 34, 35
SC_UNIFORM_CONSTANT, SC_UNIFORM, SC_PUSH_CONSTANT, SC_STORAGE_BUFFER = 0, 2, 9, 12


def reflect(spv):
    words = struct.unpack(f"<{len(spv) // 4}I", spv)
    if words[0] != 0x07230203:
        raise SystemExit("not SPIR-V")
    types, decorations, member_offsets, constants, variables = {}, {}, {}, {}, []
    i = 5
    while i < len(words):
        op, length = words[i] & 0xFFFF, words[i] >> 16
        args = words[i + 1:i + length]
        if op == OP_DECORATE:
            decorations.setdefault(args[0], {})[args[1]] = args[2] if len(args) > 2 else True
        elif op == OP_MEMBER_DECORATE and args[2] == DEC_OFFSET:
            member_offsets.setdefault(args[0], {})[args[1]] = args[3]
        elif op in (OP_TYPE_INT, OP_TYPE_FLOAT):
            types[args[0]] = ("scalar", args[1] // 8)
        elif op == OP_TYPE_VECTOR:
            types[args[0]] = ("vector", args[1], args[2])
        elif op == OP_TYPE_MATRIX:
            types[args[0]] = ("matrix", args[1], args[2])
        elif op == OP_TYPE_IMAGE:
            types[args[0]] = ("image", args[2], args[6])  # dim, sampled (1 sampled, 2 storage)
        elif op == OP_TYPE_SAMPLER:
            types[args[0]] = ("sampler",)
        elif op == OP_TYPE_SAMPLED_IMAGE:
            types[args[0]] = ("sampled_image", args[1])
        elif op == OP_TYPE_ARRAY:
            types[args[0]] = ("array", args[1], args[2])
        elif op == OP_TYPE_RUNTIME_ARRAY:
            types[args[0]] = ("runtime_array", args[1])
        elif op == OP_TYPE_STRUCT:
            types[args[0]] = ("struct", list(args[1:]))
        elif op == OP_TYPE_POINTER:
            types[args[0]] = ("pointer", args[1], args[2])
        elif op == OP_CONSTANT:
            constants[args[1]] = args[2]
        elif op == OP_VARIABLE:
            variables.append((args[1], args[0], args[2]))  # id, type, storage class
        i += length

    def size_of(t):
        kind = types[t]
        if kind[0] == "scalar":
            return kind[1]
        if kind[0] == "vector":
            return size_of(kind[1]) * kind[2]
        if kind[0] == "matrix":
            return size_of(kind[1]) * kind[2]
        if kind[0] == "array":
            return size_of(kind[1]) * constants[kind[2]]
        if kind[0] == "struct":
            offsets = member_offsets.get(t, {})
            return max((offsets.get(m, 0) + size_of(mt) for m, mt in enumerate(kind[1])), default=0)
        raise SystemExit(f"no size for {kind}")

    bindings, push = [], 0
    for var, pointer, storage in variables:
        pointee = types[pointer][2]
        if storage == SC_PUSH_CONSTANT:
            push = max(push, size_of(pointee))
            continue
        dec = decorations.get(var, {})
        if DEC_BINDING not in dec:
            continue
        inner = types[pointee]
        while inner[0] in ("array", "runtime_array"):
            inner = types[inner[1]]
        if storage == SC_STORAGE_BUFFER or (storage == SC_UNIFORM and DEC_BUFFER_BLOCK in decorations.get(pointee, {})):
            kind = "STORAGE_BUFFER"
        elif storage == SC_UNIFORM:
            kind = "UNIFORM_BUFFER"
        elif inner[0] == "sampler":
            kind = "SAMPLER"
        elif inner[0] == "image":
            kind = "STORAGE_IMAGE" if inner[2] == 2 else "SAMPLED_IMAGE"
        else:
            raise SystemExit(f"unsupported binding type {inner}")
        bindings.append({"set": dec.get(DEC_SET, 0), "binding": dec[DEC_BINDING], "type": kind})
    bindings.sort(key=lambda b: (b["set"], b["binding"]))
    return bindings, (push + 3) & ~3


def input_copy_modules(data):
    """The pinned input-copy SPIR-V modules, walked instruction by instruction."""
    found = {}
    for m in re.finditer(re.escape(struct.pack("<I", 0x07230203)), data):
        at, end = m.start(), m.start() + 20
        while end + 4 <= len(data) and end - at < 65536:
            count = struct.unpack_from("<I", data, end)[0] >> 16
            if not count:
                break
            end += 4 * count
            digest = hashlib.sha256(data[at:end]).hexdigest()
            if digest in INPUT_COPY_SHA256:
                found[INPUT_COPY_SHA256.index(digest)] = data[at:end]
                break
    if len(found) != len(INPUT_COPY_SHA256):
        raise SystemExit("input-copy shader variants not found")
    return [found[i] for i in range(len(INPUT_COPY_SHA256))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = args.dll.read_bytes()
    if hashlib.sha256(data).hexdigest() != KNOWN_DLL_SHA256:
        raise SystemExit("unexpected DLL (expected MFG Unlocker v1.7.0, SHA256SUMS.txt)")
    resources = rcdata(data)
    spirv = {rid: blob for rid, blob in resources.items() if rid >= FIRST_RESOURCE_ID
             and len(blob) >= 20 and struct.unpack_from("<I", blob)[0] == 0x07230203}
    # The program table (embedded_xvk_programs.generated.h ProgramAsset) is an
    # array of 40-byte entries: key pointer, resource id, SPIR-V hash pointer,
    # size, push-constant bytes, first binding row, binding count. Locate it by
    # its consecutive resource ids and follow the key pointers.
    image_base, sections = pe_layout(data)
    entry = None
    for m in re.finditer(re.escape(struct.pack("<I", FIRST_RESOURCE_ID)), data):
        at = m.start() - 8
        if all(struct.unpack_from("<I", data, at + 8 + 40 * k)[0] == FIRST_RESOURCE_ID + k for k in range(1, 4)):
            entry = at
            break
    if entry is None:
        raise SystemExit("program table not found")

    def string_at(va):
        off = va_to_offset(image_base, sections, va)
        return data[off:off + 64].decode("ascii")

    programs = []
    for k in range(len(spirv)):
        key_va, rid, _, spv_va, size, push_table, first, count = struct.unpack_from("<QIIQIIII", data, entry + 40 * k)
        if rid not in spirv:
            raise SystemExit(f"table entry {k} names resource {rid}, which is not SPIR-V")
        spv = spirv[rid]
        dxil, spv_hash = string_at(key_va), string_at(spv_va)
        if hashlib.sha256(spv).hexdigest() != spv_hash or size != len(spv):
            raise SystemExit(f"resource {rid} does not match its table entry")
        bindings, push = reflect(spv)
        # The table's push-constant size is the root-signature contract; a
        # module that never reads its constants carries no push block at all.
        if count != len(bindings) or (push and push != push_table):
            raise SystemExit(f"resource {rid}: reflection ({len(bindings)} bindings, {push} B) differs from the table "
                             f"({count} bindings, {push_table} B)")
        programs.append({"resource": rid, "dxil_sha256": dxil, "spv_sha256": spv_hash, "size": len(spv),
                         "push_constant_bytes": push_table, "bindings": bindings})
    if len({p["dxil_sha256"] for p in programs}) != len(programs):
        raise SystemExit("ambiguous DXIL keys")
    args.output.mkdir(parents=True, exist_ok=True)
    sums = []
    for p in programs:
        name = p["dxil_sha256"] + ".spv"
        (args.output / name).write_bytes(spirv[p["resource"]])
        sums.append(f"{p['spv_sha256']}  {name}")
    for variant, module in enumerate(input_copy_modules(data)):
        (args.output / f"vk_input_copy_{variant}.spv").write_bytes(module)
    (args.output / "programs.json").write_text(json.dumps(programs, indent=1) + "\n", encoding="utf-8")
    (args.output / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="ascii")
    print(f"extracted {len(programs)} xvk SPIR-V programs")


if __name__ == "__main__":
    main()
