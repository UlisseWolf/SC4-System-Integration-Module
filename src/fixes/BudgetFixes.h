// SC4 SIM: budget fixes. Driven by SimDllDirector. Notes: docs/game-fixes.md.
//  * Funding slider fix: the "phantom funding sliders" bug (a civic building's monthly cost changed by a mod drives its
//    funding slider to 0 % or 120 %, month after month).
//  * RemovePhantomBudgetItems cheat: port of sc4-remove-phantom-budget-items by Nicholas Hayes (MIT), merged with the
//    author's consent.
#pragma once

class cIGZMessage2;

namespace BudgetFixes
{
	// Reads [GameFixes] FundingSliderFix, FundingSliderResetStuck and RemovePhantomBudgetItems from SC4SIM.ini.
	void OnStart();
	// Hooks cSC4DepartmentBudget::RecalculateAllLineItemCurrentExpenses after checking the vtable, registers the cheats.
	void PostAppInit();
	// Restores the vtable slot and unregisters the cheats.
	void PostAppShutdown();
	// PostCityInit (0x26D31EC1), PreCityShutdown (0x26D31EC2) and cheat (0x230E27AC) messages.
	void DoMessage(cIGZMessage2* message);
}
