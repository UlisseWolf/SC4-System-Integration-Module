-----------------------------------------------------------------------------------------
-- This file centralises all constants, tuning tables, rating tables, cost tables,
-- unlock thresholds, resource IDs and character tables for the entire pack.
-- It must load BEFORE every other pack file and should be treated as read-only
-- at runtime: no simulation function ever writes to any value defined here.
--
dofile("adv_game_data.lua")       -- native game variables (game.g_*, game.ga_*, etc.)
dofile("regional_supply_lua.txt") -- regional_supply.add_to_supply / .add_to_demand etc.

-- Load order enforced by package numbers (ascending):
--   THIS FILE      0BB33100
--   QUANTUMNET     0BB34100
--   NEON           0BB35100
--   AQUA           0BB36100
--   WASTE          0BB37100
--   TELECOM        0BB39100
--   AFTERLIFE      0BB3C100
--   CTOS           0BB3D100

--#-package:0BB33100#
-- Based on the NEON ENGINE by RobertaME / Ulisse Wolf --
-- PACK_CONSTANTS --
-- CENTRALISED CONSTANTS FOR NEON / AQUA / WASTE / QUANTUMNET / TELECOM / AFTERLIFE / CTOS --

------------------------------------------------------------------------------------------
-- Abbreviations used in constant names
--
-- Prefixes:
--   g_   global value (city-wide)
--   ga_  average value (city-wide)
--   r_   ratio / multiplier
--   n_   count / number
--   f_   fixed value (never changes during a play session)
--
-- Suffixes:
--   _POP      population threshold (Sims)
--   _DEMAND   resource-demand threshold (GPD / TPD / Mbps / QNU / DPD)
--   _RATIO    dimensionless ratio (0.0 - 1.0 unless noted)
--   _MONTHLY  monthly cost or payment in Simoleons
--   _CHANCE   percentage probability for game.random_chance()  (0 - 100)
--   _GPD      Gallons Per Day
--   _TPD      Tons Per Day
--   _MBPS     Megabits Per Second
--   _QNU      QuantumNet Units Per Day
--   _DPD      Data Points Per Day
--   _MW       Megawatts
--   _KVA      Kilovolt-Ampere
------------------------------------------------------------------------------------------


-- ============================================================
-- NEON -- Building Hex ID Tables
-- (hex_id of every lot tracked by NEON.lua)
-- ============================================================

generator_hex_id = {
   DIESEL_945_KW              = "B694726D",   ---PEG-MTP LT Generator---OG_ID 0xBB352003---
   NAT_GAS_180_KW             = "AEA950C4",   ---Stromgenerator---OG_ID 0xBB352100---
   PV_PANEL_SET_25_KW         = "490FF4A1"    ---Modular Solar Panels---OG_ID 0xBB350010---
}

transformer_hex_id = {
   COM_1_5_MVA_1              = "2D2F5802",   ---KK_Transformers_COM_1_5MVA---OG_ID 0xBB350024---
   COM_1_5_MVA_2              = "ED2F5742",   ---KK_Transformers_COM_1_5MVA---OG_ID 0xBB350024---
   COM_1_5_MVA_3              = "ED2F5567",   ---KK_Transformers_COM_1_5MVA---OG_ID 0xBB350024---
   COM_1_5_MVA_4              = "6D2F54B6",   ---KK_Transformers_COM_1_5MVA---OG_ID 0xBB350024---
   COM_1_5_MVA_5              = "2D2F5915",   ---KK_Transformers_COM_1_5MVA---OG_ID 0xBB350024---
   COM_1_5_MVA_6              = "CD2F58C3",   ---KK_Transformers_COM_1_5MVA---OG_ID 0xBB350024---
   COM_1_5_MVA_7              = "0D318080",   ---KK_Transformers_COM_1_5MVA---OG_ID 0xBB350024---
   IND_3_MVA_1                = "CD30721B",   ---KK_Transformers_IND_3MVA---OG_ID 0xBB350025---
   IND_3_MVA_2                = "8D307337"    ---KK_Transformers_IND_3MVA---OG_ID 0xBB350025---
}

-- (remaining hex_id blocks from NEON.lua omitted for brevity -- copy verbatim from original)

-- ============================================================
-- NEON -- kVA Rating Table
-- Maps every tracked building OG_ID to its kVA capacity.
-- Used by run_grid_balance_variables() in NEON.lua.
-- kVA = kW / power_factor. Assumed power_factor = 0.8 throughout.
-- ============================================================

kva_rating = {}            -- populated by the extracted block below (see original NEON.lua lines 113-561)
-- (full kva_rating table: copy verbatim from NEON.lua lines 113-561)

-- ============================================================
-- NEON -- MWh Rating Table
-- Maps every tracked building OG_ID to its MW power output.
-- For W2E Plants the value is adjusted each cycle by
-- game_trends.G_GARBAGE_TO_ENERGY (see NEON.lua run_grid_balance_variables).
-- ============================================================

mwh_rating = {}            -- (full mwh_rating table: copy verbatim from NEON.lua lines 562-1063)

-- ============================================================
-- NEON -- Building Upkeep Costs (Simoleons/month)
-- One entry per OG_ID. Read by NEON.lua's get_dist_substation_cost_monthly()
-- and get_trans_station_cost_monthly() helper functions.
-- ============================================================

amps_building_costs_monthly = {} -- (copy verbatim from NEON.lua lines 1064-1575)

-- ============================================================
-- NEON -- Unlock Variables
-- Population, infrastructure and date thresholds that gate each
-- reward building in NEON.lua's reward system.
-- All values are city-level counters readable from adv_game_data.
-- ============================================================

unlock_variables = {

   -- Generator unlocks --
   DIESEL_GEN_945_KW_MONTH_AVAIL           = 3,      -- months since game start required
   DIESEL_GEN_945_KW_DISTRIB_AVAIL         = 1000,   -- kVA of distribution capacity required
   NAT_GAS_GEN_180_KW_MONTH_AVAIL          = 2,
   NAT_GAS_GEN_180_KW_DISTRIB_AVAIL        = 1000,
   NAT_GAS_GEN_180_KW_POP                  = 10000,

   -- Transformer unlocks --
   PAD_MOUNT_TRANS_50_KVA_R23_POP          = 1000,
   COM_TRANS_1500_KVA_CS_POP               = 1000,
   IND_TRANS_3_MVA_IDM_POP                 = 1000,
   IND_TRANS_6_MVA_IDM_POP                 = 5000,

   -- Transmission substation unlocks --
   TRANS_SUBSTATION_60_MVA_S_BRIDGE        = "No",   -- set to "Yes" at runtime by NEON.lua if game.g_bridge_tile_count qualifies
   TRANS_SUBSTATION_60_MVA_S_BRIDGE_TILES  = 1

   -- (remaining unlock_variables entries: copy verbatim from NEON.lua lines 1576-1639)
}

-- ============================================================
-- NEON -- Power Company Buildings
-- Buildings that belong to the Power Company reward chain
-- (Front Office, Maintenance, Central Office).
-- ============================================================

power_company_buildings = {} -- (copy verbatim from NEON.lua lines 1864-1874)


-- ============================================================
-- AQUA -- GPD Rating Table
-- Gallons Per Day capacity per building.
-- For Sewage Treatment and Sanitizer Plants this represents
-- SEWAGE TREATMENT capacity, NOT fresh water production.
-- ============================================================

gpd_rating = {
   ID_BB360000 = 50000,      -- Water Tower Small
   ID_BB360001 = 150000,     -- Water Tower Large
   ID_BB360010 = 250000,     -- Acquifer Pump Small
   ID_BB360011 = 750000,     -- Acquifer Pump Medium
   ID_BB360012 = 1500000,    -- Acquifer Pump Large
   ID_BB360020 = 1000000,    -- Fresh Pump Small
   ID_BB360021 = 4000000,    -- Fresh Pump Medium
   ID_BB360022 = 10000000,   -- Fresh Pump Large
   ID_BB360030 = 2000000,    -- Desalination Plant Small
   ID_BB360031 = 8000000,    -- Desalination Plant Large
   ID_BB360040 = 1000000,    -- Sewage Treatment Plant Small  (sewage treatment capacity only)
   ID_BB360041 = 5000000,    -- Sewage Treatment Plant Large  (sewage treatment capacity only)
   ID_BB360050 = 2000000,    -- Sewage Sanitizer Plant Small  (sewage treatment capacity)
   ID_BB360051 = 10000000    -- Sewage Sanitizer Plant Large  (sewage treatment capacity)
}

-- ============================================================
-- AQUA -- Reclaimed GPD Rating Table
-- Fresh water PRODUCTION fed back into the network per Sewage
-- Sanitizer Plant only. Sewage Treatment Plants are never listed
-- here: they treat sewage but never produce potable water.
-- ============================================================

reclaimed_gpd_rating = {
   ID_BB360050 = 900000,     -- Sewage Sanitizer Plant Small (reclaimed water output_GPD)
   ID_BB360051 = 4500000     -- Sewage Sanitizer Plant Large (reclaimed water output_GPD)
}

-- ============================================================
-- AQUA -- Building Upkeep Costs (Simoleons/month)
-- ============================================================

aqua_building_costs_monthly = {
   ID_BB360000 = 25,
   ID_BB360001 = 60,
   ID_BB360010 = 120,
   ID_BB360011 = 320,
   ID_BB360012 = 600,
   ID_BB360020 = 450,
   ID_BB360021 = 1500,
   ID_BB360022 = 3400,
   ID_BB360030 = 2200,       -- Desalination: higher cost due to energy consumption
   ID_BB360031 = 8000,
   ID_BB360040 = 500,
   ID_BB360041 = 2000,
   ID_BB360050 = 1200,       -- Sanitizer: advanced tech, higher cost
   ID_BB360051 = 4800
}

-- ============================================================
-- AQUA -- Unlock Variables
-- Thresholds gating each water-infrastructure reward in AQUA.lua.
-- ============================================================

unlock_variables_water = {
   TOWER_50K_POP                    = 500,     -- minimum game.g_population
   TOWER_150K_POP                   = 2000,
   AQUIFER_250K_POP                 = 1000,
   AQUIFER_750K_POP                 = 5000,
   AQUIFER_750K_DEMAND              = 400000,  -- minimum game.g_water_consumed (GPD)
   AQUIFER_1_5M_POP                 = 15000,
   AQUIFER_1_5M_DEMAND              = 1000000,
   FRESH_PUMP_1M_POP                = 10000,
   FRESH_PUMP_4M_POP                = 25000,
   FRESH_PUMP_4M_DEMAND             = 3000000,
   FRESH_PUMP_10M_POP               = 60000,
   FRESH_PUMP_10M_DEMAND            = 8000000,
   DESAL_2M_POP                     = 20000,   -- also requires game.g_city_has_sea >= 1
   DESAL_8M_POP                     = 50000,
   SEWAGE_TREAT_1M_POP              = 3000,
   SEWAGE_TREAT_1M_PUMP_SHUTDOWNS   = 1,       -- OR game.g_water_pollution_pump_shutdown_count >= 1
   SEWAGE_TREAT_5M_POP              = 20000,
   SEWAGE_SANITIZE_2M_POP           = 25000,
   SEWAGE_SANITIZE_2M_TREAT_COUNT   = 1,       -- requires at least 1 Sewage Treatment Plant built
   SEWAGE_SANITIZE_10M_POP          = 60000,
   SEWAGE_SANITIZE_10M_CAPACITY     = 3000000  -- minimum SEWAGE_TREAT_CAPACITY (GPD)
}

-- ============================================================
-- AQUA -- Water Ratio Constants
-- ============================================================

water_ratio = {
   SEWAGE_RETURN_RATIO = 0.80  -- fraction of water consumed that returns as sewage
}


-- ============================================================
-- WASTE -- TPD Rating Table
-- Tons Per Day disposal/processing capacity per building.
-- For Recycling Centers: TPD diverted. For Incinerators and
-- W2E Plants: TPD burned. W2E Plants also appear in
-- waste_energy_rating for their MW output.
-- ============================================================

tpd_rating = {
   ID_BB370010 = 100,     -- Recycling Center Small
   ID_BB370011 = 400,     -- Recycling Center Large
   ID_BB370020 = 200,     -- Incinerator Small
   ID_BB370021 = 800,     -- Incinerator Large
   ID_BB370030 = 300,     -- Waste-to-Energy Plant Small  (disposal capacity_TPD)
   ID_BB370031 = 1200     -- Waste-to-Energy Plant Large  (disposal capacity_TPD)
}

-- ============================================================
-- WASTE -- Waste-to-Energy Power Rating Table
-- MW of electricity fed to the grid per W2E Plant.
-- Incinerators are NOT listed here: they produce no power.
-- ============================================================

waste_energy_rating = {
   ID_BB370030 = 5,       -- Waste-to-Energy Plant Small  (power output_MW)
   ID_BB370031 = 20       -- Waste-to-Energy Plant Large  (power output_MW)
}

-- ============================================================
-- WASTE -- Building Upkeep Costs (Simoleons/month)
-- ============================================================

waste_building_costs_monthly = {
   ID_BB370010 = 300,
   ID_BB370011 = 1100,
   ID_BB370020 = 900,     -- Incineration: higher cost due to air filtration equipment
   ID_BB370021 = 3200,
   ID_BB370030 = 1800,    -- Waste-to-Energy: most expensive, most advanced technology
   ID_BB370031 = 6500
}

-- ============================================================
-- WASTE -- Unlock Variables
-- Thresholds gating each waste-management reward in WASTE.lua.
-- ============================================================

unlock_variables_waste = {
   RECYCLE_100_POP              = 500,    -- minimum game.g_population
   RECYCLE_400_POP              = 8000,
   RECYCLE_400_DEMAND           = 200,    -- minimum waste_grid_var.EFFECTIVE_LOAD (TPD)
   INCINERATOR_200_POP          = 2000,
   INCINERATOR_800_POP          = 15000,
   INCINERATOR_800_DEMAND       = 600,
   W2E_300_POP                  = 10000,
   W2E_300_INCINERATOR_COUNT    = 1,      -- requires at least 1 Incinerator already built
   W2E_1200_POP                 = 40000,
   W2E_1200_DEMAND              = 1500
}


-- ============================================================
-- QUANTUMNET -- Resource ID
-- Must match the resource ID registered by sc4-regional-supply-demand
-- for QuantumNet in its own configuration. Current value is the
-- ASCII hex encoding of "QNET" -- replace with the real ID.
-- ============================================================

QUANTUMNET_RESOURCE_ID = hex2dec("514E4554")   -- PLACEHOLDER ("QNET" in ASCII hex)

-- ============================================================
-- QUANTUMNET -- QNU Rating Table
-- QuantumNet Units Per Day (QNU/day) at base rating, before
-- the socioeconomic multiplier is applied in QUANTUMNET.lua.
-- ============================================================

qnet_rating = {
   ID_BB340000 = 400,     -- QuantumNet Data Hub Small  (base QNU/day)
   ID_BB340001 = 1600     -- QuantumNet Data Hub Large  (base QNU/day)
}

-- ============================================================
-- QUANTUMNET -- Consumer Demand Rating Table
-- QNU/day each consumer building needs to run at full efficiency.
-- Only the Fusion Power Plant has a real hex ID right now.
-- Uncomment each line and fill in the real OG_ID once known.
-- ============================================================

qnet_demand_rating = {
   ID_78A0F71A = 500      -- Fusion Power Plant (QNU/day)
   -- ID_<tidal hex>         = 150   -- Tidal/Wave Power Plant
   -- ID_<sanitizer 2M hex>  = 200   -- Sewage Sanitizer Plant Small
   -- ID_<sanitizer 10M hex> = 800   -- Sewage Sanitizer Plant Large
   -- ID_<w2e 300 hex>       = 250   -- Waste-to-Energy Plant Small
   -- ID_<w2e 1200 hex>      = 900   -- Waste-to-Energy Plant Large
}

-- ============================================================
-- QUANTUMNET -- Building Upkeep Costs (Simoleons/month)
-- ============================================================

quantumnet_building_costs_monthly = {
   ID_BB340000 = 900,     -- QuantumNet Data Hub Small
   ID_BB340001 = 3600     -- QuantumNet Data Hub Large
}

-- ============================================================
-- QUANTUMNET -- Unlock Variables
-- ============================================================

unlock_variables_quantumnet = {
   QNET_HUB_SMALL_POP     = 8000,    -- minimum game.g_population
   QNET_HUB_SMALL_EDU     = 300,     -- minimum game.ga_education
   QNET_HUB_LARGE_POP     = 30000,
   QNET_HUB_LARGE_EDU     = 600,
   QNET_HUB_LARGE_R3_POP  = 2000     -- minimum game.g_city_r3_population (wealthy Sims)
}

-- ============================================================
-- QUANTUMNET -- Ratio Constants
-- Controls wealth/education weighting and regional trade prices.
-- EDUCATION_REFERENCE is the game.ga_education value treated as
-- "average" for the education multiplier -- TUNE to observed values.
-- ============================================================

quantumnet_ratio = {
   WEALTH_WEIGHT_LOW    = 0.4,   -- R$   Sims: below-average QuantumNet contribution
   WEALTH_WEIGHT_MED    = 1.0,   -- R$$  Sims: baseline contribution
   WEALTH_WEIGHT_HIGH   = 2.2,   -- R$$$ Sims: above-average contribution
   EDUCATION_REFERENCE  = 500,   -- baseline game.ga_education  (TUNE THIS)
   EDUCATION_FACTOR_MIN = 0.5,   -- minimum education multiplier (at ga_education -> 0)
   EDUCATION_FACTOR_MAX = 2.0,   -- maximum education multiplier (at ga_education >> EDUCATION_REFERENCE)
   BUY_PRICE_PER_UNIT   = 3,     -- Simoleons paid per imported QNU
   SELL_PRICE_PER_UNIT  = 2      -- Simoleons earned per exported QNU
}


-- ============================================================
-- TELECOM -- Resource ID
-- Must match the resource ID registered by sc4-regional-supply-demand
-- for Internet bandwidth. Current value is ASCII hex of "INET".
-- ============================================================

INTERNET_RESOURCE_ID = hex2dec("494E4554")   -- PLACEHOLDER ("INET" in ASCII hex)

-- ============================================================
-- TELECOM -- Mbps Rating Table
-- Megabits Per Second capacity for all TRADABLE producers
-- (Internet Exchange Points, Data Centers, Cell Towers).
-- Satellite Uplink capacity is in satellite_mbps_rating below
-- and is intentionally excluded from regional trade.
-- ============================================================

mbps_rating = {
   ID_BB390000 = 50000,     -- Internet Exchange Point Small  (Mbps)
   ID_BB390001 = 200000,    -- Internet Exchange Point Large  (Mbps)
   ID_BB390010 = 20000,     -- Cell Tower Small               (Mbps)
   ID_BB390011 = 80000,     -- Cell Tower Large               (Mbps)
   ID_BB390020 = 800000,    -- Data Center Small              (Mbps)
   ID_BB390021 = 3000000    -- Data Center Large / Hyperscale (Mbps)
}

-- ============================================================
-- TELECOM -- Satellite Mbps Rating Table
-- Capacity for Satellite Uplink stations.
-- Never traded regionally: satisfies local demand off-grid first.
-- ============================================================

satellite_mbps_rating = {
   ID_BB390030 = 5000,      -- Satellite Uplink Small  (Mbps, off-grid)
   ID_BB390031 = 25000      -- Satellite Uplink Large  (Mbps, off-grid)
}

-- ============================================================
-- TELECOM -- Building Upkeep Costs (Simoleons/month)
-- ============================================================

telecom_building_costs_monthly = {
   ID_BB390000 = 1500,
   ID_BB390001 = 6000,
   ID_BB390010 = 700,
   ID_BB390011 = 2800,
   ID_BB390020 = 4000,
   ID_BB390021 = 15000,
   ID_BB390030 = 2500,
   ID_BB390031 = 9000
}

-- ============================================================
-- TELECOM -- Unlock Variables
-- ============================================================

unlock_variables_telecom = {
   EXCHANGE_SMALL_POP        = 1000,    -- minimum game.g_population
   EXCHANGE_LARGE_POP        = 20000,
   EXCHANGE_LARGE_C_POP      = 3000,    -- minimum game.g_city_c_population
   TOWER_SMALL_POP           = 500,
   TOWER_LARGE_POP           = 15000,
   TOWER_LARGE_R_POP         = 10000,   -- minimum game.g_city_r_population
   DATA_CENTER_SMALL_POP     = 25000,
   DATA_CENTER_SMALL_C_POP   = 8000,
   DATA_CENTER_LARGE_POP     = 60000,
   DATA_CENTER_LARGE_C_POP   = 20000,
   DATA_CENTER_LARGE_I_POP   = 10000,   -- minimum game.g_city_i_population
   SATELLITE_SMALL_POP       = 5000,
   SATELLITE_LARGE_POP       = 40000,
   SATELLITE_LARGE_SMALL_CNT = 1        -- requires >= 1 Satellite Uplink Small already built
}

-- ============================================================
-- TELECOM -- Demand Rate Constants
-- Fictitious bandwidth demand per capita by RCI category.
-- C (commercial) Sims generate more bandwidth demand than R
-- (residential) or I (industrial) Sims.
-- ============================================================

telecom_demand_rate = {
   R_MBPS_PER_CAPITA = 0.5,   -- Mbps demanded per resident Sim
   C_MBPS_PER_CAPITA = 2.0,   -- Mbps demanded per commercial Sim
   I_MBPS_PER_CAPITA = 1.0    -- Mbps demanded per industrial Sim
}

-- ============================================================
-- TELECOM -- ISP Ratio Constants
-- Controls the random ISP count and their royalty payments.
-- ISP_PER_R_POP and MAX_ISP_RANDOM_SPREAD set the width of the
-- random range; ROYALTY_PER_ISP_MONTHLY is the full-network rate.
-- ============================================================

isp_ratio = {
   MIN_ISP_BASE            = 1,      -- every city with residents has at least this many ISPs
   ISP_PER_R_POP           = 40000,  -- one additional potential ISP slot per this many R Sims
   MAX_ISP_RANDOM_SPREAD   = 4,      -- width of the random range above the scaled minimum
   ROYALTY_PER_ISP_MONTHLY = 1200    -- Simoleons per ISP per month at full network satisfaction
}


-- ============================================================
-- AFTERLIFE -- Mortality Ratio Constants
-- BASE_MORTALITY_RATE_PER_CAPITA is a monthly rate applied to
-- game.g_city_r_population. HEALTH_COVERAGE_WEIGHT controls how
-- much game.g_health_coverage_p worsens the rate when it is low.
-- ============================================================

mortality_ratio = {
   BASE_MORTALITY_RATE_PER_CAPITA = 0.0006,   -- ~0.06 % of R population per month at 100 % health coverage
   HEALTH_COVERAGE_WEIGHT         = 1.5       -- at 0 % coverage, rate is multiplied by (1 + this)
}

-- ============================================================
-- AFTERLIFE -- Judgment Ratio Constants
-- Controls the Winner (Heaven) / Sinner (Hell) split.
-- BASE_SIN_RATIO is the 50/50 default; CRIME_WEIGHT and
-- EDUCATION_WEIGHT shift it based on game.ga_crime and
-- game.ga_education. Clamped to [SIN_RATIO_MIN, SIN_RATIO_MAX].
-- ============================================================

judgment_ratio = {
   BASE_SIN_RATIO       = 0.5,    -- baseline split (0.5 = 50 % Sinners, 50 % Winners)
   CRIME_WEIGHT         = 0.3,    -- how strongly game.ga_crime / 100 pushes toward Sinner
   EDUCATION_WEIGHT     = 0.3,    -- how strongly edu surplus above EDUCATION_REFERENCE pulls toward Winner
   EDUCATION_REFERENCE  = 500,    -- baseline game.ga_education treated as "average"  (TUNE THIS)
   SIN_RATIO_MIN        = 0.1,    -- floor: at least 10 % go to Heaven regardless
   SIN_RATIO_MAX        = 0.9     -- ceiling: at least 10 % go to Hell regardless
}

-- ============================================================
-- AFTERLIFE -- I.M.P. Ratio Constants
-- Controls I.M.P. hit frequency and success rate.
-- Deliberately low SUCCESS_CHANCE per Helluva Boss canon.
-- IMP_CLIENTS: share of STANDING Hell pop that orders a hit.
-- MAX_IMP_KILL_FRACTION_POP: hard cap relative to living R pop.
-- ============================================================

imp_ratio = {
   IMP_CLIENT_RATIO           = 0.002,   -- fraction of SINNER_POPULATION_HELL that hires I.M.P. per cycle
   IMP_SUCCESS_CHANCE         = 0.15,    -- probability a hire results in an actual kill (15 %)
   IMP_KILL_RANDOM_VARIANCE   = 2,       -- +/- random jitter on expected kill count
   MAX_IMP_KILL_FRACTION_POP  = 0.01     -- sanity cap: max kills = 1 % of living R population
}

-- ============================================================
-- AFTERLIFE -- Redemption Ratio Constants
-- Fraction of STANDING Sinner population redeemed each cycle
-- (Hazbin Hotel's Charlie Morningstar premise).
-- Applied BEFORE new Sinners arrive this cycle.
-- ============================================================

redemption_ratio = {
   REDEMPTION_RATIO = 0.0015   -- 0.15 % of SINNER_POPULATION_HELL redeemed per cycle
}

-- ============================================================
-- AFTERLIFE -- Hell Native Population Ratios
-- Hellborn (never-human) demon subclasses as a proportion of
-- SINNER_POPULATION_HELL. Recomputed each cycle as a snapshot.
-- Ordered from most to least numerous per in-universe hierarchy.
-- ============================================================

hell_native_ratio = {
   IMP_RATIO               = 1.0,       -- Imps:                1 per Sinner
   HELLHOUND_RATIO         = 0.6,       -- Hellhounds:          0.6 per Sinner
   SUCCUBUS_RATIO          = 0.4,       -- Succubi:             0.4 per Sinner
   INFESTOR_RATIO          = 0.16,      -- Infestors:           0.16 per Sinner
   POSSESSOR_RATIO         = 0.1,       -- Possessors:          0.1 per Sinner
   BAPHOMET_RATIO          = 0.02,      -- Baphomets:           0.02 per Sinner
   REAPER_RATIO            = 0.01,      -- Reapers:             0.01 per Sinner
   GOETIA_ARS_GOETIA_RATIO = 0.001     -- Goetia & Ars Goetia: 0.001 per Sinner
}

-- ============================================================
-- AFTERLIFE -- Hell Native Fixed Population
-- These two subclasses are canonical fixed counts from the lore
-- and do NOT scale with Sinner population.
-- ============================================================

hell_native_fixed = {
   FALLEN_ANGELS     = 2,    -- f_: fixed forever (Lucifer + one other per canon)
   SEVEN_DEADLY_SINS = 7     -- f_: always exactly 7, per lore
}

-- ============================================================
-- AFTERLIFE -- Character Table for News Ticker
-- Reference data only: name, role, source show, species and a
-- placeholder LTEXT id per character. The ticker_text_id is the
-- SC4 LTEXT instance ID of the actual localised ticker line.
-- ============================================================

hell_news_characters = {
   KATIE_KILLJOY       = { full_name = "Katie Killjoy", role = "Anchor, 670 News", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C1000" },
   TOM_TRENCH          = { full_name = "Tom Trench", role = "Co-Anchor, 670 News", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C1001" },
   CHARLIE_MORNINGSTAR = { full_name = "Charlie Morningstar", role = "Hotel Manager, Princess of Hell", source = "Hazbin Hotel", species = "Royal Goetia (Hellborn)", ticker_text_id = "text@BB3C1010" },
   VAGGIE              = { full_name = "Vaggie", role = "Hotel Co-Manager, former Exorcist", source = "Hazbin Hotel", species = "Fallen Angel (former Exorcist)", ticker_text_id = "text@BB3C1011" },
   ALASTOR             = { full_name = "Alastor", role = "Overlord, Radio Demon", source = "Hazbin Hotel", species = "Overlord (Sinner-origin)", ticker_text_id = "text@BB3C1012" },
   VOX                 = { full_name = "Vox", role = "Overlord, VoxTek CEO", source = "Hazbin Hotel", species = "Overlord (Sinner-origin)", ticker_text_id = "text@BB3C1013" },
   VALENTINO           = { full_name = "Valentino", role = "Overlord, Studio Owner", source = "Hazbin Hotel", species = "Overlord (Sinner-origin)", ticker_text_id = "text@BB3C1014" },
   BLITZO              = { full_name = "Blitzo", role = "C.E.O., I.M.P.", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C1020" },
   MOXXIE              = { full_name = "Moxxie", role = "Field Agent, I.M.P.", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C1021" },
   MILLIE              = { full_name = "Millie", role = "Field Agent, I.M.P.", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C1022" },
   LOONA               = { full_name = "Loona", role = "Receptionist, I.M.P.", source = "Helluva Boss", species = "Hellhound (Hellborn)", ticker_text_id = "text@BB3C1023" },
   STOLAS              = { full_name = "Stolas", role = "Ars Goetia Royalty", source = "Helluva Boss", species = "Royal Goetia (Hellborn)", ticker_text_id = "text@BB3C1024" },
   VEROSIKA_MAYDAY     = { full_name = "Verosika Mayday", role = "Pop Star", source = "Helluva Boss", species = "Succubus (Hellborn)", ticker_text_id = "text@BB3C1025" },
   LUCIFER_MORNINGSTAR = { full_name = "Lucifer Morningstar", role = "King of Hell, Sin of Pride", source = "Hazbin Hotel", species = "Fallen Angel", ticker_text_id = "text@BB3C1030" },
   LILITH              = { full_name = "Lilith", role = "Queen of Hell, the first woman", source = "Hazbin Hotel", species = "First Woman (Queen of Hell)", ticker_text_id = "text@BB3C1031" },
   ANGEL_DUST          = { full_name = "Angel Dust", role = "Hotel Patron, Porn Star", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C1032" },
   HUSK                = { full_name = "Husk", role = "Hotel Bartender, former Overlord", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C1033" },
   NIFFTY              = { full_name = "Niffty", role = "Hotel Housekeeper", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C1034" },
   SIR_PENTIOUS        = { full_name = "Sir Pentious", role = "Hotel Patron, redeemed into Heaven", source = "Hazbin Hotel", species = "Sinner Demon (Redeemed)", ticker_text_id = "text@BB3C1035" },
   EGG_BOIS            = { full_name = "Egg Bois", role = "Sir Pentious' Henchmen", source = "Hazbin Hotel", species = "Egg Minions", ticker_text_id = "text@BB3C1036" },
   FAT_NUGGETS         = { full_name = "Fat Nuggets", role = "Angel Dust's Pet Pig", source = "Hazbin Hotel", species = "Pig (Hell)", ticker_text_id = "text@BB3C1037" },
   CHERRI_BOMB         = { full_name = "Cherri Bomb", role = "Angel Dust's Friend, Arsonist", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C1038" },
   VELVETTE            = { full_name = "Velvette", role = "Overlord, Fashion Influencer", source = "Hazbin Hotel", species = "Overlord (Sinner-origin)", ticker_text_id = "text@BB3C1039" },
   CARMILLA_CARMINE    = { full_name = "Carmilla Carmine", role = "Overlord, Arms Dealer", source = "Hazbin Hotel", species = "Overlord (Sinner-origin)", ticker_text_id = "text@BB3C103A" },
   ODETTE_CARMINE      = { full_name = "Odette Carmine", role = "Carmilla's Daughter", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C103B" },
   CLARA_CARMINE       = { full_name = "Clara Carmine", role = "Carmilla's Daughter", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C103C" },
   ZESTIAL             = { full_name = "Zestial", role = "Overlord, Eldest Overlord", source = "Hazbin Hotel", species = "Overlord (Sinner-origin)", ticker_text_id = "text@BB3C103D" },
   ROSIE               = { full_name = "Rosie", role = "Overlord, Cannibal Town", source = "Hazbin Hotel", species = "Overlord (Sinner-origin)", ticker_text_id = "text@BB3C103E" },
   MIMZY               = { full_name = "Mimzy", role = "Singer, Alastor's Acquaintance", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C103F" },
   SUSAN               = { full_name = "Susan", role = "Hell Resident", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C1040" },
   BAXTER              = { full_name = "Baxter", role = "Hotel Guest, Scientist", source = "Hazbin Hotel", species = "Sinner Demon", ticker_text_id = "text@BB3C1041" },
   ZEEZI               = { full_name = "Zeezi", role = "Overlord", source = "Hazbin Hotel", species = "Overlord (Sinner-origin)", ticker_text_id = "text@BB3C1042" },
   PRICK               = { full_name = "Prick", role = "Overlord", source = "Hazbin Hotel", species = "Overlord (Sinner-origin)", ticker_text_id = "text@BB3C1043" },
   ADAM                = { full_name = "Adam", role = "First Man, Exorcist Leader", source = "Hazbin Hotel", species = "Angel (Winner)", ticker_text_id = "text@BB3C1044" },
   LUTE                = { full_name = "Lute", role = "Exorcist Lieutenant", source = "Hazbin Hotel", species = "Exorcist (Angel)", ticker_text_id = "text@BB3C1045" },
   ABEL                = { full_name = "Abel", role = "Exorcist, Son of Adam", source = "Hazbin Hotel", species = "Angel (Winner)", ticker_text_id = "text@BB3C1046" },
   SERA                = { full_name = "Sera", role = "High Seraphim of Heaven", source = "Hazbin Hotel", species = "Seraphim (Angel)", ticker_text_id = "text@BB3C1047" },
   EMILY               = { full_name = "Emily", role = "Seraphim, Heaven", source = "Hazbin Hotel", species = "Seraphim (Angel)", ticker_text_id = "text@BB3C1048" },
   ST_PETER            = { full_name = "St. Peter", role = "Gatekeeper of Heaven", source = "Hazbin Hotel", species = "Angel (Winner)", ticker_text_id = "text@BB3C1049" },
   SPEAKER_OF_GOD      = { full_name = "The Speaker of God", role = "Voice of Heaven", source = "Hazbin Hotel", species = "Angel", ticker_text_id = "text@BB3C104A" },
   OCTAVIA             = { full_name = "Octavia", role = "Stolas' Daughter", source = "Helluva Boss", species = "Royal Goetia (Hellborn)", ticker_text_id = "text@BB3C1050" },
   STELLA              = { full_name = "Stella", role = "Stolas' Ex-Wife", source = "Helluva Boss", species = "Royal Goetia (Hellborn)", ticker_text_id = "text@BB3C1051" },
   ANDREALPHUS         = { full_name = "Andrealphus", role = "Stella's Brother", source = "Helluva Boss", species = "Royal Goetia (Hellborn)", ticker_text_id = "text@BB3C1052" },
   PAIMON              = { full_name = "Paimon", role = "Stolas' Father", source = "Helluva Boss", species = "Royal Goetia (Hellborn)", ticker_text_id = "text@BB3C1053" },
   VASSAGO             = { full_name = "Vassago", role = "Stolas' Friend", source = "Helluva Boss", species = "Ars Goetia (Hellborn)", ticker_text_id = "text@BB3C1054" },
   ASMODEUS            = { full_name = "Asmodeus", role = "Sin of Lust, Ozzie's Owner", source = "Helluva Boss", species = "Sin (Hellborn)", ticker_text_id = "text@BB3C1055" },
   BEELZEBUB           = { full_name = "Beelzebub", role = "Sin of Gluttony", source = "Helluva Boss", species = "Sin (Hellborn)", ticker_text_id = "text@BB3C1056" },
   SATAN               = { full_name = "Satan", role = "Sin of Wrath", source = "Helluva Boss", species = "Sin (Hellborn)", ticker_text_id = "text@BB3C1057" },
   MAMMON              = { full_name = "Mammon", role = "Sin of Greed", source = "Helluva Boss", species = "Sin (Hellborn)", ticker_text_id = "text@BB3C1058" },
   FIZZAROLLI          = { full_name = "Fizzarolli", role = "Performer, Ozzie's Partner", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C1059" },
   ROBO_FIZZ           = { full_name = "Robo Fizz", role = "Mammon's Robot Merchandise", source = "Helluva Boss", species = "Robot", ticker_text_id = "text@BB3C105A" },
   STRIKER             = { full_name = "Striker", role = "Imp Assassin", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C105B" },
   CRIMSON             = { full_name = "Crimson", role = "Imp Mob Boss, Moxxie's Father", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C105C" },
   CASH_BUCKZO         = { full_name = "Cash Buckzo", role = "Blitzo's Father", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C105D" },
   BARBIE_WIRE         = { full_name = "Barbie Wire", role = "Blitzo's Twin Sister", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C105E" },
   SALLIE_MAY          = { full_name = "Sallie May", role = "Millie's Sister", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C105F" },
   VORTEX              = { full_name = "Vortex", role = "Loona's Friend", source = "Helluva Boss", species = "Hellhound (Hellborn)", ticker_text_id = "text@BB3C1060" },
   GLITZ_AND_GLAM      = { full_name = "Glitz & Glam", role = "Imp Twin Performers", source = "Helluva Boss", species = "Imp (Hellborn)", ticker_text_id = "text@BB3C1061" },
   LEVIATHAN           = { full_name = "Leviathan", role = "Sin of Envy", source = "Helluva Boss", species = "Sin (Hellborn)", ticker_text_id = "text@BB3C1062" },
   BELPHEGOR           = { full_name = "Belphegor", role = "Sin of Sloth", source = "Helluva Boss", species = "Sin (Hellborn)", ticker_text_id = "text@BB3C1063" }
}

-- ============================================================
-- AFTERLIFE -- News Ticker Event-to-Character Mapping
-- Maps each simulation event type to the characters considered
-- "on topic" for it. get_news_ticker_character(event_type) in
-- AFTERLIFE.lua picks one at random from the matching list.
-- ============================================================

news_ticker_event_characters = {
   NATURAL_DEATH_REPORT     = { "KATIE_KILLJOY", "TOM_TRENCH" },
   IMP_KILL_REPORT          = { "BLITZO", "MOXXIE", "MILLIE", "STRIKER" },
   REDEMPTION_REPORT        = { "CHARLIE_MORNINGSTAR", "VAGGIE", "ANGEL_DUST", "HUSK", "NIFFTY", "SIR_PENTIOUS", "EGG_BOIS", "FAT_NUGGETS", "CHERRI_BOMB", "BAXTER" },
   OVERLORD_REPORT          = { "ALASTOR", "VOX", "VALENTINO", "VELVETTE", "CARMILLA_CARMINE", "ODETTE_CARMINE", "CLARA_CARMINE", "ZESTIAL", "ROSIE", "ZEEZI", "PRICK", "MIMZY", "SUSAN" },
   HELL_NATIVE_REPORT       = { "STOLAS", "LOONA", "VEROSIKA_MAYDAY", "OCTAVIA", "STELLA", "ANDREALPHUS", "PAIMON", "VASSAGO", "FIZZAROLLI", "ROBO_FIZZ", "CRIMSON", "CASH_BUCKZO", "BARBIE_WIRE", "SALLIE_MAY", "VORTEX", "GLITZ_AND_GLAM" },
   SEVEN_DEADLY_SINS_REPORT = { "LUCIFER_MORNINGSTAR", "LILITH", "ASMODEUS", "BEELZEBUB", "SATAN", "MAMMON", "LEVIATHAN", "BELPHEGOR" },
   HEAVEN_REPORT            = { "ADAM", "LUTE", "ABEL", "SERA", "EMILY", "ST_PETER", "SPEAKER_OF_GOD" }
}


-- ============================================================
-- CTOS -- DPD Rating Table
-- Data Points Per Day processing capacity per building.
-- ============================================================

dpd_rating = {
   ID_BB3D0000 = 50000,     -- ctOS Hub Small   (DPD)
   ID_BB3D0001 = 200000,    -- ctOS Hub Large   (DPD)
   ID_BB3D0010 = 30000,     -- ctOS Tower Small (DPD)
   ID_BB3D0011 = 120000     -- ctOS Tower Large (DPD)
}

-- ============================================================
-- CTOS -- Building Upkeep Costs (Simoleons/month)
-- ============================================================

ctos_building_costs_monthly = {
   ID_BB3D0000 = 2000,
   ID_BB3D0001 = 8000,
   ID_BB3D0010 = 1200,
   ID_BB3D0011 = 5000
}

-- ============================================================
-- CTOS -- Unlock Variables
-- ============================================================

unlock_variables_ctos = {
   CTOS_HUB_SMALL_POP        = 5000,     -- minimum game.g_population
   CTOS_HUB_LARGE_POP        = 30000,
   CTOS_HUB_LARGE_CRIME      = 30,       -- minimum game.ga_crime (marketed as crime-fighting upgrade)
   CTOS_TOWER_SMALL_POP      = 3000,
   CTOS_TOWER_LARGE_POP      = 20000,
   CTOS_TOWER_LARGE_HUB_CNT  = 1         -- requires >= 1 ctOS Hub already built
}

-- ============================================================
-- CTOS -- Data Generation Rate Constants
-- Fictitious Data Points Per Day generated per capita by
-- each RCI category. C Sims produce the most monetisable data.
-- ============================================================

ctos_data_ratio = {
   R_DPD_PER_CAPITA = 0.8,   -- DPD generated per residential Sim
   C_DPD_PER_CAPITA = 1.5,   -- DPD generated per commercial Sim
   I_DPD_PER_CAPITA = 0.5    -- DPD generated per industrial Sim
}

-- ============================================================
-- CTOS -- Blume Corporation Payment Constants
-- Corrupt kickback Blume pays the city for data access.
-- PAYMENT_PER_CAPITA_MONTHLY: base rate at 100 % data collection
-- and average wealth. Scaled by COLLECTION_RATIO and
-- WEALTH_DATA_VALUE_FACTOR at runtime. Zero during a breach event.
-- ============================================================

blume_ratio = {
   PAYMENT_PER_CAPITA_MONTHLY      = 0.15,   -- Simoleons per Sim per month (base)
   MANIPULATION_BONUS_PER_CAPITA   = 0.05,   -- bonus per Sim when Bellwether manipulates an outcome
   WEALTH_WEIGHT_LOW               = 0.5,    -- R$   data value multiplier
   WEALTH_WEIGHT_MED               = 1.0,    -- R$$  data value multiplier (baseline)
   WEALTH_WEIGHT_HIGH              = 2.0     -- R$$$ data value multiplier
}

-- ============================================================
-- CTOS -- Bellwether System Constants
-- Controls all boolean event probabilities and the composite
-- alert-level thresholds. All _CHANCE values are passed directly
-- to game.random_chance() (integer 0-100).
-- ============================================================

bellwether_ratio = {
   MIN_DATA_FOR_BELLWETHER         = 50000,  -- minimum DPD_COLLECTED to allow Bellwether to run
   BELLWETHER_ACTIVE_CHANCE        = 35,     -- % chance Bellwether runs this cycle (if data threshold met)
   MANIPULATION_CHANCE             = 10,     -- % chance of an outcome-manipulation event (if Bellwether active)
   BREACH_EVENT_CHANCE             = 3,      -- % chance of a data-breach / DedSec scandal this cycle
   PREDICTION_DRIFT_MAX            = 5,      -- max +/- crime-trend drift in Bellwether's crime prediction
   POWER_DEMAND_STRESS_THRESHOLD   = 1.0,    -- DEMAND_PERCENT above this = power stress domain active
   WATER_SURPLUS_STRESS_THRESHOLD  = 0,      -- WATER_AVAILABLE at or below this = water stress domain active
   SEWAGE_STRESS_THRESHOLD         = 0,      -- SEWAGE_UNTREATED above this = sewage stress domain active
   WASTE_OVERFLOW_STRESS_THRESHOLD = 0,      -- WASTE_OVERFLOW above this = waste stress domain active
   QNET_STRESS_THRESHOLD           = 0.8,    -- QNET_SATISFACTION below this = QuantumNet stress domain active
   IMP_KILL_STRESS_THRESHOLD       = 1,      -- IMP_KILLS_THIS_CYCLE above this = I.M.P. stress domain active
   ALERT_LEVEL_ELEVATED            = 0.35,   -- composite score >= this -> ELEVATED
   ALERT_LEVEL_HIGH                = 0.60,   -- composite score >= this -> HIGH
   ALERT_LEVEL_CRITICAL            = 0.80    -- composite score >= this -> CRITICAL
}

-- ============================================================
-- CTOS -- Alert Level LTEXT IDs
-- Maps each integer alert level to a SC4 LTEXT instance ID for
-- display in the news ticker or the ctOS advisor panel.
-- ============================================================

ctos_alert_labels = {
   [0] = "text@BB3D5000",   -- NORMAL
   [1] = "text@BB3D5001",   -- ELEVATED
   [2] = "text@BB3D5002",   -- HIGH
   [3] = "text@BB3D5003"    -- CRITICAL
}
