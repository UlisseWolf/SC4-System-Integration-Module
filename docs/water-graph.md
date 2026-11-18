# Water graph by source and wastewater (1.13.0)

> **Not yet tested in game.**

The Water graph of the Graphs window (graph exemplar `0x6534284A / 0xCA4AD545 / 0x00000007`, in
`a_CAM/1 CAM Core/CAM_Exemplars.dat`) shows 10 lines:

| # | Line | History type | Recorded by |
|---|---|---|---|
| 1 | Production (+ water bought) | `0x0A14EBB4` + `0x0A14EBB5` | game |
| 2 | Consumption (+ water sold) | `0xCA14EBD0` + `0xCA14EBC6` | game |
| 3 | Water Bought | `0x0A14EBB5` (`G_WATER_IMPORTED`) | game (neighbour deals) |
| 4 | Water Sold | `0xCA14EBC6` (`G_WATER_EXPORTED`) | game (neighbour deals) |
| 5 | Aquifer | `0x5C4DE021` | SC4SIM.dll |
| 6 | Fresh Water | `0x5C4DE022` | SC4SIM.dll |
| 7 | Salt Water | `0x5C4DE023` | SC4SIM.dll |
| 8 | Wastewater Produced | `0x5C4DE024` | SC4SIM.dll |
| 9 | Wastewater Treated | `0x5C4DE025` | SC4SIM.dll |
| 10 | Recycled to Fresh Water | `0x5C4DE026` | SC4SIM.dll |

Labels: LTEXT `0x2026960B / 0x6A231EAA / 0x5C4DE121-0x5C4DE128` in `CAM Locale/CAM_Locale_en.dat`. Lines 3 and 4 used
the labels of the Power graph ("Imported" and a missing one) and now have their own.

## How the lines are computed

`src/water/WaterGraphHistory.cpp` records one point per line on every SimNewMonth, in the city's history warehouse
like the power graph (see `docs/power-graph.md`). It looks at the water buildings (building occupants of group
`0x1401`):

- **Aquifer, Fresh Water, Salt Water**: current output of the water producers (Water Produced `0x88ED3303` > 0),
  read with `cISC4PlumbingSimulator::GetWaterProduction`, by their Water Source (`0x48F23A7E`, Advanced Water
  Management / New_Properties.xml: 0 fresh water pump, 1 desalination plant, 2 water tower or aquifer pump). A
  producer without Water Source is a vanilla aquifer pump.
- **Wastewater Produced**: the game has no wastewater; the module counts the water the city consumes
  (`GetWaterConsumed`) as wastewater.
- **Wastewater Treated**: what the treatment plants (groups `0x1401` + `0x1402`, the New_Properties.xml Treatment
  Plant generator and the Maxis purification plants) can treat, never more than the wastewater produced. Capacity of
  a plant: Water Treatment monthly capacity (`0xC8EBD131`); when it is 0, as in the generated plants, the
  generator's WaterOutput, estimated from Water Consumed (`0xC8ED2D84` = 10 × int(1 + WaterOutput / 2000), so
  WaterOutput ≈ 2000 × Water Consumed / 10 − 1000).
- **Recycled to Fresh Water**: output of the sewage plants (AWM group `0x280C60E3`, the Sewage Sanitizer generator),
  never more than the wastewater produced. Sewage plants are not counted in the three water sources.

## Notes

- The new lines start at the first month after installing 1.13.0 (or after turning `[WaterGraph] Enabled` on).
- Without SC4SIM.dll 1.13.0 lines 5-10 stay at zero; lines 1-4 work with the game alone.
- With `[SC4PowerPoleCustomization] DebugLog=true` the monthly totals are written to `SC4SIM.log` (`WaterGraph:`).
- Data files: `tools/power-graph/make_utility_graphs.py` rebuilds both graphs.
