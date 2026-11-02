// SC4 SIM: fix of the game's "high-tech industry has no $$$ jobs" bug (cSC4Lot::UpdateJobs reads the $ trip capacity
// for every wealth). Driven by SimDllDirector. Notes: docs/game-fixes.md.
#pragma once

namespace TripCapacityFix
{
	// Reads [GameFixes] TripCapacityWealthFix from SC4SIM.ini.
	void OnStart();
	// Patches the two operand bytes after checking the code around them.
	void PostAppInit();
	// Restores the original bytes.
	void PostAppShutdown();
}
