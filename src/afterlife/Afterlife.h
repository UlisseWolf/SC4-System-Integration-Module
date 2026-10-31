// SC4 SIM: Afterlife (fictitious mortality / afterlife statistics), the DLL version of AFTERLIFE.lua.
// Driven by SimDllDirector: SC4SIM.dll has a single GZCOM director, which forwards these calls.
// Notes: docs/afterlife.md.
#pragma once

class cIGZMessage2;

namespace Afterlife
{
	// Reads the [Afterlife] section of SC4SIM.ini.
	void OnStart();
	// PostCityInit and PreCityShutdown messages received by the director. When a city opens, the module registers
	// run_afterlife_balance_variables and get_news_ticker_character in the city's advisor Lua, over the Lua ones.
	void DoMessage(cIGZMessage2* message);
}
