# Crime and Police & Jails graphs (1.13.0)

> **Not yet tested in game.**

The Maxis Crime graph ("Crimes vs. Arrests", `0x6534284A / 0xCA4AD545 / 0x0000000C`) only showed the crimes and
arrests of the month. CAM now has two graphs, side by side at the top of the *Services and environment* column of the
Graphs window (`docs/graph-window.md`).

## Crime (`0x0C`)

Counts of the month, in people or events.

| Plot | History type | Recorded by | Label |
|---|---|---|---|
| Crimes | `0x0A5CC155` (`G_NUM_CRIMES`) | game | `0x0A5D2EA4` (game) |
| Arrests | `0x0A5CC156` (`G_NUM_ARRESTS`) | game | `0x0A5D2EA5` (game) |
| Unsolved Crimes | `0x5C4DE083` | SC4SIM.dll | `0x5C4DE1A3` |
| Criminals | `0x5C4DE081` | SC4SIM.dll | `0x5C4DE1A1` |
| Inmates | `0x5C4DE082` | SC4SIM.dll | `0x5C4DE1A2` |

Title and button caption: LTEXT `0x5C4DE1A8` "Crime" (was "Crimes vs. Arrests").

## Police & Jails (`0x1C`, new)

Percentages and indexes, on a 0-100 scale.

| Plot | History type | Recorded by | Label |
|---|---|---|---|
| Police Coverage % | `0x4A11D354` (`G_POLICE_PROTECTION_COVERAGE_P`) | game | `0x5C4DE1A4` |
| Average Crime | `0xAA662693` (`GA_CRIME`) | game | `0x5C4DE1A5` |
| Police Efficiency % | `0x5C4DE084` | SC4SIM.dll | `0x5C4DE1A6` |
| Jail Occupancy % | `0x5C4DE085` | SC4SIM.dll | `0x5C4DE1A7` |

Title and button caption: LTEXT `0x5C4DE1A0` "Police & Jails". Its button is added by SC4SIM.dll (Graph Button Cell
(2, 1)).

## What SC4SIM.dll records

`src/safety/CrimeGraphHistory.cpp`, on every SimNewMonth, in the city's history warehouse:

- **Criminals** (`0x5C4DE081`): the police simulator's criminal count (what the advisors read as `g_criminal_count`);
- **Inmates** (`0x5C4DE082`): the jail population (`g_inmate_count`);
- **Unsolved Crimes** (`0x5C4DE083`): crimes of the month (crime simulator, `g_crime_commited_count`) minus arrests
  (`g_arrest_count`), never below 0;
- **Police Efficiency %** (`0x5C4DE084`): arrests × 100 / crimes, at most 100, and 100 without crimes;
- **Jail Occupancy %** (`0x5C4DE085`): inmates × 100 / jail capacity (`g_jail_capacity`), 0 without jails. Above 100
  means overcrowded jails.

The police station and jail counts go to `SC4SIM.log` (Debug level).

The police simulator interface of gzcom-dll (`cISC4PoliceSimulator.h`) is one vtable slot short after
`GetPoliceStationCount` (`+0x60`): the game's advisor data pump (`0x00416340`-`0x00416940`) reads the jail capacity at
`+0x74`, the inmates at `+0x78`, the criminals at `+0x7C`, the arrests at `+0x88` and the jails at `+0x8C`, four bytes
after the header. The module calls these getters through the offsets the game itself uses. The crime simulator's
`GetMonthlyCrimeCount` (`+0x1C`) matches the header.

The game already records crimes and arrests at `0x006E2C99` and police coverage at `0x006E0647` (coverage × 100), and
the advisor data pump records the average crime at `0x0041680E`.

## Consistency with CAM_LUA_Controller.dat

CAM's Lua (police panel and advisor constants) was checked against the graphs:

- **Police Efficiency**: CAM's Police panel (`cmPoliceEfficiency`, Lua `0xF8B4440E`) shows arrests / crimes × 100
  over the last 12 months, and 100 % when there were no crimes. The graph uses the same name and the same rule, per
  month (the yearly value is the panel's job).
- **Inmates**: the panel shows `game.g_inmate_count`, the same jail population the DLL records.
- **Crimes and arrests**: the panel sums `game_trends.G_NUM_CRIMES` / `G_NUM_ARRESTS`, the history types the Crime
  graph plots.
- **Average Crime**: CAM's tuning constants put `GA_CRIME` low at 10, medium at 50 and high at 100, so it fits the
  0-100 scale of Police & Jails next to the percentages. The crimes per 100,000 people of the panel (C-1, yearly) are
  not plotted: a different scale.
- **graph_window_types** (advisor constants, Lua `0xFF048A3E`): `game.window_graph(graph_window_types.X)` opens a
  graph from advisor messages and scripts. `WATER_POLLUTION` (0x09) would open a graph without a button, so it now
  points to Pollution (0x08); `POLLUTION`, `POLICE_JAILS` (0x1C), `FIRE_PROTECTION` (0x1D), `HEALTH` (0x1E), `POWER_GRID` (0x1A) and
  `WATER_GRID` (0x1B) are added for scripts. No message of the game or of CAM links to the pollution or crime graphs today
  (SimCityLocale.DAT links Power, Funds, Life Expectancy, Water, EQ and Garbage). Patch:
  `tools/power-graph/patch_graph_window_types.py`.

`[CrimeGraph] Enabled=false` in `SC4SIM.ini` stops the recording (the game's series still show).
