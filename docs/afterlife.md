# Afterlife (DLL version of AFTERLIFE.lua)

SC4SIM 1.9.0 can run the calculation of `AFTERLIFE.lua` (the Afterlife engine of the NEON-derived pack) in the DLL.
The statistics are **fictitious and cosmetic**, exactly as in the Lua: Sims never die in the real simulation. The
module only **reads** `game.g_health_coverage_p`, `game.g_city_r_population`, `game.ga_crime` and
`game.ga_education`; it does not call the budget, regional supply or any game function, and it patches no game code,
so it does not depend on the game version.

## How it replaces the Lua

When a city opens, the DLL registers two global functions in the city's advisor Lua. The game has already compiled
`AFTERLIFE.lua` by then, so the DLL functions **replace** the Lua ones:

| Function | Same as in AFTERLIFE.lua |
| --- | --- |
| `run_afterlife_balance_variables(ret_type)` | One cycle of the calculation. `ret_type` is `D`, `W`, `S`, `C`, `K`, `R`, `H`, `P`, `T` or `N`, with the same meaning; another value returns nothing. |
| `get_news_ticker_character(event_type)` | A random entry of `hell_news_characters`, picked from `news_ticker_event_characters[event_type]`. |

Every script that calls them keeps working. After each cycle the DLL also copies all 24 fields into the Lua table
`afterlife_grid_var`, so a UI that reads the table directly sees the same values as before.

**Keep `AFTERLIFE.lua` in the pack.** It is the fallback for players without the DLL, and when `[Afterlife] Enabled`
is false. `SC4SIM.log` says which functions were registered and whether they replaced the Lua ones.

## Constants

The DLL does not read any other Lua file for the calculation. As in `CAM_LUA_Controller.dat`, whose scripts keep their
constants inside the code, the balance constants live in the DLL (`Afterlife::Constants` in
`src/afterlife/AfterlifeModel.h`), and each one can be overridden in the `[Afterlife]` section of `SC4SIM.ini` with
the name below (a commented line keeps the default):

| Group | Keys |
| --- | --- |
| Mortality | `BASE_MORTALITY_RATE_PER_CAPITA`, `HEALTH_COVERAGE_WEIGHT` |
| Judgment | `EDUCATION_REFERENCE`, `BASE_SIN_RATIO`, `CRIME_WEIGHT`, `EDUCATION_WEIGHT`, `SIN_RATIO_MIN`, `SIN_RATIO_MAX` |
| I.M.P. | `IMP_CLIENT_RATIO`, `IMP_SUCCESS_CHANCE`, `IMP_KILL_RANDOM_VARIANCE`, `MAX_IMP_KILL_FRACTION_POP` |
| Redemption | `REDEMPTION_RATIO` |
| Hell's natives | `HELL_IMP_RATIO`, `HELL_HELLHOUND_RATIO`, `HELL_SUCCUBUS_RATIO`, `HELL_INFESTOR_RATIO`, `HELL_POSSESSOR_RATIO`, `HELL_BAPHOMET_RATIO`, `HELL_REAPER_RATIO`, `HELL_GOETIA_ARS_GOETIA_RATIO` |
| Hell's fixed population | `FALLEN_ANGELS` (2), `SEVEN_DEADLY_SINS` (7) |

Since 1.9.1 the defaults are the real balance of the pack: the AFTERLIFE section of `PACK_CONSTANTS.lua`
(package `0BB33100`), where `AFTERLIFE.lua` now takes its constants from. The DLL keeps its own copy, so the
calculation does not need `PACK_CONSTANTS.lua` to be loaded; if you change a value there, change it in `SC4SIM.ini`
too (the Lua constants are not read by the DLL).

| Lua table | Key | Default |
| --- | --- | --- |
| `mortality_ratio` | `BASE_MORTALITY_RATE_PER_CAPITA` / `HEALTH_COVERAGE_WEIGHT` | 0.0006 / 1.5 |
| `judgment_ratio` | `BASE_SIN_RATIO` / `CRIME_WEIGHT` / `EDUCATION_WEIGHT` / `EDUCATION_REFERENCE` | 0.5 / 0.3 / 0.3 / 500 |
| | `SIN_RATIO_MIN` / `SIN_RATIO_MAX` | 0.1 / 0.9 |
| `imp_ratio` | `IMP_CLIENT_RATIO` / `IMP_SUCCESS_CHANCE` / `IMP_KILL_RANDOM_VARIANCE` / `MAX_IMP_KILL_FRACTION_POP` | 0.002 / 0.15 / 2 / 0.01 |
| `redemption_ratio` | `REDEMPTION_RATIO` | 0.0015 |
| `hell_native_ratio` | `HELL_IMP_RATIO` ... `HELL_GOETIA_ARS_GOETIA_RATIO` (INI names; `IMP_RATIO` ... in Lua) | 0.05, 0.03, 0.02, 0.008, 0.005, 0.001, 0.0005, 0.00005 |
| `hell_native_fixed` | `FALLEN_ANGELS` / `SEVEN_DEADLY_SINS` | 2 / 7 |

`EDUCATION_REFERENCE` is marked *TUNE THIS* in `PACK_CONSTANTS.lua`: adjust it to the `game.ga_education` values
your cities really reach.

A value that is not a number is ignored with a warning in `SC4SIM.log`; `EDUCATION_REFERENCE=0` is changed to 1.

`get_news_ticker_character` picks from the roster tables `news_ticker_event_characters` and
`hell_news_characters` of `PACK_CONSTANTS.lua`, so a pack can still change the roster in Lua. If the Lua does not
define them, the DLL uses its built-in copy of the same 13 characters and 5 event types (`AfterlifeModel.cpp`) and
says so once per city in `SC4SIM.log`. An unknown event type returns nothing, as in the Lua. The inputs of the calculation are the `game` values read from the city; if one is missing,
`run_afterlife_balance_variables` returns nothing, changes no state, and `SC4SIM.log` lists the missing names once per
city.

## Differences from the Lua

- **Random numbers.** The Lua seeded `math.randomseed` once with `game.g_population`, a weak seed. The DLL seeds
  its generator once from the operating system's entropy. Only the I.M.P. kill jitter and the ticker pick are random.
- **Cumulative values** (`WINNER_POPULATION_HEAVEN`, `SINNER_POPULATION_HELL`, `REDEEMED_TOTAL`, `IMP_KILLS_TOTAL`)
  are kept by the DLL and reset to zero when a city opens and when it closes. Nothing is saved in the city file, as
  before (the Lua table was not saved either).
- **Missing values** give `nil` instead of a Lua error (see above).
- **`#candidates`.** `get_news_ticker_character` in `AFTERLIFE.lua` uses the `#` length operator, which exists only
  from Lua 5.1; SimCity 4 runs Lua 5.0, where that line does not compile. The DLL version counts the list with
  `getn`, so it works; for the Lua fallback, `table.getn(candidates)` is the 5.0 form.
- A fractional `IMP_KILL_RANDOM_VARIANCE` is truncated to an integer, as `math.random` does in Lua 5.0; a negative one
  is treated as 0.

## Tested

`src/afterlife/AfterlifeModel.cpp` has no game or Windows dependency. `tools/afterlife-test` compares it with
`AFTERLIFE.lua` and `PACK_CONSTANTS.lua` run in a Lua interpreter, with the same inputs and random numbers. For 1.9.1:

- 12,000 cycles with random constants (300 sequences of 40 cycles): all 24 state values matched;
- the 23 DLL defaults are equal to the values of `PACK_CONSTANTS.lua`;
- the built-in roster is equal to `hell_news_characters` and `news_ticker_event_characters`, and the ticker pick
  returns the same character for every event type and index;
- 40,000 cycles with the real constants of `PACK_CONSTANTS.lua` (100 sequences of 400 cycles, up to 5 million
  residents): all 24 state values matched. That test
covers the calculation only. The Lua bridge (`Afterlife.cpp`) is compiled and statically checked, but **not yet
tested in game**: check `SC4SIM.log` after opening a city, then the values your UI shows.

The four `game` variable names the module reads were also checked in the 1.1.641 executable: all of them exist.

## Files

| File | Role |
| --- | --- |
| `src/afterlife/AfterlifeModel.h/.cpp` | The calculation (`Afterlife::Step`), a line-by-line port of `run_afterlife_balance_variables`; the default constants and the built-in news roster of `PACK_CONSTANTS.lua`. |
| `tools/afterlife-test` | Equivalence test with `AFTERLIFE.lua` and `PACK_CONSTANTS.lua`. |
| `src/afterlife/Afterlife.h/.cpp` | Module: reads the Lua inputs and constants, registers the two functions, mirrors `afterlife_grid_var`. |
