--#-package:0BB3C100# -- package signature (new range reserved for this fictitious simulation)
dofile("PACK_CONSTANTS.lua") -- loads all constants, rating tables, costs and unlock variables for the entire pack
-- DEPENDS ON: PACK_CONSTANTS.lua (0BB33100) -- all rating, cost and unlock tables are defined there.
-- Based on the NEON ENGINE by RobertaME / Ulisse Wolf --
-- AFTERLIFE ENGINE --
-- FICTITIOUS MORTALITY / AFTERLIFE SIMULATION -- inspired by the Heaven/Hell/Sinner mythology of
-- Hazbin Hotel and Helluva Boss (Vivienne Medrano). Sims who die are estimated and split into
-- "Winners" (Heaven) and "Sinners" (Hell); a small share of the standing Hell population then
-- hires I.M.P. (the assassination-for-hire company from Helluva Boss) to kill living Sims on Earth.
--------------------------------------------------------------------------------------------------
-- IMPORTANT DESIGN NOTE: this file does NOT and CANNOT modify the actual game simulation. Every
-- number it produces (deaths, Winners, Sinners, I.M.P. clients, I.M.P. kills) is a purely
-- fictitious, cosmetic statistic derived by READING native population/health/crime/education
-- variables from adv_game_data.lua. It never calls sc4game.budget.deposit_funds(), never touches
-- regional_supply, never counts or places any building, and never writes to any game.* variable.
-- Actual city population (game.g_population, game.g_city_r_population, etc.) is completely
-- unaffected by anything this file calculates -- Sims do not actually die in the underlying
-- simulation because of this script. It exists purely to generate flavor numbers (e.g. for display
-- in a UI/artifact built on top of it), consistent with the same "fictitious layer" principle
-- already used for the ISP mechanic in TELECOM.lua.
--------------------------------------------------------------------------------------------------
-- IMPORTANT NOTE ON RANDOMNESS: same caveat as TELECOM.lua -- math.randomseed() is seeded once
-- using game.g_population as a fallback, since the "os" library is not exposed in any reference
-- file provided (adv_game_data.lua, regional_supply_lua.txt). This is a weak seed; replace it if
-- the mod environment exposes better entropy (e.g. a game tick/timestamp variable).
--------------------------------------------------------------------------------------------------

local afterlife_seeded = false   -- guard so math.randomseed() only runs once per game session

-- Tuning table for the natural mortality estimate. BASE_MORTALITY_RATE_PER_CAPITA is a MONTHLY
-- rate (assuming this function is recalculated on the same monthly cadence as the other scripts'
-- upkeep costs), loosely modeled on a real-world crude death rate of ~0.7%/year (~0.06%/month),
-- worsened when the city's health coverage is poor.
-- (constants moved to PACK_CONSTANTS.lua)

-- Picks one character at random from the roster associated with the given event type (a key of
-- news_ticker_event_characters, e.g. "IMP_KILL_REPORT"), returning its full hell_news_characters
-- entry (or nil if the event type is unknown). A UI/artifact layered on top of this file would use
-- entry.ticker_text_id to look up the actual localized ticker line to display.
function get_news_ticker_character(event_type)
    local candidates = news_ticker_event_characters[event_type]
    if (candidates == nil or #candidates == 0) then
        return nil
    end
    local pick = candidates[math.random(1, #candidates)]
    return hell_news_characters[pick]
end

-- Set all afterlife grid logic variables to 0 to initialize them. WINNER_POPULATION_HEAVEN,
-- SINNER_POPULATION_HELL, REDEEMED_TOTAL and IMP_KILLS_TOTAL are CUMULATIVE and are never reset by
-- run_afterlife_balance_variables() below -- they only ever grow/shrink, representing the standing
-- population of the fictitious afterlife across the whole game session.
afterlife_grid_var = {
   HEALTH_QUALITY               = 0,   -- normalized game.g_health_coverage_p (0.0 - 1.0)
   SIN_RATIO                    = 0,   -- current Winner/Sinner split ratio (0.0 - 1.0, share going to Sinner)

   NATURAL_DEATHS                = 0,   -- Sims estimated to die of natural/health causes this cycle
   IMP_CLIENTS_THIS_CYCLE         = 0,   -- Sinners in Hell who hire I.M.P. this cycle
   IMP_KILLS_THIS_CYCLE           = 0,   -- living Sims actually killed by I.M.P. this cycle
   TOTAL_DEATHS_THIS_CYCLE        = 0,   -- NATURAL_DEATHS plus IMP_KILLS_THIS_CYCLE

   WINNER_COUNT_THIS_CYCLE         = 0,   -- of TOTAL_DEATHS_THIS_CYCLE, how many became Winners (Heaven) this cycle
   SINNER_COUNT_THIS_CYCLE         = 0,   -- of TOTAL_DEATHS_THIS_CYCLE, how many became Sinners (Hell) this cycle
   REDEEMED_THIS_CYCLE             = 0,   -- standing Sinners redeemed into Winners this cycle

   WINNER_POPULATION_HEAVEN       = 0,   -- CUMULATIVE, never reset
   SINNER_POPULATION_HELL         = 0,   -- CUMULATIVE, never reset
   REDEEMED_TOTAL                  = 0,   -- CUMULATIVE, never reset
   IMP_KILLS_TOTAL                 = 0,   -- CUMULATIVE, never reset

   IMP_POPULATION                  = 0,   -- Hell's native Imp population, snapshot of current SINNER_POPULATION_HELL
   HELLHOUND_POPULATION            = 0,   -- Hell's native Hellhound population, snapshot
   SUCCUBUS_POPULATION             = 0,   -- Hell's native Succubus population, snapshot
   INFESTOR_POPULATION             = 0,   -- Hell's native Infestor population, snapshot
   POSSESSOR_POPULATION            = 0,   -- Hell's native Possessor population, snapshot
   BAPHOMET_POPULATION             = 0,   -- Hell's native Baphomet population, snapshot
   REAPER_POPULATION               = 0,   -- Hell's native Reaper population, snapshot
   GOETIA_ARS_GOETIA_POPULATION    = 0,   -- Hell's native Goetia & Ars Goetia population, snapshot
   FALLEN_ANGELS_POPULATION        = 0,   -- FIXED, always hell_native_fixed.FALLEN_ANGELS
   SEVEN_DEADLY_SINS_POPULATION    = 0,   -- FIXED, always hell_native_fixed.SEVEN_DEADLY_SINS
   HELL_NATIVE_TOTAL               = 0,   -- sum of all ten native subclasses above (eight proportional + two fixed)
   ANGEL_POPULATION                = 0,   -- Heaven's native Angels, snapshot of WINNER_POPULATION_HEAVEN (SC4SIM 1.9.2)
   CHERUB_POPULATION               = 0,
   EXORCIST_POPULATION             = 0,
   SERAPHIM_POPULATION             = 0,
   SERAPHIM_COUNCIL_POPULATION     = 0,   -- FIXED, always heaven_native_fixed.SERAPHIM_COUNCIL
   HEAVEN_NATIVE_TOTAL             = 0    -- sum of the five Heaven native subclasses
}

-- Judgment ratio for an EQ (0-200), shifted by crime, plus a Sinner bonus, clamped.
function afterlife_sin_ratio(education, bonus)
    local education_factor = (education / judgment_ratio.EDUCATION_REFERENCE) - 1
    local ratio = judgment_ratio.BASE_SIN_RATIO
                  + (judgment_ratio.CRIME_WEIGHT * (game.ga_crime / 100))
                  - (judgment_ratio.EDUCATION_WEIGHT * education_factor)
                  + bonus
    return math.max(judgment_ratio.SIN_RATIO_MIN, math.min(judgment_ratio.SIN_RATIO_MAX, ratio))
end

-- Main function to call to recalculate this cycle's fictitious mortality/afterlife statistics.
-- Purely additive/read-only with respect to the real game state (see design note above).
function run_afterlife_balance_variables(ret_type)

    if (afterlife_seeded == false) then
        math.randomseed(game.g_population)
        afterlife_seeded = true
    end

    -- Base mortality (SC4SIM 1.9.3): with game.ga_life_exp, residents / (12 * life expectancy) die each month;
    -- without it, the old per-capita rate worsened by poor health coverage.
    afterlife_grid_var.HEALTH_QUALITY = math.max(math.min(game.g_health_coverage_p / 100, 1), 0)
    local life_exp = game.ga_life_exp or 0
    afterlife_grid_var.LIFE_EXPECTANCY = life_exp
    if (life_exp > 0) then
        afterlife_grid_var.BASE_DEATHS = math.floor(game.g_city_r_population * mortality_ratio.LIFE_EXPECTANCY_DEATH_SCALE / (12 * life_exp))
    else
        local mortality_rate = mortality_ratio.BASE_MORTALITY_RATE_PER_CAPITA
                              * (1 + mortality_ratio.HEALTH_COVERAGE_WEIGHT * (1 - afterlife_grid_var.HEALTH_QUALITY))
        afterlife_grid_var.BASE_DEATHS = math.floor(game.g_city_r_population * mortality_rate)
    end

    -- City Winner/Sinner judgment ratio (base deaths and I.M.P. victims).
    afterlife_grid_var.SIN_RATIO = afterlife_sin_ratio(game.ga_education, 0)

    -- Generation resets of the residential simulator (SC4SIM 1.9.3): afterlife_waves lists, per reset tract and
    -- wealth, { population, education, premature }. A share of them dies, judged with their own EQ, up to a cap.
    local wave_cap = math.floor(game.g_city_r_population * death_waves.MAX_WAVE_DEATH_FRACTION_POP)
    local old_age_wave_deaths = 0
    local wave_winners = 0
    afterlife_grid_var.PREMATURE_DEATHS = 0
    local i = 1
    while (afterlife_waves ~= nil and afterlife_waves[i] ~= nil) do
        local wave = afterlife_waves[i]
        local deaths = math.min(math.floor(wave.population * death_waves.WAVE_DEATH_SHARE), wave_cap)
        if (deaths > 0) then
            wave_cap = wave_cap - deaths
            local bonus = 0
            if (wave.premature) then
                afterlife_grid_var.PREMATURE_DEATHS = afterlife_grid_var.PREMATURE_DEATHS + deaths
                bonus = death_waves.PREMATURE_SIN_BONUS
            else
                old_age_wave_deaths = old_age_wave_deaths + deaths
            end
            wave_winners = wave_winners + math.floor(deaths * (1 - afterlife_sin_ratio(wave.education, bonus)))
        end
        i = i + 1
    end
    afterlife_grid_var.WAVE_DEATHS = old_age_wave_deaths + afterlife_grid_var.PREMATURE_DEATHS
    afterlife_grid_var.NATURAL_DEATHS = afterlife_grid_var.BASE_DEATHS + old_age_wave_deaths

    -- I.M.P.: a small share of the STANDING Hell population (accumulated from previous cycles,
    -- not just this cycle's new Sinners) commissions a hit on a living Sim this cycle. Uses an
    -- expected-value calculation with a small random jitter instead of rolling
    -- game.random_chance() once per client, since a populous, long-running city could have
    -- thousands of Hell residents and looping that many random rolls every cycle would be wasteful.
    afterlife_grid_var.IMP_CLIENTS_THIS_CYCLE = math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * imp_ratio.IMP_CLIENT_RATIO)

    local expected_kills = afterlife_grid_var.IMP_CLIENTS_THIS_CYCLE * imp_ratio.IMP_SUCCESS_CHANCE
    local jitter          = math.random(-imp_ratio.IMP_KILL_RANDOM_VARIANCE, imp_ratio.IMP_KILL_RANDOM_VARIANCE)
    local kill_cap         = math.floor(game.g_city_r_population * imp_ratio.MAX_IMP_KILL_FRACTION_POP)

    afterlife_grid_var.IMP_KILLS_THIS_CYCLE = math.max(math.floor(expected_kills + jitter), 0)
    afterlife_grid_var.IMP_KILLS_THIS_CYCLE = math.min(afterlife_grid_var.IMP_KILLS_THIS_CYCLE, kill_cap)

    -- Redemption: a small share of the STANDING Sinner population (same "before this cycle's new
    -- arrivals" snapshot used for I.M.P. above) is redeemed and moves from Hell to Heaven this
    -- cycle, mirroring Charlie Morningstar's redemption premise in Hazbin Hotel. Computed and
    -- applied here, BEFORE this cycle's new Sinners are added further down, so redemption only
    -- ever draws from Sinners who were already in Hell at the start of the cycle.
    afterlife_grid_var.REDEEMED_THIS_CYCLE = math.min(
        math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * redemption_ratio.REDEMPTION_RATIO),
        afterlife_grid_var.SINNER_POPULATION_HELL
    )
    afterlife_grid_var.SINNER_POPULATION_HELL   = afterlife_grid_var.SINNER_POPULATION_HELL - afterlife_grid_var.REDEEMED_THIS_CYCLE
    afterlife_grid_var.WINNER_POPULATION_HEAVEN = afterlife_grid_var.WINNER_POPULATION_HEAVEN + afterlife_grid_var.REDEEMED_THIS_CYCLE
    afterlife_grid_var.REDEEMED_TOTAL           = afterlife_grid_var.REDEEMED_TOTAL + afterlife_grid_var.REDEEMED_THIS_CYCLE

    -- Total deaths this cycle: natural causes plus I.M.P. murder victims, all judged with the same
    -- Winner/Sinner ratio computed above (murder victims are not automatically assumed to be
    -- Sinners themselves -- they are judged the same way as everyone else who died this cycle).
    afterlife_grid_var.TOTAL_DEATHS_THIS_CYCLE = afterlife_grid_var.NATURAL_DEATHS + afterlife_grid_var.PREMATURE_DEATHS
                                               + afterlife_grid_var.IMP_KILLS_THIS_CYCLE

    afterlife_grid_var.WINNER_COUNT_THIS_CYCLE = math.floor((afterlife_grid_var.BASE_DEATHS + afterlife_grid_var.IMP_KILLS_THIS_CYCLE)
                                                 * (1 - afterlife_grid_var.SIN_RATIO)) + wave_winners
    afterlife_grid_var.SINNER_COUNT_THIS_CYCLE = afterlife_grid_var.TOTAL_DEATHS_THIS_CYCLE - afterlife_grid_var.WINNER_COUNT_THIS_CYCLE

    -- Accumulate into the standing afterlife populations. SINNER_POPULATION_HELL grows here AFTER
    -- IMP_CLIENTS_THIS_CYCLE and REDEEMED_THIS_CYCLE were already computed above from last cycle's
    -- total, so both draw from Hell's population as it stood at the START of this cycle, not
    -- inflated by the Sinners who just died a moment ago in this same cycle.
    afterlife_grid_var.WINNER_POPULATION_HEAVEN = afterlife_grid_var.WINNER_POPULATION_HEAVEN + afterlife_grid_var.WINNER_COUNT_THIS_CYCLE
    afterlife_grid_var.SINNER_POPULATION_HELL   = afterlife_grid_var.SINNER_POPULATION_HELL + afterlife_grid_var.SINNER_COUNT_THIS_CYCLE
    afterlife_grid_var.IMP_KILLS_TOTAL          = afterlife_grid_var.IMP_KILLS_TOTAL + afterlife_grid_var.IMP_KILLS_THIS_CYCLE

    -- Hell's NATIVE population (Hellborn, never-human demons): the eight proportional subclasses
    -- are recomputed each cycle as a fresh snapshot proportion of the now-updated standing Sinner
    -- population, NOT accumulated on top of themselves (unlike Winners/Sinners/Redeemed/I.M.P.
    -- kills, which are running totals of discrete events) -- this represents the current size of
    -- Hell's native demon society, which grows and shrinks together with the Sinner population it
    -- depends on. The two FIXED subclasses (Fallen Angels, Seven Deadly Sins) are simply set to
    -- their constant lore values every cycle, completely independent of population.
    afterlife_grid_var.IMP_POPULATION              = math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * hell_native_ratio.IMP_RATIO)
    afterlife_grid_var.HELLHOUND_POPULATION         = math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * hell_native_ratio.HELLHOUND_RATIO)
    afterlife_grid_var.SUCCUBUS_POPULATION          = math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * hell_native_ratio.SUCCUBUS_RATIO)
    afterlife_grid_var.INFESTOR_POPULATION          = math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * hell_native_ratio.INFESTOR_RATIO)
    afterlife_grid_var.POSSESSOR_POPULATION         = math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * hell_native_ratio.POSSESSOR_RATIO)
    afterlife_grid_var.BAPHOMET_POPULATION          = math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * hell_native_ratio.BAPHOMET_RATIO)
    afterlife_grid_var.REAPER_POPULATION            = math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * hell_native_ratio.REAPER_RATIO)
    afterlife_grid_var.GOETIA_ARS_GOETIA_POPULATION = math.floor(afterlife_grid_var.SINNER_POPULATION_HELL * hell_native_ratio.GOETIA_ARS_GOETIA_RATIO)

    afterlife_grid_var.FALLEN_ANGELS_POPULATION     = hell_native_fixed.FALLEN_ANGELS       -- FIXED: always 2
    afterlife_grid_var.SEVEN_DEADLY_SINS_POPULATION = hell_native_fixed.SEVEN_DEADLY_SINS    -- FIXED: always 7

    afterlife_grid_var.HELL_NATIVE_TOTAL = afterlife_grid_var.IMP_POPULATION + afterlife_grid_var.HELLHOUND_POPULATION
                                          + afterlife_grid_var.SUCCUBUS_POPULATION + afterlife_grid_var.INFESTOR_POPULATION
                                          + afterlife_grid_var.POSSESSOR_POPULATION + afterlife_grid_var.BAPHOMET_POPULATION
                                          + afterlife_grid_var.REAPER_POPULATION + afterlife_grid_var.GOETIA_ARS_GOETIA_POPULATION
                                          + afterlife_grid_var.FALLEN_ANGELS_POPULATION + afterlife_grid_var.SEVEN_DEADLY_SINS_POPULATION

    -- Hell's natives always outnumber the Sinners: any shortfall is made up with Imps.
    if (afterlife_grid_var.HELL_NATIVE_TOTAL <= afterlife_grid_var.SINNER_POPULATION_HELL) then
        local shortfall = afterlife_grid_var.SINNER_POPULATION_HELL + 1 - afterlife_grid_var.HELL_NATIVE_TOTAL
        afterlife_grid_var.IMP_POPULATION = afterlife_grid_var.IMP_POPULATION + shortfall
        afterlife_grid_var.HELL_NATIVE_TOTAL = afterlife_grid_var.HELL_NATIVE_TOTAL + shortfall
    end

    -- Heaven's NATIVE population (Heavenborn angels), a snapshot of the Winners, as in Hell.
    afterlife_grid_var.ANGEL_POPULATION            = math.floor(afterlife_grid_var.WINNER_POPULATION_HEAVEN * heaven_native_ratio.ANGEL_RATIO)
    afterlife_grid_var.CHERUB_POPULATION           = math.floor(afterlife_grid_var.WINNER_POPULATION_HEAVEN * heaven_native_ratio.CHERUB_RATIO)
    afterlife_grid_var.EXORCIST_POPULATION         = math.floor(afterlife_grid_var.WINNER_POPULATION_HEAVEN * heaven_native_ratio.EXORCIST_RATIO)
    afterlife_grid_var.SERAPHIM_POPULATION         = math.floor(afterlife_grid_var.WINNER_POPULATION_HEAVEN * heaven_native_ratio.SERAPHIM_RATIO)
    afterlife_grid_var.SERAPHIM_COUNCIL_POPULATION = heaven_native_fixed.SERAPHIM_COUNCIL
    afterlife_grid_var.HEAVEN_NATIVE_TOTAL = afterlife_grid_var.ANGEL_POPULATION + afterlife_grid_var.CHERUB_POPULATION
                                            + afterlife_grid_var.EXORCIST_POPULATION + afterlife_grid_var.SERAPHIM_POPULATION
                                            + afterlife_grid_var.SERAPHIM_COUNCIL_POPULATION
    -- Heaven's natives always outnumber the Winners: any shortfall is made up with Angels.
    if (afterlife_grid_var.HEAVEN_NATIVE_TOTAL <= afterlife_grid_var.WINNER_POPULATION_HEAVEN) then
        local shortfall = afterlife_grid_var.WINNER_POPULATION_HEAVEN + 1 - afterlife_grid_var.HEAVEN_NATIVE_TOTAL
        afterlife_grid_var.ANGEL_POPULATION = afterlife_grid_var.ANGEL_POPULATION + shortfall
        afterlife_grid_var.HEAVEN_NATIVE_TOTAL = afterlife_grid_var.HEAVEN_NATIVE_TOTAL + shortfall
    end

    -- Function call return values --
    if (ret_type == "D") then
        return afterlife_grid_var.TOTAL_DEATHS_THIS_CYCLE      -- total Sims who died this cycle (natural + I.M.P.)
    elseif (ret_type == "W") then
        return afterlife_grid_var.WINNER_COUNT_THIS_CYCLE       -- Sims who became Winners (Heaven) this cycle
    elseif (ret_type == "S") then
        return afterlife_grid_var.SINNER_COUNT_THIS_CYCLE       -- Sims who became Sinners (Hell) this cycle
    elseif (ret_type == "C") then
        return afterlife_grid_var.IMP_CLIENTS_THIS_CYCLE        -- Sinners who hired I.M.P. this cycle
    elseif (ret_type == "K") then
        return afterlife_grid_var.IMP_KILLS_THIS_CYCLE          -- living Sims killed by I.M.P. this cycle
    elseif (ret_type == "R") then
        return afterlife_grid_var.REDEEMED_THIS_CYCLE            -- Sinners redeemed into Winners this cycle
    elseif (ret_type == "H") then
        return afterlife_grid_var.SINNER_POPULATION_HELL        -- cumulative standing Hell (Sinner) population
    elseif (ret_type == "P") then
        return afterlife_grid_var.WINNER_POPULATION_HEAVEN      -- cumulative standing Heaven population
    elseif (ret_type == "T") then
        return afterlife_grid_var.IMP_KILLS_TOTAL               -- cumulative I.M.P. kills across the whole game session
    elseif (ret_type == "N") then
        return afterlife_grid_var.HELL_NATIVE_TOTAL              -- current total native (Hellborn) Hell population
    elseif (ret_type == "A") then
        return afterlife_grid_var.HEAVEN_NATIVE_TOTAL            -- current total native (Heavenborn) Heaven population
    end
end
