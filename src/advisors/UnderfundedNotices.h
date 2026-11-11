// SC4 SIM: priorities of CoriBoom's Fixed Underfunded Notices (FUN), set from SC4SIM.ini. Driven by SimDllDirector.
// Notes: docs/underfunded-notices.md.
#pragma once

class cIGZMessage2;

namespace UnderfundedNotices
{
	// Reads [UnderfundedNotices] from SC4SIM.ini.
	void OnStart();
	// PostCityInit (0x26D31EC1): writes the chosen priorities into the FUN advice records of the advisor Lua.
	void DoMessage(cIGZMessage2* message);
}
