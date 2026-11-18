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

## Maxis graphs of the column

| Graph | Change |
|---|---|
| Jobs & Pop. (`0x11`) | Adds **Total Jobs (C + I)** (game data: `G_C_POPULATION` + `G_I_POPULATION` through Graph Data Source 2 `0x6A4AEE5F` with combine 1 = add, as Maxis does in the Traffic Volume graph; label `0x5C4DE1F1`) and **Workforce** (`0x5C4DE0E1`), so jobs and workers can be compared next to the 12 groups. |
| RCI Demand (`0x0E`) | Unchanged: a bar chart of this month's demand; its trend over time is the new Demand Forecast graph. |
| Population by Age (`0x13`) | Unchanged: the game computes exactly these 9 age groups (loop at `0x006F80C0`), nothing is missing. |
| Life Expectancy (`0x05`) | Adds **Highest Neighborhood** and **Lowest Neighborhood** life expectancy (`0x5C4DE0C7`, `0x5C4DE0C8`; `docs/health-graph.md`). |
| Education (`0x0D`) | Adds **Education Coverage %**, **School / Library / Museum Grade %** and **Highest / Lowest Neighborhood EQ** next to the average EQ (fixed 0-200 scale, the percentages fit). |
| Education by Age (`0x12`) | Unchanged: the game records exactly 9 EQ age groups (`0xCA545CCA` + 0-8, loop at `0x006FEC73`). |

The plots are appended by `EXTEND` in `tools/power-graph/make_utility_graphs.py`; the Maxis plots stay as they are.

### Education (`src/population/EducationGraphHistory.cpp`)

From the advisor Lua table `game`, as filled by the data pump:

| Plot | History type | Field | Label |
|---|---|---|---|
| Education Coverage % | `0x5C4DE0F1` | `g_education_coverage_p` (`0x0041529F`) | `0x5C4DE1F3` |
| School Grade % | `0x5C4DE0F2` | `ga_school_grade` (`0x00414C21`) | `0x5C4DE1F4` |
| Library Grade % | `0x5C4DE0F3` | `ga_library_grade` (`0x00415244`) | `0x5C4DE1F5` |
| Museum Grade % | `0x5C4DE0F4` | `ga_museum_grade` (`0x00414F36`) | `0x5C4DE1F6` |
| Highest Neighborhood EQ | `0x5C4DE0F5` | `l_tract_eq_h` (`0x00415924`) | `0x5C4DE1F7` |
| Lowest Neighborhood EQ | `0x5C4DE0F6` | `l_tract_eq_l` (`0x004158A2`) | `0x5C4DE1F8` |

`[EducationGraph] Enabled=false` in `SC4SIM.ini` stops it (the average EQ still shows).

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
