# Adds bought / sold energy and the production of each energy class to the Power graph of CAM.
#   python make_power_graph.py <CAM_Exemplars.dat> <CAM_Locale_en.dat> <out_dir>
# Writes <out_dir>/CAM_Exemplars.dat (graph exemplar 0x6534284A / 0xCA4AD545 / 0x00000006 rebuilt, every other entry
# unchanged) and <out_dir>/CAM_Locale_en.dat (the 16 plot labels added, LTEXT group 0x6A231EAA). The class lines read
# the history types SC4SIM.dll 1.13+ records every month (src/power/PowerGraphHistory.h). See docs/power-graph.md.
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbpf2

EXEMPLAR, LTEXT = 0x6534284A, 0x2026960B
GRAPH_GROUP, POWER_GRAPH = 0xCA4AD545, 0x00000006
LTEXT_GROUP = 0x6A231EAA
P_SOURCE1, P_SOURCE2, P_COMBINE, P_LABEL, P_COLOR = 0x6A4AEE40, 0x6A4AEE5F, 0x6A4AEE68, 0x6A4AEEDC, 0x6A4AEEDF
T_UINT8, T_UINT32 = 0x01, 0x03

G_POWER_PRODUCED, G_POWER_CONSUMED = 0xAA11F7CE, 0xAA11F7D2
G_POWER_IMPORTED, G_POWER_EXPORTED = 0xAA11F837, 0xAA11F83A

# label instance, text, history type (None = vanilla), RGB
PLOTS = [
    (0x0A5D2E9D, None, G_POWER_PRODUCED, (83, 101, 153)),     # game label; + bought (data source 2)
    (0x0A5D2E9E, None, G_POWER_CONSUMED, (93, 189, 152)),     # game label; + sold (data source 2)
    (0x5C4DE10C, 'Energy Bought', G_POWER_IMPORTED, (220, 11, 11)),
    (0x5C4DE10D, 'Energy Sold', G_POWER_EXPORTED, (0, 234, 255)),
    (0x5C4DE101, 'Coal', 0x5C4DE001, (128, 128, 128)),
    (0x5C4DE102, 'Oil', 0x5C4DE002, (150, 95, 40)),
    (0x5C4DE103, 'Natural Gas', 0x5C4DE003, (255, 150, 0)),
    (0x5C4DE104, 'Nuclear Fission', 0x5C4DE004, (170, 60, 255)),
    (0x5C4DE105, 'Nuclear Fusion', 0x5C4DE005, (255, 60, 190)),
    (0x5C4DE106, 'Solar', 0x5C4DE006, (255, 225, 0)),
    (0x5C4DE107, 'Wind', 0x5C4DE007, (160, 230, 120)),
    (0x5C4DE108, 'Geothermal', 0x5C4DE008, (200, 70, 40)),
    (0x5C4DE109, 'Hydroelectric', 0x5C4DE009, (40, 110, 255)),
    (0x5C4DE10A, 'Tidal', 0x5C4DE00A, (0, 160, 150)),
    (0x5C4DE10B, 'Microwave', 0x5C4DE00B, (120, 200, 255)),
    (0x5C4DE10E, 'Biomass', 0x5C4DE00C, (60, 150, 40)),
    (0x5C4DE10F, 'Generators', 0x5C4DE00D, (230, 230, 230)),
    (0x5C4DE110, 'Waste to Energy', 0x5C4DE00E, (140, 60, 90)),
]

def records(data):
    """Exemplar -> (header, [(id, raw record bytes)]), raw kept for every property."""
    assert data[:8] == b'EQZB1###', data[:8]
    n = struct.unpack_from('<I', data, 20)[0]
    p, out = 24, []
    size = {1: 1, 2: 2, 3: 4, 7: 4, 8: 8, 9: 4, 0xB: 1, 0xC: 1}
    for _ in range(n):
        start = p
        pid, typ, kt = struct.unpack_from('<IHH', data, p); p += 9
        sz = size[typ >> 8]
        if kt & 0x80:
            cnt = struct.unpack_from('<I', data, p)[0]; p += 4 + sz * cnt
        else:
            p += sz
        out.append((pid, data[start:p]))
    assert p == len(data), (p, len(data))
    return data[:20], out

def array(pid, typ, fmt, values):
    return struct.pack('<IHHBI', pid, typ << 8, 0x80, 0, len(values)) + struct.pack('<%d%s' % (len(values), fmt), *values)

def build_graph(data):
    header, props = records(data)
    new = {
        P_SOURCE1: array(P_SOURCE1, T_UINT32, 'I', [p[2] for p in PLOTS]),
        P_LABEL: array(P_LABEL, T_UINT32, 'I', [p[0] for p in PLOTS]),
        P_COLOR: array(P_COLOR, T_UINT8, 'B', [c for p in PLOTS for c in p[3]]),
    }
    # Data source 2 and combine op keep the vanilla two entries: plot 1 = produced + bought, plot 2 = consumed + sold.
    ids = [pid for pid, _ in props]
    assert P_SOURCE2 in ids and P_COMBINE in ids
    out = [new.get(pid, raw) for pid, raw in props]
    return header + struct.pack('<I', len(out)) + b''.join(out)

def ltext(text):
    return struct.pack('<HH', len(text), 0x1000) + text.encode('utf-16-le')

def main(exemplars, locale, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    raw, entries = dbpf2.read(exemplars)
    hits = 0
    for e in entries:
        if (e['t'], e['g'], e['i']) == (EXEMPLAR, GRAPH_GROUP, POWER_GRAPH):
            e['data'] = build_graph(e['data']); hits += 1
    assert hits == 1, 'Power graph exemplar not found'
    dbpf2.write(os.path.join(out_dir, 'CAM_Exemplars.dat'), entries, header_src=raw)

    raw, entries = dbpf2.read(locale)
    labels = {p[0]: p[1] for p in PLOTS if p[1]}
    entries = [e for e in entries if not (e['t'] == LTEXT and e['g'] == LTEXT_GROUP and e['i'] in labels)]
    for instance, text in labels.items():
        entries.append(dict(t=LTEXT, g=LTEXT_GROUP, i=instance, data=ltext(text)))
    dbpf2.write(os.path.join(out_dir, 'CAM_Locale_en.dat'), entries, header_src=raw)

if __name__ == '__main__':
    main(*sys.argv[1:4])
