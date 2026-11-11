////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: budget fixes (SimCity 4 1.1.641).
//
// 1. Funding slider fix ("phantom funding sliders").
//
// Every civic building with a funding slider is a locally funded object of a department (cSC4DepartmentBudget). Its
// line item (one per building type) keeps FullExpenses, the sum of the monthly costs at 100 %, as an accumulator saved
// with the city: AddLocallyFundedObject (0x006AB0C0) adds the building's cost when it is plopped and
// RemoveLocallyFundedObject (0x006AA530) subtracts it when it is demolished. The building's own cost (Budget Item:
// Cost, 0xEA54D286) is usually not copied into the building: cSCExemplarPropertyHolder::GetProperty (0x005FEDA0) falls
// back to the exemplar, so a mod that changes the exemplar changes the cost of every existing building on the next
// load, while FullExpenses still holds the old costs.
//
// Every month cSC4BudgetSimulator (0x00692510) calls RecalculateAllLineItemCurrentExpenses (0x006ABB50, vtable slot
// +0x74) on every department. It sums cost x local funding of every locally funded object (current costs) into C,
// sums FullExpenses of every line item into F and sets the department to p' = C / F when both |p' - p| > 0.5 points
// and |C - p F| > n + 2. It then writes p' into every building's slider (0x006AB810 with propagate = true). With the
// current costs H different from the saved ones, p' = p x H / F (damped by fixed items), every month, until 120 % or
// about 0 %. Plumbing and pollution buildings and transit lots are not affected: the game copies their cost into the
// building and keeps FullExpenses in step (ChangeBudgetItemCost callers 0x006DED5A, 0x006E41CE; traffic simulator
// CopyBudgetItemProperties 0x00724037).
//
// The fix hooks vtable slot +0x74 of cSC4DepartmentBudget (0x00AB0F24 -> 0x006ABB50). Before the game's calculation,
// it sets FullExpenses of every locally funded expense line item to the sum of the current costs of the buildings on
// that line, counted exactly as the game counts them: the department's set of (property holder, purpose) pairs at
// +0x58, every budget item of this department whose line is in the set, with no purpose filter. C / F then equals the
// buildings' own funding and the slider stays where the player put it. A mod's new cost applies to existing buildings
// at once. Lines whose buildings are all gone (or whose plugin was removed) go back to 0. Nothing is written to the
// save beyond the corrected FullExpenses. The same resync runs when a city opens.
//
// 2. RemovePhantomBudgetItems cheat.
//
// Port of sc4-remove-phantom-budget-items by Nicholas Hayes (MIT, see LICENSE-sc4-remove-phantom-budget-items.txt),
// merged into SC4SIM.dll with the author's consent. "RemovePhantomBudgetItems <category> [all]" removes from a budget
// category the buildings whose plugin is no longer installed (no Exemplar Type property 0x10), or every building with
// "all", and their line items. Behaviour and messages are those of the original 1.x DLL.
//
////////////////////////////////////////////////////////////////////////////

#include "BudgetFixes.h"
#include <utility> // before SC4Vector.h, which uses std::exchange
#include "SimLog.h"
#include "SC4NotificationDialog.h"
#include "VersionDetection.h"

#include "cIGZCheatCodeManager.h"
#include "cIGZMessage2Standard.h"
#include "cIGZString.h"
#include "cISC4App.h"
#include "cISC4BudgetSimulator.h"
#include "cISC4BuildingOccupant.h"
#include "cISC4City.h"
#include "cISC4DepartmentBudget.h"
#include "cISC4LineItem.h"
#include "cISC4Occupant.h"
#include "cISCPropertyHolder.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include "SC4List.h"
#include "SC4Percentage.h"
#include "SC4Vector.h"

#include <Windows.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessagePostCityInit = 0x26D31EC1;
	constexpr uint32_t kMessagePreCityShutdown = 0x26D31EC2;
	constexpr uint32_t kMessageCheatIssued = 0x230E27AC;

	// Cheat IDs. RemovePhantomBudgetItems keeps the ID of the standalone DLL.
	constexpr uint32_t kCheatRemovePhantomBudgetItems = 0x7630FE7F;
	constexpr uint32_t kCheatFundingResync = 0xC4511B01;
	constexpr uint32_t kCheatFundingReset = 0xC4511B02;
	constexpr const char* kRemovePhantomBudgetItemsName = "RemovePhantomBudgetItems";

	// SimCity 4 1.1.641.
	constexpr uint16_t kSupportedGameVersion = 641;
	constexpr uintptr_t kBudgetSimulatorVtable = 0x00AB0908;   // cISC4BudgetSimulator interface of cSC4BudgetSimulator
	constexpr uintptr_t kDepartmentBudgetVtable = 0x00AB0EB0;  // cISC4DepartmentBudget interface of cSC4DepartmentBudget
	constexpr uintptr_t kRecalculateSlot = kDepartmentBudgetVtable + 0x74; // 0x00AB0F24, slot 29
	constexpr uintptr_t kRecalculateOriginal = 0x006ABB50;     // RecalculateAllLineItemCurrentExpenses
	constexpr size_t kBudgetSimulatorDepartments = 0x54;       // map<department ID, cISC4DepartmentBudget*>: header pointer
	constexpr size_t kDepartmentLocalObjects = 0x58;           // set<pair<cISCPropertyHolder*, purpose>>: header pointer
	constexpr size_t kDepartmentLocalObjectCount = 0x5C;
	constexpr size_t kTreeNodeValue = 0x10;                    // STLport red-black tree node: color, parent, left, right, value
	constexpr uint32_t kPropertyExemplarType = 0x00000010;

	struct TreeNode
	{
		uint32_t color;
		TreeNode* parent;
		TreeNode* left;
		TreeNode* right;
	};

	using RecalculateFn = bool(__thiscall*)(cISC4DepartmentBudget*);

	bool fixEnabled = true;
	bool resetStuck = false;
	bool removePhantomEnabled = true;

	bool hookInstalled = false;
	bool versionOk = false;
	RecalculateFn originalRecalculate = nullptr;
	bool inResync = false;
	bool walkWarningLogged = false;

	cISC4BudgetSimulator* budgetSimulator = nullptr;
	cRZAutoRefCount<cIGZCheatCodeManager> cheatManager;
	bool removePhantomRegistered = false;

	std::filesystem::path ModuleFolder()
	{
		wchar_t modulePath[MAX_PATH]{};
		if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return {};
		return std::filesystem::path(modulePath).parent_path();
	}

	bool ReadBool(const std::filesystem::path& ini, const wchar_t* key, bool fallback)
	{
		wchar_t value[32]{};
		GetPrivateProfileStringW(L"GameFixes", key, fallback ? L"true" : L"false", value, 32, ini.c_str());
		std::wstring text = value;
		for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
		if (text == L"false" || text == L"0" || text == L"no" || text == L"off") return false;
		if (text == L"true" || text == L"1" || text == L"yes" || text == L"on") return true;
		return fallback;
	}

	bool HasVtable(const void* object, uintptr_t vtable)
	{
		return object && *reinterpret_cast<const uintptr_t*>(object) == vtable;
	}

	TreeNode* NextNode(TreeNode* node)
	{
		if (node->right)
		{
			node = node->right;
			while (node->left) node = node->left;
			return node;
		}
		TreeNode* parent = node->parent;
		while (node == parent->right)
		{
			node = parent;
			parent = parent->parent;
		}
		if (node->right != parent) node = parent;
		return node;
	}

	// Visits every node of the STLport tree whose header pointer is at headerAddress. Returns false if the tree has
	// more than limit nodes (not the expected layout).
	template <typename Visitor>
	bool WalkTree(const void* headerAddress, size_t limit, Visitor visit)
	{
		TreeNode* const header = *static_cast<TreeNode* const*>(headerAddress);
		if (!header) return false;
		size_t visited = 0;
		for (TreeNode* node = header->left; node != header; node = NextNode(node))
		{
			if (!node || ++visited > limit) return false;
			visit(reinterpret_cast<const uint8_t*>(node) + kTreeNodeValue);
		}
		return true;
	}

	template <typename Visitor>
	bool ForEachDepartment(Visitor visit)
	{
		if (!versionOk || !HasVtable(budgetSimulator, kBudgetSimulatorVtable)) return false;
		const auto* base = reinterpret_cast<const uint8_t*>(budgetSimulator);
		return WalkTree(base + kBudgetSimulatorDepartments, 4096, [&](const uint8_t* value)
		{
			auto* department = *reinterpret_cast<cISC4DepartmentBudget* const*>(value + 4);
			if (HasVtable(department, kDepartmentBudgetVtable)) visit(department);
		});
	}

	uint32_t LocalObjectCount(cISC4DepartmentBudget* department)
	{
		return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(department) + kDepartmentLocalObjectCount);
	}

	// Sets FullExpenses of every locally funded expense line item to the current costs of its buildings.
	// Returns the number of line items changed.
	unsigned ResyncDepartmentUnguarded(cISC4DepartmentBudget* department)
	{
		if (!budgetSimulator || !HasVtable(department, kDepartmentBudgetVtable)) return 0;

		std::unordered_map<uint32_t, int64_t> target;
		std::unordered_map<uint32_t, cISC4LineItem*> lines;
		SC4Vector<cISC4LineItem*> lineItems;
		department->GetAllLineItems(lineItems);
		for (cISC4LineItem* line : lineItems)
		{
			if (line && line->IsLocalFundingItem() && line->GetType() == cISC4LineItem::Type::Expense)
			{
				target.emplace(line->GetID(), 0);
				lines.emplace(line->GetID(), line);
			}
		}
		if (target.empty()) return 0;

		const uint32_t departmentID = department->GetDepartmentID();
		const auto* base = reinterpret_cast<const uint8_t*>(department);
		const bool walked = WalkTree(base + kDepartmentLocalObjects, LocalObjectCount(department) + 1, [&](const uint8_t* value)
		{
			auto* holder = *reinterpret_cast<cISCPropertyHolder* const*>(value);
			if (!holder) return;
			SC4Vector<cISC4BudgetSimulator::BudgetItem> items;
			if (!budgetSimulator->GetBudgetItemInfo(holder, items)) return;
			for (const cISC4BudgetSimulator::BudgetItem& item : items)
			{
				if (item.departmentID != departmentID) continue;
				const auto entry = target.find(item.lineItem);
				if (entry != target.end()) entry->second += item.cost;
			}
		});
		if (!walked)
		{
			if (!walkWarningLogged)
			{
				SimLog::Write(SimLog::Level::Warning,
					"FundingSliderFix: department 0x%08X: the locally funded set is not the expected layout; not resynced.", departmentID);
				walkWarningLogged = true;
			}
			return 0;
		}

		unsigned changed = 0;
		for (const auto& [lineID, expenses] : target)
		{
			cISC4LineItem* const line = lines[lineID];
			const int64_t saved = line->GetFullExpenses();
			if (saved != expenses)
			{
				line->SetFullExpenses(expenses);
				++changed;
				SimLog::Write(SimLog::Level::Info,
					"FundingSliderFix: department 0x%08X line 0x%08X: full funding %lld -> %lld.",
					departmentID, lineID, static_cast<long long>(saved), static_cast<long long>(expenses));
			}
		}
		return changed;
	}

	// SetFullExpenses notifies the budget simulator; the guard keeps a nested recalculation from resyncing again.
	unsigned ResyncDepartment(cISC4DepartmentBudget* department)
	{
		if (inResync) return 0;
		inResync = true;
		const unsigned changed = ResyncDepartmentUnguarded(department);
		inResync = false;
		return changed;
	}

	bool __fastcall RecalculateHook(cISC4DepartmentBudget* self, void* /*edx*/)
	{
		ResyncDepartment(self);
		return originalRecalculate(self);
	}

	bool WriteSlot(uintptr_t address, uintptr_t value)
	{
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) return false;
		InterlockedExchange(reinterpret_cast<volatile LONG*>(address), static_cast<LONG>(value));
		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<void*>(address), sizeof(uintptr_t), oldProtect, &ignored);
		return true;
	}

	void InstallHook()
	{
		const uintptr_t current = *reinterpret_cast<const uintptr_t*>(kRecalculateSlot);
		if (current != kRecalculateOriginal)
		{
			SimLog::Write(SimLog::Level::Warning,
				"FundingSliderFix: vtable slot 0x%08X holds 0x%08X, not 0x%08X (another DLL hooks it?): not installed.",
				static_cast<unsigned>(kRecalculateSlot), static_cast<unsigned>(current), static_cast<unsigned>(kRecalculateOriginal));
			return;
		}
		originalRecalculate = reinterpret_cast<RecalculateFn>(current);
		hookInstalled = WriteSlot(kRecalculateSlot, reinterpret_cast<uintptr_t>(&RecalculateHook));
		SimLog::Write(hookInstalled ? SimLog::Level::Info : SimLog::Level::Error, hookInstalled
			? "FundingSliderFix: department budgets resync full funding with the buildings' current costs every month."
			: "FundingSliderFix: could not write the vtable slot.");
	}

	unsigned ResyncAll(unsigned* departmentsChanged)
	{
		unsigned lines = 0, departments = 0;
		ForEachDepartment([&](cISC4DepartmentBudget* department)
		{
			const unsigned changed = ResyncDepartment(department);
			lines += changed;
			departments += changed ? 1 : 0;
			if (changed && resetStuck)
			{
				const SC4Percentage* funding = department->GetFundingPercentage();
				const SC4Percentage* maximum = department->GetMaxAllowedFundingPercentage();
				const float before = funding ? funding->percentage : 1.0f;
				if (maximum && (before <= 0.005f || before >= maximum->percentage - 0.005f))
				{
					SC4Percentage full{};
					full.percentage = 1.0f;
					department->SetFundingPercentage(full, 0);
					SimLog::Write(SimLog::Level::Info, "FundingSliderFix: department 0x%08X was at %.0f %%: reset to 100 %%.",
						department->GetDepartmentID(), before * 100.0);
				}
			}
		});
		if (departmentsChanged) *departmentsChanged = departments;
		return lines;
	}

	void ShowDialog(const char* caption, const char* text)
	{
		SC4NotificationDialog::ShowDialog(cRZBaseString(text), cRZBaseString(caption));
	}

	std::vector<std::string_view> SplitArguments(std::string_view text)
	{
		std::vector<std::string_view> arguments;
		size_t start = 0;
		while (start < text.size())
		{
			const size_t end = text.find(' ', start);
			const size_t stop = end == std::string_view::npos ? text.size() : end;
			if (stop > start) arguments.push_back(text.substr(start, stop - start));
			start = stop + 1;
		}
		return arguments;
	}

	bool EqualsIgnoreCase(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size()) return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
		}
		return true;
	}

	////////////////////////////////////////////////////////////////////////
	// RemovePhantomBudgetItems: port of sc4-remove-phantom-budget-items.
	// Copyright (c) 2024 Nicholas Hayes. MIT License, see LICENSE-sc4-remove-phantom-budget-items.txt.
	////////////////////////////////////////////////////////////////////////

	bool ParseBudgetCategoryName(std::string_view input, std::vector<std::pair<uint32_t, uint32_t>>& outDepartmentAndPurpose)
	{
		outDepartmentAndPurpose.clear();

		if (EqualsIgnoreCase(input, "Fire"))
		{
			outDepartmentAndPurpose.emplace_back(0x28F55A9F, 0xEA567BC3);
		}
		else if (EqualsIgnoreCase(input, "Police"))
		{
			outDepartmentAndPurpose.emplace_back(0xA2963983, 0x0A567BAA);
		}
		else if (EqualsIgnoreCase(input, "Jail"))
		{
			outDepartmentAndPurpose.emplace_back(0xA2963984, 0xEA56768A);
		}
		else if (EqualsIgnoreCase(input, "Power"))
		{
			outDepartmentAndPurpose.emplace_back(0x8910BC8A, 0xCA58E540);
		}
		else if (EqualsIgnoreCase(input, "Education"))
		{
			outDepartmentAndPurpose.emplace_back(0x09188F4C, 0xEA5654B6); // Education Staff
			outDepartmentAndPurpose.emplace_back(0x4A538CC6, 0x4A5654BA); // Education Coverage
		}
		else if (EqualsIgnoreCase(input, "Health"))
		{
			outDepartmentAndPurpose.emplace_back(0x09188F42, 0xCA565486); // Health Staff
			outDepartmentAndPurpose.emplace_back(0xAA538CB3, 0xEA56549E); // Health Coverage
		}

		return !outDepartmentAndPurpose.empty();
	}

	bool GetBudgetItemsToRemove(
		cISC4DepartmentBudget* pDepartmentBudget,
		uint32_t purposeID,
		bool clearAllItems,
		std::vector<cRZAutoRefCount<cISC4BuildingOccupant>>& orphanedBuildingOccupants)
	{
		orphanedBuildingOccupants.clear();

		// The game's GetLocallyFundedItemsByPurpose (0x006AA9C0) returns no value: only the list is used.
		SC4List<cISCPropertyHolder*> locallyFundedItems;
		pDepartmentBudget->GetLocallyFundedItemsByPurpose(purposeID, locallyFundedItems);

		for (cISCPropertyHolder* propertyHolder : locallyFundedItems)
		{
			// If the property holder doesn't have an exemplar type property that
			// indicates the item has been deleted from the plugin folder.
			if (propertyHolder && (clearAllItems || !propertyHolder->HasProperty(kPropertyExemplarType)))
			{
				cRZAutoRefCount<cISC4BuildingOccupant> buildingOccupant;

				if (propertyHolder->QueryInterface(GZIID_cISC4BuildingOccupant, buildingOccupant.AsPPVoid()))
				{
					orphanedBuildingOccupants.push_back(std::move(buildingOccupant));
				}
			}
		}

		return !orphanedBuildingOccupants.empty();
	}

	void RemovePhantomBudgetItems(const cIGZString* cheatText)
	{
		const std::vector<std::string_view> arguments = SplitArguments(cheatText ? cheatText->ToChar() : "");

		if (arguments.size() != 2 && arguments.size() != 3)
		{
			ShowDialog(kRemovePhantomBudgetItemsName,
				"Usage: RemovePhantomBudgetItems <category> [all]\n"
				"<category>: One of Fire, Police, Jail, Power, Education or Health.\n"
				"[all] A non-default option that clears the entire budget category, instead of only plugin(s) that no longer exist.");
			return;
		}

		const std::string_view category = arguments[1];
		const bool clearAllItems = arguments.size() == 3 && EqualsIgnoreCase(arguments[2], "all");

		std::vector<std::pair<uint32_t, uint32_t>> departmentAndPurpose;
		if (!ParseBudgetCategoryName(category, departmentAndPurpose))
		{
			const std::string message = "Unknown Budget Category: " + std::string(category);
			ShowDialog(kRemovePhantomBudgetItemsName, message.c_str());
			return;
		}
		if (!budgetSimulator) return;

		std::unordered_set<uint32_t> orphanedBuildings;

		for (const auto& [departmentID, purposeID] : departmentAndPurpose)
		{
			cISC4DepartmentBudget* pDepartmentBudget = budgetSimulator->GetDepartmentBudget(departmentID);

			if (pDepartmentBudget)
			{
				std::vector<cRZAutoRefCount<cISC4BuildingOccupant>> buildingOccupants;

				if (GetBudgetItemsToRemove(pDepartmentBudget, purposeID, clearAllItems, buildingOccupants))
				{
					for (cISC4BuildingOccupant* pBuildingOccupant : buildingOccupants)
					{
						// Remove the building from the game's locally funded item list.
						pDepartmentBudget->RemoveLocallyFundedObject(pBuildingOccupant->AsOccupant()->AsPropertyHolder(), purposeID);

						const uint32_t buildingType = pBuildingOccupant->GetBuildingType();

						// Some buildings may also have an associated line item.
						pDepartmentBudget->RemoveLineItem(buildingType);

						if (!clearAllItems)
						{
							// The orphaned buildings are counted by building type because the Education and
							// Health categories can have the same building in two different departments.
							orphanedBuildings.emplace(buildingType);
						}
					}
				}

				// SC4SIM: bring the remaining line items in step at once rather than at the next month.
				if (fixEnabled && hookInstalled)
				{
					ResyncDepartment(pDepartmentBudget);
				}
			}
		}

		char message[128]{};
		if (clearAllItems)
		{
			std::snprintf(message, sizeof(message), "Cleared the %.*s budget category.", static_cast<int>(category.size()), category.data());
		}
		else if (orphanedBuildings.empty())
		{
			std::snprintf(message, sizeof(message), "No orphaned budget items found.");
		}
		else
		{
			std::snprintf(message, sizeof(message), "Removed %u orphaned budget item(s).", static_cast<unsigned>(orphanedBuildings.size()));
		}
		SimLog::Write(SimLog::Level::Info, "RemovePhantomBudgetItems: %s", message);
		ShowDialog(kRemovePhantomBudgetItemsName, message);
	}

	////////////////////////////////////////////////////////////////////////
	// SC4SIM cheats for the funding slider fix.
	////////////////////////////////////////////////////////////////////////

	void FundingResync()
	{
		char message[160]{};
		if (!hookInstalled || !budgetSimulator)
		{
			std::snprintf(message, sizeof(message), "The funding slider fix is not active (see SC4SIM.log).");
		}
		else
		{
			unsigned departments = 0;
			const unsigned lines = ResyncAll(&departments);
			std::snprintf(message, sizeof(message), "%u line item(s) corrected in %u department(s).", lines, departments);
		}
		SimLog::Write(SimLog::Level::Info, "FundingResync: %s", message);
		ShowDialog("FundingResync", message);
	}

	void FundingReset(const cIGZString* cheatText)
	{
		const std::vector<std::string_view> arguments = SplitArguments(cheatText ? cheatText->ToChar() : "");
		int percent = 100;
		if (arguments.size() >= 2)
		{
			const std::string value(arguments[1]);
			char* end = nullptr;
			const long parsed = std::strtol(value.c_str(), &end, 10);
			if (end == value.c_str() || parsed < 0 || parsed > 120)
			{
				ShowDialog("FundingReset", "Usage: FundingReset [percent]\n[percent]: 0 to 120, 100 by default.");
				return;
			}
			percent = static_cast<int>(parsed);
		}

		unsigned departments = 0;
		ForEachDepartment([&](cISC4DepartmentBudget* department)
		{
			if (LocalObjectCount(department) == 0 || department->GetIsFixedFunding()) return;
			SC4Percentage value{};
			value.percentage = percent / 100.0f;
			if (const SC4Percentage* maximum = department->GetMaxAllowedFundingPercentage())
			{
				if (value.percentage > maximum->percentage) value.percentage = maximum->percentage;
			}
			department->SetFundingPercentage(value, 0);
			++departments;
		});

		char message[128]{};
		std::snprintf(message, sizeof(message), "Funding set to %d %% in %u department(s).", percent, departments);
		SimLog::Write(SimLog::Level::Info, "FundingReset: %s", message);
		ShowDialog("FundingReset", message);
	}
}

void BudgetFixes::OnStart()
{
	const std::filesystem::path folder = ModuleFolder();
	if (folder.empty()) return;
	const std::filesystem::path ini = folder / L"SC4SIM.ini";
	fixEnabled = ReadBool(ini, L"FundingSliderFix", true);
	resetStuck = ReadBool(ini, L"FundingSliderResetStuck", false);
	removePhantomEnabled = ReadBool(ini, L"RemovePhantomBudgetItems", true);
}

void BudgetFixes::PostAppInit()
{
	const uint16_t version = VersionDetection::GetInstance().GetGameVersion();
	versionOk = version == kSupportedGameVersion;
	if (!versionOk)
	{
		SimLog::Write(SimLog::Level::Warning,
			"BudgetFixes: game version %u, the budget fixes need 1.1.641: FundingSliderFix and the funding cheats are off.", version);
	}

	if (!fixEnabled)
	{
		SimLog::Write(SimLog::Level::Info, "FundingSliderFix: disabled in SC4SIM.ini ([GameFixes] FundingSliderFix=false).");
	}
	else if (versionOk)
	{
		InstallHook();
	}

	cISC4AppPtr app;
	cIGZCheatCodeManager* cheats = app ? app->GetCheatCodeManager() : nullptr;
	if (!cheats) return;
	cheatManager = cheats;

	if (hookInstalled)
	{
		cheats->RegisterCheatCode(kCheatFundingResync, cRZBaseString("FundingResync"));
		cheats->RegisterCheatCode(kCheatFundingReset, cRZBaseString("FundingReset"));
	}

	if (!removePhantomEnabled)
	{
		SimLog::Write(SimLog::Level::Info, "RemovePhantomBudgetItems: disabled in SC4SIM.ini.");
	}
	else if (std::filesystem::exists(ModuleFolder() / L"SC4RemovePhantomBudgetItems.dll"))
	{
		SimLog::Write(SimLog::Level::Warning,
			"RemovePhantomBudgetItems: SC4RemovePhantomBudgetItems.dll is installed next to SC4SIM.dll; the standalone DLL "
			"handles the cheat. Remove it to use the SC4SIM version.");
	}
	else
	{
		removePhantomRegistered = cheats->RegisterCheatCode(kCheatRemovePhantomBudgetItems, cRZBaseString(kRemovePhantomBudgetItemsName));
		SimLog::Write(removePhantomRegistered ? SimLog::Level::Info : SimLog::Level::Warning, removePhantomRegistered
			? "RemovePhantomBudgetItems: cheat registered (port of sc4-remove-phantom-budget-items by Nicholas Hayes)."
			: "RemovePhantomBudgetItems: the cheat code is already registered by another DLL.");
	}
}

void BudgetFixes::PostAppShutdown()
{
	if (cheatManager)
	{
		if (hookInstalled)
		{
			cheatManager->UnregisterCheatCode(kCheatFundingResync);
			cheatManager->UnregisterCheatCode(kCheatFundingReset);
		}
		if (removePhantomRegistered)
		{
			cheatManager->UnregisterCheatCode(kCheatRemovePhantomBudgetItems);
			removePhantomRegistered = false;
		}
		cheatManager.Reset();
	}
	if (hookInstalled)
	{
		WriteSlot(kRecalculateSlot, kRecalculateOriginal);
		hookInstalled = false;
	}
	budgetSimulator = nullptr;
}

void BudgetFixes::DoMessage(cIGZMessage2* message)
{
	auto* standardMessage = static_cast<cIGZMessage2Standard*>(message);
	switch (message->GetType())
	{
	case kMessagePostCityInit:
	{
		cISC4City* city = static_cast<cISC4City*>(standardMessage->GetVoid1());
		budgetSimulator = city ? city->GetBudgetSimulator() : nullptr;
		if (hookInstalled && budgetSimulator)
		{
			unsigned departments = 0;
			const unsigned lines = ResyncAll(&departments);
			SimLog::Write(SimLog::Level::Info, "FundingSliderFix: city opened, %u line item(s) corrected in %u department(s).",
				lines, departments);
		}
		break;
	}
	case kMessagePreCityShutdown:
		budgetSimulator = nullptr;
		break;
	case kMessageCheatIssued:
	{
		const auto* cheatText = static_cast<const cIGZString*>(standardMessage->GetVoid2());
		switch (static_cast<uint32_t>(standardMessage->GetData1()))
		{
		case kCheatRemovePhantomBudgetItems:
			if (removePhantomRegistered) RemovePhantomBudgetItems(cheatText);
			break;
		case kCheatFundingResync:
			if (hookInstalled) FundingResync();
			break;
		case kCheatFundingReset:
			if (hookInstalled) FundingReset(cheatText);
			break;
		}
		break;
	}
	}
}
