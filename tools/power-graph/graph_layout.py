# Order of the buttons of the Graphs window, by theme, one column per theme:
#   column 0: population and education    column 1: government, economy, transport    column 2: services, environment
# instance of the graph exemplar (0x6534284A / 0xCA4AD545) -> (column 0-2, row). Rows 0-5 hold the 18 Maxis graphs
# (the Graphs UI has 6 rows); rows from 6 need SC4SIM.dll, which adds the panel rows (docs/graph-window.md).
# Written into every graph exemplar as Graph Button Cell (0x5C4DE350) by make_utility_graphs.py, and used by
# make_graph_ui.py to place the 18 Maxis buttons of the UI, so the order is the same with or without the DLL.
LAYOUT = {
    # population and education
    0x11: (0, 0),   # Jobs & Pop.
    0x0E: (0, 1),   # RCI Demand
    0x13: (0, 2),   # Population by Age
    0x05: (0, 3),   # Life Expectancy
    0x0D: (0, 4),   # Education
    0x12: (0, 5),   # Education by Age
    # government, economy, transport
    0x01: (1, 0),   # Mayor Rating
    0x02: (1, 1),   # Funds
    0x03: (1, 2),   # City Income/Expenses
    0x04: (1, 3),   # Res. Avg. Income
    0x0B: (1, 4),   # Commute Time
    0x18: (1, 5),   # Traffic Volume
    # services and environment
    0x0C: (2, 0),   # Crime
    0x06: (2, 1),   # Power
    0x07: (2, 2),   # Water
    0x0A: (2, 3),   # Garbage
    0x08: (2, 4),   # Air Pollution
    0x09: (2, 5),   # Water Pollution
    0x1A: (2, 6),   # Power Grid (SC4SIM graph, button added by the DLL)
    0x1B: (2, 7),   # Water Grid (SC4SIM graph, button added by the DLL)
    0x19: (2, 8),   # Garbage Pollution (SC4SIM graph, button added by the DLL)
}
