// SC4 SIM: Advanced Water Management (port of sc4-advanced-water-management by Nicholas Hayes, MIT).
// Driven by SimDllDirector: SC4SIM.dll has a single GZCOM director, which forwards these calls.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace AdvancedWater
{
	// Reads the [AdvancedWaterManagement] section of SC4SIM.ini.
	void OnStart();
	// Installs the water building query hook (SimCity 4 1.1.641 only) and registers the ScanPumps cheat.
	// Does nothing if the standalone AdvancedWaterManagement.dll is loaded or the feature is disabled.
	bool PostAppInit();
	void PostAppShutdown();
	// PostCityInit, PreCityShutdown, SimNewMonth and cheat-code messages received by the director.
	void DoMessage(cIGZMessage2* message);

	inline constexpr uint32_t kMessageSimNewMonth = 0x66956816;
}
