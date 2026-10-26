// SC4 SIM: custom power poles (port of the sc4-render-services "re/custom-powerlines" sample, LGPL 2.1).
// Driven by SimDllDirector: SC4SIM.dll has a single GZCOM director, which forwards these calls.
#pragma once

class cIGZMessage2;
class cIGZMessageTarget2;

namespace PowerPoleCustomization
{
	// Reads the [SC4PowerPoleCustomization] and [PowerPoles.*] sections of SC4SIM.ini.
	void OnStart();
	// Checks the game version (1.1.641 only), installs the patches, subscribes `target` to
	// PreCityShutdown and registers the keepwires cheat (its messages go to `target`).
	bool PostAppInit(cIGZMessageTarget2* target);
	void PostAppShutdown(cIGZMessageTarget2* target);
	// PreCityShutdown and cheat-code messages received by the director.
	void DoMessage(cIGZMessage2* message);
}
