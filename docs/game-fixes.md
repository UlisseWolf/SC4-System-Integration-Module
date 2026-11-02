# Game fixes

## Trip capacity by wealth ("high-tech industry has no $$$ jobs", 1.9.3)

`[GameFixes] TripCapacityWealthFix=true` (default) in `SC4SIM.ini`. Source: `src/fixes/TripCapacityFix.cpp`.

### The bug

`cSC4Lot::UpdateJobs` (`0x006BF6A0`, SimCity 4 1.1.641) copies the lot's $, $$ and $$$ job counts into the traffic
simulator's maximum trip capacity (lot property `0x8A0B4100`, indices 0-2), the value the path finder checks when a
commuter looks for a workplace. For each wealth it:

1. rounds the job count to a whole number;
2. reads the current capacity with `GetMaxTripCapacity`, but always for wealth 0: the three reads at `0x006BF6C9`,
   `0x006BF72F` and `0x006BF795` all `push 0`;
3. writes the count with `SetMaxTripCapacity` for the right wealth (`push 0`, `push 1`, `push 2`) only if it differs
   from the value read by more than 10 % of the larger of the two (`0.1` at `0x00A8C950`).

So the $$ and $$$ capacities are written only when their counts differ from the **$** capacity by more than 10 %.
Otherwise a new lot keeps a capacity of 0 for that wealth (no commuter of that wealth can work there) and an older lot
keeps a stale value. The Mac build does the same: this is how Maxis shipped it.

| Case | What happens |
| --- | --- |
| Stock high-tech industry | $ 10 %, $$ 80 %, $$$ 10 %: $ and $$$ are equal, so $$$ is never written. |
| Data fixes (HeinBloed 10/78/12, CAM 15/60/25) | They work on larger lots; rounding brings the bug back on small ones (a 20-job lot at 10/78/12 has $ 2 and $$$ 2.4 → 2). |
| Medical Clinic, solar and nuclear plants | The stock jobs lose the Clinic's $$$, Solar's $$$ and Nuclear's $$. |
| SC4SIM mixed occupancy | Secondary jobs added by the `GetJobs` hook pass through `UpdateJobs` too. |

### The fix

The operand byte of the $$ read's `push 0` (`0x006BF730`) becomes `01` and that of the $$$ read (`0x006BF796`)
becomes `02`, so each check reads its own capacity. The module patches only if both reads are `6A 00 57`
(`push 0; push edi`) and both matching writes are `6A 01 57` / `6A 02 57`; otherwise nothing is patched and
`SC4SIM.log` says why. The bytes are restored when the game closes.

No data change is needed and every building is fixed at once; the data fixes do no harm. Existing lots catch up the
next time the game refreshes their travel population (`UpdateJobs`' only caller is `cSC4Lot::AddTravelPopulation`,
call at `0x006C0CE1`).

### To check in game

`SC4SIM.log`: `TripCapacityFix: cSC4Lot::UpdateJobs reads the $$ and $$$ trip capacities for their own wealth (2 of 2
sites).` On a stock high-tech lot, lot property `0x8A0B4100` index 2 (the $$$ trip capacity) should become nonzero
after the patch; without it, it stays 0.
