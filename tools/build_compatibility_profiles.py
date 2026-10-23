"""Derive reviewable instruction/content identities from local PE files.

Emits metadata and small patch sites, never CUDA payloads. Inputs must be
explicitly selected. dumpbin verifies instruction boundaries in addition to the
whole executable-section fingerprint. Generated profiles require review.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path
from build_thin_geometry_variants import fnv1a64, pe_sections, iter_fatbins, iter_entries, fingerprint_elf

def inspect(path, dumpbin):
    data, unused = pe_sections(path)
    pe = struct.unpack_from('<I', data, 60)[0]
    count, stamp, optional_size = (struct.unpack_from('<H',data,pe+6)[0],struct.unpack_from('<I',data,pe+8)[0],struct.unpack_from('<H',data,pe+20)[0])
    preferred = struct.unpack_from('<Q', data, pe + 24 + 24)[0]
    image_size = struct.unpack_from('<I', data, pe + 24 + 56)[0]
    image = bytearray(image_size)
    sections = []
    code = bytearray()
    for index in range(count):
        at = pe + 24 + optional_size + 40 * index
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII',data,at+8)
        flags = struct.unpack_from('<I',data,at+36)[0]
        image[rva:rva+raw_size] = data[raw:raw+raw_size]
        sections.append((rva, virtual_size, flags))
        if flags & 0x20000000: code += image[rva:rva+virtual_size]
    output = subprocess.check_output([str(dumpbin), '/DISASM:NOBYTES', str(path)], text=True, errors='replace')
    instructions = {}
    for line in output.splitlines():
        match = re.match(r'^\s*([0-9A-Fa-f]{16}):\s*(.*)', line)
        if match: instructions[int(match[1],16)-preferred] = match[2].strip().lower()
    sites = []
    for rva, n, flags in sections:
        if not flags & 0x20000000: continue
        for at in range(rva,rva+n-7):
            insn = instructions.get(at,'')
            if not insn.startswith('cmp'): continue
            offset = 1 if image[at:at+5] == b'\x3d\xb0\x01\0\0' else 2 if image[at] == 0x81 and 0xf8 <= image[at+1] <= 0xff and image[at+2:at+6] == b'\xb0\x01\0\0' else 0
            if offset:
                before = bytes(image[at:at+offset+4]); after = bytearray(before);after[offset]=0x90
                sites.append(dict(kind='arch',rva=at,before=before.hex(),after=after.hex()))
    for rva,n,flags in sections:
        if not flags & 0x20000000:continue
        for at in range(rva,rva+n-10):
            before=bytes(image[at:at+10])
            if instructions.get(at,'').startswith('mov') and before[0]==0xba and 1<=before[1]<=8 and before[2:5]==b'\0\0\0' and before[5:]==bytes.fromhex('3bca0f42d1'):
                sites.append(dict(kind='ceiling',rva=at,before=before.hex(),after=(before[:9]+b'\xd2').hex()))
    marker = image.find(b'FG1 DLL has been detected')
    field = value = None
    if marker >= 0:
        for rva,n,flags in sections:
            if not flags & 0x20000000:continue
            for at in range(rva,rva+n-8):
                if not instructions.get(at,'').startswith('lea'):continue
                if image[at] not in (0x48,0x4c) or image[at+1] != 0x8d or image[at+2]&0xc7 != 5:continue
                if at+7+struct.unpack_from('<i',image,at+3)[0] != marker:continue
                for pos in range(at,min(at+0x200,rva+n-7)):
                    if not instructions.get(pos,'').startswith('mov'):continue
                    if image[pos] == 0xc6 and 0x80 <= image[pos+1] <= 0xbf:
                        candidate=struct.unpack_from('<I',image,pos+2)[0]
                        if 0x100 < candidate < 0x20000 and image[pos+6] <= 1:
                            field,value=candidate,image[pos+6];break
                if field is not None:break
            if field is not None:break
    if field is not None:
        for rva,n,flags in sections:
            if not flags & 0x20000000:continue
            for at in range(rva,rva+n-7):
                if not instructions.get(at,'').startswith('mov'):continue
                before=bytes(image[at:at+7]);after=None
                if before[0] == 0xc6 and 0x80 <= before[1] <= 0xbf and before[1]&7 != 4 and struct.unpack_from('<I',before,2)[0] == field and before[6] == 1-value:
                    after=before[:6]+bytes([value])
                elif before[:2] == b'\x40\x88' and 0x80 <= before[2] <= 0xbf and before[2]&7 != 4 and struct.unpack_from('<I',before,3)[0] == field:
                    after=bytes([0xc6,0x80|(before[2]&7)])+struct.pack('<I',field)+bytes([value])
                if after:sites.append(dict(kind='legacy',rva=at,before=before.hex(),after=after.hex()))
    return dict(name=path.name,sha256=hashlib.sha256(data).hexdigest(),timestamp=stamp,image_size=image_size,code_hash=f'{fnv1a64(code):016x}',sites=sites,legacy_field=field,legacy_value=value)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',action='append',type=Path,required=True)
    p.add_argument('--dumpbin',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    profiles=[]; identities=set()
    for path in args.input:
        profile=inspect(path,args.dumpbin)
        if profile['sites'] and (profile['timestamp'],profile['image_size'],profile['code_hash']) not in {(x['timestamp'],x['image_size'],x['code_hash']) for x in profiles}:profiles.append(profile)
        data,sections=pe_sections(path)
        for start,end in iter_fatbins(data,sections):
            for kind,arch,at,n,compressed,raw in iter_entries(data,start,end):
                if kind==2 and arch==89 and not compressed:
                    blob=data[at:at+n];fp=fingerprint_elf(blob)
                    if fp[1] in [7776,3920,784]:identities.add((*fp,n,fnv1a64(blob)))
    args.output.write_text(json.dumps(dict(profiles=profiles,kernel_identities=[list(x[:4])+[f'{x[4]:016x}'] for x in sorted(identities)]),indent=2)+'\n',encoding='utf-8')
    lines=['// SPDX-License-Identifier: MIT','// Local PE instruction identities; generated by build_compatibility_profiles.py.','#pragma once','#include <cstdint>','#include <cstddef>','namespace mfgunlock::binaryprofiles {',
        'struct Site { uint32_t rva; uint8_t size; uint8_t before[16], after[16]; };',
        'struct Profile { uint32_t timestamp, image_size; uint64_t code_hash; const Site* sites; size_t count; uint32_t kind; uint32_t field, value; };']
    for i,profile in enumerate(profiles):
        for kind in ['arch','legacy','ceiling']:
            sites=[s for s in profile['sites'] if s['kind']==kind]
            if not sites:continue
            lines.append(f'inline constexpr Site kSites{i}_{kind}[] = {{')
            for site in sites:
                def values(key):return ','.join('0x'+site[key][a:a+2] for a in range(0,len(site[key]),2))
                lines.append(f'  {{0x{site["rva"]:x}u,{len(site["before"])//2},{{{values("before")}}},{{{values("after")}}}}},')
            lines.append('};')
    lines.append('inline constexpr Profile kProfiles[] = {')
    for i,profile in enumerate(profiles):
        for kind in ['arch','legacy','ceiling']:
            sites=[s for s in profile['sites'] if s['kind']==kind]
            if not sites:continue
            lines.append(f'  {{0x{profile["timestamp"]:x}u,{profile["image_size"]}u,0x{profile["code_hash"]}ull,kSites{i}_{kind},{len(sites)},'+str(['arch','legacy','ceiling'].index(kind))+f',{profile["legacy_field"] or 0}u,{profile["legacy_value"] or 0}u}},')
    lines+=['};','}  // namespace mfgunlock::binaryprofiles']
    args.output.with_suffix('.hpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(f'{len(profiles)} executable identities; {len(identities)} original kernel identities')
if __name__=='__main__':main()
