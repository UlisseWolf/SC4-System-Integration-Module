// SC4SIM DLL - minimal log file (SC4SIM.log next to the DLL).
#pragma once
#include <cstdarg>

namespace SimLog
{
	enum class Level { Info, Warning, Error, Debug };
	void Open();
	void Write(Level level, const char* format, ...);
}
