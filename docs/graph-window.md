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
   window in `docs/power-graph.md`).

**Buttons of CAM's graphs are in the UI.** In the first in-game test the buttons the DLL created at run time from
the template did not switch the graph and stayed checked when another graph was chosen (the game's graph handler,
`0x0076E500`, never received their clicks), while the buttons loaded with the UI worked. So `make_graph_ui.py`
writes a button for every graph of `graph_layout.py` into `CAM_UI.dat` (a copy of the Maxis Crime button, ID
`0x4000 + instance`, caption = the graph's title); the DLL only moves them to their cells. Buttons for graphs of
other plugins are still created at run time.

**Graphs of other plugins (1.14.0).** A button sends its click to its *notification target*. The buttons loaded with
the Graphs UI have the window whose handler (`0x0076E500`) selects graph `ID & 0xFF`; a button built from the
template UI keeps the template's own root as target, which is why its clicks reached nothing. The DLL now reads the
target of a button of the UI (the first graph button found, else the Previous arrow `0x5C4DE320`) with
`cIGZWin::GetNotificationTarget` and gives it to every button it creates (`SetNotificationTarget`). The game then
selects the graph and keeps the radio state itself, so an external mod only needs a graph exemplar with *Graph
Button Cell*: no UI file is required. The log shows `buttons added (N routed to the graph handler)` and, at Debug
level, each created button's target next to the one the template gave it; a warning is logged if no button of the
UI gives a target. Confirmed in game with two test graphs (instances `0x30` and `0x31`, page 2, no UI file): both
buttons switch the graph and the radio state follows (`2 buttons added (2 routed to the graph handler)`).

`CAM_UI.dat` already sizes the panel for every row of `graph_layout.py` (10
   rows: the panel and both graph windows are 72 pixels taller upward, with taller backgrounds), because a window
   resized at run time keeps its old paint buffer: in the first test the rows past the sixth drew in magenta and did
   not all take clicks. Only when a graph needs a row past those of the UI does the DLL still grow the panel upward
   (one 18-pixel row per row, moving the two graph windows `0x8A8B5B71` and `0x8A8B5B72` up and showing the filler
   `0x5C4DE310`), and it logs a warning.

## Pages (1.13.0)

The button container keeps the 6 rows of the Maxis window, and the graphs are split in **pages** of 6 rows: row r
of *Graph Button Cell* is row r % 6 of page r / 6 (so rows 6-11 are page 2, 12-17 page 3, up to 31). One page is
shown at a time. The last cell of the right column (column 2, row 5 of every page) is kept free for the page
controls: a left arrow, the page number and a right arrow at the right end of the cell (`0x5C4DE320`, `0x5C4DE322`,
`0x5C4DE321` in `CAM_UI.dat`; the arrows are the Maxis "previous / next picture" images scaled to the 18-pixel rows,
PNG `0x46A006B0 / 0x5C4DE220` and `0x5C4DE221`), shown only when there is more than one page. The window keeps the exact Maxis size and position: the game anchors the graph frame and Time
Scale to the bottom of the window, so any extra row would cover them. Page 1 holds the Maxis
graphs, as in the original window, page 2 the graphs SC4SIM adds, each in the column of its theme.

The DLL takes the Previous / Next clicks from the graph window's message handler (`cIGZWinProc` built at
`0x0076CFE0`, `DoWinProcMessage` = `0x0076E500` in vtable slot `0x00AB4FEC`; button clicks arrive there as message
type 3, `0x287259F6`, button ID), turns the page and leaves every other message to the game. The slot is checked
before patching and restored when the game closes. Every CAM button is in `CAM_UI.dat` (pages after the first start
hidden); buttons of other plugins are created at run time and routed to the same handler (see above). Without the DLL only page 1 is shown.

The page row is **above** the buttons. The panel and both graph windows grow only upward (24 pixels for the page
row, 120 for the graph area): their bottoms and the 6 button rows are exactly where the Maxis window has them. In
earlier tests a row under the buttons ended behind the news ticker even with the windows lifted, since the game
keeps the window's bottom in place. Previous and Next are text-only buttons (no image, transparent, the flags of the
Maxis graph buttons): the flat button image of another Maxis UI drew black boxes.

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

- an exemplar of a **new** graph without the property adds no button; it is listed in `SC4SIM.log` (Debug level) as
  "has no Graph Button Cell; ignored";
- exception, for the 18 **Maxis** graphs (their buttons are in the UI): when a plugin loaded after
  `CAM_Exemplar_Graphs.dat` replaces one of them with an exemplar without the property, the Maxis button stays
  where the UI puts it (its cell of `graph_layout.py`) and a warning names the graph, so the graph is not lost. In
  the first test RCI Demand and Commute Time vanished this way;
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

| Page | Row | Population, health, education | Government, economy, transport | Services, environment |
|---|---|---|---|---|
| 1 | 0 | Jobs & Pop. | Mayor Rating | Crime |
| 1 | 1 | RCI Demand | Funds | Power |
| 1 | 2 | Population by Age | City Income/Expenses | Water |
| 1 | 3 | Life Expectancy | Res. Avg. Income | Garbage |
| 1 | 4 | Education | Commute Time | Pollution |
| 1 | 5 | Education by Age | Traffic Volume | |
| 2 | 6 | Population R/C/I | Tax Rates | Police & Jails |
| 2 | 7 | Demand Forecast | Mass Transit | Fire Protection |
| 2 | 8 | Health | Traffic Congestion | Power Grid |
| 2 | 9 | Afterlife | | Water Grid |

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
