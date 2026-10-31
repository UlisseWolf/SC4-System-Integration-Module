# Afterlife equivalence test

Compares `src/afterlife/AfterlifeModel.cpp` with the original `AFTERLIFE.lua` over random inputs and constants
(300 sequences of 40 cycles, 24 state values each).

```sh
pip install lupa
g++ -std=c++20 -I src/afterlife tools/afterlife-test/harness.cpp src/afterlife/AfterlifeModel.cpp -o harness
python3 tools/afterlife-test/compare_with_lua.py path/to/AFTERLIFE.lua   # run from the folder holding ./harness
```

Expected output: `cycles compared: 12000 mismatches: 0`. Random numbers are replaced by the same fixed values on both
sides, with `math.random` truncating its arguments as Lua 5.0 does.
