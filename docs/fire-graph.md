# Fire Protection graph (1.13.0)

> **Not yet tested in game.**

SimCity 4 has no fire graph. CAM now has one, **Fire Protection** (`0x6534284A / 0xCA4AD545 / 0x0000001D`), under
Crime and Police & Jails in the *Services and environment* column of the Graphs window (`docs/graph-window.md`). Its
button is added by SC4SIM.dll (Graph Button Cell (2, 2)).

| Plot | History type | Recorded by | Label |
|---|---|---|---|
| Fire Coverage % | `0x2A11D203` (`G_FIRE_PROTECTION_COVERAGE_P`) | game | `0x5C4DE1B1` |
| Average Fire Protection | `0x2A11D204` (`GA_FIRE_PROTECTION`, 0-256) | game | `0x5C4DE1B2` |
| Average Flammability | `0x2A11CB48` (`GA_FLAMMABILITY`, 0-256) | game | `0x5C4DE1B3` |
| Fire Outbreaks | `0x5C4DE0A1` | SC4SIM.dll | `0x5C4DE1B4` |
| Fire Stations | `0x5C4DE0A2` | SC4SIM.dll | `0x5C4DE1B5` |

Title and button caption: LTEXT `0x5C4DE1B0` "Fire Protection".

Average fire protection and average flammability share the game's 0-256 grid scale, so the two lines can be compared
directly: protection above flammability is the goal. CAM's tuning constants (CAM_LUA_Controller.dat) put flammability
low at 50, medium at 70 and "tinderbox" at 140.

## What SC4SIM.dll records

`src/safety/FireGraphHistory.cpp`, on every SimNewMonth, in the city's history warehouse:

- **Fire Outbreaks** (`0x5C4DE0A1`): the game records a sample of `FIRE_DISASTER` (`0x49D15B74`, value 1) when a fire
  breaks out (`0x0057A133`). The DLL asks the advisors' own `game.trend_count(game_trends.FIRE_DISASTER, 1)` (Lua,
  implemented at `0x00409900`) for the samples of the last month, so it follows the game's history rules: several
  outbreaks on the same day count as one;
- **Fire Stations** (`0x5C4DE0A2`): `cISC4FireProtectionSimulator::GetFireStationCount` (`+0x5C`, the slot the
  advisor data pump reads as `g_fire_station_count` at `0x004160AD`; the gzcom-dll header matches up to there).

The game records fire coverage at `0x006AD342` (coverage × 100), average fire protection right after it, and average
flammability at `0x006AF899`. CAM's Police/Fire panel (C-6) shows the same coverage (`game.g_fire_coverage_p`).

`game.window_graph(graph_window_types.FIRE_PROTECTION)` opens the graph from Lua (CAM's advisor constants, patched by
`tools/power-graph/patch_graph_window_types.py`).

`[FireGraph] Enabled=false` in `SC4SIM.ini` stops the recording (the game's series still show).
