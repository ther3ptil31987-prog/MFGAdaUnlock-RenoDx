# SPDX-License-Identifier: MIT
# 1.5.2: the published addon must load like 1.4.5 for the DLSS-G backend. Only
# KERNEL32, USER32 and VERSION are static imports; the DLLs that only XeMFG
# calls are delay-loaded, so ReShade's ADDON.LoadFromDllMain maps nothing else.
import struct, sys
from pathlib import Path

ALLOWED = {'kernel32.dll', 'user32.dll', 'version.dll'}
DELAYED = {'d3d12.dll', 'dxgi.dll', 'bcrypt.dll', 'shell32.dll'}

def imports(path):
    data = Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe+4] != b'PE\0\0': raise SystemExit('not a PE image')
    sections_count = struct.unpack_from('<H', data, pe+6)[0]
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    optional = pe+24
    if struct.unpack_from('<H', data, optional)[0] != 0x20B: raise SystemExit('not a PE32+ image')
    directories = optional+112
    sections = [struct.unpack_from('<8sIIII', data, optional+optional_size+i*40) for i in range(sections_count)]
    def offset(rva):
        for _, virtual_size, address, raw_size, raw in sections:
            if address <= rva < address+max(virtual_size, raw_size): return rva-address+raw
        raise SystemExit(f'RVA {rva:#x} outside sections')
    def name(rva):
        start = offset(rva); return data[start:data.index(b'\0', start)].decode('ascii').lower()
    def table(index, size, name_field):
        rva = struct.unpack_from('<I', data, directories+index*8)[0]
        found = []
        while rva:
            entry = data[offset(rva):offset(rva)+size]
            if not any(entry): break
            found.append(name(struct.unpack_from('<I', entry, name_field)[0])); rva += size
        return set(found)
    return table(1, 20, 12), table(13, 32, 4)

static, delayed = imports(sys.argv[1])
print('static:', sorted(static)); print('delay-loaded:', sorted(delayed))
if static != ALLOWED: raise SystemExit(f'unexpected static imports: {sorted(static-ALLOWED)} missing: {sorted(ALLOWED-static)}')
if not DELAYED <= delayed: raise SystemExit(f'not delay-loaded: {sorted(DELAYED-delayed)}')
print('PASS: the addon maps only the 1.4.5 static imports at load')
