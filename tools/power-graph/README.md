# Power graph data

`make_power_graph.py` adds energy bought and sold and the production of each energy class to CAM's Power graph:

```
python make_power_graph.py "<SIM>/a_CAM/1 CAM Core/CAM_Exemplars.dat" "<SIM>/a_CAM/1 CAM Core/CAM Locale/CAM_Locale_en.dat" out
```

It writes `out/CAM_Exemplars.dat` (graph exemplar `0x6534284A / 0xCA4AD545 / 0x00000006` rebuilt with 18 plots) and
`out/CAM_Locale_en.dat` (16 plot labels added). Copy both back into the package. Plot list, colours and labels are the
`PLOTS` table of the script. The class lines need SC4SIM.dll 1.13.0 or later; see `docs/power-graph.md`.

`make_graph_ui.py` makes the Graphs window taller so the 18-line legend fits:

```
python make_graph_ui.py "<SimCity 4>/SimCity_1.dat" "<SIM>/a_CAM/1 CAM Core/CAM_UI.dat" out [extra_height]
```

It writes `out/CAM_UI.dat` (the Graphs window UI 0x6BC9065A grown upward, default 180 pixels, and its taller
background image 0x5C4DE200). Needs Pillow.
