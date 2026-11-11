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

## Phantom funding sliders (1.10.0)

`[GameFixes] FundingSliderFix=true` (default) in `SC4SIM.ini`. Source: `src/fixes/BudgetFixes.cpp`. SimCity 4 1.1.641
only. *Static analysis; not yet tested in game.*

### The bug

A mod changes the monthly cost (`Budget Item: Cost`, `0xEA54D286`) of a civic building that is already in the city:
a school, a hospital, a police or fire station, a power plant. From then on, every month, the funding slider of those
buildings, and of the other buildings in the same department, moves the same way until it reaches 0 % or 120 %.
The usual remedies are to bulldoze the buildings plopped after the mod and remove the mod, or to bulldoze every
building of that type.

The game keeps two figures that should agree and never compares them:

| Figure | Where | Value |
| --- | --- | --- |
| Full funding of the line item (F) | `cSC4LineItem` `+0x38`, saved with the city (`Write` `0x006A9AA0`) | Sum of the costs **at the time each building was plopped**: `AddLocallyFundedObject` (`0x006AB0C0`) adds the cost, `RemoveLocallyFundedObject` (`0x006AA530`) subtracts the cost the building has when it is demolished. |
| Current cost of each building (H) | Property `0xEA54D286` | Usually not copied into the building: `cSCExemplarPropertyHolder::GetProperty` (`0x005FEDA0`) falls back to the exemplar, so after a mod it is the mod's cost. |

Every month `cSC4BudgetSimulator` (`0x00692510`) calls `RecalculateAllLineItemCurrentExpenses` (`0x006ABB50`, vtable
slot `+0x74`) on every department. It sums cost × local funding of the department's locally funded buildings into C
and FullExpenses of every line item into F, and sets the department to p′ = C / F if |p′ − p| > 0.5 points and
|C − p·F| > n + 2 §. It then writes p′ into every building's slider (`0x006AB810` with propagate = true). With H ≠ F,
p′ = p · (H<sub>local</sub> + F<sub>fixed</sub>) / (F<sub>local</sub> + F<sub>fixed</sub>), every month, until the
120 % cap or until one of the two tolerances stops it (about 3 % in a decay).

Not affected: water producers and pollution buildings, whose cost the game changes while they stand and copies into
the building, keeping F in step (`ChangeBudgetItemCost` from `0x006DED5A` and `0x006E41CE`); transit lots, whose
budget item is copied (`CopyBudgetItemProperties` from `0x00724037` / `0x007240C4`).

Why the remedies work: a building plopped and bulldozed while the mod is active adds and removes the same cost. When
the last building of a type is bulldozed, `cSC4ResidentialSimulator::DoBuildingChanged` (`0x006FB120`) removes the
line item and its F (`0x006FB25A`). The power simulator (`0x006EBF60`) does not, so the second remedy can leave a
residue for power plants.

### The fix

The module replaces vtable slot `0x00AB0F24` (`cSC4DepartmentBudget::RecalculateAllLineItemCurrentExpenses`,
`0x006ABB50`) with a function that resyncs the department, then calls the game's function. The resync sets
FullExpenses of every locally funded expense line item to the sum of the current costs of its buildings. It counts
them exactly as the game does: it walks the department's set of (property holder, purpose) pairs (`+0x58`, count
`+0x5C`) and adds every budget item of this department whose line is one of those line items, with no purpose
filter. C / F then equals the buildings' own funding, so the slider stays where the player put it. The resync also
runs when a city opens.

- A mod's new cost applies to the buildings already in the city at once. Installing or removing the mod no longer
  needs any bulldozing.
- A line item whose buildings are all gone (power plants), or whose plugin was removed, goes back to 0.
- The save format does not change. Without the DLL, a save loads as before.
- The module checks the game version (1.1.641), the slot (it must hold `0x006ABB50`) and the vtables of the budget
  simulator (`0x00AB0908`) and of each department (`0x00AB0EB0`) before reading them. Otherwise it does nothing and
  `SC4SIM.log` says why. The slot is restored when the game closes.

Sliders the bug has already moved stay where they are. To repair them, use `FundingReset` (every slider to 100 %) or
`FundingReset <percent>`, or set the sliders again in the budget window. `FundingSliderResetStuck=true` does it
automatically when a city opens, for departments that needed a resync and sit at 0 % or at their maximum.

### Cheats

| Cheat | Effect |
| --- | --- |
| `FundingResync` | Resyncs every department now and shows how many line items changed. |
| `FundingReset [percent]` | Sets every department with locally funded buildings to `percent` (0–120, default 100), and every building's slider with it. |
| `RemovePhantomBudgetItems <category> [all]` | See below. |

### To check in game

`SC4SIM.log`: `FundingSliderFix: department budgets resync full funding with the buildings' current costs every
month.`, then, when a city opens, `FundingSliderFix: city opened, N line item(s) corrected in M department(s).` Every
corrected line item is logged with its old and new full funding.

Test: a city with 10 elementary schools; install a mod that lowers their cost; advance 3 months. Without the fix the
education slider falls every month; with it, it stays put and the log shows the corrected full funding.

## Phantom budget items (1.10.0)

`[GameFixes] RemovePhantomBudgetItems=true` (default). Port of
[sc4-remove-phantom-budget-items](https://github.com/0xC0000054/sc4-remove-phantom-budget-items) by Nicholas Hayes
(MIT, `src/fixes/LICENSE-sc4-remove-phantom-budget-items.txt`), merged into SC4SIM with the author's consent.

When a building's plugin is removed, the building stays in its department's budget list. The cheat
`RemovePhantomBudgetItems <category> [all]` (category: `Fire`, `Police`, `Jail`, `Power`, `Education` or `Health`)
removes from that category the buildings without an Exemplar Type property (`0x10`), that is, whose plugin is no
longer installed, together with their line items. With `all` it clears the whole category. This is meant for
entries that remain after every building of the category has been demolished. The buildings, lots and their other
effects are not removed.

Differences from the standalone DLL:

- After a removal, SC4SIM resyncs the department at once, so the funding of the remaining buildings is right without
  waiting for the next month.
- If `SC4RemovePhantomBudgetItems.dll` is next to `SC4SIM.dll`, SC4SIM leaves the cheat to it and says so in the log.
  Remove the standalone DLL to use the SC4SIM version.

A removed plugin also feeds the funding slider bug: the game can no longer read the cost of those buildings, so the
cost leaves C but stays in F. The resync keeps the slider steady until the entries are removed.

## Port upgrades (1.11.0)

`[GameFixes] PortUpgradeFix=true` (default) in `SC4SIM.ini`. Source: `src/fixes/PortUpgradeFix.cpp`. SimCity 4 1.1.641
only.

### The bug

The port developer (`cSC4PortDeveloper`, vtable `0x00AB2790`) keeps one entry per seaport or airport, with the lot
and the utilisation (usage / capacity) last applied to the demand simulator (`+0x1C`). A building whose exemplar has
`0x27812844 = true` gets its Demand Satisfied (`0x27812840`) and Demand Created (`0x27812841`) from the port developer,
not from the demand simulator's lot state handler (`0x006A9470` skips it). Every month (`0x006E9980`) the port
developer calls `cSC4DemandSimulator::UpdateOccupantEffects` (vtable slot 7, `0x006A6C90`) with the lot's current
building type, the old and the new utilisation: the demand simulator removes old × values and adds new × values of
that building type (`0x006A5920`).

Maxis and PEG seaports have none of these properties. The BSC Functional Seaports give every stage Demand Satisfied on
the industrial caps `0x4A00`, `0x4B00` and `0x4C00` that grows with the stage (BRT Seaport: 2400, 7200, 12000, 16400,
21200, 25600, 30400, 35200), plus jobs, all with `0x27812844 = true`.

The upgrade (`0x006E92A0`, called from `0x006E9C0E`) sets the entry's "upgrade in progress" flag (`+0x18`), demolishes
the old lot and builds the next stage in the same entry, keeping `+0x1C`. The old lot's demolition (`0x006E9EA0`,
state 5) sees the flag, clears it and removes nothing. On the next month the port developer passes the new building
type, so the demand simulator removes old utilisation × the new stage's values instead of the old stage's. Each upgrade
loses utilisation × (new value − old value) of every cap, saved with the city:

| BRT Seaport, utilisation about 1 at each upgrade | Net relief of the port on each industrial cap |
|---|---|
| Stage 1 | +2400 |
| Stage 8, utilisation 1 | 35200 − 32800 = +2400: the stages added nothing |
| Stage 8, utilisation 0.5 (the upgrades added capacity) | 17600 − 32800 = −15200 |
| Demolished | −32800, for good |

This is the long-known "upgradeable seaports eat industrial cap relief" behaviour: it needs relief that changes with
the stage, which only the staged ports have.

### The fix

The call at `0x006E9C0E` goes to SC4SIM first. If the entry has a utilisation applied, SC4SIM calls
`UpdateOccupantEffects(old stage's building type, utilisation, 0)` and sets `+0x1C` to 0, then lets the game upgrade.
The next monthly update adds the new stage's values from zero, so each stage takes out exactly what it put in. Before
patching, SC4SIM checks the game version, that the call still targets `0x006E92A0` and that slot 7 of the demand
simulator's vtable is `0x006A6C90`; the original call is restored when the game closes. Each corrected upgrade is
written to `SC4SIM.log`.

Cities saved with the bug keep the relief already lost; the fix only stops further losses. Nicholas Hayes's
SeaportDemandCapFix changes the seaport trips term of the monthly connection demand (`0x006A3ED0`): it is a different
part of the code and works together with this fix.

### To check in game

Use a seaport of the BSC Functional Seaports with enough freight to upgrade. When it upgrades, `SC4SIM.log` shows
`PortUpgradeFix: port at cell (x, z) upgrading from building 0x...`. The I-D, I-M and I-HT cap relief (data view or
demand cheats) should grow with the stages instead of staying at the stage 1 value or dropping.
