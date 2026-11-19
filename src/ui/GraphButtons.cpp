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
// exemplar of instance 0x01-0xFF that asks for one: the button comes from the template UI 0x00000000 / 0x96A006B0 /
// 0x5C4DE300 (a copy of the Maxis graph button, in CAM_UI.dat), gets ID 0x4000 + instance and the graph's name
// (User Visible Name Key 0x8A416A99, else the exemplar name).
//
// The window is driven by Graph Button Cell (0x5C4DE350: column 0-2, row 0-31) alone: only graph exemplars with it get
// a button, in that cell of the 3-column grid; a new graph without it is ignored, so an old or foreign exemplar cannot
// take a cell or add a button by mistake. A Maxis graph (its button is in the UI) replaced by a plugin with an
// exemplar without it keeps its Maxis button where the UI has it, with a warning. Graph Button Hidden (0x5C4DE351, Bool) also gives a graph no button. A cell outside the grid or already
// taken is logged and the graph goes to the next free cell. When no graph exemplar has Graph Button Cell (SC4SIM data
// not installed, Maxis files only), the window is left as the game builds it.
//
// Pages: the button container keeps its 6 rows; row r of Graph Button Cell is row r % 6 of page r / 6, and one page
// is shown at a time. CAM_UI.dat has every CAM button (pages past the first hidden) and, under the container, the
// Previous / Next buttons and the page number (0x5C4DE320-0x5C4DE322); their clicks are taken from the graph window's
// message handler (vtable slot 0x00AB4FEC, 0x0076E500) before it handles the graph buttons. Buttons created here
// from the template (graphs of other plugins) do not receive clicks yet: CAM's buttons are therefore in the UI.
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
#include <utility>
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
	constexpr uint32_t kPropGraphButtonHidden = 0x5C4DE351; // Bool: no button (a UI button is hidden)

	constexpr uint32_t kUiType = 0x00000000;
	constexpr uint32_t kUiGroup = 0x96A006B0;
	constexpr uint32_t kButtonTemplateUi = 0x5C4DE300;
	constexpr uint32_t kButtonTemplateId = 0x5C4DE301;

	constexpr uint32_t kButtonContainerId = 0xAA9244B6;
	constexpr uint32_t kGraphButtonBase = 0x4000;

	constexpr int32_t kRowHeight = 18;
	constexpr int32_t kMaxRows = 32;
	constexpr int32_t kColumns = 3;
	constexpr std::array<int32_t, 3> kColumnLeft = { 0, 170, 340 };
	constexpr std::array<int32_t, 3> kColumnRight = { 166, 336, 496 };

	// Pages: < / page number / > (CAM_UI.dat, in the button container, hidden until the DLL shows them), in the last
	// cell of the right column of every page, which no graph may use.
	constexpr uint32_t kNavPrevId = 0x5C4DE320;
	constexpr uint32_t kNavNextId = 0x5C4DE321;
	constexpr uint32_t kNavLabelId = 0x5C4DE322;
	constexpr int32_t kNavColumn = 2;
	// The graph window's message handler (cIGZWinProc::DoWinProcMessage of the object built at 0x0076CFE0), slot 3
	// of its vtable 0x00AB4FE0: button clicks of the Graphs window (message type 3, 0x287259F6, button ID) arrive
	// there; the Previous / Next clicks are taken before it.
	constexpr uintptr_t kGraphProcSlot = 0x00AB4FEC;
	constexpr uintptr_t kGraphProc = 0x0076E500;
	constexpr uint32_t kButtonClickMessage = 0x287259F6;

	bool enabled = true;
	PowerPoles::RelativeCallPatch buildPatch;
	bool procPatched = false;

	// Buttons by page, the navigation windows and the page shown.
	std::vector<std::pair<cRZAutoRefCount<cIGZWin>, int32_t>> pageButtons;
	cRZAutoRefCount<cIGZWin> navPrev, navNext, navLabel, buttonContainer;
	int32_t pageCount = 1;
	int32_t currentPage = 0;

	// Keeps a reference to the button (operator= adds it; the AddRef constructor of cRZAutoRefCount warns C4100).
	void AddPageButton(cIGZWin* button, int32_t page)
	{
		cRZAutoRefCount<cIGZWin> reference;
		reference = button;
		pageButtons.emplace_back(std::move(reference), page);
	}

	void ShowPage(int32_t page)
	{
		currentPage = (page % pageCount + pageCount) % pageCount;
		for (auto& [button, buttonPage] : pageButtons)
		{
			if (buttonPage == currentPage) button->ShowWindow(); else button->HideWindow();
		}
		if (navLabel)
		{
			cRZBaseString text;
			text.Sprintf("%d / %d", currentPage + 1, pageCount);
			navLabel->SetCaption(text);
		}
		if (buttonContainer) buttonContainer->InvalidateSelf();
	}

	using GraphProcFn = bool(__thiscall*)(void*, void*, void*);

	bool __fastcall GraphProcHook(void* self, void* /*edx*/, void* window, void* message)
	{
		if (message && pageCount > 1)
		{
			const uint32_t* const m = static_cast<const uint32_t*>(message);
			if (m[0] == 3 && m[1] == kButtonClickMessage && (m[2] == kNavPrevId || m[2] == kNavNextId))
			{
				ShowPage(currentPage + (m[2] == kNavNextId ? 1 : -1));
				return false;
			}
		}
		return reinterpret_cast<GraphProcFn>(kGraphProc)(self, window, message);
	}

	bool WriteSlot(uintptr_t slot, uintptr_t value)
	{
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(slot), sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) return false;
		*reinterpret_cast<uintptr_t*>(slot) = value;
		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<void*>(slot), sizeof(uintptr_t), oldProtect, &ignored);
		return true;
	}

	struct GraphInfo
	{
		uint32_t instance = 0;
		cRZBaseString name;
		cIGZWin* button = nullptr;  // button of the UI, if any
		bool listed = false;        // has Graph Button Cell: the graph gets a button
		bool hasCell = false;       // ... and the cell is inside the grid
		bool hidden = false;        // no Graph Button Cell, or Graph Button Hidden: no button
		bool keep = false;          // Maxis button whose exemplar lost Graph Button Cell: stays where the UI has it
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

		if (const cISCProperty* const hiddenProperty = holder ? holder->GetProperty(kPropGraphButtonHidden) : nullptr)
		{
			const cIGZVariant* const value = hiddenProperty->GetPropertyValue();
			bool flag = false;
			uint32_t number = 0;
			info.hidden = value && ((value->GetType() == cIGZVariant::Bool && value->GetValBool(flag) && flag)
				|| (value->GetType() == cIGZVariant::Uint32 && value->GetValUint32(number) && number != 0));
		}
		info.listed = ReadPair(holder, kPropGraphButtonCell, info.column, info.row);
		info.hasCell = info.listed;
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

		// Without any Graph Button Cell (Maxis files only) the window stays as the game built it.
		if (std::none_of(graphs.begin(), graphs.end(), [](const GraphInfo& info) { return info.listed; }))
		{
			SimLog::Write(SimLog::Level::Info,
				"GraphWindow: no graph exemplar has Graph Button Cell (0x%08X); Graphs window left unchanged.", kPropGraphButtonCell);
			return;
		}

		// Column geometry from the Maxis buttons of the UI (CAM_UI.dat can make the window wider): each column spans the
		// buttons the UI puts in it; a column without one keeps the vanilla 166-pixel layout.
		std::array<int32_t, kColumns> columnLeft = kColumnLeft;
		std::array<int32_t, kColumns> columnRight = kColumnRight;
		std::array<bool, kColumns> columnSeen{};
		for (const GraphInfo& info : graphs)
		{
			if (!info.button || !info.listed || info.hidden || info.column < 0 || info.column >= kColumns) continue;
			const int32_t c = info.column;
			columnLeft[c] = columnSeen[c] ? std::min(columnLeft[c], info.button->GetL()) : info.button->GetL();
			columnRight[c] = columnSeen[c] ? std::max(columnRight[c], info.button->GetR()) : info.button->GetR();
			columnSeen[c] = true;
		}

		// Cells. 1) Graphs without Graph Button Cell, or with Graph Button Hidden, are ignored: no button, a button of
		// the UI is hidden. 2) The others (lowest instance first) take their cell; buttons of the UI are moved there.
		// 3) Those whose cell is outside the grid or taken fill the free cells row by row.
		uint32_t hiddenCount = 0;
		uint32_t ignoredCount = 0;
		uint32_t keptCount = 0;
		std::array<std::array<bool, kColumns>, kMaxRows> taken{};
		for (GraphInfo& info : graphs)
		{
			// A Maxis graph (its button is in the UI) whose exemplar has no Graph Button Cell was replaced by a plugin
			// loaded after CAM_Exemplar_Graphs.dat: its button stays where the UI puts it, so the graph is not lost.
			if (!info.listed && !info.hidden && info.button)
			{
				const int32_t left = info.button->GetL();
				int32_t column = 0;
				for (int32_t c = 1; c < kColumns; ++c)
				{
					if (left >= columnLeft[c]) column = c;
				}
				info.column = column;
				info.row = std::clamp((info.button->GetT() + kRowHeight / 2 - 1) / kRowHeight, 0, kMaxRows - 1);
				info.keep = true;
				taken[info.row][info.column] = true;
				++keptCount;
				SimLog::Write(SimLog::Level::Warning,
					"GraphWindow: graph 0x%02X \"%s\" has no Graph Button Cell (replaced by another plugin?); its Maxis button stays at column %d, row %d.",
					info.instance, info.name.ToChar(), info.column, info.row);
				continue;
			}
			if (!info.listed && !info.hidden)
			{
				SimLog::Write(SimLog::Level::Debug, "GraphWindow: graph 0x%02X \"%s\" has no Graph Button Cell; ignored.",
					info.instance, info.name.ToChar());
				++ignoredCount;
				info.hidden = true;
			}
			else if (info.hidden)
			{
				++hiddenCount;
			}
			if (!info.hidden) continue;
			if (info.button) info.button->HideWindow();
			info.row = -1;
		}
		// The page-button cell of every page (last row of the right column) stays free when the UI has the buttons.
		cIGZWin* const navInUi = container->GetChildWindowFromID(kNavPrevId);
		const int32_t pageRowsForNav = std::max(1, (container->GetB() - container->GetT() - 1 + kRowHeight - 1) / kRowHeight);
		if (navInUi)
		{
			for (int32_t row = pageRowsForNav - 1; row < kMaxRows; row += pageRowsForNav) taken[row][kNavColumn] = true;
		}
		for (GraphInfo& info : graphs)
		{
			if (info.hidden || info.keep || !info.hasCell) continue;
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
			if (info.hidden || info.keep || info.hasCell) continue;
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

		// Pages of as many rows as the button container has (6): row r of Graph Button Cell is row r % rows of page
		// r / rows. One page is shown at a time; Previous / Next (CAM_UI.dat) turn the pages.
		const int32_t pageRows = std::max(1, (container->GetB() - container->GetT() - 1 + kRowHeight - 1) / kRowHeight);
		uint32_t added = 0;
		uint32_t moved = 0;
		pageButtons.clear();
		int32_t lastPage = 0;
		for (GraphInfo& info : graphs)
		{
			if (info.row < 0) continue;
			cIGZWin* button = info.button;
			cRZAutoRefCount<cIGZWin> created;
			if (info.keep)
			{
				// Kept where the UI has it: on the first page if the UI shows it, else on the second.
				const int32_t page = button->IsVisible() ? 0 : 1;
				AddPageButton(button, page);
				lastPage = std::max(lastPage, page);
				continue;
			}
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
				++added;
			}
			else
			{
				++moved;
			}
			const int32_t page = info.row / pageRows;
			const int32_t top = 1 + (info.row % pageRows) * kRowHeight;
			button->SetArea(columnLeft[info.column], top, columnRight[info.column], top + kRowHeight);
			AddPageButton(button, page);
			lastPage = std::max(lastPage, page);
			SimLog::Write(SimLog::Level::Debug, "GraphWindow: button 0x%04X \"%s\" on page %d, column %d, row %d.",
				kGraphButtonBase + info.instance, info.name.ToChar(), page + 1, info.column, info.row % pageRows);
		}

		pageCount = lastPage + 1;
		buttonContainer = container;
		navPrev = container->GetChildWindowFromID(kNavPrevId);
		navNext = container->GetChildWindowFromID(kNavNextId);
		navLabel = container->GetChildWindowFromID(kNavLabelId);
		for (cIGZWin* const nav : { static_cast<cIGZWin*>(navPrev), static_cast<cIGZWin*>(navNext), static_cast<cIGZWin*>(navLabel) })
		{
			if (!nav) continue;
			if (pageCount > 1) nav->ShowWindow(); else nav->HideWindow();
		}
		if (pageCount > 1 && (!navPrev || !navNext || !procPatched))
		{
			SimLog::Write(SimLog::Level::Warning,
				"GraphWindow: %d pages but no page buttons (CAM_UI.dat %s); only the first page can be shown.",
				pageCount, procPatched ? "without Previous / Next" : "handler not patched");
		}
		ShowPage(0);
		SimLog::Write(SimLog::Level::Info,
			"GraphWindow: %u graphs, %u buttons added, %u moved, %u hidden, %u ignored, %u Maxis buttons kept; %d page(s) of %d rows.",
			static_cast<unsigned>(graphs.size()), added, moved, hiddenCount, ignoredCount, keptCount, pageCount, pageRows);
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
	if (*reinterpret_cast<const uintptr_t*>(kGraphProcSlot) == kGraphProc)
	{
		procPatched = WriteSlot(kGraphProcSlot, reinterpret_cast<uintptr_t>(&GraphProcHook));
	}
	if (!procPatched)
	{
		SimLog::Write(SimLog::Level::Warning, "GraphWindow: graph window handler not as expected; page buttons disabled.");
	}
	if (buildPatch.Install())
	{
		SimLog::Write(SimLog::Level::Info, "GraphWindow: a button for every graph exemplar with Graph Button Cell.");
	}
	else
	{
		SimLog::Write(SimLog::Level::Warning, "GraphWindow: call site not as expected; graph buttons not added.");
	}
}

void GraphButtons::PostAppShutdown()
{
	buildPatch.Uninstall();
	if (procPatched)
	{
		WriteSlot(kGraphProcSlot, kGraphProc);
		procPatched = false;
	}
	pageButtons.clear();
	navPrev = nullptr; navNext = nullptr; navLabel = nullptr; buttonContainer = nullptr;
}
