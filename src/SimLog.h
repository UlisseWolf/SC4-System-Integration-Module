// SC4SIM DLL - minimal log file (SC4SIM.log next to the DLL).
#pragma once
#include <cstdarg>

namespace SimLog
{
	enum class Level { Info, Warning, Error, Debug };
	void Open();
	// Level::Debug lines are written only while SetDebug(true) is in effect ([SC4PowerPoleCustomization] DebugLog).
	void Write(Level level, const char* format, ...);
	// [SC4SIM] DebugCheats in SC4SIM.ini, read by Open(): the diagnostic cheats (MixedOccupancy, IMZoneInfo,
	// FundingResync, LogIndustrialDemandValues) are registered only when it is true. Off by default.
	bool DebugCheatsEnabled();
}
