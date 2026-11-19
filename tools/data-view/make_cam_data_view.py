#!/usr/bin/env python3
# Builds CAM_DataView.dat: the Deathcare data view for SC4DataViewExtensions 1.1 (dependency, not modified).
#
#   python tools/data-view/make_cam_data_view.py <sc4-data-view-extensions>/dat/ExtendedDataViewUI.dat <out dir>
#
# SC4DataViewExtensions gives a button to every data view with Data View Button Cell (0x5C4DF150), but only when its
# button 0x5200 + view mode is in the Data Views panel UI (buttons created at run time do not receive clicks). So the
# file holds:
#   - the data view exemplar 0x6534284A / 0x690F693F / 0x4A0B6800 + DEATHCARE_MODE: the transient aura grid (data source
#     14 of the DLL), where SC4SIM writes the uncollected dead of each tract as a negative aura (docs/deathcare.md),
#     with a colour ramp for the negative side only and its legend, in cell (3, 7) (page 2, under Transient Aura);
#   - a copy of the panel UI of ExtendedDataViewUI.dat (0x00000000 / 0x96A006B0 / 0x2BC9060F) with the button
#     0x5200 + DEATHCARE_MODE added. It must load after ExtendedDataViewUI.dat, and must be rebuilt when that UI changes;
#   - the caption and legend texts.
import os, re, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'power-graph'))
import dbpf2

UI_KEY = (0x00000000, 0x96A006B0, 0x2BC9060F)
LTEXT, EXEMPLAR = 0x2026960B, 0x6534284A
DATA_VIEW_GROUP, DATA_VIEW_BASE = 0x690F693F, 0x4A0B6800
BUTTON_TEXT_GROUP, LEGEND_TEXT_GROUP = 0x6A554AFD, 0x6A231EAA
GRID = 0x8A2871D6
CUSTOM_BUTTON_BASE = 0x5200
TEMPLATE_BUTTON = CUSTOM_BUTTON_BASE + 0x0E    # Transient Aura button of the dependency's UI

DEATHCARE_MODE = 0xC0                         # view mode: exemplar 0x4A0B68C0, button 0x52C0
CELL = (3, 7)                                 # column, row (page 2, last row of the right column)
CAPTION = 0x5C4DDA00                          # "Deathcare" (button)
LEGEND_HEADER = 0x5C4DDA01
DATA_SOURCE_TRANSIENT_AURA = 14
ROW, PAGE_ROWS = 18, 4
COLUMN_X = ((4, 124), (128, 212), (212, 352), (356, 502))

# Ramp index = transient aura + 128 (0 = -128, 128 = no aura). SC4SIM writes 4 per 1 % of uncollected dead, at most 60
# (DEATHCARE_AURA_PER_PERCENT, DEATHCARE_AURA_MAX in SC4SIM.ini). Positive aura is not shown. Colours are ARGB.
RAMP = [(0, 0xB0300040), (68, 0xB0400050), (88, 0xA0703090), (108, 0x80A070C0), (127, 0x50C0A0D0),
        (128, 0x00FFFFFF), (255, 0x00FFFFFF)]
LEGEND = [  # (colour, LTEXT instance, text)
    (0xB0400050, 0x5C4DDA02, '15% or more uncollected'),
    (0xA0703090, 0x5C4DDA03, 'About 10% uncollected'),
    (0x80A070C0, 0x5C4DDA04, 'About 5% uncollected'),
    (0x50C0A0D0, 0x5C4DDA05, 'A few uncollected'),
    (0x99CCCCCC, 0x5C4DDA06, 'None'),
]

def ltext(s):
    return struct.pack('<HH', len(s), 0x1000) + s.encode('utf-16-le')

def prop_u32(pid, v):
    return struct.pack('<IHHBI', pid, 0x0300, 0, 0, v)

def prop_bool(pid, v):
    return struct.pack('<IHHBB', pid, 0x0B00, 0, 0, 1 if v else 0)

def prop_u32s(pid, vals):
    return struct.pack('<IHHBI', pid, 0x0300, 0x80, 0, len(vals)) + struct.pack('<%dI' % len(vals), *vals)

def prop_str(pid, s):
    return struct.pack('<IHHBI', pid, 0x0C00, 0x80, 0, len(s)) + s.encode('latin1')

def exemplar():
    legend = LEGEND + [(0, 0, '')] * (9 - len(LEGEND))
    props = [
        prop_u32(0x10, 35),                                   # Exemplar Type: data view
        prop_str(0x20, 'DataView: Deathcare (CAM)'),
        prop_bool(0x4A0B47E2, False),                         # Hide zones
        prop_u32(0x4A0B47E3, 0),                              # View level: surface
        prop_u32s(0x4A0B47E4, [x for pair in RAMP for x in pair]),
        prop_u32(0x4A0B47E5, DATA_SOURCE_TRANSIENT_AURA),
        prop_bool(0x4A0B47E7, True),                          # Interpolate data
        prop_u32(0x4A0B47EC, 100),                            # Maximum scale
        prop_u32s(0x4A0B47F2, [c for c, _, _ in legend]),
        prop_u32s(0x4A0B47F3, [i for _, i, _ in legend]),
        prop_u32(0x4A0B47F4, LEGEND_HEADER),
        prop_u32s(0x5C4DF150, list(CELL)),                    # Data View Button Cell
    ]
    return b'EQZB1###' + b'\0' * 12 + struct.pack('<I', len(props)) + b''.join(props)

def build_ui(text):
    lines = text.split('\r\n')
    assert 'id=0x%08x' % (CUSTOM_BUTTON_BASE + DEATHCARE_MODE) not in text, 'the UI already has the Deathcare button'
    start = next(i for i, l in enumerate(lines) if 'id=0x%08x' % GRID in l)
    end = start + 2
    while lines[end].strip() != '</CHILDREN>':
        end += 1
    template = next(l for l in lines[start + 2:end] if 'id=0x%08x' % TEMPLATE_BUTTON in l)
    indent = re.match(r'\s*', template).group(0)
    line = template.strip().replace('id=0x%08x' % TEMPLATE_BUTTON, 'id=0x%08x' % (CUSTOM_BUTTON_BASE + DEATHCARE_MODE), 1)
    line = re.sub(r'caption="[^"]*" captionres=\{[^}]*\}',
                  'caption="Deathcare" captionres={%08x,%08x}' % (BUTTON_TEXT_GROUP, CAPTION), line, count=1)
    column, row = CELL
    top = 1 + ROW * (row % PAGE_ROWS + 1)
    line, n = re.subn(r'area=\([^)]*\)', 'area=(%d,%d,%d,%d)' % (COLUMN_X[column][0], top, COLUMN_X[column][1], top + ROW),
                      line, count=1)
    assert n == 1 and ('winflag_visible=no' in line) == (row >= PAGE_ROWS)
    lines.insert(end, indent + line)
    return '\r\n'.join(lines)

def main(extended_ui, out_dir):
    ui = next(e for e in dbpf2.read(extended_ui)[1] if (e['t'], e['g'], e['i']) == UI_KEY)
    entries = [
        dict(t=UI_KEY[0], g=UI_KEY[1], i=UI_KEY[2], data=build_ui(ui['data'].decode('latin1')).encode('latin1')),
        dict(t=EXEMPLAR, g=DATA_VIEW_GROUP, i=DATA_VIEW_BASE + DEATHCARE_MODE, data=exemplar()),
        dict(t=LTEXT, g=BUTTON_TEXT_GROUP, i=CAPTION, data=ltext('Deathcare')),
        dict(t=LTEXT, g=LEGEND_TEXT_GROUP, i=LEGEND_HEADER, data=ltext('Uncollected Dead')),
    ] + [dict(t=LTEXT, g=LEGEND_TEXT_GROUP, i=i, data=ltext(s)) for _, i, s in LEGEND]
    os.makedirs(out_dir, exist_ok=True)
    dbpf2.write(os.path.join(out_dir, 'CAM_DataView.dat'), entries)
    print('CAM_DataView.dat: %d entries' % len(entries))

if __name__ == '__main__':
    main(*sys.argv[1:3])
