# Power graph by energy class (1.13.0)

> **Not yet tested in game.**

The Power graph of the Graphs window (graph exemplar `0x6534284A / 0xCA4AD545 / 0x00000006`, in
`a_CAM/1 CAM Core/CAM_Exemplar_Graphs.dat`) shows 18 lines:

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
| 16 | Biomass | `0x5C4DE00C` | SC4SIM.dll |
| 17 | Generators | `0x5C4DE00D` | SC4SIM.dll |
| 18 | Waste to Energy | `0x5C4DE00E` | SC4SIM.dll |

Lines 1-4 are the CAM graph as it was; lines 3 and 4 now have their own labels (`0x5C4DE10C` "Energy Bought",
`0x5C4DE10D` "Energy Sold") instead of the "Imported" label shared with the Water graph and a missing one. The labels
are LTEXT `0x2026960B / 0x6A231EAA / 0x5C4DE101-0x5C4DE110` in `CAM Locale/CAM_Locale_en.dat`.

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
   Hydroelectric `0x0A8B2E1C`, Tidal and Wave `0x951653B7`, Microwave `0x03D5C8FD`, Biomass `0x9BE9788B`,
   Electric Generator `0xEA9F7384`, Waste To Energy `0xC291E217`. Power Storage and the transformers are not plotted.
2. Without it, **Power Plant Type** (`0x27812853`): 1 Coal, 2 Hydrogen (= Nuclear Fusion, the Maxis fusion plant),
   3 Natural Gas, 4 Microwave, 5 Nuclear Fission, 6 Oil, 7 Solar, 8 Waste to Energy, 9 Wind. 10 (Auxiliary) is not
   plotted. Biomass plants (type 6) and generators (type 3) without ExtendedPowerType count as oil and natural gas.

## Notes

- The class lines start at the first month after installing 1.13.0 (or after turning `[PowerGraph] Enabled` on);
  earlier months are empty.
- Without SC4SIM.dll 1.13.0 the class lines stay at zero; lines 1-4 work with the game alone.
- `[PowerGraph] Enabled=false` stops the recording; points already saved in the city stay.
- With `[SC4PowerPoleCustomization] DebugLog=true` the monthly totals are written to `SC4SIM.log` (`PowerGraph:`).

## Taller Graphs window

The game lays the legend out one entry under the other, from the height of the graph area (`0x8A4B0AA0`, legend
code from `0x0076E0F0`), and cuts the entries that do not fit: the vanilla window shows about 11 lines. CAM_UI.dat
therefore overrides the Graphs window UI (`0x00000000 / 0x96A006B0 / 0x6BC9065A`, loaded at `0x007EEB9E`): the
expanded graph window (`0x8A8B5B71`) grows 100 pixels upward (180 before the 10 button rows of 1.13.0; the 18
lines of the Power legend need 94), the graph area, its frame, the Time Scale texts and the
slider keep their distance from the bottom, and the background is a taller copy of the Maxis image
(`0x856DDBAC / 0x46A006B0 / 0x5C4DE200`). The radio-button panel and the collapsed window are unchanged. Every graph
gets the taller area.

`tools/power-graph/make_graph_ui.py <SimCity_1.dat> <CAM_UI.dat> <out_dir> [extra_height]` builds it from the game's
own UI file.

## Wider Graphs window and legend (1.13.0)

> **Not yet tested in game.**

The legend has a fixed width: the line graph (layout at `0x0076DAF1`) ends the plot at W - 110 (W = width of the
graph area), puts each colour swatch at W - 108 .. W - 92 and starts the text at W - 90 (W - 106 with 1 or 2 plots);
the text window ends at W - 4. A legend text gets 72 pixels, so *Nuclear Fission* and *Nuclear Fusion* wrap to two
lines, and widening the window alone would only widen the plot.

- **SC4SIM.dll** (`src/ui/GraphLegendPatch.cpp`) moves the five offsets (`0x0076DD4E` plot end, `0x0076E0F5` and
  `0x0076E1F8` text start, `0x0076E159` / `0x0076E162` swatch) 40 pixels further left (`[GraphWindow]
  LegendExtraWidth`, 0 = off): legend texts get 112 pixels. The offsets are 8-bit immediates (at most 17 more
  pixels), so each site jumps to a small block of the DLL that runs the same instructions with 32-bit offsets and
  jumps back. All five sites are checked before patching and restored when the game closes.
- **CAM_UI.dat** (`make_graph_ui.py`, `WIDTH` = 40) makes the expanded and collapsed graph windows and the button
  panel 40 pixels wider, to the left: the graph area and its frame, the title bars and the button container widen,
  the close buttons move right, and both backgrounds are widened copies of the Maxis images (a middle column
  repeated; expanded `0x5C4DE200`, collapsed `0x5C4DE201`). So the plot keeps its vanilla 333 pixels. The three
  button columns become 176 pixels wide (0-176, 180-356, 360-536); SC4SIM.dll takes the column positions from the
  Maxis buttons of the UI, so it follows whatever width the UI has.

Each half works without the other: without the DLL the plot is 40 pixels wider and the legend stays narrow; with the
vanilla UI the legend is wider and the plot 40 pixels narrower.

## Data files

Since 1.13.0 every graph exemplar of the Graphs window is in `a_CAM/1 CAM Core/CAM_Exemplar_Graphs.dat`: the 18 of
the game (`0x6534284A / 0xCA4AD545 / 0x01`-`0x13`, `0x18`), with CAM's versions of Power, Water, Garbage and Demands.
`CAM_Exemplars.dat` no longer holds graphs. `tools/power-graph/make_graph_exemplars.py <SimCity_1.dat>
<CAM_Exemplars.dat> <out_dir>` builds both files; then `make_utility_graphs.py` revises Power, Water and Garbage.

`tools/power-graph/make_utility_graphs.py <CAM_Exemplar_Graphs.dat> <CAM_Locale_en.dat> <out_dir>` rebuilds the Power and
Water graph exemplars (every other entry and property unchanged) and adds their labels. The Water graph is described
in `docs/water-graph.md`.
