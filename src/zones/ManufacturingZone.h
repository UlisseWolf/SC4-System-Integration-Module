// SC4 SIM: a dedicated manufacturing (I-M) zone, on one of the game's 16 zone slots.
// Driven by SimDllDirector: SC4SIM.dll has a single GZCOM director, which forwards these calls.
// Notes and hook plan: docs/manufacturing-zone.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace ManufacturingZone
{
	// Reads the [ManufacturingZone] section of SC4SIM.ini.
	void OnStart();
	// Registers the IMZone, IMZoneConvert and IMZoneInfo cheats. The hooks are installed when the first city opens,
	// because they need the city's zone manager and building development simulator.
	void PostAppInit();
	void PostAppShutdown();
	// PostCityInit, PreCityShutdown and cheat-code messages received by the director.
	void DoMessage(cIGZMessage2* message);
}
