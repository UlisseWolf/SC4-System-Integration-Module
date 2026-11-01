# Afterlife news generator

`roster.py` is the single source of the Afterlife news: 60 characters (key, name, role, show, species, LTEXT instance,
English ticker line) and the 7 event lists. Edit it, then run from the repository root:

```sh
python3 tools/afterlife-news/build_news.py --dat path/to/CAM_LUA_Controller.dat out/CAM_LUA_Controller.dat out/CAM_Afterlife_News_en.dat
```

It rewrites:

- the built-in roster of `src/afterlife/AfterlifeModel.cpp` (rebuild the DLL afterwards);
- `lua/afterlife_news.lua`, the news script;
- the roster tables of `tools/afterlife-test/reference/PACK_CONSTANTS.lua` (test fixture);
- with `--dat`: `CAM_LUA_Controller.dat` with the news script added as `CA63E2A3-4A5E8EF6-8BB3C100` (every other entry
  copied unchanged, written uncompressed), and `CAM_Afterlife_News_en.dat` with one LTEXT per character
  (`2026960B-6A231EAA-BB3C1000`...`BB3C1063`).

`LTEXT_GROUP` in `build_news.py` must match the group of the pack's other advisor strings: see `docs/afterlife.md`.
`dbpf2.py` reads DBPF 1.0 files, including QFS-compressed entries, and writes them uncompressed.
