# Economy and transport graphs (1.13.0)

> **Not yet tested in game.**

The *government, economy, transport* column of the Graphs window (`docs/graph-window.md`, column 1). Every new value
comes from the advisor Lua table `game`, filled by the game's advisor data pump, and is recorded every month by
`src/economy/AdvisorGraphHistory.cpp` (one table of field / history type / factor).

| Row | Graph | Change |
|---|---|---|
| 0 | Mayor Rating (`0x01`) | Unchanged: the game exposes only the city average (`ga_mayor_rating`), no per-factor or per-neighbourhood value. |
| 1 | Funds (`0x02`) | Adds **Loans Outstanding** and **Borrowing Limit**. |
| 2 | City Income/Expenses (`0x03`) | Adds **Tax Income**, **Neighbor Deals Income**, **Neighbor Deals Expense** and **Loan Payments**. |
| 3 | **Tax Rates** (`0x21`, new) | The 12 tax rates. |
| 4 | Res. Avg. Income (`0x04`) | Unchanged: the game has one residential average income and no per-wealth value. |
| 5 | Commute Time (`0x0B`) | Unchanged: one city value; its causes are in the two new graphs below. |
| 6 | **Mass Transit** (`0x22`, new) | Station utilization of every transit type. |
| 7 | Traffic Volume (`0x18`) | Unchanged: the 9 travel modes are all there. |
| 8 | **Traffic Congestion** (`0x23`, new) | Average congestion of every network and potholes. |

The traffic simulator also records an unnamed percentage (`0xEA5BA01D`, traffic simulator `+0x1E4` × 100, at
`0x00718B39`) right before the commute time; nothing in the game or in CAM names or plots it, so it is left out.

## Funds and City Income/Expenses

| Plot | History type | Field | Label |
|---|---|---|---|
| Loans Outstanding | `0x5C4DE101` | `g_borrowed` / 1000 | `0x5C4DE201` |
| Borrowing Limit | `0x5C4DE102` | `g_borrowing_limit` / 1000 | `0x5C4DE202` |
| Tax Income | `0x5C4DE103` | `g_tax_income` | `0x5C4DE203` |
| Neighbor Deals Income | `0x5C4DE104` | `g_nd_income` | `0x5C4DE204` |
| Neighbor Deals Expense | `0x5C4DE105` | `g_nd_expense` | `0x5C4DE205` |
| Loan Payments | `0x5C4DE106` | `g_bond_payments_monthly` | `0x5C4DE206` |

The Funds graph is in thousands (the game records funds / 1000 at `0x00692A47`), so the loans are divided by 1000
too. The budget fields are written by the data pump at `0x00413A46`-`0x00413CEB`.

## Tax Rates (`0x21`, cell (1, 3))

The 12 rates `g_tax_rate_r_low`, `_r_med`, `_r_high`, `_cs_low`, `_cs_med`, `_cs_high`, `_co_med`, `_co_high`,
`_i_resource`, `_i_dirty`, `_i_manufacturing`, `_i_hightech` as history types `0x5C4DE111`-`0x5C4DE11C`. The game
sets taxes in 0.1 steps and the history keeps integers, so they are recorded × 10 and drawn with Graph Plot scale 0.1
(template: the Res. Avg. Income graph, which has the property). Labels and colours are those of the Maxis Jobs & Pop.
groups (R-§ ... I-HT). Title LTEXT `0x5C4DE210` "Tax Rates %".

## Mass Transit (`0x22`, cell (1, 6))

| Plot | History type | Field | Label |
|---|---|---|---|
| Bus Station Use % | `0x5C4DE121` | `ga_bus_station_utilization_p` | `0x5C4DE221` |
| Train Station Use % | `0x5C4DE122` | `ga_train_station_utilization_p` | `0x5C4DE222` |
| Subway Station Use % | `0x5C4DE123` | `ga_subway_station_utilization_p` | `0x5C4DE223` |
| El Train Station Use % | `0x5C4DE124` | `ga_elevated_station_utilization_p` | `0x5C4DE224` |
| Monorail Station Use % | `0x5C4DE125` | `ga_monorail_station_utilization_p` | `0x5C4DE225` |

The pump writes ratio × 100 (`0x0041C2BD` and following). It writes the monorail and elevated values twice
(`0x0041C281`, `0x0041C29F` with other fields, then `0x0041C317`, `0x0041C335`): the last write, which the DLL
reads, is the right one.

## Traffic Congestion (`0x23`, cell (1, 8))

Average congestion × 100 of roads, streets, avenues, bridges, rail, subway, elevated train and monorail
(`ga_*_congestion`, history types `0x5C4DE131`-`0x5C4DE138`, labels `0x5C4DE231`-`0x5C4DE238`) and the potholes
(`g_pothole_count`, `0x5C4DE139`, label `0x5C4DE239`). Below 100 there is no congestion (adv_game_data.lua). Title
LTEXT `0x5C4DE230`.

## Settings and Lua

`[EconomyGraph] Enabled=false` stops the Funds, Income and Tax Rates series, `[TransportGraph] Enabled=false` the
Mass Transit and Traffic Congestion series. `graph_window_types.TAX_RATES`, `MASS_TRANSIT` and `TRAFFIC_CONGESTION`
open the graphs from Lua (`tools/power-graph/patch_graph_window_types.py`).
