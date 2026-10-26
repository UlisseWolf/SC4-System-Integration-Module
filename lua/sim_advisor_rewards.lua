--#-package:0151a000# -- package signature --
-- SC4 System Integration Module (SIM) - advisor rewards
-- Any Lua file that uses these functions must have a higher package number (NEON: 0BB35100).
--
-- Maxis only defines reward creators for three advisors: create_reward_cityplanning (adv_cityplanning.lua),
-- create_reward_finances (adv_finances.lua) and create_reward_safety (adv_safety.lua).
-- SIM adds the same function for the other advisors, so a mod can let the right advisor deliver its rewards:
--
--    a = create_reward_utilities('<building IID>')        -- Utilities advisor
--    a = create_reward_transportation('<building IID>')   -- Transportation advisor
--    a = create_reward_environment('<building IID>')      -- Environment advisor
--    a = create_reward_health_education('<building IID>') -- Health & Education advisor
--
-- They work exactly like create_reward_cityplanning: a cSC4RewardAdvice record (class 0xAA371C32), not "once",
-- with the advice type of that advisor. A function is defined only if no other file defines it already.
-- Written by Ulisse Wolf 2026 for the Colossus Addon Mod (CAM) - SC4 System Integration Module.

sim_advisor_rewards = {}
sim_advisor_rewards.version = "1.0"

-- Each function loads the Maxis script of its advisor when the advice creator is not defined yet.
function sim_advisor_rewards.finish(a)
   a.class_id = hex2dec("aa371c32") -- cSC4RewardAdvice class ID
   a.once = 0
   return a
end

if (create_reward_utilities == nil) then
   function create_reward_utilities(guid_string)
      if (create_advice_utilities_with_base == nil) then dofile("adv_utilities.lua") end
      return sim_advisor_rewards.finish(create_advice_utilities_with_base(guid_string, nil))
   end
end

if (create_reward_transportation == nil) then
   function create_reward_transportation(guid_string)
      if (create_advice_transportation_with_base == nil) then dofile("adv_transportation.lua") end
      return sim_advisor_rewards.finish(create_advice_transportation_with_base(guid_string, nil))
   end
end

if (create_reward_environment == nil) then
   function create_reward_environment(guid_string)
      -- "environemt" (sic) is the Maxis name in adv_environment.lua
      if (create_advice_environemt_with_base == nil) then dofile("adv_environment.lua") end
      return sim_advisor_rewards.finish(create_advice_environemt_with_base(guid_string, nil))
   end
end

if (create_reward_health_education == nil) then
   function create_reward_health_education(guid_string)
      if (create_advice_hq_with_base == nil) then dofile("adv_hq.lua") end
      return sim_advisor_rewards.finish(create_advice_hq_with_base(guid_string, nil))
   end
end
