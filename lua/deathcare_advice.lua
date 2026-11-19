--#-package:0BB3C400# -- package signature --
-- Lua file entry IID 0x8bb3c400
-- SC4SIM DEATHCARE ADVICE (SC4SIM.dll 1.13) --
-- The deathcare notices as advice of the Health & Education advisor, instead of the plain notification window of
-- earlier versions. SC4SIM.dll decides each month which notice is due (each at most once a year, with the thresholds
-- DEATHCARE_NOTIFY_UNCOLLECTED and DEATHCARE_NOTIFY_NEARLY_FULL of SC4SIM.ini) and leaves it in afterlife_grid_var:
--   DEATHCARE_ADVICE        0 none, 1 uncollected dead, 2 every cemetery full and no crematorium, 3 nearly full
--   DEATHCARE_ADVICE_COUNT  the dead left uncollected (1, 2) or the cemeteries nearly full (3)
--   DEATHCARE_ADVICE_X/_Z   the cell of the worst neighbourhood (1, 2)
-- The value stays for one game month. Texts: LTEXT 6A231EA4 / BB3C4000 - BB3C4005 (CAM_Locale_en.dat).
-- Without the DLL afterlife_grid_var.DEATHCARE_ADVICE stays 0 and no advice is given.
--------------------------------------------------------------------------------------------------
if (create_advice_hq == nil) then dofile("adv_hq.lua") end

if (afterlife_grid_var == nil) then afterlife_grid_var = {} end
if (afterlife_grid_var.DEATHCARE_ADVICE == nil) then afterlife_grid_var.DEATHCARE_ADVICE = 0 end
if (afterlife_grid_var.DEATHCARE_ADVICE_COUNT == nil) then afterlife_grid_var.DEATHCARE_ADVICE_COUNT = 0 end
if (afterlife_grid_var.DEATHCARE_ADVICE_X == nil) then afterlife_grid_var.DEATHCARE_ADVICE_X = 0 end
if (afterlife_grid_var.DEATHCARE_ADVICE_Z == nil) then afterlife_grid_var.DEATHCARE_ADVICE_Z = 0 end

-- Uncollected dead
a = create_advice_hq('bb3c4010')
a.trigger   = "afterlife_grid_var.DEATHCARE_ADVICE == 1"
a.title     = [[text@bb3c4000]]
a.message   = [[text@bb3c4001]]
a.priority  = tuning_constants.ADVICE_PRIORITY_URGENT
a.mood      = advice_moods.BAD_JOB
a.frequency = 300
a.timeout   = 30

-- Every cemetery full and no crematorium
a = create_advice_hq('bb3c4011')
a.trigger   = "afterlife_grid_var.DEATHCARE_ADVICE == 2"
a.title     = [[text@bb3c4002]]
a.message   = [[text@bb3c4003]]
a.priority  = tuning_constants.ADVICE_PRIORITY_URGENT
a.mood      = advice_moods.BAD_JOB
a.frequency = 300
a.timeout   = 30

-- Cemeteries nearly full
a = create_advice_hq('bb3c4012')
a.trigger   = "afterlife_grid_var.DEATHCARE_ADVICE == 3"
a.title     = [[text@bb3c4004]]
a.message   = [[text@bb3c4005]]
a.priority  = tuning_constants.ADVICE_PRIORITY_HIGH
a.mood      = advice_moods.BAD_JOB
a.frequency = 300
a.timeout   = 30
