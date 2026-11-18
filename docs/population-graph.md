# Population R/C/I and Demand Forecast graphs (1.13.0)

> **Not yet tested in game.**

Two new graphs in the *population, health and education* column of the Graphs window (`docs/graph-window.md`), each
under the Maxis graph it completes. Their buttons are added by SC4SIM.dll.

## Population R/C/I (`0x6534284A / 0xCA4AD545 / 0x0000001F`, cell (0, 1))

The Maxis *Jobs & Pop.* graph shows the 12 wealth / density groups; this one shows the totals.

| Plot | History type | Recorded by | Label |
|---|---|---|---|
| Total Population | `0xAA1A2CCA` (`G_POPULATION`) | game | `0x5C4DE1D1` |
| Residential | `0x2A4E2003` (`G_R_POPULATION`, residents) | game | `0x5C4DE1D2` |
| Commercial | `0x0A4E2056` (`G_C_POPULATION`, commercial jobs) | game | `0x5C4DE1D3` |
| Industrial | `0x4A4E206B` (`G_I_POPULATION`, industrial jobs) | game | `0x5C4DE1D4` |
| Workforce | `0x5C4DE0E1` | SC4SIM.dll | `0x5C4DE1D5` |

Title: LTEXT `0x5C4DE1D0` "Population R/C/I". The first four lines work without the DLL (only the button needs it).

## Demand Forecast (`0x6534284A / 0xCA4AD545 / 0x00000020`, cell (0, 3))

The Maxis *RCI Demand* graph shows the demand of this month; this one shows how the demand simulator's forecast
(extrapolated demand) moves over time. Values can be negative, like the demand bars.

| Plot | History type | Source (advisor Lua `game`) | Label |
|---|---|---|---|
| Residential Forecast | `0x5C4DE0E2` | `g_r_demand_extrap` | `0x5C4DE1E1` |
| Commercial Forecast | `0x5C4DE0E3` | `g_c_demand_extrap` | `0x5C4DE1E2` |
| Industrial Forecast | `0x5C4DE0E4` | `g_i_demand_extrap` | `0x5C4DE1E3` |
| Workforce Demand | `0x5C4DE0E5` | `g_workforce_demand` | `0x5C4DE1E4` |

Title: LTEXT `0x5C4DE1E0` "Demand Forecast". Every line is recorded by SC4SIM.dll.

## What SC4SIM.dll records

`src/population/PopulationGraphHistory.cpp`, on every SimNewMonth, in the city's history warehouse. The workforce and
the forecasts exist only in the advisor Lua table `game` (the "Extrapolated" block of `adv_game_data.lua`), which the
game's advisor data pump fills: `g_city_workforce_population` at `0x00412A3C`, `g_r_demand_extrap` at `0x0041337F`,
`g_c_demand_extrap` at `0x004134D3`, `g_i_demand_extrap` at `0x004135E5` (the pump reads the demand simulator's
extrapolated value for every demand ID, `0x1010` = R1 and so on). The DLL reads the values of the last advisor update
before the month starts and rounds them.

`game.window_graph(graph_window_types.POPULATION_RCI)` and `graph_window_types.DEMAND_FORECAST` open the graphs from
Lua (CAM's advisor constants, patched by `tools/power-graph/patch_graph_window_types.py`).

`[PopulationGraph] Enabled=false` in `SC4SIM.ini` stops the recording (the game's population lines still show).
