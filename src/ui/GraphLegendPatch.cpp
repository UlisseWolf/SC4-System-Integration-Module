////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: wider legend in the line graphs of the Graphs window.
//
// The line graph lays itself out in 0x0076DAF1 (SimCity 4 1.1.641) from the width W of its graph area (0x8A4B0AA0):
//  - plot rectangle: x 45 .. W - 0x6E (0x0076DD4E), so the legend gets the last 110 pixels;
//  - with more than 2 plots each legend line has a colour swatch at W - 0x6C .. W - 0x5C (0x0076E162, 0x0076E159)
//    and its text starts at W - 0x5A (0x0076E1F8); with 1 or 2 plots the text starts at W - 0x6A (0x0076E0F5);
//  - the text window then runs from that x + 14 to W - 4.
// So a legend text has 72 pixels (with swatches), and "Nuclear Fission" or "Nuclear Fusion" wrap to two lines.
//
// The patch moves all five offsets left by LegendExtraWidth pixels ([GraphWindow], default 40), so the legend
// column, swatches and texts get that much wider and the plot that much narrower; CAM_UI.dat makes the Graphs window
// and its graph area 40 pixels wider (tools/power-graph/make_graph_ui.py), so the plot keeps its size. The offsets
// are 8-bit immediates that cannot hold more than 17 extra pixels, so every site jumps to a small block of code of
// the DLL that runs the same instructions with 32-bit offsets and jumps back. Each site's bytes are checked first;
// nothing is patched unless all five match, and the bytes are restored when the game closes.
//
////////////////////////////////////////////////////////////////////////////

#include "GraphLegendPatch.h"
#include "SimLog.h"
#include "VersionDetection.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	struct Site
	{
		uintptr_t address;              // first overwritten byte
		std::vector<uint8_t> original;  // expected bytes (5 or more: a jump fits)
		uintptr_t resume;               // address after the overwritten bytes
	};

	// The five sites of 0x0076DAF1, each with the instructions it overwrites.
	const Site kSites[] = {
		{ 0x0076DD4B, { 0x83, 0xE8, 0x14, 0x83, 0xEA, 0x6E }, 0x0076DD51 },             // sub eax,14h; sub edx,6Eh
		{ 0x0076E0F3, { 0x2B, 0xDA, 0x83, 0xEB, 0x6A }, 0x0076E0F8 },                   // sub ebx,edx; sub ebx,6Ah
		{ 0x0076E159, { 0x8D, 0x4A, 0xA4, 0x51, 0x8B, 0x4C, 0x24, 0x20 }, 0x0076E161 }, // lea ecx,[edx-5Ch]; push ecx; mov ecx,[esp+20h]
		{ 0x0076E162, { 0x83, 0xC2, 0x94, 0x52, 0x8B, 0xC8 }, 0x0076E168 },             // add edx,-6Ch; push edx; mov ecx,eax
		{ 0x0076E1F8, { 0x83, 0xEB, 0x5A, 0xE8, 0xE0, 0x49, 0xE9, 0xFF }, 0x0076E200 }, // sub ebx,5Ah; call 00602BE0
	};
	constexpr uintptr_t kCallTarget = 0x00602BE0;

	int32_t extraWidth = 40;
	uint8_t* cave = nullptr;
	bool patched = false;

	void Put32(std::vector<uint8_t>& code, uint32_t value)
	{
		for (int i = 0; i < 4; i++) code.push_back(static_cast<uint8_t>(value >> (8 * i)));
	}

	// jmp/call rel32 written at code.size(), relative to the cave address of the next instruction.
	void PutRelative(std::vector<uint8_t>& code, uint8_t opcode, uintptr_t target)
	{
		code.push_back(opcode);
		const uintptr_t next = reinterpret_cast<uintptr_t>(cave) + code.size() + 4;
		Put32(code, static_cast<uint32_t>(target - next));
	}

	// The relocated instructions of site i with offset + extraWidth, then a jump back.
	void BuildBlock(std::vector<uint8_t>& code, size_t i)
	{
		const uint32_t d = static_cast<uint32_t>(extraWidth);
		switch (i)
		{
		case 0: // sub eax,14h; sub edx,6Eh+d
			code.insert(code.end(), { 0x83, 0xE8, 0x14, 0x81, 0xEA }); Put32(code, 0x6E + d); break;
		case 1: // sub ebx,edx; sub ebx,6Ah+d
			code.insert(code.end(), { 0x2B, 0xDA, 0x81, 0xEB }); Put32(code, 0x6A + d); break;
		case 2: // lea ecx,[edx-(5Ch+d)]; push ecx; mov ecx,[esp+20h]
			code.insert(code.end(), { 0x8D, 0x8A }); Put32(code, static_cast<uint32_t>(-static_cast<int32_t>(0x5C + d)));
			code.insert(code.end(), { 0x51, 0x8B, 0x4C, 0x24, 0x20 }); break;
		case 3: // add edx,-(6Ch+d); push edx; mov ecx,eax
			code.insert(code.end(), { 0x81, 0xC2 }); Put32(code, static_cast<uint32_t>(-static_cast<int32_t>(0x6C + d)));
			code.insert(code.end(), { 0x52, 0x8B, 0xC8 }); break;
		case 4: // sub ebx,5Ah+d; call 00602BE0
			code.insert(code.end(), { 0x81, 0xEB }); Put32(code, 0x5A + d);
			PutRelative(code, 0xE8, kCallTarget); break;
		}
		PutRelative(code, 0xE9, kSites[i].resume);
	}

	bool WriteCode(uintptr_t address, const uint8_t* bytes, size_t size)
	{
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
		std::memcpy(reinterpret_cast<void*>(address), bytes, size);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &ignored);
		return true;
	}
}

void GraphLegendPatch::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");
	const int value = static_cast<int>(GetPrivateProfileIntW(L"GraphWindow", L"LegendExtraWidth", 40, ini.c_str()));
	extraWidth = value < 0 ? 0 : (value > 200 ? 200 : value);
}

void GraphLegendPatch::PostAppInit()
{
	if (extraWidth == 0)
	{
		SimLog::Write(SimLog::Level::Info, "GraphLegend: LegendExtraWidth=0, legend unchanged.");
		return;
	}
	if (VersionDetection::GetInstance().GetGameVersion() != 641)
	{
		SimLog::Write(SimLog::Level::Warning, "GraphLegend: needs SimCity 4 1.1.641; legend unchanged.");
		return;
	}
	for (const Site& site : kSites)
	{
		if (std::memcmp(reinterpret_cast<const void*>(site.address), site.original.data(), site.original.size()) != 0)
		{
			SimLog::Write(SimLog::Level::Warning,
				"GraphLegend: code at 0x%08X is not the expected one (another version or DLL); legend unchanged.",
				static_cast<unsigned>(site.address));
			return;
		}
	}
	cave = static_cast<uint8_t*>(VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (!cave) return;

	std::vector<uint8_t> code;
	std::array<uintptr_t, std::size(kSites)> blocks{};
	for (size_t i = 0; i < std::size(kSites); i++)
	{
		blocks[i] = reinterpret_cast<uintptr_t>(cave) + code.size();
		BuildBlock(code, i);
	}
	std::memcpy(cave, code.data(), code.size());
	FlushInstructionCache(GetCurrentProcess(), cave, code.size());

	for (size_t i = 0; i < std::size(kSites); i++)
	{
		const Site& site = kSites[i];
		std::vector<uint8_t> jump(site.original.size(), 0x90); // jmp block; nop...
		jump[0] = 0xE9;
		const uint32_t rel = static_cast<uint32_t>(blocks[i] - (site.address + 5));
		std::memcpy(&jump[1], &rel, 4);
		WriteCode(site.address, jump.data(), jump.size());
	}
	patched = true;
	SimLog::Write(SimLog::Level::Info, "GraphLegend: graph legend %d pixels wider.", extraWidth);
}

void GraphLegendPatch::PostAppShutdown()
{
	if (!patched) return;
	for (const Site& site : kSites) WriteCode(site.address, site.original.data(), site.original.size());
	patched = false;
}
