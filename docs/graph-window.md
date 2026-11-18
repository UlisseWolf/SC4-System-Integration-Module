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
   name), and puts it in the next cell of the 3-column grid, below the 6 Maxis rows;
3. makes the panel one 18-pixel row taller for every 3 new buttons, upward so its bottom stays in place, and moves the
   two graph windows (`0x8A8B5B71` expanded, `0x8A8B5B72` collapsed) up by the same amount. The new rows are painted
   by the filler `0x5C4DE310` (hidden blue FlatRect in `CAM_UI.dat`, behind the button container, whose background
   image now ends at the container's bottom).

Order: by instance. `[GraphWindow] Enabled=false` in `SC4SIM.ini` turns it off. SimCity 4 1.1.641 only (the call
site is checked before patching).

## Adding a graph

1. Make a graph exemplar (type `0x6534284A`, group `0xCA4AD545`, a free instance up to `0xFF`), for example by
   copying one of `CAM_Exemplar_Graphs.dat` and changing *Graph Plot data source 1* (`0x6A4AEE40`, history types),
   *Graph Plot label key* (`0x6A4AEEDC`), *Graph Plot color* (`0x6A4AEEDF`) and *User Visible Name Key*
   (`0x8A416A99`, LTEXT of the title and of the button).
2. Put it in any plugin. The button appears the next time a city is opened.

The data sources are history types of the city's history warehouse: those the game records (the `game_trends` list of
the advisor Lua) or new ones recorded by a DLL, as SC4SIM does for the Power and Water graphs.

## Included example

`CAM_Exemplar_Graphs.dat` has a new graph, **Garbage Pollution** (`0x19`): the garbage pollution index
(`G_TOTAL_GARBAGE_POLLUTION`, `0xEA110134`), which was moved out of the Garbage graph. Its button is the first one the
DLL adds, in the first cell of a seventh row.
