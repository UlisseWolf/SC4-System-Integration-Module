"""Builds R+C mixed copies of the MN Mediterranean building exemplars for SC4SIM MixedOccupancy.

Rule (SHARE = 0.25): each building keeps its own capacities and gains ONE secondary entry at its own wealth:
  * R building  -> CS (or CO when the same model ships as CO) = 25% of the commercial version's capacity;
  * CS/CO bldg  -> R = 25% of the residential version's capacity;
  * a model without the other version uses the pack's average ratio (CS = 0.6 x R, CO = 1.0 x R).
Every exemplar also gets Allow Joint Occupancy (0x27812836) = True.
"""
import os, re, sys, glob, shutil, json
sys.path.insert(0, os.path.dirname(__file__))
import dbpf

SHARE = 0.25
RATIO = {0x3100: 0.6, 0x3300: 1.0}   # commercial / residential capacity of the same model

def parse(txt):
    def prop(pid):
        m = re.search(r'^0x%08x:\{"[^"]*"\}=[^:]+:\d+:\{([^}]*)\}' % pid, txt, re.M | re.I)
        return m.group(1) if m else None
    name = re.search(r'\{"Exemplar Name"\}=String:1:\{"([^"]+)"\}', txt).group(1)
    purpose = int(prop(0x27812833), 16); wealth = int(prop(0x27812832), 16)
    vals = [int(v, 16) for v in prop(0x27812834).split(',')]
    caps = dict(zip(vals[0::2], vals[1::2]))
    return name, purpose, wealth, caps

def model_key(name):
    return re.sub(r'^(CS|CO) ', '', name)

def main(src, dst):
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    files = sorted(glob.glob(dst + '/**/*.SC4Desc', recursive=True))
    info = {}
    for p in files:
        _, es = dbpf.read(p)
        info[p] = parse(es[0]['data'].decode('latin1'))
    by_model = {}
    for p, (name, purpose, wealth, caps) in info.items():
        by_model.setdefault(model_key(name), {})['R' if purpose == 1 else 'C'] = (name, purpose, wealth, caps)

    report = []
    for p in files:
        name, purpose, wealth, caps = info[p]
        other = by_model[model_key(name)].get('C' if purpose == 1 else 'R')
        if purpose == 1:   # residential primary -> commercial secondary
            base = 0x3300 if other and other[1] == 3 else 0x3100
            sec = base + wealth * 0x10
            if other:
                oc = other[3]
                equiv = oc.get(sec) or oc[max(oc)]
                source = other[0]
            else:
                equiv = caps[0x1000 + wealth * 0x10] * RATIO[base]
                source = 'estimate %.1f x R' % RATIO[base]
        else:              # commercial primary -> residential secondary
            sec = 0x1000 + wealth * 0x10
            base = 0x3300 if purpose == 3 else 0x3100
            if other:
                equiv = other[3].get(sec) or other[3][max(other[3])]
                source = other[0]
            else:
                equiv = caps[base + wealth * 0x10] / RATIO[base]
                source = 'estimate C / %.1f' % RATIO[base]
        capacity = max(1, round(equiv * SHARE))
        assert sec not in caps

        d, es = dbpf.read(p)
        txt = es[0]['data'].decode('latin1')
        line = re.search(r'^0x27812834:\{"Capacity Satisfied"\}=Uint32:(\d+):\{([^}]*)\}\r\n', txt, re.M)
        n = int(line.group(1)) + 2
        new_caps = line.group(2) + ',0x%08x,0x%08x' % (sec, capacity)
        new = '0x27812834:{"Capacity Satisfied"}=Uint32:%d:{%s}\r\n' % (n, new_caps)
        new += '0x27812836:{"Allow Joint Occupancy"}=Bool:0:{True}\r\n'
        assert '0x27812836' not in txt.lower()
        txt = txt[:line.start()] + new + txt[line.end():]
        count = re.search(r'PropCount=0x([0-9A-Fa-f]+)', txt)
        txt = txt.replace(count.group(0), 'PropCount=0x%08X' % (int(count.group(1), 16) + 1), 1)
        es[0]['data'] = txt.encode('latin1')
        dbpf.write(p, es, d)
        report.append(dict(file=os.path.relpath(p, dst), name=name, purpose={1: 'R', 2: 'CS', 3: 'CO'}[purpose],
                           wealth='$' * wealth, primary=caps.get((0x1000 if purpose == 1 else base) + wealth * 0x10),
                           secondary='0x%04X' % sec, secondary_capacity=capacity, source=source))
    return report

if __name__ == '__main__':
    rep = main(sys.argv[1], sys.argv[2])
    json.dump(rep, open(sys.argv[3], 'w'), indent=1)
    for r in rep:
        print('%-26s %-3s %-4s prim %4s  + %s %4d   (%s)' % (r['name'], r['purpose'], r['wealth'], r['primary'], r['secondary'], r['secondary_capacity'], r['source']))
