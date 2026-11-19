#!/usr/bin/env python3
# Puts lua/deathcare_advice.lua into CAM_LUA_Controller.dat and the deathcare advice texts (DEATHCARE_TEXTS of
# tools/afterlife-news/build_news.py) into CAM_Locale_en.dat, replacing older copies.
#
#   python tools/deathcare-advice/install_advice.py CAM_LUA_Controller.dat CAM_Locale_en.dat <out dir>
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'afterlife-news'))
import dbpf2
from build_news import DEATHCARE_TEXTS, ltext, LUA_TYPE, LUA_GROUP, LTEXT_TYPE, LTEXT_GROUP

ADVICE_LUA_INSTANCE = 0x8BB3C400   # package signature 0BB3C400, high bit set as the pack does

def main(controller, locale, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    header, entries = dbpf2.read(controller)
    entries = [e for e in entries if not (e['t'] == LUA_TYPE and e['i'] == ADVICE_LUA_INSTANCE)]
    lua = open(os.path.join(ROOT, 'lua', 'deathcare_advice.lua'), encoding='utf-8').read()
    entries.append(dict(t=LUA_TYPE, g=LUA_GROUP, i=ADVICE_LUA_INSTANCE, data=lua.encode('utf-8')))
    dbpf2.write(os.path.join(out_dir, os.path.basename(controller)), entries, header)

    header, entries = dbpf2.read(locale)
    keys = {(LTEXT_TYPE, LTEXT_GROUP, i) for i, _ in DEATHCARE_TEXTS}
    entries = [e for e in entries if (e['t'], e['g'], e['i']) not in keys]
    entries += [dict(t=LTEXT_TYPE, g=LTEXT_GROUP, i=i, data=ltext(t)) for i, t in DEATHCARE_TEXTS]
    dbpf2.write(os.path.join(out_dir, os.path.basename(locale)), entries, header)

if __name__ == '__main__':
    main(*sys.argv[1:4])
