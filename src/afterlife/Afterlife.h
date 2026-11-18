// SC4 SIM: Afterlife (fictitious mortality / afterlife statistics), the DLL version of AFTERLIFE.lua.
// Driven by SimDllDirector: SC4SIM.dll has a single GZCOM director, which forwards these calls.
// Notes: docs/afterlife.md.
#pragma once

class cIGZMessage2;

namespace Afterlife
{
	// Reads the [Afterlife] section of SC4SIM.ini.
	void OnStart();
	// PostCityInit, PreCityShutdown, SimNewMonth, Load, Save and PostSave messages received by the director (Load and
	// Save carry the city file, where the module keeps its state). When a city opens, the module registers
	// run_afterlife_balance_variables and get_news_ticker_character in the city's advisor Lua, over the Lua ones.
	void DoMessage(cIGZMessage2* message);

	// History types recorded after every monthly cycle for the Afterlife graph (0x6534284A / 0xCA4AD545 / 0x00000024,
	// docs/afterlife.md): deaths, premature deaths, I.M.P. kills, Winners, Sinners, redeemed (this month), buried and
	// cremated (deathcare, this month) and uncollected dead (now), 0x5C4DE141-0x5C4DE149.
	inline constexpr unsigned kHistoryFirst = 0x5C4DE141;
}
