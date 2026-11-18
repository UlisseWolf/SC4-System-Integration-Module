# Water Grid graph (1.13.0)

> **Not yet tested in game.**

A new graph of the Graphs window, **Water Grid** (`0x6534284A / 0xCA4AD545 / 0x0000001B`, in
`CAM_Exemplar_Graphs.dat`; button added by SC4SIM.dll at column 2, row 7, under Power Grid). It shows the state of
the water network.

| # | Line | History type | Source |
|---|---|---|---|
| 1 | Water Demand | `0x5C4DE064` | SC4SIM.dll: `cISC4PlumbingSimulator::GetWaterDemand` |
| 2 | Water Surplus | `0x5C4DE065` | SC4SIM.dll: production capacity − consumption, never below 0 |
| 3 | Watered Buildings | `0xCA78A955` (`G_WATERED_BUILDING_COUNT`) | game |
| 4 | Unwatered Buildings | `0x8A78AB06` (`G_UNWATERED_BUILDING_COUNT`) | game |
| 5 | Water Pipes | `0xCA14EBFE` (`G_WATER_PIPES_COUNT`) | game |
| 6 | Distressed Pipes | `0xCA14EC19` (`G_WATER_PIPES_COUNT_DISTRESSED`) | game |
| 7 | Burst Pipes | `0x5C4DE061` | SC4SIM.dll: `GetNumBurstPipes` |
| 8 | Water Sources | `0x5C4DE062` | SC4SIM.dll: `GetNumWaterSources` |
| 9 | Polluted Sources | `0x5C4DE063` | SC4SIM.dll: `GetNumPollutedWaterSources` |

Lines 1-2 are amounts of water per month, lines 3-9 are counts (buildings, pipe tiles, water producers). The game
lines (3-6) work without the DLL; the others are recorded by `src/water/WaterGraphHistory.cpp` on every SimNewMonth,
with `[WaterGraph] Enabled` in `SC4SIM.ini`, from the first month after installing.
