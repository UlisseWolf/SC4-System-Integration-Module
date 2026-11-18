# Graph buttons driven by the graph exemplars (1.13.0)

> **Not yet tested in game.**

## The game already handles more graphs

Nothing in the game limits the graphs to the 18 of Maxis:

- a click on a window of ID `0x4000`-`0x4FFF` in the Graphs window selects graph `ID & 0xFF` (message handler,
  `0x0076E5D0`);
- the radio state follows window `0x4000 + index` (`0x0076B8B0`);
- the graph is loaded from exemplar `0x6534284A / 0xCA4AD545 / index` (`0x0076D3D0`), so its lines, colours, labels
  and title all come from the exemplar.

So a new graph is just a new exemplar in group `0xCA4AD545` with an instance from `0x01` to `0xFF` (the Maxis ones use
`0x01`-`0x13` and `0x18`; see `CAM_Exemplar_Graphs.dat`). What was missing is a button: the Graphs UI
(`0x00000000 / 0x96A006B0 / 0x6BC9065A`) has exactly 18.

## Buttons added at run time

`src/ui/GraphButtons.cpp` hooks the call that builds the Graphs UI (`0x007F4665`, `cSC4ControlPanel` →
`0x007EDEB0`, once per city view; the radio-button panel `0x0A4A8176` is kept at `this + 0x304`). After the game has
built it, the module:

1. looks for every graph exemplar `0x6534284A / 0xCA4AD545 / 0x01-0xFF` (`TestForKey`) and keeps only those with
   *Graph Button Cell* (below); the others are ignored and a button of the UI for them is hidden;
2. moves the buttons of the UI to their cell and creates each missing button from the template UI `0x00000000 / 0x96A006B0 / 0x5C4DE300` (a copy of the Maxis
   graph button, in `CAM_UI.dat`) with `cIGZUIScriptService::CreateWindowFromScript`, gives it ID
   `0x4000 + instance` and the graph's name (the LTEXT of *User Visible Name Key* `0x8A416A99`, else the exemplar
   name), and puts it in the cell the exemplar asks for;
3. places the buttons in the columns of the UI's Maxis buttons (CAM_UI.dat makes them 176 pixels wide, see the wider
   window in `docs/power-graph.md`) and makes the panel one 18-pixel row taller for every row past the 6 Maxis rows, upward so its bottom stays in place, and moves the
   two graph windows (`0x8A8B5B71` expanded, `0x8A8B5B72` collapsed) up by the same amount. The new rows are painted
   by the filler `0x5C4DE310` (hidden blue FlatRect in `CAM_UI.dat`, behind the button container, whose background
   image now ends at the container's bottom).

## Button position: Graph Button Cell

| ID | Property | Type | Meaning |
|---|---|---|---|
| `0x5C4DE350` | Graph Button Cell | Uint32 ×2 | Column (0-2, left to right) and row (from the top; rows 0-5 are the Maxis buttons, so the first free row is 6, up to 31) of the graph's button. |
| `0x5C4DE351` | Graph Button Hidden | Bool | The graph gets no button; a button of the UI for it is hidden and its cell is free for other graphs. Used for graphs merged into another one. |

The position lives in the graph exemplar itself: no `SC4SIM.ini` key is needed. Graphs with the property (lowest
instance first) take their cell; a button of the UI is moved there. A cell outside the grid or already taken is
logged in `SC4SIM.log` and the graph goes to the next free cell instead (row by row, left to right). The panel gets
as many rows as the lowest button needs, so an empty row between buttons is allowed. Both properties are defined in
New_Properties.xml (Reader and PIM-X show them).

### Graphs without Graph Button Cell are ignored

*Graph Button Cell* is what puts a graph in the window. A graph exemplar without it gets no button, and a button
of the UI for it (one of the 18 Maxis buttons) is hidden, whoever made the exemplar: the Maxis originals in
`SimCity_1.dat`, an older CAM version or another mod. So:

- an exemplar that overrides one of `CAM_Exemplar_Graphs.dat` without the property (an older mod loaded later)
  removes that graph's button instead of taking a cell at random; it is listed in `SC4SIM.log` (Debug level) as
  "has no Graph Button Cell; ignored", and adding the property brings it back;
- a stray exemplar in group `0xCA4AD545` adds no button;
- the window is described by the data alone: the set of exemplars with Graph Button Cell *is* the list of buttons,
  without relying on the 18 buttons of the Maxis UI. This is meant to ease a later move from the Maxis files to
  SC4SIM data (for example an open reimplementation such as OpenSC4): the graph exemplars already carry everything
  the window needs (data sources, labels, colours, title, position).

Transition: when no graph exemplar has Graph Button Cell (SC4SIM's `CAM_Exemplar_Graphs.dat` not installed, Maxis
files only), the DLL leaves the window exactly as the game builds it, with the 18 Maxis buttons, and logs it.
*Graph Button Hidden* still works on a graph with Graph Button Cell (hide it without losing its position).

`[GraphWindow] Enabled=false` in `SC4SIM.ini` turns it off. SimCity 4 1.1.641 only (the call
site is checked before patching).

## Order of the graphs

CAM orders the graphs by theme, one column per theme (`tools/power-graph/graph_layout.py`, written into every graph
exemplar of `CAM_Exemplar_Graphs.dat` as Graph Button Cell):

| Row | Population and education | Government, economy, transport | Services and environment |
|---|---|---|---|
| 0 | Jobs & Pop. | Mayor Rating | Crime |
| 1 | Population R/C/I | Funds | Police & Jails |
| 2 | RCI Demand | City Income/Expenses | Fire Protection |
| 3 | Demand Forecast | Tax Rates | Power |
| 4 | Population by Age | Res. Avg. Income | Power Grid |
| 5 | Life Expectancy | Commute Time | Water |
| 6 | Health | Mass Transit | Water Grid |
| 7 | Afterlife | Traffic Volume | Garbage |
| 8 | Education | Traffic Congestion | Pollution |
| 9 | Education by Age | | |

The 17 remaining Maxis buttons are placed in the same order in the Graphs UI of `CAM_UI.dat`, packed in its 6 rows
(column 2: Crime, Power, Water, Garbage, Pollution), so the order is the same without the DLL; the DLL adds Population R/C/I,
Demand Forecast, Health, Afterlife, Tax Rates, Mass Transit, Traffic Congestion, Police & Jails, Fire Protection, Power Grid and Water Grid and moves every button to its cell. The Water Pollution button (`0x4009`) is removed from the
UI and its exemplar has Graph Button Hidden, since its data is now in the Pollution graph.

## Adding a graph

1. Make a graph exemplar (type `0x6534284A`, group `0xCA4AD545`, a free instance up to `0xFF`), for example by
   copying one of `CAM_Exemplar_Graphs.dat` and changing *Graph Plot data source 1* (`0x6A4AEE40`, history types),
   *Graph Plot label key* (`0x6A4AEEDC`), *Graph Plot color* (`0x6A4AEEDF`) and *User Visible Name Key*
   (`0x8A416A99`, LTEXT of the title and of the button); optionally add *Graph Button Cell* (`0x5C4DE350`).
2. Put it in any plugin. The button appears the next time a city is opened.

The data sources are history types of the city's history warehouse: those the game records (the `game_trends` list of
the advisor Lua) or new ones recorded by a DLL, as SC4SIM does for the Power and Water graphs.

## Pollution graph

The Maxis Air Pollution graph (`0x08`) becomes **Pollution**, with the four pollution totals the game records (no
DLL needed for the data):

| Plot | History type | Label | Color |
|---|---|---|---|
| Air Pollution | `0x0A1100E8` (`G_TOTAL_AIR_POLLUTION`) | `0x0A5D2EA0` (game) | grey |
| Water Pollution | `0xEA110130` (`G_TOTAL_WATER_POLLUTION`) | `0x0A5D2EA1` (game) | blue |
| Garbage Pollution | `0xEA110134` (`G_TOTAL_GARBAGE_POLLUTION`) | `0xFF5D2EA7` (CAM) | brown |
| Radiation | `0xEA110137` (`G_TOTAL_RADIATION_POLLUTION`) | `0x5C4DE191` (new) | yellow |

Title and button caption: LTEXT `0x5C4DE190` "Pollution". The separate Water Pollution graph (`0x09`) and the
Garbage Pollution graph of the previous SC4SIM version (`0x19`) are gone: `0x09` is kept but hidden, `0x19` is no
longer in `CAM_Exemplar_Graphs.dat`. Without SC4SIM.dll 1.13.0 the Water Pollution exemplar stays in the game but has
no button, because the UI no longer has it.

## Included graphs

The Tax Rates (`0x21`), Mass Transit (`0x22`) and Traffic Congestion (`0x23`, `docs/economy-transport-graphs.md`),
Population R/C/I (`0x1F`) and Demand Forecast (`0x20`, `docs/population-graph.md`), Health (`0x1E`,
`docs/health-graph.md`), Police & Jails (`0x1C`, `docs/crime-graph.md`), Fire Protection
(`0x1D`, `docs/fire-graph.md`), Power Grid (`0x1A`, `docs/power-grid-graph.md`) and Water Grid (`0x1B`,
`docs/water-grid-graph.md`) graphs are new exemplars whose buttons are added by the DLL, at (1, 3), (1, 6),
(1, 8), (0, 1), (0, 3), (0, 6), (0, 7) (Afterlife, `0x24`, `docs/afterlife.md`), (2, 1), (2, 2), (2, 4) and (2, 6). The Crime graph (`0x0C`) is also rebuilt (`docs/crime-graph.md`).
