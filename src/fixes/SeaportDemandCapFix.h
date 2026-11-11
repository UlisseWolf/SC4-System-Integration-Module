// SC4 SIM: seaport demand cap fix, port of sc4-seaport-demand-cap-fix by Nicholas Hayes (LGPL 2.1), merged with the
// author's consent. Driven by SimDllDirector. Notes: docs/game-fixes.md.
#pragma once

class cIGZMessage2;

namespace SeaportDemandCapFix
{
	// Reads [GameFixes] SeaportDemandCapFix from SC4SIM.ini.
	void OnStart();
	// Patches the two operands after checking the code, registers the LogIndustrialDemandValues cheat.
	void PostAppInit();
	// Restores the original operands and unregisters the cheat.
	void PostAppShutdown();
	// Cheat (0x230E27AC) messages.
	void DoMessage(cIGZMessage2* message);
}
