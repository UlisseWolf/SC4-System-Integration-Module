# Fixed Underfunded Notices: priorities from SC4SIM.ini (1.10.0)

`[UnderfundedNotices]` in `SC4SIM.ini`. Source: `src/advisors/UnderfundedNotices.cpp`. *Not yet tested in game.*

## What FUN is

**Fixed Underfunded Notices (FUN)** by CoriBoom (v1.0, 2021) is included in CAM: Lua `0x4A5E8EF6` / `0x434FA499` in
`CAM_LUA_Controller.dat`, with its texts in the CAM locale. It deletes eight advisor notices and recreates them:

| Notice | GUID | Urgent file | Med-High file | `SC4SIM.ini` key |
| --- | --- | --- | --- | --- |
| Local medical facility underfunded ("Pockets Picked") | `2A71398D` | Urgent | Med-High | `HospitalUnderfunded` |
| Hospital workers strike | `EA5E97A8` | Urgent | Urgent | `HealthStrike` |
| Teachers threaten to strike (school underfunded) | `AA713E17` | Urgent | High | `SchoolUnderfunded` |
| Teachers strike | `0A5E79A1` | Urgent | Urgent | `SchoolStrike` |
| Library underfunded (Maxis's unused library strike) | `EAABFE7E` | Urgent | Medium | `LibraryUnderfunded` |
| Museum underfunded (Maxis's unused museum strike) | `EAAC049A` | Urgent | Medium | `MuseumUnderfunded` |
| Fire fighters strike | `2A64D230` | Urgent | Urgent | `FireStrike` |
| Police strike | `CA5E732A` | Urgent | Urgent | `PoliceStrike` |

The underfunded notices fire when a building of that kind has more users than its capacity, and link the camera to
it (also with the game paused). CoriBoom released two files that differ only in the four priorities above; CAM ships
the Urgent one. With SC4SIM the choice moves to `SC4SIM.ini`, and every notice can have its own priority.

## Settings

```ini
[UnderfundedNotices]
Enabled=true
Preset=Urgent          ; Urgent or MedHigh
HospitalUnderfunded=   ; empty = preset; VeryLow, Low, MedLow, MedMedLow, Medium, MedHigh, High, Urgent or 0-100
...
```

Named priorities are read from the game's `tuning_constants` (`ADVICE_PRIORITY_*`, CAM values: VeryLow 10, Low 20,
MedLow 27, MedMedLow 34, Medium 45, MedHigh 60, High 80, Urgent 100). A notice pops up as a window when its priority
reaches the popup threshold of the difficulty level (`ADVICE_POPUP_PRIORITIES_BY_SKILL_LEVEL`: Easy 60, Medium 80,
Hard 100); otherwise it only appears in the advisor panel. For example, with `MedHigh`, a school underfunded notice
(High, 80) pops up on Easy and Medium but not on Hard.

## How it works

The game builds each notice from its Lua record: `cSC4Advice::Init` (`0x00408C70`, SimCity 4 1.1.641) stores the
GUID, finds the record in the global `advices` table and copies its priority, frequency and timeout into the object
(`GetPriority`, `0x00407AB0`, returns the copy at `+0x78`). `Clone` (`0x00407C50`) calls `Init` again for every notice
the game shows, so the record is read every time.

When a city opens (`PostCityInit`), the module goes through `advices` in the advisor scripting context and, for each
of the eight GUIDs, writes the chosen value into the record's `priority` field. It changes a record only if its
title is the FUN text (`text@ea5655d2`, `text@8a565562`, ...), so without FUN the Maxis notices keep their own
priorities. Nothing is saved with the city; the priorities are set again every time a city opens.

The FUN Lua and its texts stay as they are; only their priority changes. Lua files and LTEXTs remain translatable
and editable as before.

## To check in game

`SC4SIM.log` when a city opens: one `UnderfundedNotices: <notice> (0x<GUID>) priority <n>.` line per notice, then
`UnderfundedNotices: 8 of 8 FUN notices set (preset Urgent).` If FUN is missing: `no FUN notice found`.

Test: set `Preset=MedHigh` and `LibraryUnderfunded=Urgent`, open a city on Medium difficulty with an overcrowded
library and a hospital over capacity. The library notice should pop up and the hospital one (Med-High, 60) should
only appear in the advisor panel.

Open point: a notice already created before the city opened (if the game creates any at load) keeps the priority
it was created with until the game creates it again.
