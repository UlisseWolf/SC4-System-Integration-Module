#!/usr/bin/env python3
"""Builds a .dat of SC4SIM pole-style exemplars from [PowerPoles.*] sections (same keys as SC4SIM.ini).

usage: python make_pole_styles.py styles.ini OUT.dat

Every [PowerPoles.<Name>] section becomes one exemplar (type 0x6534284A, group 0x5C4DB000) read by
SC4SIM.dll 1.12+ at the first city it opens. Extra keys, only for this tool:
  Instance = 0x........   exemplar instance (default: CRC32 of the lower-case name)
  Order    = n            position in the Tab cycle (default: 10, 20, 30... in file order)
[PowerPoles.FAR] becomes the FAR-default exemplar (property 0x5C4DB005 = true).
"""
import configparser, struct, sys, zlib, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbpf2

EXEMPLAR = 0x6534284A
GROUP = 0x5C4DB000
P_NAME, P_REGULAR, P_FAR, P_DISTANCE, P_ORDER, P_FARDEFAULT = 0x20, 0x5C4DB001, 0x5C4DB002, 0x5C4DB003, 0x5C4DB004, 0x5C4DB005
REG = ["none", "x", "dp", "x+dp", "z", "x+z", "dp+z", "x+dp+z",
       "dn", "x+dn", "dp+dn", "x+dp+dn", "z+dn", "x+z+dn", "dp+z+dn", "x+dp+z+dn"]
FAR = [f"far-{r}.{o}" for r in ("1.5", "2", "3", "6") for o in ("xp", "xn", "zp", "zn")]
T_UINT32, T_BOOL, T_STRING = 0x03, 0x0B, 0x0C

def prop_uint32(pid, v):  return struct.pack('<IHHB', pid, T_UINT32 << 8, 0x00, 0) + struct.pack('<I', v)
def prop_bool(pid, v):    return struct.pack('<IHHB', pid, T_BOOL << 8, 0x00, 0) + struct.pack('<?', v)
def prop_uint32s(pid, vs): return struct.pack('<IHHBI', pid, T_UINT32 << 8, 0x80, 0, len(vs)) + struct.pack('<%dI' % len(vs), *vs)
def prop_string(pid, s):
    b = s.encode('latin1'); return struct.pack('<IHHBI', pid, T_STRING << 8, 0x80, 0, len(b)) + b

def exemplar(props):
    return b'EQZB1###' + struct.pack('<IIII', 0, 0, 0, len(props)) + b''.join(props)

def hexint(v): return int(v.strip(), 0)

def main(src, dst):
    cp = configparser.ConfigParser(inline_comment_prefixes=(';',), comment_prefixes=(';', '#'), strict=False)
    cp.optionxform = str.lower
    cp.read(src, encoding='utf-8')
    entries, order = [], 0
    for section in cp.sections():
        if not section.lower().startswith('powerpoles.'):
            continue
        name = section[len('powerpoles.'):]
        s = cp[section]
        order += 10
        is_far_default = name.lower() == 'far'
        regular = [hexint(s[f"reg.{k}"]) if f"reg.{k}" in s else 0 for k in REG]
        far = [hexint(s[k]) if k in s else 0 for k in FAR]
        unknown = [k for k in s if not (k.startswith('reg.') and k[4:] in REG) and k not in FAR
                   and k not in ('interpoledistance', 'instance', 'order')]
        if unknown:
            sys.exit(f"[{section}] unknown keys: {', '.join(unknown)}")
        instance = hexint(s['instance']) if 'instance' in s else zlib.crc32(name.lower().encode()) & 0xFFFFFFFF
        props = [prop_string(P_NAME, name), prop_uint32(P_ORDER, int(s.get('order', order), 0))]
        if is_far_default:
            props.append(prop_bool(P_FARDEFAULT, True))
        else:
            props.append(prop_uint32s(P_REGULAR, regular))
        if any(far):
            props.append(prop_uint32s(P_FAR, far))
        if 'interpoledistance' in s and not is_far_default:
            props.append(prop_uint32(P_DISTANCE, int(s['interpoledistance'], 0)))
        entries.append(dict(t=EXEMPLAR, g=GROUP, i=instance, data=exemplar(props)))
        print(f"{name}: instance 0x{instance:08X}, {sum(1 for v in regular if v)} direction masks, "
              f"{sum(1 for v in far if v)} FAR headings{' (FAR default)' if is_far_default else ''}")
    if not entries:
        sys.exit("no [PowerPoles.*] section found")
    dbpf2.write(dst, entries)
    print(f"wrote {len(entries)} exemplar(s) to {dst}")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
