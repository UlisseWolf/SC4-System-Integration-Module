// SC4 SIM: mixed residential + commercial occupancy (R + C on one lot).
// Driven by SimDllDirector: SC4SIM.dll has a single GZCOM director, which forwards these calls.
// Reverse-engineering notes: docs/mixed-occupancy.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace MixedOccupancy
{
	// Reads the [MixedOccupancy] section of SC4SIM.ini.
	void OnStart();
	// Installs the lot and census hooks (SimCity 4 1.1.641 only) and registers the MixedOccupancy cheat.
	void PostAppInit();
	void PostAppShutdown();
	// PostCityInit, PreCityShutdown, SimNewMonth and cheat-code messages received by the director.
	void DoMessage(cIGZMessage2* message);
}
