# Graph buttons for every graph exemplar (1.13.0)

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

1. looks for every graph exemplar `0x6534284A / 0xCA4AD545 / 0x01-0xFF` (`TestForKey`) whose button
   (`0x4000 + instance`) is not in the panel;
2. creates each missing button from the template UI `0x00000000 / 0x96A006B0 / 0x5C4DE300` (a copy of the Maxis
   graph button, in `CAM_UI.dat`) with `cIGZUIScriptService::CreateWindowFromScript`, gives it ID
   `0x4000 + instance` and the graph's name (the LTEXT of *User Visible Name Key* `0x8A416A99`, else the exemplar
   name), and puts it in the cell the exemplar asks for (*Graph Button Cell*, below) or, without it, in the next
   free cell of the 3-column grid, below the 6 Maxis rows;
3. makes the panel one 18-pixel row taller for every 3 new buttons, upward so its bottom stays in place, and moves the
   two graph windows (`0x8A8B5B71` expanded, `0x8A8B5B72` collapsed) up by the same amount. The new rows are painted
   by the filler `0x5C4DE310` (hidden blue FlatRect in `CAM_UI.dat`, behind the button container, whose background
   image now ends at the container's bottom).

## Button position: Graph Button Cell

| ID | Property | Type | Meaning |
|---|---|---|---|
| `0x5C4DE350` | Graph Button Cell | Uint32 ×2 | Column (0-2, left to right) and row (from the top; rows 0-5 are the Maxis buttons, so the first free row is 6, up to 31) of the graph's button. |

The position lives in the graph exemplar itself: no `SC4SIM.ini` key is needed. It applies to every button, the
Maxis ones included: a button of the UI is moved to its exemplar's cell. Order of placement: buttons of the UI
without the property keep their place; then graphs with it (lowest instance first) take their cell; then new buttons
without it fill the free cells row by row, left to right, in instance order. A cell outside the grid or already taken
is logged in `SC4SIM.log` and the graph goes to the next free cell instead. The panel gets as many rows as the lowest button needs, so an
empty row between buttons is allowed. The property is defined in New_Properties.xml (Reader and PIM-X show it).

`[GraphWindow] Enabled=false` in `SC4SIM.ini` turns it off. SimCity 4 1.1.641 only (the call
site is checked before patching).

## Order of the graphs

CAM orders the graphs by theme, one column per theme (`tools/power-graph/graph_layout.py`, written into every graph
exemplar of `CAM_Exemplar_Graphs.dat` as Graph Button Cell):

| Row | Population and education | Government, economy, transport | Services and environment |
|---|---|---|---|
| 0 | Jobs & Pop. | Mayor Rating | Crime |
| 1 | RCI Demand | Funds | Power |
| 2 | Population by Age | City Income/Expenses | Water |
| 3 | Life Expectancy | Res. Avg. Income | Garbage |
| 4 | Education | Commute Time | Air Pollution |
| 5 | Education by Age | Traffic Volume | Water Pollution |
| 6 | | | Power Grid |
| 7 | | | Water Grid |
| 8 | | | Garbage Pollution |

The 18 Maxis buttons are also placed this way in the Graphs UI of `CAM_UI.dat`, so the order is the same without the
DLL (which only adds row 6 and later).

## Adding a graph

1. Make a graph exemplar (type `0x6534284A`, group `0xCA4AD545`, a free instance up to `0xFF`), for example by
   copying one of `CAM_Exemplar_Graphs.dat` and changing *Graph Plot data source 1* (`0x6A4AEE40`, history types),
   *Graph Plot label key* (`0x6A4AEEDC`), *Graph Plot color* (`0x6A4AEEDF`) and *User Visible Name Key*
   (`0x8A416A99`, LTEXT of the title and of the button); optionally add *Graph Button Cell* (`0x5C4DE350`).
2. Put it in any plugin. The button appears the next time a city is opened.

The data sources are history types of the city's history warehouse: those the game records (the `game_trends` list of
the advisor Lua) or new ones recorded by a DLL, as SC4SIM does for the Power and Water graphs.

## Included example

`CAM_Exemplar_Graphs.dat` has a new graph, **Garbage Pollution** (`0x19`): the garbage pollution index
(`G_TOTAL_GARBAGE_POLLUTION`, `0xEA110134`), which was moved out of the Garbage graph. Its exemplar sets Graph Button Cell to
(2, 8). The Power Grid (`0x1A`, `docs/power-grid-graph.md`) and Water Grid (`0x1B`, `docs/water-grid-graph.md`)
graphs are at (2, 6) and (2, 7).
