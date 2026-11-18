# Health graph (1.13.0)

> **Not yet tested in game.**

SimCity 4 only graphs life expectancy (Maxis Life Expectancy graph, `0x05`). CAM now has a **Health** graph
(`0x6534284A / 0xCA4AD545 / 0x0000001E`), right under Life Expectancy in the *population, health and education*
column of the Graphs window (`docs/graph-window.md`). Its button is added by SC4SIM.dll (Graph Button Cell (0, 4)).

| Plot | History type | Source (advisor Lua `game`) | Label |
|---|---|---|---|
| Health Coverage % | `0x5C4DE0C1` | `g_health_coverage_p` | `0x5C4DE1C1` |
| Average Health % | `0x5C4DE0C2` | `ga_health` / 2 | `0x5C4DE1C2` |
| Health Grade % | `0x5C4DE0C3` | `ga_health_grade` | `0x5C4DE1C3` |
| Hospital Occupancy % | `0x5C4DE0C4` | `g_num_treated_patients` × 100 / `g_health_capacity` | `0x5C4DE1C4` |
| Hospitals | `0x5C4DE0C5` | `g_num_hospitals` | `0x5C4DE1C5` |
| Clinics | `0x5C4DE0C6` | `g_num_clinics` | `0x5C4DE1C6` |

Title and button caption: LTEXT `0x5C4DE1C0` "Health". Every line is recorded by SC4SIM.dll; without it the graph
stays empty (and has no button).

## What SC4SIM.dll records

`src/safety/HealthGraphHistory.cpp`, on every SimNewMonth, in the city's history warehouse. The game keeps no history
of health except life expectancy (`0x2A5E7009`): `G_POPULATION_HEALTH_COVERAGE` (`0xAA1A2C9F`) is in the advisor
constants but the game never records it. The health simulator has no interface in gzcom-dll, so the DLL reads the
advisor Lua table `game`, which the game's advisor data pump fills (`0x004143C8`-`0x004157CE`) with the values of the
health simulator and of the hospitals and clinics. The values are those of the last advisor update before the month
starts.

- **Health Coverage %**: `g_health_coverage_p` (the pump divides and multiplies by 100);
- **Average Health %**: `ga_health` / 2, CAM's *Health Rating* (panel C-4, `cbHealthRating`);
- **Health Grade %**: `ga_health_grade` (CAM's advisors: low below 30);
- **Hospital Occupancy %**: treated patients over health capacity; above 100 means the hospitals and clinics are full;
- **Hospitals** and **Clinics**: the counts the advisors and CAM's medical tooltip use.

## Life Expectancy graph

The same module records the life expectancy of the best and worst neighbourhood (`l_tract_life_exp_h` at
`0x004156FA`, `l_tract_life_exp_l` at `0x004155EE`, in years) as `0x5C4DE0C7` and `0x5C4DE0C8`; the Maxis Life
Expectancy graph (`0x05`) plots them next to the city average (labels `0x5C4DE1C7` "Highest Neighborhood",
`0x5C4DE1C8` "Lowest Neighborhood"). `[HealthGraph] Enabled=false` stops them too.

`game.window_graph(graph_window_types.HEALTH)` opens the graph from Lua (CAM's advisor constants, patched by
`tools/power-graph/patch_graph_window_types.py`).

`[HealthGraph] Enabled=false` in `SC4SIM.ini` stops the recording.
