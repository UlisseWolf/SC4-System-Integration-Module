# Rebuilds CAM's Power and Water graphs:
#  - Power (0x6534284A / 0xCA4AD545 / 0x00000006): energy bought and sold and the production of each energy class;
#  - Water (0x6534284A / 0xCA4AD545 / 0x00000007): water bought and sold, water by source and wastewater;
#  - Garbage (0x6534284A / 0xCA4AD545 / 0x0000000A): garbage produced added, real scale, garbage pollution moved out,
#    own labels for garbage imported and exported, trailing line breaks removed from two labels;
#  - Garbage Pollution (0x6534284A / 0xCA4AD545 / 0x00000019, new): the garbage pollution index, moved out of the
#    Garbage graph. Its button is added by SC4SIM.dll (docs/graph-window.md).
#   python make_utility_graphs.py <CAM_Exemplar_Graphs.dat> <CAM_Locale_en.dat> <out_dir>
# Writes <out_dir>/<same file name> (the three graph exemplars rebuilt, every other entry unchanged) and
# <out_dir>/CAM_Locale_en.dat (the plot labels added, LTEXT group 0x6A231EAA). The new lines read the history types
# SC4SIM.dll 1.13+ records every month (src/power/PowerGraphHistory.h, src/water/WaterGraphHistory.h).
# See docs/power-graph.md and docs/water-graph.md.
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbpf2
from graph_layout import LAYOUT

EXEMPLAR, LTEXT = 0x6534284A, 0x2026960B
GRAPH_GROUP, POWER_GRAPH, WATER_GRAPH, GARBAGE_GRAPH = 0xCA4AD545, 0x00000006, 0x00000007, 0x0000000A
LTEXT_GROUP = 0x6A231EAA
P_SOURCE1, P_SOURCE2, P_COMBINE, P_LABEL, P_COLOR = 0x6A4AEE40, 0x6A4AEE5F, 0x6A4AEE68, 0x6A4AEEDC, 0x6A4AEEDF
P_SCALE = 0x6A4AEEE2
T_UINT8, T_UINT32, T_FLOAT32 = 0x01, 0x03, 0x09

G_POWER_PRODUCED, G_POWER_CONSUMED = 0xAA11F7CE, 0xAA11F7D2
G_POWER_IMPORTED, G_POWER_EXPORTED = 0xAA11F837, 0xAA11F83A
G_WATER_PRODUCED, G_WATER_CONSUMED = 0x0A14EBB4, 0xCA14EBD0
G_WATER_IMPORTED, G_WATER_EXPORTED = 0x0A14EBB5, 0xCA14EBC6

# label instance, text, history type (None = vanilla), RGB
POWER_PLOTS = [
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

WATER_PLOTS = [
    (0x0A5D2E9D, None, G_WATER_PRODUCED, (83, 101, 153)),     # game label; + bought (data source 2)
    (0x0A5D2E9E, None, G_WATER_CONSUMED, (93, 189, 152)),     # game label; + sold (data source 2)
    (0x5C4DE121, 'Water Bought', G_WATER_IMPORTED, (220, 11, 11)),
    (0x5C4DE122, 'Water Sold', G_WATER_EXPORTED, (0, 234, 255)),
    (0x5C4DE123, 'Aquifer', 0x5C4DE021, (150, 95, 40)),
    (0x5C4DE124, 'Fresh Water', 0x5C4DE022, (40, 110, 255)),
    (0x5C4DE125, 'Salt Water', 0x5C4DE023, (0, 160, 150)),
    (0x5C4DE126, 'Wastewater Produced', 0x5C4DE024, (128, 128, 128)),
    (0x5C4DE127, 'Wastewater Treated', 0x5C4DE025, (170, 60, 255)),
    (0x5C4DE128, 'Recycled to Fresh Water', 0x5C4DE026, (60, 150, 40)),
]

# Garbage graph. CAM drew the eight amounts at 1/100 (Graph Plot scale 0.01) next to the garbage pollution index
# (G_TOTAL_GARBAGE_POLLUTION 0xEA110134, scale 1): the amounts are now at real scale and the pollution index, a
# different unit, is left to the pollution data views. Garbage produced (G_GARBAGE_PRODUCED 0xEA110158, recorded by
# the game) is added; imported and exported get their own labels.
GARBAGE_PLOTS = [
    (0x0A5D2E9D, None, 0x2A8C4696, (83, 101, 153)),            # game label "Capacity"
    (0xFF5D2E97, None, 0x2A8C4697, (93, 189, 170)),            # CAM label "Total Garbage" (line break removed)
    (0x5C4DE133, 'Garbage Produced', 0xEA110158, (255, 140, 0)),
    (0x5C4DE131, 'Garbage Imported', 0xEA11017B, (0, 234, 255)),
    (0x5C4DE132, 'Garbage Exported', 0xEA11017E, (220, 11, 11)),
    (0xFF5D2E95, None, 0xEA1101AA, (147, 88, 31)),             # Landfill
    (0xFF5D2E93, None, 0xEA11016F, (0, 216, 0)),               # Recycled
    (0xFF5D2E96, None, 0xEA110197, (0, 0, 0)),                 # Incinerated
    (0xFF5D2E94, None, 0xEA1101A1, (224, 224, 0)),             # Waste to Energy
]

GRAPHS = {POWER_GRAPH: POWER_PLOTS, WATER_GRAPH: WATER_PLOTS, GARBAGE_GRAPH: GARBAGE_PLOTS}

# New graphs: instance -> (template graph, name, title LTEXT instance, title text, button cell, plots). The template
# gives every other property (graph type, value range); its plots are replaced. Button cell: (column 0-2, row) of the
# button SC4SIM.dll adds in the Graphs window (Graph Button Cell 0x5C4DE350; rows 0-5 are the Maxis buttons).
NEW_GRAPHS = {
    0x19: (0x09, 'Garbage Pollution', 0x5C4DE140, 'Garbage Pollution', LAYOUT[0x19],
           [(0xFF5D2EA7, None, 0xEA110134, (255, 0, 210))]),      # CAM label "Garbage Pollution"; G_TOTAL_GARBAGE_POLLUTION
}
P_NAME, P_TITLE, P_BUTTON_CELL = 0x00000020, 0x8A416A99, 0x5C4DE350
T_STRING = 0x0C

def string_prop(pid, text):
    data = text.encode('latin1')
    return struct.pack('<IHHBI', pid, T_STRING << 8, 0x80, 0, len(data)) + data

def new_graph(template, name, title_instance, cell, plots):
    header, props = records(build_graph(template, plots))
    replace = {P_NAME: string_prop(P_NAME, name),
               P_TITLE: array(P_TITLE, T_UINT32, 'I', [LTEXT, LTEXT_GROUP, title_instance])}
    out = [replace.get(pid, raw) for pid, raw in props if pid != P_BUTTON_CELL]
    if cell is not None:
        out.append(array(P_BUTTON_CELL, T_UINT32, 'I', list(cell)))
    return header + struct.pack('<I', len(out)) + b''.join(out)

def set_button_cell(data, cell):
    header, props = records(data)
    out = [raw for pid, raw in props if pid != P_BUTTON_CELL] + [array(P_BUTTON_CELL, T_UINT32, 'I', list(cell))]
    return header + struct.pack('<I', len(out)) + b''.join(out)

# CAM labels whose text ends with a line break (an empty extra legend row): rewritten without it.
TRIM_LABELS = (0xFF5D2E97, 0xFF5D2E98)

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

def build_graph(data, plots):
    header, props = records(data)
    new = {
        P_SOURCE1: array(P_SOURCE1, T_UINT32, 'I', [p[2] for p in plots]),
        P_LABEL: array(P_LABEL, T_UINT32, 'I', [p[0] for p in plots]),
        P_COLOR: array(P_COLOR, T_UINT8, 'B', [c for p in plots for c in p[3]]),
    }
    # Data source 2 and combine op (Power and Water) keep their two entries: plot 1 = produced + bought, plot 2 =
    # consumed + sold. Graph Plot scale, where present (Garbage), becomes 1.0 for every plot.
    ids = [pid for pid, _ in props]
    if P_SCALE in ids:
        new[P_SCALE] = array(P_SCALE, T_FLOAT32, 'f', [1.0] * len(plots))
    out = [new.get(pid, raw) for pid, raw in props]
    return header + struct.pack('<I', len(out)) + b''.join(out)

def ltext(text):
    return struct.pack('<HH', len(text), 0x1000) + text.encode('utf-16-le')

def main(exemplars, locale, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    raw, entries = dbpf2.read(exemplars)
    done = set()
    for e in entries:
        if e['t'] == EXEMPLAR and e['g'] == GRAPH_GROUP and e['i'] in GRAPHS:
            e['data'] = build_graph(e['data'], GRAPHS[e['i']]); done.add(e['i'])
    assert done == set(GRAPHS), 'graph exemplar missing'
    by_instance = {e['i']: e for e in entries if e['t'] == EXEMPLAR and e['g'] == GRAPH_GROUP}
    for instance, (template, name, title, _, cell, plots) in NEW_GRAPHS.items():
        data = new_graph(by_instance[template]['data'], name, title, cell, plots)
        if instance in by_instance:
            by_instance[instance]['data'] = data
        else:
            entries.append(dict(t=EXEMPLAR, g=GRAPH_GROUP, i=instance, data=data))
    # Button order by theme (graph_layout.py): Graph Button Cell in every graph exemplar.
    for e in entries:
        if e['t'] == EXEMPLAR and e['g'] == GRAPH_GROUP and e['i'] in LAYOUT:
            e['data'] = set_button_cell(e['data'], LAYOUT[e['i']])
    dbpf2.write(os.path.join(out_dir, os.path.basename(exemplars)), entries, header_src=raw)

    raw, entries = dbpf2.read(locale)
    labels = {p[0]: p[1] for plots in GRAPHS.values() for p in plots if p[1]}
    labels.update({title: text for _, _, title, text, _, _ in NEW_GRAPHS.values()})
    for e in entries:
        if e['t'] == LTEXT and e['g'] == LTEXT_GROUP and e['i'] in TRIM_LABELS:
            text = e['data'][4:].decode('utf-16-le').rstrip('\r\n ')
            e['data'] = ltext(text)
    entries = [e for e in entries if not (e['t'] == LTEXT and e['g'] == LTEXT_GROUP and e['i'] in labels)]
    for instance, text in labels.items():
        entries.append(dict(t=LTEXT, g=LTEXT_GROUP, i=instance, data=ltext(text)))
    dbpf2.write(os.path.join(out_dir, 'CAM_Locale_en.dat'), entries, header_src=raw)

if __name__ == '__main__':
    main(*sys.argv[1:4])
