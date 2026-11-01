# Afterlife equivalence test

Compares `src/afterlife/AfterlifeModel.cpp` with the last Lua version of the engine. `AFTERLIFE.lua` and
`PACK_CONSTANTS.lua` are no longer in the pack; they are kept in `reference/` as test fixtures only. The roster and
event tables of `reference/PACK_CONSTANTS.lua` are rewritten by `tools/afterlife-news/build_news.py`.

```sh
pip install lupa
g++ -std=c++20 -I src/afterlife tools/afterlife-test/harness.cpp src/afterlife/AfterlifeModel.cpp -o harness
python3 tools/afterlife-test/compare_with_lua.py tools/afterlife-test/reference/AFTERLIFE.lua tools/afterlife-test/reference/PACK_CONSTANTS.lua
```

Expected output (the script exits with 1 on any mismatch; `./harness` must be in the current folder):

```
random constants   - cycles compared: 12000 mismatches: 0
defaults           - constants compared: 23 mismatches: 0
news roster        - characters: 60 events: 7 match: True
ticker picks       - compared: 24 mismatches: 0
PACK_CONSTANTS run - cycles compared: 40000 mismatches: 0
```

Without `PACK_CONSTANTS.lua` only the first line runs. Random numbers are replaced by the same fixed values on both
sides, with `math.random` truncating its arguments as Lua 5.0 does. `dofile` and `hex2dec` are stubbed.
