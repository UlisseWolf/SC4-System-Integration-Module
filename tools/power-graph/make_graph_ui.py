# Makes the Graphs window taller so the legend of the Power graph (18 lines) fits, and wider (WIDTH, 40 pixels) so
# the wider legend of SC4SIM.dll's GraphLegendPatch leaves the plot its size and long plot names (Nuclear Fission)
# fit on one line.
#   python make_graph_ui.py <SimCity_1.dat> <CAM_UI.dat> <out_dir> [extra_height]
# Reads the Graphs window UI (0x00000000 / 0x96A006B0 / 0x6BC9065A) and its background image
# (PNG 0x856DDBAC / 0x46A006B0 / 0xEBBEAE28) from the game, grows the expanded graph window (0x8A8B5B71) upward by
# extra_height pixels (default 120: the 18 lines of the Power graph legend need 94) and writes <out_dir>/CAM_UI.dat = the input CAM_UI.dat plus the edited UI and a
# taller background image (PNG 0x856DDBAC / 0x46A006B0 / 0x5C4DE200). The radio-button panel and the collapsed
# window are unchanged; the graph area (0x8A4B0AA0), its frame, the Time Scale texts and the slider keep their
# distance from the bottom. The game lays the legend out from the graph area's height (0x0076E0F0 and following), so
# a taller area shows more legend lines. See docs/power-graph.md.
# For SC4SIM.dll's graph buttons (docs/graph-window.md) it also adds the button template UI 0x5C4DE300 (a copy of the
# Maxis Crime button, ID 0x5C4DE301) and, in the radio-button panel (0x0A4A8176), a hidden filler 0x5C4DE310 behind
# the button container, whose background image now stops at the container's bottom so that new rows can be painted.
import io, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbpf2
from graph_layout import LAYOUT, HIDDEN, PAGE_ROWS
from make_utility_graphs import NEW_GRAPHS
from PIL import Image

UI, PNG = 0x00000000, 0x856DDBAC
UI_GROUP, UI_GRAPHS = 0x96A006B0, 0x6BC9065A
IMG_GROUP, IMG_BACKGROUND, IMG_TALL = 0x46A006B0, 0xEBBEAE28, 0x5C4DE200
UI_BUTTON_TEMPLATE, BUTTON_TEMPLATE_ID, FILLER_ID = 0x5C4DE300, 0x5C4DE301, 0x5C4DE310
PANEL_BLUE = (156, 178, 194)  # colour of the panel background (image 0xEBBEAE28, rows 332-439)
SPLIT_ROW = 200   # background rows copied to grow the image (inside the light graph frame)
WIDTH = 40        # extra width of the three windows (= [GraphWindow] LegendExtraWidth of SC4SIM.ini); 488 + WIDTH
                  # must be a multiple of 3 so the three button columns stay equal (4-pixel gaps)
SPLIT_COLUMN = 258  # background column copied to widen the images (middle of the window)
IMG_COLLAPSED = 0x5C4DE201  # wider background of the collapsed window (PNG 0x856DDBAC / 0x46A006B0 / 0xABBEAD86)
IMG_COLLAPSED_SRC = 0xABBEAD86
assert (488 + WIDTH) % 3 == 0
assert PAGE_ROWS == 6        # the button container keeps the 6 Maxis rows; more graphs go on more pages
PAGES = max(row for _, row in LAYOUT.values()) // PAGE_ROWS + 1
EXTRA = 24                  # navigation row under the buttons (Previous, page number, Next): the panel and both graph
                            # windows grow upward by EXTRA
NAV_TOP = 113               # top of the navigation row in the panel (the container's last row ends at 113)
NAV_PREV, NAV_NEXT, NAV_LABEL = 0x5C4DE320, 0x5C4DE321, 0x5C4DE322
LIFT = 36                   # the three windows move up by this many pixels, so that the 10 button rows clear the
                            # news ticker at the bottom of the screen (third in-game test: the last rows were behind it)
PANEL_ROW = 400             # background row (inside the button panel) repeated EXTRA times, expanded window image
COLLAPSED_PANEL_ROW = 100   # the same, collapsed window image

def find(entries, t, g, i):
    for e in entries:
        if (e['t'], e['g'], e['i']) == (t, g, i):
            return e['data']
    raise SystemExit('missing %08X/%08X/%08X' % (t, g, i))

def area(line):
    m = re.search(r'area=\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\)', line)
    return m, [int(v) for v in m.groups()]

def set_area(line, a):
    m, _ = area(line)
    return line[:m.start()] + 'area=(%d,%d,%d,%d)' % tuple(a) + line[m.end():]

def edit_ui(text, h):
    lines = text.split('\r\n')
    out, in_window, depth, done = [], False, 0, False
    for line in lines:
        s = line.strip()
        if not done and s.startswith('<LEGACY') and 'id=0x8a8b5b71' in s:
            in_window = True
            _, a = area(line); a[1] -= h; line = set_area(line, a)
            out.append(line); continue
        if in_window:
            if s == '<CHILDREN>': depth += 1
            elif s == '</CHILDREN>':
                depth -= 1
                if depth == 0: in_window, done = False, True
            elif s.startswith('<LEGACY') and depth >= 1:
                _, a = area(line)
                if 'id=0x6a2871bc' in s:                                   # background
                    a[3] += h; line = set_area(line, a)
                    line = line.replace('image={46a006b0,ebbeae28}', 'image={46a006b0,%08x}' % IMG_TALL)
                    line = re.sub(r'imagerect=\(0,0,516,448\)', 'imagerect=(0,0,%d,%d)' % (516 + WIDTH, 448 + h), line)
                    assert '%08x' % IMG_TALL in line and 'imagerect=(0,0,%d,%d)' % (516 + WIDTH, 448 + h) in line
                elif depth == 1 and a[1] in (31, 32) and a[3] <= 330:            # frame, graph area, border: grow
                    a[3] += h; line = set_area(line, a)
                elif depth == 1 and a[1] >= 32:                             # below the graph: move down
                    a[1] += h; a[3] += h; line = set_area(line, a)
                elif depth == 2 and a[1] >= 200:                            # Time Scale texts in the frame
                    a[1] += h; a[3] += h; line = set_area(line, a)
        out.append(line)
    assert done
    return edit_panel(order_buttons(taller_panel(widen(out, h))), h)

def taller_panel(lines):
    """Room for the navigation row: the three windows grow upward by EXTRA (their bottoms stay) and are lifted by
    LIFT, the backgrounds get EXTRA more panel rows, the resize grips (negative x, at the bottom) move down with the
    bottom. The button container keeps its 6 rows."""
    out, root, depth = [], None, 0
    for line in lines:
        s = line.strip()
        if s == '<CHILDREN>': depth += 1
        elif s == '</CHILDREN>': depth -= 1
        elif s.startswith('<LEGACY'):
            m = re.search(r' id=(0x[0-9a-f]+)', s)
            wid = m.group(1) if m else None
            _, a = area(line)
            if depth == 0:
                root = wid
                if root in ('0x8a8b5b71', '0x8a8b5b72', '0x0a4a8176'):
                    a[1] -= EXTRA + LIFT; a[3] -= LIFT; line = set_area(line, a)
            elif depth == 1 and root in ('0x8a8b5b71', '0x8a8b5b72'):
                if wid == '0x6a2871bc':                                    # background
                    a[3] += EXTRA; line = set_area(line, a)
                    line = re.sub(r'imagerect=\((\d+),(\d+),(\d+),(\d+)\)',
                                  lambda m: 'imagerect=(%s,%s,%s,%d)' % (m.group(1), m.group(2), m.group(3), int(m.group(4)) + EXTRA), line)
                elif 'GZWinBMP' in s and a[0] < 0:                          # resize grip at the bottom
                    a[1] += EXTRA; a[3] += EXTRA; line = set_area(line, a)
        out.append(line)
    return out

def widen(lines, h):
    """The three windows WIDTH pixels wider, to the left: the root moves left, backgrounds, title bars, graph frame,
    graph area and button container get wider, the close buttons move right. Slider, Time Scale texts and the
    resize grips (negative x) stay."""
    out, root, depth = [], None, 0
    for line in lines:
        s = line.strip()
        if s == '<CHILDREN>': depth += 1
        elif s == '</CHILDREN>': depth -= 1
        elif s.startswith('<LEGACY'):
            m = re.search(r' id=(0x[0-9a-f]+)', s)
            wid = m.group(1) if m else None
            _, a = area(line)
            if depth == 0:
                root = wid
                if root in ('0x8a8b5b71', '0x8a8b5b72', '0x0a4a8176'):
                    a[0] -= WIDTH; line = set_area(line, a)
            elif root in ('0x8a8b5b71', '0x8a8b5b72'):
                if wid in ('0x0a8b7e00', '0x0a8b7e01'):                     # close buttons
                    a[0] += WIDTH; a[2] += WIDTH; line = set_area(line, a)
                elif a[0] >= 0 and 'GZWinSlider' not in s and wid not in ('0x8a2871d9', '0x8a2871d8'):
                    a[2] += WIDTH; line = set_area(line, a)
            elif root == '0x0a4a8176' and wid == '0xaa9244b6':            # button container
                a[2] += WIDTH; line = set_area(line, a)
            if root == '0x8a8b5b72' and wid == '0x6a2871bc':              # collapsed background image
                assert 'image={46a006b0,abbead86} imagerect=(0,0,516,146)' in line
                line = line.replace('image={46a006b0,abbead86} imagerect=(0,0,516,146)',
                                    'image={46a006b0,%08x} imagerect=(0,0,%d,146)' % (IMG_COLLAPSED, 516 + WIDTH))
        out.append(line)
    return out

COLUMN_X = ((0, (488 + WIDTH) // 3), ((488 + WIDTH) // 3 + 4, 2 * (488 + WIDTH) // 3 + 4),
            (2 * (488 + WIDTH) // 3 + 8, 496 + WIDTH))

# Captions of Maxis buttons whose graph changed: instance -> (LTEXT instance in group 0x6A231EAA, text).
CAPTIONS = {0x08: (0x5C4DE190, 'Pollution'), 0x0C: (0x5C4DE1A8, 'Crime')}

def order_buttons(lines):
    """The Maxis graph buttons (IDs 0x4000 + instance) in their cells of graph_layout.py; buttons of
    hidden graphs removed; captions of changed graphs replaced."""
    out, placed = [], set()
    for line in lines:
        m = re.search(r'id=0x0000(4[0-9a-f]{3})', line)
        if m and 'clsid=GZWinBtn' in line:
            instance = int(m.group(1), 16) - 0x4000
            if instance in HIDDEN:
                placed.add(instance)
                continue
            if instance in CAPTIONS:
                ltext, text = CAPTIONS[instance]
                line = re.sub(r'caption="[^"]*" captionres=\{[^}]*\}', 'caption="%s" captionres={6a231eaa,%08x}' % (text, ltext), line)
                assert '%08x' % ltext in line
            line = place(line, instance)
            placed.add(instance)
        out.append(line)
    assert len(placed) == 18, sorted(placed)
    return add_buttons(out)

def add_buttons(lines):
    """A button in the UI for every CAM graph of graph_layout.py that the Maxis UI lacks (a copy of the Crime button,
    ID 0x4000 + instance, caption = the graph's title). Buttons the game loads with the UI work like the Maxis ones;
    in the first test, buttons that SC4SIM.dll created at run time from the template did not switch the graph and
    stayed checked. The DLL still creates buttons for graphs of other plugins."""
    crime = next(i for i, l in enumerate(lines) if 'id=0x0000400c' in l and 'clsid=GZWinBtn' in l)
    last = max(i for i, l in enumerate(lines) if re.search(r'id=0x00004[0-9a-f]{3}', l) and 'clsid=GZWinBtn' in l)
    present = {int(m.group(1), 16) - 0x4000 for l in lines
               for m in [re.search(r'id=0x0000(4[0-9a-f]{3})', l)] if m and 'clsid=GZWinBtn' in l}
    new = []
    for instance in sorted(LAYOUT):
        if instance in present:
            continue
        _, _, title, text, _, _ = NEW_GRAPHS[instance]
        line = lines[crime].replace('id=0x0000400c', 'id=0x%08x' % (0x4000 + instance))
        line = re.sub(r'caption="[^"]*" captionres=\{[^}]*\}', 'caption="%s" captionres={6a231eaa,%08x}' % (text, title), line)
        assert '%08x' % title in line
        new.append(place(line, instance))
    return lines[:last + 1] + new + lines[last + 1:]

def place(line, instance):
    """The button in its cell: the row within its page; buttons of the pages after the first start hidden."""
    column, row = LAYOUT[instance]
    page, row = divmod(row, PAGE_ROWS)
    left, right = COLUMN_X[column]
    line = set_area(line, [left, 1 + 18 * row, right, 19 + 18 * row])
    if page > 0:
        assert 'winflag_visible=yes' in line
        line = line.replace('winflag_visible=yes', 'winflag_visible=no', 1)
    return line

def edit_panel(lines, h):
    """Radio-button panel: background image of the container stops at its bottom (no rows of the image's lower
    border), and a hidden filler FlatRect is inserted behind the container."""
    out, done = [], False
    for line in lines:
        s = line.strip()
        if not done and s.startswith('<LEGACY') and 'id=0xaa9244b6' in s:
            assert 'image={46a006b0,ebbeae28} imagerect=(6,332,509,448)' in line and 'winflag_sizeable=no' in line
            line = line.replace('image={46a006b0,ebbeae28} imagerect=(6,332,509,448)',
                                'image={46a006b0,%08x} imagerect=(6,%d,%d,%d)' % (IMG_TALL, 332 + h, 509 + WIDTH, 439 + h))
            _, a = area(line)
            assert a == [6, 4, 505 + WIDTH, 111], a
            line = line.replace('winflag_sizeable=no', 'winflag_sizeable=yes')
            indent = line[:len(line) - len(line.lstrip())]
            out.append(indent + filler_line(lines))
            out.extend(indent + nav for nav in nav_lines(lines))
            done = True
        out.append(line)
    assert done
    return '\r\n'.join(out)

def filler_line(lines):
    """The filler: a copy of the graph frame FlatRect of the Maxis UI, hidden, blue, zero height."""
    frame = next(l for l in lines if 'clsid=GZWinFlatRect' in l and 'area=(9,32,%d,' % (507 + WIDTH) in l).strip()
    frame = frame.replace('clsid=GZWinFlatRect iid=IGZWinFlatRect ', 'clsid=GZWinFlatRect iid=IGZWinFlatRect id=0x%08x ' % FILLER_ID, 1)
    frame = re.sub(r'area=\([^)]*\)', 'area=(6,111,%d,111)' % (505 + WIDTH), frame, count=1)
    blue = '(%d,%d,%d)' % PANEL_BLUE
    frame = re.sub(r'(fillcolor|colorleft|colortop|colorright|colorbottom)=\([^)]*\)', lambda m: m.group(1) + '=' + blue, frame)
    frame = frame.replace('winflag_visible=yes', 'winflag_visible=no').replace('winflag_ignoremouse=no', 'winflag_ignoremouse=yes')
    assert 'winflag_visible=no' in frame and frame.endswith('>')
    return frame

# Button of the Maxis UIs used for the navigation buttons (flat text button, 18 pixels high).
NAV_BUTTON = ('<LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x%08x area=(%d,%d,%d,%d) fillcolor=(156,178,194) caption="%s" '
    'winflag_visible=no winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no '
    'winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=yes '
    'winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,14416242} font=GenBodyLight colorfontnormal=(63,73,103) '
    'colorfontdisabled=(102,102,102) colorfonthilited=(255,255,255) colorfontnormalbkg=(153,187,204) '
    'colorfontdisabledbkg=(153,187,204) colorfonthilitedbkg=(153,187,204) toggle=off triggerondown=off showcaption=yes '
    'fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard '
    'gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,8a5c321b} >')

def nav_lines(lines):
    """Previous / page number / Next under the buttons, hidden: SC4SIM.dll shows them when there is more than one
    page and handles their clicks (Graph pages, docs/graph-window.md)."""
    text = next(l for l in lines if 'id=0x8a2871d8' in l).strip()          # the "Time Scale" text of the Maxis UI
    text = text.replace('id=0x8a2871d8', 'id=0x%08x' % NAV_LABEL)
    text = re.sub(r'caption="[^"]*" captionres=\{[^}]*\}', 'caption="Page 1 / %d"' % PAGES, text)
    right = 505 + WIDTH                                                     # right edge of the button container
    text = re.sub(r'area=\([^)]*\)', 'area=(%d,%d,%d,%d)' % (110, NAV_TOP, right - 104, NAV_TOP + 18), text, count=1)
    text = text.replace('align=rightcenter', 'align=center').replace('winflag_visible=yes', 'winflag_visible=no', 1)
    assert 'align=center' in text and 'winflag_visible=no' in text
    width = 100
    return [NAV_BUTTON % (NAV_PREV, 8, NAV_TOP, 8 + width, NAV_TOP + 18, '< Previous'),
            text,
            NAV_BUTTON % (NAV_NEXT, right - width, NAV_TOP, right, NAV_TOP + 18, 'Next >')]

def button_template(text):
    """The Maxis Crime button (0x0000400C) as a one-window UI: ID 0x5C4DE301, no caption, at the origin."""
    line = next(l for l in text.split('\r\n') if 'id=0x0000400c' in l).strip()
    line = line.replace('id=0x0000400c', 'id=0x%08x' % BUTTON_TEMPLATE_ID)
    line = re.sub(r'area=\([^)]*\)', 'area=(0,0,%d,18)' % COLUMN_X[0][1], line)
    line = line.replace('caption="Crime" ', 'caption="" ')
    line = re.sub(r'captionres=\{[^}]*\} ', '', line)
    assert 'style=radiocheck' in line and 'captionres' not in line
    return ('# SC4SIM graph button template (copy of the Maxis graph button)\r\n' + line + '\r\n').encode('latin1')

def wide_image(im):
    """WIDTH copies of column SPLIT_COLUMN inserted there."""
    w, ht = im.size
    new = Image.new('RGBA', (w + WIDTH, ht))
    new.paste(im.crop((0, 0, SPLIT_COLUMN, ht)), (0, 0))
    column = im.crop((SPLIT_COLUMN, 0, SPLIT_COLUMN + 1, ht))
    for x in range(WIDTH):
        new.paste(column, (SPLIT_COLUMN + x, 0))
    new.paste(im.crop((SPLIT_COLUMN, 0, w, ht)), (SPLIT_COLUMN + WIDTH, 0))
    return new

def taller_image(im, row):
    """EXTRA copies of the given row inserted there."""
    w, ht = im.size
    new = Image.new('RGBA', (w, ht + EXTRA))
    new.paste(im.crop((0, 0, w, row)), (0, 0))
    line = im.crop((0, row, w, row + 1))
    for y in range(EXTRA):
        new.paste(line, (0, row + y))
    new.paste(im.crop((0, row, w, ht)), (0, row + EXTRA))
    return new

def collapsed_image(png):
    im = taller_image(Image.open(io.BytesIO(png)).convert('RGBA'), COLLAPSED_PANEL_ROW)
    buf = io.BytesIO(); wide_image(im).save(buf, 'PNG')
    return buf.getvalue()

def tall_image(png, h):
    im = Image.open(io.BytesIO(png)).convert('RGBA')
    w, ht = im.size
    new = Image.new('RGBA', (w, ht + h))
    new.paste(im.crop((0, 0, w, SPLIT_ROW)), (0, 0))
    row = im.crop((0, SPLIT_ROW, w, SPLIT_ROW + 1))
    for y in range(h):
        new.paste(row, (0, SPLIT_ROW + y))
    new.paste(im.crop((0, SPLIT_ROW, w, ht)), (0, SPLIT_ROW + h))
    buf = io.BytesIO(); wide_image(taller_image(new, PANEL_ROW + h)).save(buf, 'PNG')
    return buf.getvalue()

def main(game, cam_ui, out_dir, h='120'):
    h = int(h)
    _, game_entries = dbpf2.read(game)
    ui = find(game_entries, UI, UI_GROUP, UI_GRAPHS).decode('latin1')
    png = find(game_entries, PNG, IMG_GROUP, IMG_BACKGROUND)
    png_collapsed = find(game_entries, PNG, IMG_GROUP, IMG_COLLAPSED_SRC)
    raw, entries = dbpf2.read(cam_ui)
    keys = {(UI, UI_GROUP, UI_GRAPHS), (PNG, IMG_GROUP, IMG_TALL), (UI, UI_GROUP, UI_BUTTON_TEMPLATE),
            (PNG, IMG_GROUP, IMG_COLLAPSED)}
    entries = [e for e in entries if (e['t'], e['g'], e['i']) not in keys]
    entries.append(dict(t=UI, g=UI_GROUP, i=UI_GRAPHS, data=edit_ui(ui, h).encode('latin1')))
    entries.append(dict(t=PNG, g=IMG_GROUP, i=IMG_TALL, data=tall_image(png, h)))
    entries.append(dict(t=PNG, g=IMG_GROUP, i=IMG_COLLAPSED, data=collapsed_image(png_collapsed)))
    entries.append(dict(t=UI, g=UI_GROUP, i=UI_BUTTON_TEMPLATE, data=button_template(ui)))
    os.makedirs(out_dir, exist_ok=True)
    dbpf2.write(os.path.join(out_dir, 'CAM_UI.dat'), entries, header_src=raw)

if __name__ == '__main__':
    main(*sys.argv[1:])
