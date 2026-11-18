# Power and Water graph data

`make_utility_graphs.py` adds energy bought and sold and the production of each energy class to CAM's Power graph,
and water bought and sold, water by source and wastewater to its Water graph; the Garbage graph gets garbage produced, real
scale and its own labels:

```
python make_utility_graphs.py "<SIM>/a_CAM/1 CAM Core/CAM_Exemplars.dat" "<SIM>/a_CAM/1 CAM Core/CAM Locale/CAM_Locale_en.dat" out
```

It writes `out/CAM_Exemplars.dat` (graph exemplars `0x6534284A / 0xCA4AD545 / 0x00000006` with 18 plots and
`0x00000007` with 10 plots) and `out/CAM_Locale_en.dat` (the plot labels added). Copy both back into the package. Plot
lists, colours and labels are the `POWER_PLOTS` and `WATER_PLOTS` tables of the script. The new lines need SC4SIM.dll
1.13.0 or later; see `docs/power-graph.md` and `docs/water-graph.md`.

`make_graph_ui.py` makes the Graphs window taller so the 18-line legend fits:

```
python make_graph_ui.py "<SimCity 4>/SimCity_1.dat" "<SIM>/a_CAM/1 CAM Core/CAM_UI.dat" out [extra_height]
```

It writes `out/CAM_UI.dat` (the Graphs window UI 0x6BC9065A grown upward, default 180 pixels, and its taller
background image 0x5C4DE200). Needs Pillow.
