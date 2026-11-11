// SC4 SIM: fix of the game's port upgrade bug (a staged seaport loses demand relief at every upgrade, the "BSC
// upgradeable seaports eat industrial cap relief" bug). Driven by SimDllDirector. Notes: docs/game-fixes.md.
#pragma once

namespace PortUpgradeFix
{
	// Reads [GameFixes] PortUpgradeFix from SC4SIM.ini.
	void OnStart();
	// Redirects the port developer's call to its upgrade function after checking the game code.
	void PostAppInit();
	// Restores the original call.
	void PostAppShutdown();
}
