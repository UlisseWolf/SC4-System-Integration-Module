--#-package:0C4DD500# -- package signature --
-- Lua file entry IID 0x8c4dd500, group 0x4a5e8f3f (automata scripts, loaded before the C++ part of the game)
-- SC4SIM DEATHCARE HEARSES (SC4SIM.dll 1.9.4) --
-- Cosmetic: hearses drive out of SC4SIM cemeteries and crematoria on the roads around them. They use the game's own
-- "hearse" automata group (vehicles.lua, the vehicle of the Maxis cemetery) and do not change the simulation; the
-- dead are collected by SC4SIM.dll by radius and capacity (docs/deathcare.md).
--------------------------------------------------------------------------------------------------
dofile("_templates.lua")
dofile("vehicles.lua")

generator.sc4sim_hearse_out =
{
   automata = { "hearse" },
   count = 1,
   rate = 2,                              -- ...X times per...
   rate_scale = RateScale.PER_HOUR,       -- ...game hour
   max_count = 2,
   radius = { 16, 48 },                   -- meters from the lot
   follow_roads = true,
   destroy_automata = true,
   lifetime = 120,
}

-- Crematorium (SC4SIM), occupant group 0x5C4DD100
occupant_group.sc4sim_crematorium =
{
   group_id = "0x5C4DD100",
   controllers = { "sc4sim_hearse_out" },
}

-- Cemetery (SC4SIM Deathcare), occupant group 0x5C4DD101
occupant_group.sc4sim_deathcare_cemetery =
{
   group_id = "0x5C4DD101",
   controllers = { "sc4sim_hearse_out" },
}
