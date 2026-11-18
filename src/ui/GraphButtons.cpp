////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: buttons of the Graphs window for every graph exemplar.
//
// The game already handles any number of graphs: a click on a window of ID 0x4000-0x4FFF selects graph (ID & 0xFF)
// (cSC4GraphWindow message handler, 0x0076E5D0), the radio state follows window 0x4000 + index (0x0076B8B0) and the
// graph is loaded from exemplar 0x6534284A / 0xCA4AD545 / index (0x0076D3D0). Only the window lacks buttons: the UI
// (0x00000000 / 0x96A006B0 / 0x6BC9065A) has 18, one per Maxis graph (instances 0x01-0x13, 0x18).
//
// The control panel builds the Graphs UI in 0x007EDEB0 (called once, at 0x007F4665), keeping the radio-button panel
// (window 0x0A4A8176) at this+0x304. This module hooks that call and, after it, adds a button for every graph
// exemplar of instance 0x01-0xFF that has none: the button comes from the template UI 0x00000000 / 0x96A006B0 /
// 0x5C4DE300 (a copy of the Maxis graph button, in CAM_UI.dat), gets ID 0x4000 + instance and the graph's name
// (User Visible Name Key 0x8A416A99, else the exemplar name). Every button, the Maxis ones included, goes to the cell
// of the 3-column grid that its exemplar asks for with Graph Button Cell (0x5C4DE350: column 0-2, row 0-31): this is
// how CAM_Exemplar_Graphs.dat orders the graphs by theme. Buttons of the UI without it stay where they are; new
// buttons without it, or whose cell is taken, go to the next free cell. The panel grows upward by one 18-pixel row for every 3 new buttons; the two graph windows (0x8A8B5B71
// expanded, 0x8A8B5B72 collapsed) move up with it, and the filler 0x5C4DE310 (in CAM_UI.dat) paints the new rows.
//
////////////////////////////////////////////////////////////////////////////

#include "GraphButtons.h"
#include "SimLog.h"
#include "RelativeCallPatch.h"
#include "VersionDetection.h"

#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZString.h"
#include "cIGZUIScriptService.h"
#include "cIGZVariant.h"
#include "cIGZWin.h"
#include "cISCProperty.h"
#include "cISCPropertyHolder.h"
#include "cISCResExemplar.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include "StringResourceKey.h"
#include "StringResourceManager.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uintptr_t kBuildGraphsUiCall = 0x007F4665;   // call cSC4ControlPanel::BuildGraphsUI
	constexpr uintptr_t kBuildGraphsUi = 0x007EDEB0;
	constexpr size_t kPanelField = 0x304;                   // cIGZWin* radio-button panel (0x0A4A8176)

	constexpr uint32_t kExemplarType = 0x6534284A;
	constexpr uint32_t kGraphGroup = 0xCA4AD545;
	constexpr uint32_t kPropExemplarName = 0x00000020;
	constexpr uint32_t kPropUserVisibleNameKey = 0x8A416A99;
	constexpr uint32_t kPropGraphButtonCell = 0x5C4DE350;   // Uint32 x2: column (0-2), row (6 = first row after Maxis)

	constexpr uint32_t kUiType = 0x00000000;
	constexpr uint32_t kUiGroup = 0x96A006B0;
	constexpr uint32_t kButtonTemplateUi = 0x5C4DE300;
	constexpr uint32_t kButtonTemplateId = 0x5C4DE301;
	constexpr uint32_t kFillerId = 0x5C4DE310;

	constexpr uint32_t kButtonContainerId = 0xAA9244B6;
	constexpr uint32_t kGraphWindowExpandedId = 0x8A8B5B71;
	constexpr uint32_t kGraphWindowCollapsedId = 0x8A8B5B72;
	constexpr uint32_t kGraphButtonBase = 0x4000;

	constexpr int32_t kRowHeight = 18;
	constexpr int32_t kVanillaRows = 6;
	constexpr int32_t kMaxRows = 32;
	constexpr int32_t kColumns = 3;
	constexpr std::array<int32_t, 3> kColumnLeft = { 0, 170, 340 };
	constexpr std::array<int32_t, 3> kColumnRight = { 166, 336, 496 };

	bool enabled = true;
	PowerPoles::RelativeCallPatch buildPatch;

	struct GraphInfo
	{
		uint32_t instance = 0;
		cRZBaseString name;
		cIGZWin* button = nullptr;  // button of the UI, if any
		bool hasCell = false;
		bool keep = false;          // UI button without Graph Button Cell: stays where it is
		int32_t column = 0;
		int32_t row = 0;
	};

	// First two values of a numeric array property (Uint8, Uint32 or Sint32).
	bool ReadPair(const cISCPropertyHolder* holder, uint32_t id, int32_t& first, int32_t& second)
	{
		const cISCProperty* const property = holder ? holder->GetProperty(id) : nullptr;
		const cIGZVariant* const variant = property ? property->GetPropertyValue() : nullptr;
		if (!variant || variant->GetCount() < 2) return false;
		switch (variant->GetType())
		{
		case cIGZVariant::Uint8Array: first = variant->RefUint8()[0]; second = variant->RefUint8()[1]; return true;
		case cIGZVariant::Uint32Array:
			first = static_cast<int32_t>(variant->RefUint32()[0]); second = static_cast<int32_t>(variant->RefUint32()[1]);
			return true;
		case cIGZVariant::Sint32Array: first = variant->RefSint32()[0]; second = variant->RefSint32()[1]; return true;
		default: return false;
		}
	}

	void ReadGraphInfo(cIGZPersistResourceManager* resources, GraphInfo& info)
	{
		const uint32_t instance = info.instance;
		cRZAutoRefCount<cISCResExemplar> exemplar;
		const cISCPropertyHolder* holder = nullptr;
		if (resources->GetResource(cGZPersistResourceKey(kExemplarType, kGraphGroup, instance), GZIID_cISCResExemplar,
			exemplar.AsPPVoid(), 0, nullptr) && exemplar)
		{
			holder = exemplar->AsISCPropertyHolder();
		}

		info.hasCell = ReadPair(holder, kPropGraphButtonCell, info.column, info.row);
		if (info.hasCell && (info.column < 0 || info.column >= kColumns || info.row < 0 || info.row >= kMaxRows))
		{
			SimLog::Write(SimLog::Level::Warning,
				"GraphWindow: graph 0x%02X: Graph Button Cell (%d, %d) outside the grid (column 0-2, row 0-%d); "
				"next free cell used.", instance, info.column, info.row, kMaxRows - 1);
			info.hasCell = false;
		}

		const cISCProperty* const property = holder ? holder->GetProperty(kPropUserVisibleNameKey) : nullptr;
		const cIGZVariant* const variant = property ? property->GetPropertyValue() : nullptr;
		if (variant && variant->GetType() == cIGZVariant::Uint32Array && variant->GetCount() >= 3)
		{
			const uint32_t* const key = variant->RefUint32();
			cRZAutoRefCount<cIGZString> text;
			if (StringResourceManager::GetLocalizedString(StringResourceKey(key[1], key[2]), text) && text && text->Strlen() > 0)
			{
				info.name.Copy(*text);
				return;
			}
		}
		if (holder && holder->HasProperty(kPropExemplarName) && holder->GetProperty(kPropExemplarName, info.name)
			&& info.name.Strlen() > 0)
		{
			return;
		}
		info.name.Sprintf("Graph 0x%02X", instance);
	}

	void AddGraphButtons(void* controlPanel)
	{
		cIGZWin* const panel = *reinterpret_cast<cIGZWin**>(static_cast<uint8_t*>(controlPanel) + kPanelField);
		cIGZWin* const container = panel ? panel->GetChildWindowFromID(kButtonContainerId) : nullptr;
		cIGZWin* const parent = panel ? panel->GetParentWin() : nullptr;
		if (!container || !parent)
		{
			SimLog::Write(SimLog::Level::Warning, "GraphWindow: Graphs window not found; no button added.");
			return;
		}

		cIGZPersistResourceManagerPtr resources;
		cIGZUIScriptServicePtr scripts;
		if (!resources || !scripts) return;
		if (!resources->TestForKey(cGZPersistResourceKey(kUiType, kUiGroup, kButtonTemplateUi)))
		{
			SimLog::Write(SimLog::Level::Warning,
				"GraphWindow: button template UI 0x%08X missing (CAM_UI.dat): no button added.", kButtonTemplateUi);
			return;
		}

		// Every graph exemplar, with the button it already has in the UI (the Maxis ones) or none.
		std::vector<GraphInfo> graphs;
		for (uint32_t instance = 1; instance <= 0xFF; ++instance)
		{
			if (resources->TestForKey(cGZPersistResourceKey(kExemplarType, kGraphGroup, instance)))
			{
				GraphInfo& info = graphs.emplace_back();
				info.instance = instance;
				info.button = container->GetChildWindowFromID(kGraphButtonBase + instance);
				ReadGraphInfo(resources, info);
			}
		}

		// Cells. 1) Buttons of the UI without Graph Button Cell keep their place. 2) Graphs with Graph Button Cell
		// (lowest instance first) take their cell; existing buttons are moved there. 3) New buttons without it fill
		// the free cells row by row after the Maxis rows.
		std::array<std::array<bool, kColumns>, kMaxRows> taken{};
		for (GraphInfo& info : graphs)
		{
			if (!info.button || info.hasCell) continue;
			const int32_t left = info.button->GetL();
			info.column = left < kColumnLeft[1] ? 0 : (left < kColumnLeft[2] ? 1 : 2);
			info.row = std::clamp((info.button->GetT() + kRowHeight / 2 - 1) / kRowHeight, 0, kMaxRows - 1);
			taken[info.row][info.column] = true;
			info.keep = true;
		}
		for (GraphInfo& info : graphs)
		{
			if (!info.hasCell) continue;
			if (taken[info.row][info.column])
			{
				SimLog::Write(SimLog::Level::Warning, "GraphWindow: graph 0x%02X: cell (%d, %d) is taken; next free cell used.",
					info.instance, info.column, info.row);
				info.hasCell = false;
				continue;
			}
			taken[info.row][info.column] = true;
		}
		int32_t nextCell = 0;
		for (GraphInfo& info : graphs)
		{
			if (info.hasCell || info.keep) continue;
			while (nextCell < kMaxRows * kColumns && taken[nextCell / kColumns][nextCell % kColumns]) ++nextCell;
			if (nextCell >= kMaxRows * kColumns)
			{
				SimLog::Write(SimLog::Level::Warning, "GraphWindow: no free cell for graph 0x%02X.", info.instance);
				info.row = -1;
				continue;
			}
			info.row = nextCell / kColumns;
			info.column = nextCell % kColumns;
			taken[info.row][info.column] = true;
		}

		uint32_t added = 0;
		uint32_t moved = 0;
		int32_t lastRow = kVanillaRows - 1;
		for (GraphInfo& info : graphs)
		{
			if (info.row < 0) continue;
			lastRow = std::max(lastRow, info.row);
			if (info.keep) continue;
			cIGZWin* button = info.button;
			cRZAutoRefCount<cIGZWin> created;
			if (!button)
			{
				if (!scripts->CreateWindowFromScript(cGZPersistResourceKey(kUiType, kUiGroup, kButtonTemplateUi), container,
					kButtonTemplateId, GZIID_cIGZWin, created.AsPPVoid()) || !created)
				{
					SimLog::Write(SimLog::Level::Warning, "GraphWindow: button for graph 0x%02X not created.", info.instance);
					continue;
				}
				button = created;
				button->SetID(kGraphButtonBase + info.instance);
				button->SetCaption(info.name);
				button->ShowWindow();
				++added;
			}
			else
			{
				++moved;
			}
			const int32_t top = 1 + info.row * kRowHeight;
			button->SetArea(kColumnLeft[info.column], top, kColumnRight[info.column], top + kRowHeight);
			SimLog::Write(SimLog::Level::Debug, "GraphWindow: button 0x%04X \"%s\" at column %d, row %d.",
				kGraphButtonBase + info.instance, info.name.ToChar(), info.column, info.row);
		}
		SimLog::Write(SimLog::Level::Info, "GraphWindow: %u graphs, %u buttons added, %u moved.",
			static_cast<unsigned>(graphs.size()), added, moved);
		if (lastRow < kVanillaRows)
		{
			container->InvalidateSelf();
			return;
		}

		// Grow upward: the panel's top moves up by the new rows, the button container and the filler grow down by
		// the same amount (so the bottom stays where it was), the graph windows above move up with the panel.
		const int32_t delta = (lastRow + 1 - kVanillaRows) * kRowHeight;
		const int32_t containerBottom = container->GetB();
		panel->SetArea(panel->GetL(), panel->GetT() - delta, panel->GetR(), panel->GetB());
		container->SetArea(container->GetL(), container->GetT(), container->GetR(), containerBottom + delta);
		if (cIGZWin* const filler = panel->GetChildWindowFromID(kFillerId))
		{
			filler->SetArea(container->GetL(), containerBottom, container->GetR(), containerBottom + delta);
			filler->ShowWindow();
		}
		for (const uint32_t id : { kGraphWindowExpandedId, kGraphWindowCollapsedId })
		{
			if (cIGZWin* const window = parent->GetChildWindowFromID(id))
			{
				window->GZWinOffset(0, -delta);
			}
		}
		panel->InvalidateSelfAndParents();
		SimLog::Write(SimLog::Level::Info, "GraphWindow: panel %d pixels taller.", delta);
	}

	void __fastcall BuildGraphsUiHook(void* controlPanel, void* /*edx*/)
	{
		using BuildFn = void(__thiscall*)(void*);
		reinterpret_cast<BuildFn>(kBuildGraphsUi)(controlPanel);
		AddGraphButtons(controlPanel);
	}
}

void GraphButtons::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");
	wchar_t value[16]{};
	GetPrivateProfileStringW(L"GraphWindow", L"Enabled", L"true", value, 16, ini.c_str());
	std::wstring text(value);
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
}

void GraphButtons::PostAppInit()
{
	if (!enabled) return;
	if (VersionDetection::GetInstance().GetGameVersion() != 641)
	{
		SimLog::Write(SimLog::Level::Warning, "GraphWindow: needs SimCity 4 1.1.641; graph buttons not added.");
		return;
	}
	buildPatch.Configure("GraphWindow BuildGraphsUI", kBuildGraphsUiCall, reinterpret_cast<void*>(&BuildGraphsUiHook),
		kBuildGraphsUi);
	if (buildPatch.Install())
	{
		SimLog::Write(SimLog::Level::Info, "GraphWindow: a button for every graph exemplar.");
	}
	else
	{
		SimLog::Write(SimLog::Level::Warning, "GraphWindow: call site not as expected; graph buttons not added.");
	}
}

void GraphButtons::PostAppShutdown()
{
	buildPatch.Uninstall();
}
