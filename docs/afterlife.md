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

The balance stays in `PACK_CONSTANTS.lua`. The DLL reads, at every call, the same tables the Lua used:

| Table | Fields |
| --- | --- |
| `mortality_ratio` | `BASE_MORTALITY_RATE_PER_CAPITA`, `HEALTH_COVERAGE_WEIGHT` |
| `judgment_ratio` | `EDUCATION_REFERENCE`, `BASE_SIN_RATIO`, `CRIME_WEIGHT`, `EDUCATION_WEIGHT`, `SIN_RATIO_MIN`, `SIN_RATIO_MAX` |
| `imp_ratio` | `IMP_CLIENT_RATIO`, `IMP_SUCCESS_CHANCE`, `IMP_KILL_RANDOM_VARIANCE`, `MAX_IMP_KILL_FRACTION_POP` |
| `redemption_ratio` | `REDEMPTION_RATIO` |
| `hell_native_ratio` | `IMP_RATIO`, `HELLHOUND_RATIO`, `SUCCUBUS_RATIO`, `INFESTOR_RATIO`, `POSSESSOR_RATIO`, `BAPHOMET_RATIO`, `REAPER_RATIO`, `GOETIA_ARS_GOETIA_RATIO` |
| `hell_native_fixed` | `FALLEN_ANGELS`, `SEVEN_DEADLY_SINS` |
| `news_ticker_event_characters`, `hell_news_characters` | roster tables used by `get_news_ticker_character` |

If a number is missing (for example because nothing loads `PACK_CONSTANTS.lua` once `AFTERLIFE.lua` no longer does
`dofile`), `run_afterlife_balance_variables` returns nothing, changes no state, and `SC4SIM.log` lists the missing
names once per city. **If the pack relies on `AFTERLIFE.lua` to load `PACK_CONSTANTS.lua`, keep that `dofile`.**

## Differences from the Lua

- **Random numbers.** The Lua seeded `math.randomseed` once with `game.g_population`, a weak seed. The DLL seeds
  its generator once from the operating system's entropy. Only the I.M.P. kill jitter and the ticker pick are random.
- **Cumulative values** (`WINNER_POPULATION_HEAVEN`, `SINNER_POPULATION_HELL`, `REDEEMED_TOTAL`, `IMP_KILLS_TOTAL`)
  are kept by the DLL and reset to zero when a city opens and when it closes. Nothing is saved in the city file, as
  before (the Lua table was not saved either).
- **Missing values** give `nil` instead of a Lua error (see above).
- A fractional `IMP_KILL_RANDOM_VARIANCE` is truncated to an integer, as `math.random` does in Lua 5.0; a negative one
  is treated as 0.

## Tested

`src/afterlife/AfterlifeModel.cpp` has no game or Windows dependency. It was compared with the original
`AFTERLIFE.lua` (run in a Lua interpreter with the same constants, inputs and random numbers) over 12,000 cycles of
random inputs and constants, in 300 sequences of 40 cycles: every one of the 24 state values matched. That test
covers the calculation only. The Lua bridge (`Afterlife.cpp`) is compiled and statically checked, but **not yet
tested in game**: check `SC4SIM.log` after opening a city, then the values your UI shows.

The four `game` variable names the module reads were also checked in the 1.1.641 executable: all of them exist.

## Files

| File | Role |
| --- | --- |
| `src/afterlife/AfterlifeModel.h/.cpp` | The calculation (`Afterlife::Step`), a line-by-line port of `run_afterlife_balance_variables`. |
| `src/afterlife/Afterlife.h/.cpp` | Module: reads the Lua inputs and constants, registers the two functions, mirrors `afterlife_grid_var`. |
