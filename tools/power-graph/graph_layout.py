# Order of the buttons of the Graphs window, by theme, one column per theme:
#   column 0: population, health, education   column 1: government, economy, transport    column 2: services, environment
# instance of the graph exemplar (0x6534284A / 0xCA4AD545) -> (column 0-2, row 0-31); every PAGE_ROWS rows make a
# page. Written into every graph exemplar as Graph Button Cell (0x5C4DE350) by make_utility_graphs.py, and used by
# make_graph_ui.py to write every button into the Graphs UI (pages past the first hidden until SC4SIM.dll shows them).
LAYOUT = {
    # Pages of 6 rows (SC4SIM.dll shows one page at a time, Previous / Next under the buttons): rows 0-5 are page 1
    # (the Maxis graphs, as in the original window), rows 6-11 page 2 (the graphs SC4SIM adds), and so on.
    # page 1 - population, health and education
    0x11: (0, 0),   # Jobs & Pop.
    0x0E: (0, 1),   # RCI Demand
    0x13: (0, 2),   # Population by Age
    0x05: (0, 3),   # Life Expectancy
    0x0D: (0, 4),   # Education
    0x12: (0, 5),   # Education by Age
    # page 1 - government, economy, transport
    0x01: (1, 0),   # Mayor Rating
    0x02: (1, 1),   # Funds (with loans)
    0x03: (1, 2),   # City Income/Expenses (with tax income, neighbour deals, loan payments)
    0x04: (1, 3),   # Res. Avg. Income
    0x0B: (1, 4),   # Commute Time
    0x18: (1, 5),   # Traffic Volume
    # page 1 - services and environment
    0x0C: (2, 0),   # Crime (crimes, arrests, unsolved crimes, criminals, inmates)
    0x06: (2, 1),   # Power
    0x07: (2, 2),   # Water
    0x0A: (2, 3),   # Garbage
    0x08: (2, 4),   # Pollution (the Maxis Air Pollution graph, now air, water, garbage and radiation)
    # page 2 - population, health and education
    0x1F: (0, 6),   # Population R/C/I
    0x20: (0, 7),   # Demand Forecast
    0x1E: (0, 8),   # Health
    0x24: (0, 9),   # Afterlife
    # page 2 - government, economy, transport
    0x21: (1, 6),   # Tax Rates
    0x22: (1, 7),   # Mass Transit
    0x23: (1, 8),   # Traffic Congestion
    # page 2 - services and environment
    0x1C: (2, 6),   # Police & Jails
    0x1D: (2, 7),   # Fire Protection
    0x1A: (2, 8),   # Power Grid
    0x1B: (2, 9),   # Water Grid
}
PAGE_ROWS = 6

# Graphs without a button: Graph Button Hidden (0x5C4DE351) in the exemplar, and the button removed from the UI.
HIDDEN = {
    0x09,           # Water Pollution: merged into Pollution (0x08)
}
