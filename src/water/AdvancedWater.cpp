////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: Advanced Water Management.
//
// Port of sc4-advanced-water-management (https://github.com/0xC0000054/sc4-advanced-water-management),
// a DLL Plugin for SimCity 4 that improves interoperability with advanced water management.
// Copyright (c) 2024, 2025 Nicholas Hayes. Licensed under the MIT License,
// see LICENSE-sc4-advanced-water-management.txt.
//
// This file merges AdvancedWaterDllDirector.cpp and cSC4ViewInputControlQueryHooks.cpp of the original.
// Changes for SC4SIM:
//  * the director is now a module driven by the SC4SIM director (AdvancedWater.h); message subscriptions
//    and the cheat-code notification are made once by the SC4SIM director;
//  * log in SC4SIM.log, setting [AdvancedWaterManagement] Enabled in SC4SIM.ini;
//  * the module stays off if the standalone AdvancedWaterManagement.dll is loaded;
//  * the query hook is installed at PostAppInit instead of OnStart, without the Windows Implementation Library;
//  * the game version comes from the shared VersionDetection (src/powerpoles).
// The behaviour (query dialog efficiency meter, hover tool tip, monthly check of fresh/salt water pumps,
// ScanPumps cheat) is unchanged.
//
////////////////////////////////////////////////////////////////////////////

#include "AdvancedWater.h"
#include "SimLogFormat.h"
#include "SC4NotificationDialog.h"
#include "VersionDetection.h"
#include "WaterBuildingOccupantFilter.h"
#include "WaterBuildingPropertyUtil.h"
#include "QueryUIHooks/cIBuildingQueryCustomToolTipHookTarget.h"
#include "QueryUIHooks/cIBuildingQueryHookServer.h"

#include "cIGZCheatCodeManager.h"
#include "cIGZCOM.h"
#include "cIGZMessage2Standard.h"
#include "cIGZString.h"
#include "cIGZWin.h"
#include "cISC4App.h"
#include "cISC4BuildingOccupant.h"
#include "cISC4City.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantManager.h"
#include "cISC4PlumbingSimulator.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include "SC4Percentage.h"
#include "StringResourceKey.h"
#include "StringResourceManager.h"

#include <Windows.h>
#include <cwctype>
#include <filesystem>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessageCheatIssued = 0x230E27AC;
	constexpr uint32_t kMessagePostCityInit = 0x26D31EC1;
	constexpr uint32_t kMessagePreCityShutdown = 0x26D31EC2;

	constexpr uint32_t kOccupantGroup_Building_Water = 0x1401;
	constexpr uint32_t kScanPumpsCheatID = 0x8E9F3621;

	cISC4PlumbingSimulator* pPlumbingSim = nullptr;
	bool enabled = true;
	bool active = false;
	cRZAutoRefCount<cIGZCheatCodeManager> cheatManager;

	// ---------------------------------------------------------------- query dialog hook

	typedef bool(__cdecl* pfn_SetPerformanceMeterValue)(cIGZWin*, uint32_t, float);
	pfn_SetPerformanceMeterValue SetPerformanceMeterValue = nullptr;

	constexpr uintptr_t kJumpTableEntry641 = 0xA92570;          // water building efficiency (query dialog)
	constexpr uintptr_t kSetPerformanceMeterValue641 = 0x4C3CA0;
	uintptr_t originalJumpTableValue = 0;

	bool __cdecl Hook_SetOrGetWaterBuildingEfficiency(cIGZWin* pWin, uint32_t controlID, bool getValue, cISC4Occupant* pOccupant)
	{
		if (!getValue)
		{
			return true;
		}

		SC4Percentage efficiency{};
		uint32_t currentCapacity = 0;
		uint32_t maxCapacity = 0;
		bool shutDownByPollution = false;

		pPlumbingSim->GetWaterProducerInfo(pOccupant, &currentCapacity, &maxCapacity, &efficiency, &shutDownByPollution, nullptr);

		float efficiencyPercentage = efficiency.percentage * 100.0f;

		if (currentCapacity == 0 && maxCapacity > 0 && !shutDownByPollution)
		{
			if (WaterBuildingPropertyUtil::IsNonAquiferWaterSource(pOccupant))
			{
				// A fresh or salt water producer with no output was placed too far from its water source:
				// show it with an efficiency of 0.
				efficiencyPercentage = 0.0f;
			}
		}

		return SetPerformanceMeterValue(pWin, controlID, efficiencyPercentage);
	}

	bool WriteJumpTableEntry(uintptr_t address, uintptr_t value)
	{
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<LPVOID>(address), sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect))
		{
			return false;
		}
		*reinterpret_cast<uintptr_t*>(address) = value;
		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<LPVOID>(address), sizeof(uintptr_t), oldProtect, &ignored);
		return true;
	}

	void InstallQueryHook()
	{
		const uint16_t gameVersion = VersionDetection::GetInstance().GetGameVersion();
		if (gameVersion != 641)
		{
			LOG_ERROR("AdvancedWater: the water building query UI hook requires game version 641, found {}.", gameVersion);
			return;
		}

		originalJumpTableValue = *reinterpret_cast<uintptr_t*>(kJumpTableEntry641);
		SetPerformanceMeterValue = reinterpret_cast<pfn_SetPerformanceMeterValue>(kSetPerformanceMeterValue641);
		if (WriteJumpTableEntry(kJumpTableEntry641, reinterpret_cast<uintptr_t>(&Hook_SetOrGetWaterBuildingEfficiency)))
		{
			LOG_INFO("AdvancedWater: installed the water building query UI hook.");
		}
		else
		{
			originalJumpTableValue = 0;
			LOG_ERROR("AdvancedWater: failed to install the water building query UI hook (VirtualProtect error {}).", GetLastError());
		}
	}

	void UninstallQueryHook()
	{
		if (originalJumpTableValue != 0)
		{
			WriteJumpTableEntry(kJumpTableEntry641, originalJumpTableValue);
			originalJumpTableValue = 0;
		}
	}

	// ---------------------------------------------------------------- monthly pump check

	void ShowBrokenWaterBuildingDialog(uint32_t ltextMessageInstanceID, uint32_t ltextCaptionInstanceID)
	{
		constexpr uint32_t kLTEXTGroupID = 0x6A231EA4;
		SC4NotificationDialog::ShowDialog(
			StringResourceKey(kLTEXTGroupID, ltextMessageInstanceID),
			StringResourceKey(kLTEXTGroupID, ltextCaptionInstanceID));
	}

	bool WaterBuildingIterator(cISC4Occupant* pOccupant, void* /*pData*/)
	{
		WaterBuildingPropertyUtil::WaterSource source = WaterBuildingPropertyUtil::WaterSource::Aquifer;

		if (WaterBuildingPropertyUtil::IsWaterProducer(pOccupant, source)
			&& (source == WaterBuildingPropertyUtil::WaterSource::FreshWater
				|| source == WaterBuildingPropertyUtil::WaterSource::SaltWater)
			&& pPlumbingSim->GetWaterProduction(pOccupant) == 0)
		{
			// A fresh or salt water producer with no output: probably too far from its water source.
			if (source == WaterBuildingPropertyUtil::WaterSource::FreshWater)
			{
				ShowBrokenWaterBuildingDialog(0xC38E721E, 0x6242E7DF);
			}
			else
			{
				ShowBrokenWaterBuildingDialog(0x00C64A89, 0x503E14E3);
			}
			return false; // only one message per month
		}

		return true;
	}

	void ScanForNonWorkingWaterProducers()
	{
		if (!pPlumbingSim)
		{
			return;
		}

		cISC4AppPtr pSC4App;
		cISC4City* pCity = pSC4App ? pSC4App->GetCity() : nullptr;
		cISC4OccupantManager* pOccupantManager = pCity ? pCity->GetOccupantManager() : nullptr;
		if (pOccupantManager)
		{
			WaterBuildingOccupantFilter waterBuildingFilter;
			pOccupantManager->IterateOccupants(WaterBuildingIterator, nullptr, nullptr, nullptr, &waterBuildingFilter);
		}
	}

	// ---------------------------------------------------------------- hover tool tip (sc4-query-ui-hooks)

	bool GetBuildingOccupantName(cISC4Occupant* occupant, cIGZString& destination)
	{
		cRZAutoRefCount<cISC4BuildingOccupant> buildingOccupant;
		return occupant
			&& occupant->QueryInterface(GZIID_cISC4BuildingOccupant, buildingOccupant.AsPPVoid())
			&& buildingOccupant->GetName(destination);
	}

	bool SetLocalizedStringValue(uint32_t ltextGroupID, uint32_t ltextInstanceID, cIGZString& destination)
	{
		cRZAutoRefCount<cIGZString> localizedString;
		if (StringResourceManager::GetLocalizedString(StringResourceKey(ltextGroupID, ltextInstanceID), localizedString))
		{
			destination.Copy(*localizedString);
			return true;
		}
		return false;
	}

	// The tool tip callback object registered with the sc4-query-ui-hooks server. Static lifetime.
	class WaterToolTipTarget final : public cIBuildingQueryCustomToolTipHookTarget
	{
	public:
		bool QueryInterface(uint32_t riid, void** ppvObj) override
		{
			if (riid == GZIID_cIBuildingQueryCustomToolTipHookTarget)
			{
				*ppvObj = static_cast<cIBuildingQueryCustomToolTipHookTarget*>(this);
				AddRef();
				return true;
			}
			if (riid == GZIID_cIGZUnknown)
			{
				*ppvObj = static_cast<cIGZUnknown*>(this);
				AddRef();
				return true;
			}
			return false;
		}
		uint32_t AddRef() override { return ++refCount; }
		uint32_t Release() override { return refCount > 0 ? --refCount : 0; }

		bool ProcessToolTip(
			cISC4Occupant* const occupant,
			bool debugQuery,
			cIGZString& title,
			cIGZString& text,
			uint32_t& backgroundImageIID,
			uint32_t& meterImageIID,
			float& meterPercentage) override
		{
			constexpr uint32_t kUtilityToolTipBackgroundBitmapIID = 0x1441620A;
			constexpr uint32_t kWaterBuildingMeterBitmapIID = 0x14416219;

			if (debugQuery || !pPlumbingSim || !occupant || !occupant->IsOccupantGroup(kOccupantGroup_Building_Water)
				|| !GetBuildingOccupantName(occupant, title))
			{
				return false;
			}

			SC4Percentage efficiency{};
			uint32_t currentCapacity = 0;
			uint32_t maxCapacity = 0;
			bool shutDownByPollution = false;
			pPlumbingSim->GetWaterProducerInfo(occupant, &currentCapacity, &maxCapacity, &efficiency, &shutDownByPollution, nullptr);

			// Both the background and meter image must be set for the meter to appear.
			backgroundImageIID = kUtilityToolTipBackgroundBitmapIID;
			meterImageIID = kWaterBuildingMeterBitmapIID;
			meterPercentage = efficiency.percentage;

			if (shutDownByPollution)
			{
				return SetLocalizedStringValue(0xEA5524EB, 0x4A5FB66A, text);
			}
			if (currentCapacity == 0 && maxCapacity > 0 && WaterBuildingPropertyUtil::IsNonAquiferWaterSource(occupant))
			{
				meterPercentage = 0.0f;
			}
			return true;
		}

	private:
		uint32_t refCount = 0;
	};

	WaterToolTipTarget toolTipTarget;
	bool toolTipRegistered = false;

	bool GetHookServer(cRZAutoRefCount<cIBuildingQueryHookServer>& hookServer)
	{
		cIGZCOM* const pCOM = GZCOM();
		return pCOM && pCOM->GetClassObject(GZCLSID_cIBuildingQueryHookServer, GZIID_cIBuildingQueryHookServer, hookServer.AsPPVoid());
	}

	void PostCityInit(cIGZMessage2Standard* message)
	{
		cISC4City* pCity = static_cast<cISC4City*>(message->GetVoid1());
		if (!pCity)
		{
			return;
		}
		pPlumbingSim = pCity->GetPlumbingSimulator();

		cRZAutoRefCount<cIBuildingQueryHookServer> hookServer;
		if (GetHookServer(hookServer))
		{
			toolTipRegistered = hookServer->AddNotification(&toolTipTarget);
		}
		else
		{
			LOG_WARN("AdvancedWater: sc4-query-ui-hooks (SC4QueryUIHooks.dll) not found: no custom tool tip for water buildings.");
		}
	}

	void PreCityShutdown()
	{
		pPlumbingSim = nullptr;
		if (toolTipRegistered)
		{
			cRZAutoRefCount<cIBuildingQueryHookServer> hookServer;
			if (GetHookServer(hookServer))
			{
				hookServer->RemoveNotification(&toolTipTarget);
			}
			toolTipRegistered = false;
		}
	}

	bool ParseBool(std::wstring value, bool fallback)
	{
		for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
		if (value == L"true" || value == L"1" || value == L"yes" || value == L"on") return true;
		if (value == L"false" || value == L"0" || value == L"no" || value == L"off") return false;
		return fallback;
	}
}

void AdvancedWater::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	wchar_t value[32]{};
	GetPrivateProfileStringW(L"AdvancedWaterManagement", L"Enabled", L"true", value, 32, ini.c_str());
	enabled = ParseBool(value, true);
}

bool AdvancedWater::PostAppInit()
{
	if (GetModuleHandleW(L"AdvancedWaterManagement.dll") != nullptr)
	{
		LOG_WARN("AdvancedWater: the standalone AdvancedWaterManagement.dll is loaded; SC4SIM leaves water management "
			"to it. Remove one of the two.");
		return true;
	}
	if (!enabled)
	{
		LOG_INFO("AdvancedWater: disabled in SC4SIM.ini.");
		return true;
	}

	InstallQueryHook();

	cISC4AppPtr pSC4App;
	if (pSC4App)
	{
		if (cIGZCheatCodeManager* pCheatMgr = pSC4App->GetCheatCodeManager())
		{
			if (pCheatMgr->RegisterCheatCode(kScanPumpsCheatID, cRZBaseString("ScanPumps")))
			{
				cheatManager = pCheatMgr;
			}
		}
	}

	active = true;
	LOG_INFO("AdvancedWater: enabled (monthly check of fresh and salt water pumps, ScanPumps cheat).");
	return true;
}

void AdvancedWater::PostAppShutdown()
{
	if (cheatManager)
	{
		cheatManager->UnregisterCheatCode(kScanPumpsCheatID);
		cheatManager.Reset();
	}
	UninstallQueryHook();
	active = false;
}

void AdvancedWater::DoMessage(cIGZMessage2* message)
{
	if (!active || !message)
	{
		return;
	}

	auto* const standardMessage = static_cast<cIGZMessage2Standard*>(message);
	switch (message->GetType())
	{
	case kMessageCheatIssued:
		if (static_cast<uint32_t>(standardMessage->GetData1()) == kScanPumpsCheatID)
		{
			ScanForNonWorkingWaterProducers();
		}
		break;
	case kMessageSimNewMonth:
		ScanForNonWorkingWaterProducers();
		break;
	case kMessagePostCityInit:
		PostCityInit(standardMessage);
		break;
	case kMessagePreCityShutdown:
		PreCityShutdown();
		break;
	}
}
