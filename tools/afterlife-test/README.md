# Afterlife equivalence test

Compares `src/afterlife/AfterlifeModel.cpp` with the original `AFTERLIFE.lua` and, optionally, `PACK_CONSTANTS.lua`.

```sh
pip install lupa
g++ -std=c++20 -I src/afterlife tools/afterlife-test/harness.cpp src/afterlife/AfterlifeModel.cpp -o harness
python3 tools/afterlife-test/compare_with_lua.py path/to/AFTERLIFE.lua path/to/PACK_CONSTANTS.lua   # from the folder holding ./harness
```

Expected output (the script exits with 1 on any mismatch):

```
random constants   - cycles compared: 12000 mismatches: 0
defaults           - constants compared: 23 mismatches: 0
news roster        - characters: 58 events: 5 match: True
ticker picks       - compared: 18 mismatches: 0
PACK_CONSTANTS run - cycles compared: 40000 mismatches: 0
```

Without `PACK_CONSTANTS.lua` only the first line runs. Random numbers are replaced by the same fixed values on both
sides, with `math.random` truncating its arguments as Lua 5.0 does. `dofile` and `hex2dec` are stubbed.
