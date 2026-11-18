# Power graph by energy class (1.13.0)

> **Not yet tested in game.**

The Power graph of the Graphs window (graph exemplar `0x6534284A / 0xCA4AD545 / 0x00000006`, in
`a_CAM/1 CAM Core/CAM_Exemplars.dat`) shows 15 lines:

| # | Line | History type | Recorded by |
|---|---|---|---|
| 1 | Production (+ energy bought) | `0xAA11F7CE` + `0xAA11F837` | game |
| 2 | Consumption (+ energy sold) | `0xAA11F7D2` + `0xAA11F83A` | game |
| 3 | Energy Bought | `0xAA11F837` (`G_POWER_IMPORTED`) | game (neighbour deals) |
| 4 | Energy Sold | `0xAA11F83A` (`G_POWER_EXPORTED`) | game (neighbour deals) |
| 5 | Coal | `0x5C4DE001` | SC4SIM.dll |
| 6 | Oil | `0x5C4DE002` | SC4SIM.dll |
| 7 | Natural Gas | `0x5C4DE003` | SC4SIM.dll |
| 8 | Nuclear Fission | `0x5C4DE004` | SC4SIM.dll |
| 9 | Nuclear Fusion | `0x5C4DE005` | SC4SIM.dll |
| 10 | Solar | `0x5C4DE006` | SC4SIM.dll |
| 11 | Wind | `0x5C4DE007` | SC4SIM.dll |
| 12 | Geothermal | `0x5C4DE008` | SC4SIM.dll |
| 13 | Hydroelectric | `0x5C4DE009` | SC4SIM.dll |
| 14 | Tidal | `0x5C4DE00A` | SC4SIM.dll |
| 15 | Microwave | `0x5C4DE00B` | SC4SIM.dll |

Lines 1-4 are the CAM graph as it was; lines 3 and 4 now have their own labels (`0x5C4DE10C` "Energy Bought",
`0x5C4DE10D` "Energy Sold") instead of the "Imported" label shared with the Water graph and a missing one. The labels
are LTEXT `0x2026960B / 0x6A231EAA / 0x5C4DE101-0x5C4DE10D` in `CAM Locale/CAM_Locale_en.dat`.

## How the lines are recorded

Graphs read their points from the city's history warehouse (`cISC4HistoryWarehouse`, saved with the city). Each plot
of a graph exemplar names a history type in *Graph Plot data source 1* (`0x6A4AEE40`). The warehouse creates an entry
for any type the first time a value is recorded (`RecordHistoryData`, vtable `+0x30`, `0x00475AA0`; handler
`0x00476EE0`, find-or-create `0x00476E40`), so the new types need no registration in the game.

The game's own consumption by Power Plant Type (`0xAA120D5E`-`0xAA120D6D`, `G_POWER_CONSUMED_COAL` and following)
cannot be used: the power simulator keeps only two points of each (`SetHistoryDataQueueLength(type, 2)` at
`0x006F050A`), and the vanilla types cannot tell geothermal and hydroelectric plants (type 3, natural gas) or tidal
plants (type 9, wind) apart.

`src/power/PowerGraphHistory.cpp` records one point per class on every SimNewMonth (`0x66956816`): the sum of the
current output of the power plants of that class, read with `cISC4PowerSimulator::GetOccupantPowerProduced` (vtable
slot 33, `0x006EB420`: Power Generated × the plant's efficiency). Power plants are the building occupants of group
`0x1400` (Building_Power) that have Power Generated (`0x27812852`).

### Class of a plant

1. **ExtendedPowerType** (`0x713C8912`, New_Properties.xml, set by the PIM-X generators):
   Coal `0xB0C1D6F9`, Oil `0x45DEA4F8`, Natural Gas `0x5D4CE371`, Nuclear Fission `0xD8B65889`,
   Nuclear Fusion `0xF5DAF839`, Solar `0xA934E93E`, Wind `0x9E43F5C9`, Geothermal `0xA4FFC9DA`,
   Hydroelectric `0x0A8B2E1C`, Tidal and Wave `0x951653B7`, Microwave `0x03D5C8FD`. Biomass, Waste To Energy,
   Electric Generator, Power Storage and the transformers are not plotted.
2. Without it, **Power Plant Type** (`0x27812853`): 1 Coal, 2 Hydrogen (= Nuclear Fusion, the Maxis fusion plant),
   3 Natural Gas, 4 Microwave, 5 Nuclear Fission, 6 Oil, 7 Solar, 9 Wind. 8 (Waste To Energy) and 10 (Auxiliary) are
   not plotted.

## Notes

- The class lines start at the first month after installing 1.13.0 (or after turning `[PowerGraph] Enabled` on);
  earlier months are empty.
- Without SC4SIM.dll 1.13.0 the class lines stay at zero; lines 1-4 work with the game alone.
- `[PowerGraph] Enabled=false` stops the recording; points already saved in the city stay.
- With `[SC4PowerPoleCustomization] DebugLog=true` the monthly totals are written to `SC4SIM.log` (`PowerGraph:`).

## Data files

`tools/power-graph/make_power_graph.py <CAM_Exemplars.dat> <CAM_Locale_en.dat> <out_dir>` rebuilds the graph
exemplar (every other entry and property unchanged) and adds the 13 labels.
