# Makes the Graphs window taller so the legend of the Power graph (18 lines) fits.
#   python make_graph_ui.py <SimCity_1.dat> <CAM_UI.dat> <out_dir> [extra_height]
# Reads the Graphs window UI (0x00000000 / 0x96A006B0 / 0x6BC9065A) and its background image
# (PNG 0x856DDBAC / 0x46A006B0 / 0xEBBEAE28) from the game, grows the expanded graph window (0x8A8B5B71) upward by
# extra_height pixels (default 180) and writes <out_dir>/CAM_UI.dat = the input CAM_UI.dat plus the edited UI and a
# taller background image (PNG 0x856DDBAC / 0x46A006B0 / 0x5C4DE200). The radio-button panel and the collapsed
# window are unchanged; the graph area (0x8A4B0AA0), its frame, the Time Scale texts and the slider keep their
# distance from the bottom. The game lays the legend out from the graph area's height (0x0076E0F0 and following), so
# a taller area shows more legend lines. See docs/power-graph.md.
import io, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbpf2
from PIL import Image

UI, PNG = 0x00000000, 0x856DDBAC
UI_GROUP, UI_GRAPHS = 0x96A006B0, 0x6BC9065A
IMG_GROUP, IMG_BACKGROUND, IMG_TALL = 0x46A006B0, 0xEBBEAE28, 0x5C4DE200
SPLIT_ROW = 200   # background rows copied to grow the image (inside the light graph frame)

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
                    line = re.sub(r'imagerect=\(0,0,516,448\)', 'imagerect=(0,0,516,%d)' % (448 + h), line)
                    assert '%08x' % IMG_TALL in line and 'imagerect=(0,0,516,%d)' % (448 + h) in line
                elif depth == 1 and a[1] in (31, 32) and a[3] <= 330:            # frame, graph area, border: grow
                    a[3] += h; line = set_area(line, a)
                elif depth == 1 and a[1] >= 32:                             # below the graph: move down
                    a[1] += h; a[3] += h; line = set_area(line, a)
                elif depth == 2 and a[1] >= 200:                            # Time Scale texts in the frame
                    a[1] += h; a[3] += h; line = set_area(line, a)
        out.append(line)
    assert done
    return '\r\n'.join(out)

def tall_image(png, h):
    im = Image.open(io.BytesIO(png)).convert('RGBA')
    w, ht = im.size
    new = Image.new('RGBA', (w, ht + h))
    new.paste(im.crop((0, 0, w, SPLIT_ROW)), (0, 0))
    row = im.crop((0, SPLIT_ROW, w, SPLIT_ROW + 1))
    for y in range(h):
        new.paste(row, (0, SPLIT_ROW + y))
    new.paste(im.crop((0, SPLIT_ROW, w, ht)), (0, SPLIT_ROW + h))
    buf = io.BytesIO(); new.save(buf, 'PNG')
    return buf.getvalue()

def main(game, cam_ui, out_dir, h='180'):
    h = int(h)
    _, game_entries = dbpf2.read(game)
    ui = find(game_entries, UI, UI_GROUP, UI_GRAPHS).decode('latin1')
    png = find(game_entries, PNG, IMG_GROUP, IMG_BACKGROUND)
    raw, entries = dbpf2.read(cam_ui)
    keys = {(UI, UI_GROUP, UI_GRAPHS), (PNG, IMG_GROUP, IMG_TALL)}
    entries = [e for e in entries if (e['t'], e['g'], e['i']) not in keys]
    entries.append(dict(t=UI, g=UI_GROUP, i=UI_GRAPHS, data=edit_ui(ui, h).encode('latin1')))
    entries.append(dict(t=PNG, g=IMG_GROUP, i=IMG_TALL, data=tall_image(png, h)))
    os.makedirs(out_dir, exist_ok=True)
    dbpf2.write(os.path.join(out_dir, 'CAM_UI.dat'), entries, header_src=raw)

if __name__ == '__main__':
    main(*sys.argv[1:])
