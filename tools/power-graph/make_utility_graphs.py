# Rebuilds CAM's Power and Water graphs:
#  - Power (0x6534284A / 0xCA4AD545 / 0x00000006): energy bought and sold and the production of each energy class;
#  - Water (0x6534284A / 0xCA4AD545 / 0x00000007): water bought and sold, water by source and wastewater;
#  - Garbage (0x6534284A / 0xCA4AD545 / 0x0000000A): garbage produced added, real scale, garbage pollution moved out,
#    own labels for garbage imported and exported, trailing line breaks removed from two labels;
#  - Pollution (0x6534284A / 0xCA4AD545 / 0x00000008, the Maxis Air Pollution graph): air, water, garbage and
#    radiation pollution in one graph; Water Pollution (0x09) gets Graph Button Hidden;
#  - Crime (0x0C): crimes and arrests with unsolved crimes, criminals and inmates; Police & Jails (0x1C), new;
#  - Fire Protection (0x1D), new: coverage, protection, flammability, fire outbreaks, fire stations;
#  - Population R/C/I (0x1F) and Demand Forecast (0x20), new;
#  - Jobs & Pop. (0x11), Life Expectancy (0x05) and Education (0x0D): plots added (EXTEND);
#  - Health (0x1E), new: coverage, average health, health grade, hospital occupancy, hospitals, clinics;
#  - Power Grid (0x1A) and Water Grid (0x1B), new: buttons added by SC4SIM.dll (docs/graph-window.md).
#   python make_utility_graphs.py <CAM_Exemplar_Graphs.dat> <CAM_Locale_en.dat> <out_dir>
# Writes <out_dir>/<same file name> (the three graph exemplars rebuilt, every other entry unchanged) and
# <out_dir>/CAM_Locale_en.dat (the plot labels added, LTEXT group 0x6A231EAA). The new lines read the history types
# SC4SIM.dll 1.13+ records every month (src/power/PowerGraphHistory.h, src/water/WaterGraphHistory.h).
# See docs/power-graph.md and docs/water-graph.md.
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbpf2
from graph_layout import LAYOUT, HIDDEN

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
    # Pollution: the four pollution totals the game records (replaces the Maxis Air Pollution graph; the separate
    # Water Pollution graph is hidden). See docs/graph-window.md.
    0x08: (0x08, 'Pollution', 0x5C4DE190, 'Pollution', LAYOUT[0x08], [
        (0x0A5D2EA0, None, 0x0A1100E8, (128, 128, 128)),           # game label "Air Pollution"; G_TOTAL_AIR_POLLUTION
        (0x0A5D2EA1, None, 0xEA110130, (40, 110, 255)),            # game label "Water Pollution"; G_TOTAL_WATER_POLLUTION
        (0xFF5D2EA7, None, 0xEA110134, (150, 95, 40)),             # CAM label "Garbage Pollution"; G_TOTAL_GARBAGE_POLLUTION
        (0x5C4DE191, 'Radiation', 0xEA110137, (200, 210, 0)),      # G_TOTAL_RADIATION_POLLUTION
    ]),
    # Crime: the game's crimes and arrests, with unsolved crimes, criminals and inmates (SC4SIM.dll). Police & Jails:
    # the game's police coverage and average crime, with arrest rate and jail occupancy (SC4SIM.dll). See
    # docs/crime-graph.md.
    0x0C: (0x0C, 'Crime', 0x5C4DE1A8, 'Crime', LAYOUT[0x0C], [
        (0x0A5D2EA4, None, 0x0A5CC155, (83, 101, 153)),            # game label "Crimes"; G_NUM_CRIMES
        (0x0A5D2EA5, None, 0x0A5CC156, (93, 189, 152)),            # game label "Arrests"; G_NUM_ARRESTS
        (0x5C4DE1A3, 'Unsolved Crimes', 0x5C4DE083, (220, 11, 11)),
        (0x5C4DE1A1, 'Criminals', 0x5C4DE081, (255, 140, 0)),
        (0x5C4DE1A2, 'Inmates', 0x5C4DE082, (128, 128, 128)),
    ]),
    0x1C: (0x0C, 'Police & Jails', 0x5C4DE1A0, 'Police & Jails', LAYOUT[0x1C], [
        (0x5C4DE1A4, 'Police Coverage %', 0x4A11D354, (40, 110, 255)),   # G_POLICE_PROTECTION_COVERAGE_P
        (0x5C4DE1A5, 'Average Crime', 0xAA662693, (220, 11, 11)),        # GA_CRIME
        (0x5C4DE1A6, 'Police Efficiency %', 0x5C4DE084, (93, 189, 152)),  # as CAM's Police panel
        (0x5C4DE1A7, 'Jail Occupancy %', 0x5C4DE085, (128, 128, 128)),
    ]),
    # Fire Protection: the game's fire coverage, average fire protection and average flammability (both 0-256, CAM's
    # tuning: flammability low 50, medium 70, tinderbox 140), with fire outbreaks and fire stations (SC4SIM.dll). See
    # docs/fire-graph.md.
    0x1D: (0x0C, 'Fire Protection', 0x5C4DE1B0, 'Fire Protection', LAYOUT[0x1D], [
        (0x5C4DE1B1, 'Fire Coverage %', 0x2A11D203, (40, 110, 255)),         # G_FIRE_PROTECTION_COVERAGE_P
        (0x5C4DE1B2, 'Average Fire Protection', 0x2A11D204, (93, 189, 152)), # GA_FIRE_PROTECTION
        (0x5C4DE1B3, 'Average Flammability', 0x2A11CB48, (255, 140, 0)),     # GA_FLAMMABILITY
        (0x5C4DE1B4, 'Fire Outbreaks', 0x5C4DE0A1, (220, 11, 11)),
        (0x5C4DE1B5, 'Fire Stations', 0x5C4DE0A2, (128, 128, 128)),
    ]),
    # Health: coverage, average health (CAM's Health Rating), health grade, hospital occupancy, hospitals and clinics,
    # all from SC4SIM.dll (the advisor Lua's game table). See docs/health-graph.md.
    0x1E: (0x0C, 'Health', 0x5C4DE1C0, 'Health', LAYOUT[0x1E], [
        (0x5C4DE1C1, 'Health Coverage %', 0x5C4DE0C1, (40, 110, 255)),
        (0x5C4DE1C2, 'Average Health %', 0x5C4DE0C2, (93, 189, 152)),
        (0x5C4DE1C3, 'Health Grade %', 0x5C4DE0C3, (170, 60, 255)),
        (0x5C4DE1C4, 'Hospital Occupancy %', 0x5C4DE0C4, (220, 11, 11)),
        (0x5C4DE1C5, 'Hospitals', 0x5C4DE0C5, (128, 128, 128)),
        (0x5C4DE1C6, 'Clinics', 0x5C4DE0C6, (255, 140, 0)),
    ]),
    # Population R/C/I: total population and the R / C / I totals (game), workforce (SC4SIM.dll). Demand Forecast: the
    # demand simulator's extrapolated R / C / I demand and the workforce demand (SC4SIM.dll, from the advisor Lua's
    # game table). See docs/population-graph.md.
    0x1F: (0x0C, 'Population R/C/I', 0x5C4DE1D0, 'Population R/C/I', LAYOUT[0x1F], [
        (0x5C4DE1D1, 'Total Population', 0xAA1A2CCA, (0, 0, 0)),          # G_POPULATION
        (0x5C4DE1D2, 'Residential', 0x2A4E2003, (0, 170, 0)),             # G_R_POPULATION
        (0x5C4DE1D3, 'Commercial', 0x0A4E2056, (40, 110, 255)),           # G_C_POPULATION
        (0x5C4DE1D4, 'Industrial', 0x4A4E206B, (220, 180, 0)),            # G_I_POPULATION
        (0x5C4DE1D5, 'Workforce', 0x5C4DE0E1, (170, 60, 255)),
    ]),
    0x20: (0x0C, 'Demand Forecast', 0x5C4DE1E0, 'Demand Forecast', LAYOUT[0x20], [
        (0x5C4DE1E1, 'Residential Forecast', 0x5C4DE0E2, (0, 170, 0)),
        (0x5C4DE1E2, 'Commercial Forecast', 0x5C4DE0E3, (40, 110, 255)),
        (0x5C4DE1E3, 'Industrial Forecast', 0x5C4DE0E4, (220, 180, 0)),
        (0x5C4DE1E4, 'Workforce Demand', 0x5C4DE0E5, (170, 60, 255)),
    ]),
    # Power grid: demand and unused power (game), production by load class (SC4SIM.dll, from NEON when installed),
    # NEON's load balance and free grid capacities (SC4SIM.dll, only with NEON). See docs/power-grid-graph.md.
    0x1A: (0x09, 'Power Grid', 0x5C4DE160, 'Power Grid', LAYOUT[0x1A], [
        (0x5C4DE150, 'Power Demand', 0xAA11F7D5, (83, 101, 153)),          # G_POWER_DEMANDED
        (0x5C4DE151, 'Unused Power', 0xAA11F7D8, (128, 128, 128)),         # G_POWER_UNUSED
        (0x5C4DE152, 'Base Load', 0x5C4DE041, (150, 95, 40)),
        (0x5C4DE153, 'Intermediate Load', 0x5C4DE042, (60, 150, 40)),
        (0x5C4DE154, 'Peak Load', 0x5C4DE043, (255, 140, 0)),
        (0x5C4DE155, 'Base Used (NEON)', 0x5C4DE044, (200, 120, 70)),
        (0x5C4DE156, 'Intermediate Used (NEON)', 0x5C4DE045, (120, 210, 90)),
        (0x5C4DE157, 'Peak Used (NEON)', 0x5C4DE046, (255, 200, 60)),
        (0x5C4DE158, 'Peak Shortage (NEON)', 0x5C4DE047, (220, 11, 11)),
        (0x5C4DE159, 'Transmission Free kVA (NEON)', 0x5C4DE048, (170, 60, 255)),
        (0x5C4DE15A, 'Distribution Free kVA (NEON)', 0x5C4DE049, (40, 110, 255)),
        (0x5C4DE15B, 'Transformer Free kVA (NEON)', 0x5C4DE04A, (0, 160, 150)),
    ]),
    # Water grid: demand and surplus (SC4SIM.dll), watered / unwatered buildings and pipes (game), burst pipes and
    # water sources (SC4SIM.dll). See docs/water-grid-graph.md.
    0x1B: (0x09, 'Water Grid', 0x5C4DE180, 'Water Grid', LAYOUT[0x1B], [
        (0x5C4DE170, 'Water Demand', 0x5C4DE064, (83, 101, 153)),
        (0x5C4DE171, 'Water Surplus', 0x5C4DE065, (93, 189, 152)),
        (0x5C4DE172, 'Watered Buildings', 0xCA78A955, (40, 110, 255)),       # G_WATERED_BUILDING_COUNT
        (0x5C4DE173, 'Unwatered Buildings', 0x8A78AB06, (220, 11, 11)),      # G_UNWATERED_BUILDING_COUNT
        (0x5C4DE174, 'Water Pipes', 0xCA14EBFE, (128, 128, 128)),            # G_WATER_PIPES_COUNT
        (0x5C4DE175, 'Distressed Pipes', 0xCA14EC19, (255, 140, 0)),         # G_WATER_PIPES_COUNT_DISTRESSED
        (0x5C4DE176, 'Burst Pipes', 0x5C4DE061, (140, 60, 90)),
        (0x5C4DE177, 'Water Sources', 0x5C4DE062, (0, 160, 150)),
        (0x5C4DE178, 'Polluted Sources', 0x5C4DE063, (150, 95, 40)),
    ]),
}
# Maxis graphs that keep their plots and get more: instance -> plots appended (label instance, text or None for a label
# already written, history type, RGB[, second history type added to the first]). See docs/population-graph.md.
EXTEND = {
    # Jobs & Pop.: total jobs (game: commercial + industrial jobs, Graph Data Source 2 with combine 1 = add) and the
    # workforce (SC4SIM.dll), to compare jobs and workers.
    0x11: [
        (0x5C4DE1F1, 'Total Jobs (C + I)', 0x0A4E2056, (0, 0, 0), 0x4A4E206B),   # G_C_POPULATION + G_I_POPULATION
        (0x5C4DE1D5, None, 0x5C4DE0E1, (170, 60, 255)),                         # "Workforce"
    ],
    # Life Expectancy: best and worst neighbourhood (SC4SIM.dll).
    0x05: [
        (0x5C4DE1C7, 'Highest Neighborhood', 0x5C4DE0C7, (0, 170, 0)),
        (0x5C4DE1C8, 'Lowest Neighborhood', 0x5C4DE0C8, (220, 11, 11)),
    ],
    # Education (EQ, fixed 0-200 scale): coverage, grades and best / worst neighbourhood EQ (SC4SIM.dll).
    0x0D: [
        (0x5C4DE1F3, 'Education Coverage %', 0x5C4DE0F1, (40, 110, 255)),
        (0x5C4DE1F4, 'School Grade %', 0x5C4DE0F2, (0, 170, 0)),
        (0x5C4DE1F5, 'Library Grade %', 0x5C4DE0F3, (170, 60, 255)),
        (0x5C4DE1F6, 'Museum Grade %', 0x5C4DE0F4, (255, 140, 0)),
        (0x5C4DE1F7, 'Highest Neighborhood EQ', 0x5C4DE0F5, (93, 189, 152)),
        (0x5C4DE1F8, 'Lowest Neighborhood EQ', 0x5C4DE0F6, (220, 11, 11)),
    ],
}
P_NAME, P_TITLE, P_BUTTON_CELL, P_BUTTON_HIDDEN = 0x00000020, 0x8A416A99, 0x5C4DE350, 0x5C4DE351
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

def set_hidden(data):
    header, props = records(data)
    out = [raw for pid, raw in props if pid not in (P_BUTTON_CELL, P_BUTTON_HIDDEN)]
    out.append(struct.pack('<IHHB', P_BUTTON_HIDDEN, 0x0B << 8, 0x00, 0) + b'\x01')
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

def read_array(raw):
    pid, typ, kt = struct.unpack_from('<IHH', raw, 0)
    fmt = {T_UINT8: 'B', T_UINT32: 'I', T_FLOAT32: 'f'}[typ >> 8]
    if not kt & 0x80:
        return list(struct.unpack_from('<' + fmt, raw, 9))
    count = struct.unpack_from('<I', raw, 9)[0]
    return list(struct.unpack_from('<%d%s' % (count, fmt), raw, 13))

def extend_graph(data, plots):
    """Appends plots to a graph, keeping its own; Graph Data Source 2 and combine are padded with 0 (none)."""
    header, props = records(data)
    raw = dict(props)
    sources, labels = read_array(raw[P_SOURCE1]), read_array(raw[P_LABEL])
    colors = read_array(raw[P_COLOR]) if P_COLOR in raw else [83, 101, 153] * len(sources)
    assert len(labels) == len(sources) and len(colors) == 3 * len(sources)
    second = read_array(raw[P_SOURCE2]) if P_SOURCE2 in raw else []
    combine = read_array(raw[P_COMBINE]) if P_COMBINE in raw else []
    use_second = bool(second) or any(len(p) > 4 for p in plots)
    second += [0] * (len(sources) - len(second))
    combine += [0] * (len(sources) - len(combine))
    for p in plots:
        sources.append(p[2]); labels.append(p[0]); colors += list(p[3])
        second.append(p[4] if len(p) > 4 else 0); combine.append(1 if len(p) > 4 else 0)
    new = {P_SOURCE1: array(P_SOURCE1, T_UINT32, 'I', sources), P_LABEL: array(P_LABEL, T_UINT32, 'I', labels),
           P_COLOR: array(P_COLOR, T_UINT8, 'B', colors)}
    if use_second:
        new[P_SOURCE2] = array(P_SOURCE2, T_UINT32, 'I', second)
        new[P_COMBINE] = array(P_COMBINE, T_UINT32, 'I', combine)
    out = [new.pop(pid, r) for pid, r in props] + list(new.values())
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
    for e in entries:
        if e['t'] == EXEMPLAR and e['g'] == GRAPH_GROUP and e['i'] in EXTEND:
            e['data'] = extend_graph(e['data'], EXTEND[e['i']]); done.add(e['i'])
    assert set(EXTEND) <= done, 'graph exemplar missing'
    by_instance = {e['i']: e for e in entries if e['t'] == EXEMPLAR and e['g'] == GRAPH_GROUP}
    for instance, (template, name, title, _, cell, plots) in NEW_GRAPHS.items():
        data = new_graph(by_instance[template]['data'], name, title, cell, plots)
        if instance in by_instance:
            by_instance[instance]['data'] = data
        else:
            entries.append(dict(t=EXEMPLAR, g=GRAPH_GROUP, i=instance, data=data))
    # Button order by theme (graph_layout.py): Graph Button Cell in every graph exemplar, Graph Button Hidden in the
    # merged ones.
    for e in entries:
        if e['t'] == EXEMPLAR and e['g'] == GRAPH_GROUP and e['i'] in LAYOUT:
            e['data'] = set_button_cell(e['data'], LAYOUT[e['i']])
        elif e['t'] == EXEMPLAR and e['g'] == GRAPH_GROUP and e['i'] in HIDDEN:
            e['data'] = set_hidden(e['data'])
    dbpf2.write(os.path.join(out_dir, os.path.basename(exemplars)), entries, header_src=raw)

    raw, entries = dbpf2.read(locale)
    labels = {p[0]: p[1] for plots in GRAPHS.values() for p in plots if p[1]}
    labels.update({title: text for _, _, title, text, _, _ in NEW_GRAPHS.values()})
    labels.update({p[0]: p[1] for *_, plots in NEW_GRAPHS.values() for p in plots if p[1]})
    labels.update({p[0]: p[1] for plots in EXTEND.values() for p in plots if p[1]})
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
