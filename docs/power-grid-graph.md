# Power Grid graph (1.13.0)

> **Not yet tested in game.**

A new graph of the Graphs window, **Power Grid** (`0x6534284A / 0xCA4AD545 / 0x0000001A`, in
`CAM_Exemplar_Graphs.dat`; button added by SC4SIM.dll at column 2, row 6, under the Power graph's column). It shows
the electric grid by load class, as the NEON mod ([SC4 Next-gen Energy Operating System](https://github.com/UlisseWolf/SC4-Nex-gen-Energy-Operating-System))
models it, and, when NEON is installed, NEON's own grid data.

| # | Line | History type | Source |
|---|---|---|---|
| 1 | Power Demand | `0xAA11F7D5` (`G_POWER_DEMANDED`) | game |
| 2 | Unused Power | `0xAA11F7D8` (`G_POWER_UNUSED`) | game |
| 3 | Base Load | `0x5C4DE041` | SC4SIM.dll: NEON `GEN_B_POWER`, or the plants without NEON |
| 4 | Intermediate Load | `0x5C4DE042` | SC4SIM.dll: NEON `GEN_I_POWER`, or the plants |
| 5 | Peak Load | `0x5C4DE043` | SC4SIM.dll: NEON `GEN_P_POWER`, or the plants |
| 6 | Base Used (NEON) | `0x5C4DE044` | NEON `USED_B_POWER` |
| 7 | Intermediate Used (NEON) | `0x5C4DE045` | NEON `USED_I_POWER` |
| 8 | Peak Used (NEON) | `0x5C4DE046` | NEON `USED_P_POWER` |
| 9 | Peak Shortage (NEON) | `0x5C4DE047` | NEON `SHORT_P_POWER` |
| 10 | Transmission Free kVA (NEON) | `0x5C4DE048` | NEON `TRANSMIT_AVAILABLE` |
| 11 | Distribution Free kVA (NEON) | `0x5C4DE049` | NEON `DISTRIB_AVAILABLE` |
| 12 | Transformer Free kVA (NEON) | `0x5C4DE04A` | NEON `TRANSFORM_AVAILABLE` |

Lines 1-9 are MWh per month; lines 10-12 are kVA (the free capacity of transmission substations, distribution
substations and transformers, never below 0).

## Without NEON

`src/power/PowerGraphHistory.cpp` groups the monthly output of the plants (the same reading as the Power graph's
energy classes) into NEON's three load classes:

- **Base**: coal, nuclear fission, nuclear fusion, geothermal, hydroelectric, biomass, waste to energy;
- **Intermediate**: solar, wind, tidal;
- **Peak**: oil, natural gas, microwave, generators.

Lines 6-12 are not recorded (NEON computes them from its own building groups and ratings).

## With NEON (optional, automatic)

Nothing to configure. On every SimNewMonth the DLL looks in the advisor Lua for NEON's `run_grid_balance_variables`
and `grid_logic_var` (NEON.lua, Lua package `0x0BB35100`). If both exist it calls `run_grid_balance_variables()` (as
NEON's own reward checks do) and records:

- lines 3-5 from NEON's production by load class (`GEN_B/I/P_POWER`: NEON's ratings corrected by the plants' real
  output, so they add up to the game's production, imports excluded);
- lines 6-9 from NEON's load balance: how much of each class the grid can use under NEON's limits (base at most
  60 %, intermediate at most 40 %, at least 20 % of demand from peak plants; the limits depend on difficulty and on
  the power-company buildings) and the peak shortage;
- lines 10-12 from NEON's free grid capacities.

If the call fails, the error is written to `SC4SIM.log` and the month is recorded as without NEON. The lines start at
the first month after installing (or after installing NEON).
