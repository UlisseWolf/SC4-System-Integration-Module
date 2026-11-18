# Order of the buttons of the Graphs window, by theme, one column per theme:
#   column 0: population, health, education   column 1: government, economy, transport    column 2: services, environment
# instance of the graph exemplar (0x6534284A / 0xCA4AD545) -> (column 0-2, row 0-31). Rows from 6 need SC4SIM.dll,
# which adds the panel rows (docs/graph-window.md). Written into every graph exemplar as Graph Button Cell
# (0x5C4DE350) by make_utility_graphs.py. make_graph_ui.py places the Maxis buttons of the Graphs UI (used without the
# DLL) in the same order, packed in its 6 rows; the DLL then moves every button to its cell.
LAYOUT = {
    # population, health and education
    0x11: (0, 0),   # Jobs & Pop.
    0x1F: (0, 1),   # Population R/C/I (SC4SIM graph, button added by the DLL)
    0x0E: (0, 2),   # RCI Demand
    0x20: (0, 3),   # Demand Forecast (SC4SIM graph, button added by the DLL)
    0x13: (0, 4),   # Population by Age
    0x05: (0, 5),   # Life Expectancy
    0x1E: (0, 6),   # Health (SC4SIM graph, button added by the DLL)
    0x24: (0, 7),   # Afterlife (SC4SIM graph, button added by the DLL)
    0x0D: (0, 8),   # Education
    0x12: (0, 9),   # Education by Age
    # government, economy, transport
    0x01: (1, 0),   # Mayor Rating
    0x02: (1, 1),   # Funds (with loans)
    0x03: (1, 2),   # City Income/Expenses (with tax income, neighbour deals, loan payments)
    0x21: (1, 3),   # Tax Rates (SC4SIM graph, button added by the DLL)
    0x04: (1, 4),   # Res. Avg. Income
    0x0B: (1, 5),   # Commute Time
    0x22: (1, 6),   # Mass Transit (SC4SIM graph, button added by the DLL)
    0x18: (1, 7),   # Traffic Volume
    0x23: (1, 8),   # Traffic Congestion (SC4SIM graph, button added by the DLL)
    # services and environment
    0x0C: (2, 0),   # Crime (crimes, arrests, unsolved crimes, criminals, inmates)
    0x1C: (2, 1),   # Police & Jails (SC4SIM graph, button added by the DLL)
    0x1D: (2, 2),   # Fire Protection (SC4SIM graph, button added by the DLL)
    0x06: (2, 3),   # Power
    0x1A: (2, 4),   # Power Grid (SC4SIM graph, button added by the DLL)
    0x07: (2, 5),   # Water
    0x1B: (2, 6),   # Water Grid (SC4SIM graph, button added by the DLL)
    0x0A: (2, 7),   # Garbage
    0x08: (2, 8),   # Pollution (the Maxis Air Pollution graph, now air, water, garbage and radiation)
}

# Graphs without a button: Graph Button Hidden (0x5C4DE351) in the exemplar, and the button removed from the UI.
HIDDEN = {
    0x09,           # Water Pollution: merged into Pollution (0x08)
}
