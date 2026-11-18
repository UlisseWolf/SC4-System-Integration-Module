// SC4 SIM: one button in the Graphs window for every graph exemplar, added at run time. Driven by SimDllDirector.
// Notes: docs/graph-window.md.
#pragma once

namespace GraphButtons
{
	// Reads [GraphWindow] from SC4SIM.ini.
	void OnStart();
	// Hooks the creation of the Graphs window (SimCity 4 1.1.641 only).
	void PostAppInit();
	void PostAppShutdown();
}
