# Graph data

`make_graph_exemplars.py` collects every graph exemplar of the Graphs window (the 18 of the game, with CAM's versions
where CAM has one) into `CAM_Exemplar_Graphs.dat`, and writes `CAM_Exemplars.dat` without them:

```
python make_graph_exemplars.py "<SimCity 4>/SimCity_1.dat" "<SIM>/a_CAM/1 CAM Core/CAM_Exemplars.dat" out
```

Then run `make_utility_graphs.py` on `out/CAM_Exemplar_Graphs.dat`.

`make_utility_graphs.py` adds energy bought and sold and the production of each energy class to CAM's Power graph,
and water bought and sold, water by source and wastewater to its Water graph; the Garbage graph gets garbage produced, real
scale and its own labels:

```
python make_utility_graphs.py "out/CAM_Exemplar_Graphs.dat" "<SIM>/a_CAM/1 CAM Core/CAM Locale/CAM_Locale_en.dat" out
```

It writes the graphs file under the same name in the output folder (graph exemplars `0x6534284A / 0xCA4AD545 / 0x00000006` with 18 plots and
`0x00000007` with 10 plots, Garbage `0x0000000A` with 9) and `out/CAM_Locale_en.dat` (the plot labels added). Copy both back into the package. Plot
lists, colours and labels are the `POWER_PLOTS`, `WATER_PLOTS` and `GARBAGE_PLOTS` tables of the script. The new lines need SC4SIM.dll
1.13.0 or later; see `docs/power-graph.md` and `docs/water-graph.md`.

`make_graph_ui.py` makes the Graphs window taller so the 18-line legend fits:

```
python make_graph_ui.py "<SimCity 4>/SimCity_1.dat" "<SIM>/a_CAM/1 CAM Core/CAM_UI.dat" out [extra_height]
```

It writes `out/CAM_UI.dat` (the Graphs window UI 0x6BC9065A grown upward, default 180 pixels, and its taller
background image 0x5C4DE200). Needs Pillow.

`make_graph_ui.py` also writes the button template UI (0x5C4DE300) and the panel filler (0x5C4DE310) used by
SC4SIM.dll to add a button for every graph exemplar (`docs/graph-window.md`); `make_utility_graphs.py` adds the
Garbage Pollution graph (0x19).
