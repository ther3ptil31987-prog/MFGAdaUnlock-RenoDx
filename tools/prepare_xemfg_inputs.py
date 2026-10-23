"""Read data resources from an explicitly supplied, pinned ASI; never load it.

The Intel-derived programs stay in ignored build output. No model weights or
Intel runtime binaries are extracted or distributed by this tool.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ASI_SHA = '161b9e1245a3736dc462ee83f4ad51e6125d8e7e73ce3eedde89d2eed60c69fa'
MANIFEST_SHA = 'd74bd783d29280ed372653701ce9b0395362e381faace1213206e0bdd9bfc9e5'


def resources(data):
    def u16(p): return struct.unpack_from('<H', data, p)[0]
    def u32(p): return struct.unpack_from('<I', data, p)[0]
    if data[:2] != b'MZ': raise ValueError('Not a PE image')
    nt = u32(60)
    if data[nt:nt+4] != b'PE\0\0' or u16(nt+24) != 0x20b:
        raise ValueError('Expected a PE32+ image')
    sections = nt + 24 + u16(nt+20)
    def offset(rva, size=1):
        for i in range(u16(nt+6)):
            p = sections+i*40
            va, rawsize, raw = u32(p+12), u32(p+16), u32(p+20)
            if va <= rva and rva+size <= va+rawsize:
                out = raw+rva-va
                if out+size > len(data): break
                return out
        raise ValueError('Resource outside file-backed section')
    base = offset(u32(nt+24+112+16))
    def entries(relative):
        p = base+relative
        n = u16(p+12)+u16(p+14)
        if n > 4096: raise ValueError('Resource directory too large')
        return [struct.unpack_from('<II', data, p+16+8*i) for i in range(n)]
    result = {}
    for typ, branch in entries(0):
        if typ != 10: continue
        if not branch & 0x80000000: raise ValueError('Invalid type directory')
        for name, langs in entries(branch & 0x7fffffff):
            if name & 0x80000000: continue
            values = entries(langs & 0x7fffffff)
            if len(values) != 1: raise ValueError('Ambiguous resource language')
            _, leaf = values[0]
            if leaf & 0x80000000: raise ValueError('Invalid data entry')
            rva, size = struct.unpack_from('<II', data, base+leaf)
            if size > 16_000_000: raise ValueError('Oversized resource')
            p = offset(rva, size)
            result[name] = data[p:p+size]
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--asi', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    data = args.asi.read_bytes()
    if hashlib.sha256(data).hexdigest() != ASI_SHA:
        raise SystemExit('ASI hash is not the reviewed user-supplied 1.6.0.1010')
    blobs = resources(data)
    by_hash = {hashlib.sha256(v).hexdigest(): v for v in blobs.values()}
    manifest = by_hash[MANIFEST_SHA]
    directory = args.output/'programs'
    directory.mkdir(parents=True, exist_ok=True)
    (directory/'programs.tsv').write_bytes(manifest)
    rows = manifest.decode('ascii').splitlines()
    if len(rows) != 54: raise ValueError('Expected 54 programs')
    for row in rows:
        key, digest = row.split('\t')
        blob = by_hash[digest]
        if not key.isdigit() or blob[:4] != b'DXBC': raise ValueError('Invalid program')
        (directory/(key+'.dxil')).write_bytes(blob)
    shaders = [
        ('splat_motion_aware', '43ccfe670409fd807b604b870674012958fbb4067305a449304ee91ab6154f3f'),
        ('splat_motion_aware_inverted', '6c9bf4b59956c7fec3d764ffe6520e7aec3e6137553b9fca0ba65ecdd7642ce8'),
        ('unet_3ed03daf', '9900c30febf95bfa81b4da1178e5773863116beba4d3f032897da4254b6d8926'),
        ('unet_748b0baf', 'e78063f97ad2c0913412f07589cd17d0f2a6ebeeb6edd70647a69cca6b6a02bf'),
        ('unet_cb391582', '7a29fbf311cd8dee5b8c1cfdadf31ee983092a512cf9c10845a079b457eb62a9'),
        ('unet_aa016078', 'ce847b58086ce7e9d25f7d8b90e989247cd4582226506d9552112f1ef4130643'),
        ('unet_efbe3788', 'efa8da5ef5f90c62b789b0fed8e77e2ce043e558d164cc552ca7fb9127cedb00'),
    ]
    rc, header = [], ['#pragma once', '#include "runtime_payload.h"',
                      'namespace xess_payload::embedded { inline constexpr Asset shaders[] = {']
    for i, (name, digest) in enumerate(shaders):
        blob = by_hash[digest]
        path = args.output/(name+'.dxil')
        path.write_bytes(blob)
        rc.append(f'{7100+i} RCDATA "{path.resolve().as_posix()}"')
        header.append(f'{{{7100+i}, "shaders/{name}.dxil", "{digest}", {len(blob)}}},')
    header.append('}; }')
    (args.output/'embedded_xess_shaders.generated.h').write_text('\n'.join(header), encoding='utf-8')
    (args.output/'embedded_xess_shaders.rc').write_text('\n'.join(rc), encoding='utf-8')
    digest = '2ffae52e7878ae7e374ff953b864204f079c3199dc904b2ce7ecf8b37ee130f7'
    blob = by_hash[digest]
    path = args.output/'xefg_cuda_layers.fatbin'
    path.write_bytes(blob)
    (args.output/'embedded_xefg_cuda.generated.h').write_text(
        '#pragma once\n#include "runtime_payload.h"\nnamespace xefg_cuda::embedded { '
        f'inline constexpr xess_payload::Asset fatbin{{7300,"xefg_cuda_layers.fatbin","{digest}",{len(blob)}}}; }}', encoding='utf-8')
    (args.output/'embedded_xefg_cuda.rc').write_text(
        f'7300 RCDATA "{path.resolve().as_posix()}"', encoding='utf-8')
    (args.output/'input-provenance.json').write_text(json.dumps({
        'source_asi': str(args.asi.resolve()), 'sha256': ASI_SHA,
        'manifest_sha256': MANIFEST_SHA, 'programs': len(rows),
        'method': 'Static PE resource parsing; no code loaded or executed',
        'notice': 'Intel-derived program data: vendor/XEFG-PROGRAMS-NOTICE.txt'
    }, indent=2), encoding='utf-8')
    print('Prepared 54 hash-verified DXIL programs from data resources')


if __name__ == '__main__': main()
