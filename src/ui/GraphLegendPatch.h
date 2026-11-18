// SC4 SIM: wider legend in the line graphs of the Graphs window, so long plot names (Nuclear Fission, Nuclear Fusion)
// fit on one line. Driven by SimDllDirector. Notes: docs/graph-window.md.
#pragma once

namespace GraphLegendPatch
{
	// Reads [GraphWindow] LegendExtraWidth from SC4SIM.ini.
	void OnStart();
	// Patches the legend layout of the line graph (1.1.641 only, after checking the code).
	void PostAppInit();
	// Restores the original code.
	void PostAppShutdown();
}
