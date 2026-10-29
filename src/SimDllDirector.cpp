// SC4SIM.dll - DLL of the SC4 System Integration Module (SIM) of the Colossus Addon Mod (CAM).
//
// Version 1.0.0:
//  * "sim" Lua table in the advisor scripting context: sim.version() returns the DLL version, so scripts can
//    check that the DLL is installed (if sim ~= nil and sim.version ~= nil).
//  * When a city opens, it checks that the SIM Lua (sim_advisor_rewards.lua in CAM_LUA_Controller.dat) is loaded and
//    that every advisor reward creator exists, and writes the result to SC4SIM.log.
//
// Version 1.1.0:
//  * Custom power poles: port of the "re/custom-powerlines" sample of sc4-render-services (LGPL 2.1), without the
//    SC4RenderServices ImGui overlay. See src/powerpoles and docs/power-pole-customization.md; settings in SC4SIM.ini.
//
// Version 1.2.0:
//  * Advanced Water Management: port of sc4-advanced-water-management by Nicholas Hayes (MIT). See src/water.
//  * The director subscribes once to every message its modules need and forwards them.
//
// Version 1.3.0:
//  * Power lines across empty zones and farm fields (CellIsOKForPlacement hook, CrossZones in SC4SIM.ini).
//    See docs/power-lines-across-zones.md.
//
// Version 1.4.0:
//  * Dezoning under new poles (PlacePoles hook, DezoneUnderPoles / DezoneFieldCells in SC4SIM.ini).
//
// Version 1.5.0:
//  * Mixed residential + commercial occupancy (src/mixed, [MixedOccupancy] in SC4SIM.ini, MixedOccupancy cheat).
//    See docs/mixed-occupancy.md.
//
// Version 1.5.1:
//  * Mixed occupancy: the monthly fill of the secondary types is removed. The game's own growth code
//    (cSC4GrowthDeveloper::DoIdle -> cSC4TractDeveloper::UpdatePopulation) already fills them through the
//    GetAvailableCapacity hook. The MixedOccupancy cheat only writes the mixed lots to the log.
// The reward creators themselves are Lua (create_reward_utilities, ...): reward scripts call them while the game
// compiles them, before any DLL can reach the city's Lua, so they live in the SIM Lua package 0x0151A000.
#include "SimLog.h"
#include "mixed/MixedOccupancy.h"
#include "powerpoles/PowerPoleCustomization.h"
#include "water/AdvancedWater.h"
#include "version.h"

#include "cIGZCOM.h"
#include "cIGZFrameWork.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZMessageTarget2.h"
#include "cISC4AdvisorSystem.h"
#include "cISC4City.h"
#include "cISCLua.h"
#include "cRZAutoRefCount.h"
#include "cRZCOMDllDirector.h"
#include "GZCLSIDDefs.h"
#include "GZServPtrs.h"
#include "cIGZCheatCodeManager.h"
#include "cISC4App.h"
#include "SCLuaUtil.h"

#include <array>
#include <string_view>

static constexpr uint32_t kSimDirectorID = 0xC4511A01;
static constexpr uint32_t kMsgPostCityInit = 0x26D31EC1;
static constexpr uint32_t kMsgPreCityShutdown = 0x26D31EC2;
static constexpr uint32_t kMsgCheatIssued = 0x230E27AC;

namespace
{
	int32_t Version(lua_State* state)
	{
		cRZAutoRefCount<cISCLua> lua = SCLuaUtil::GetISCLuaFromFunctionState(state);
		lua->PushString(SIM_DLL_VERSION_STR);
		return 1;
	}

	// Reward creators added by the SIM Lua, and the Maxis ones they complete.
	constexpr std::array<const char*, 4> kSimRewardCreators = {
		"create_reward_utilities", "create_reward_transportation", "create_reward_environment", "create_reward_health_education" };
	constexpr std::array<const char*, 3> kMaxisRewardCreators = {
		"create_reward_cityplanning", "create_reward_finances", "create_reward_safety" };

	bool IsGlobalFunction(cISCLua* lua, const char* name)
	{
		lua->GetGlobal(name);
		const bool result = lua->IsFunction(-1);
		lua->Pop(1);
		return result;
	}
}

class SimDllDirector final : public cRZCOMDllDirector, public cIGZMessageTarget2
{
public:
	uint32_t GetDirectorID() const override { return kSimDirectorID; }

	bool QueryInterface(uint32_t riid, void** ppvObj) override
	{
		if (riid == GZCLSID::kcIGZMessageTarget2)
		{
			*ppvObj = static_cast<cIGZMessageTarget2*>(this);
			AddRef();
			return true;
		}
		return cRZCOMDllDirector::QueryInterface(riid, ppvObj);
	}
	uint32_t AddRef() override { return cRZCOMDllDirector::AddRef(); }
	uint32_t Release() override { return cRZCOMDllDirector::Release(); }

	bool OnStart(cIGZCOM* pCOM) override
	{
		SimLog::Open();
		SimLog::Write(SimLog::Level::Info, "SC4 SIM DLL " SIM_DLL_VERSION_STR);
		PowerPoleCustomization::OnStart();
		AdvancedWater::OnStart();
		MixedOccupancy::OnStart();
		cIGZFrameWork* const framework = pCOM->FrameWork();
		if (framework->GetState() < cIGZFrameWork::kStatePreAppInit) framework->AddHook(this);
		else PostAppInit();
		return true;
	}

	bool PostAppInit() override
	{
		// One subscription per message for all modules: modules never subscribe themselves.
		cIGZMessageServer2Ptr ms2;
		if (ms2)
		{
			for (uint32_t type : { kMsgPostCityInit, kMsgPreCityShutdown, AdvancedWater::kMessageSimNewMonth })
			{
				if (!ms2->AddNotification(this, type))
				{
					SimLog::Write(SimLog::Level::Error, "Could not subscribe to message 0x%08X.", type);
				}
			}
		}
		else
		{
			SimLog::Write(SimLog::Level::Error, "Message server unavailable: no city notifications.");
		}

		PowerPoleCustomization::PostAppInit(this);
		AdvancedWater::PostAppInit();
		MixedOccupancy::PostAppInit();

		// Cheat codes (keepwires, ScanPumps, MixedOccupancy) are registered by the modules; the notification is added once here.
		cISC4AppPtr app;
		if (app)
		{
			if (cIGZCheatCodeManager* cheats = app->GetCheatCodeManager())
			{
				cheats->AddNotification2(this, 0);
				cheatManager = cheats;
			}
		}
		return true;
	}

	bool PostAppShutdown() override
	{
		if (cheatManager)
		{
			cheatManager->RemoveNotification2(this, 0);
			cheatManager.Reset();
		}
		MixedOccupancy::PostAppShutdown();
		AdvancedWater::PostAppShutdown();
		PowerPoleCustomization::PostAppShutdown(this);
		return true;
	}

	bool DoMessage(cIGZMessage2* message) override
	{
		const uint32_t type = message->GetType();
		if (type == kMsgPostCityInit)
		{
			PostCityInit(static_cast<cISC4City*>(static_cast<cIGZMessage2Standard*>(message)->GetVoid1()));
			AdvancedWater::DoMessage(message);
			MixedOccupancy::DoMessage(message);
		}
		else if (type == kMsgPreCityShutdown || type == kMsgCheatIssued)
		{
			PowerPoleCustomization::DoMessage(message);
			AdvancedWater::DoMessage(message);
			MixedOccupancy::DoMessage(message);
		}
		else if (type == AdvancedWater::kMessageSimNewMonth)
		{
			AdvancedWater::DoMessage(message);
			MixedOccupancy::DoMessage(message);
		}
		return true;
	}

private:
	cRZAutoRefCount<cIGZCheatCodeManager> cheatManager;

	void PostCityInit(cISC4City* city)
	{
		cISC4AdvisorSystem* advisors = city ? city->GetAdvisorSystem() : nullptr;
		cISCLua* lua = advisors ? advisors->GetScriptingContext() : nullptr;
		if (!lua)
		{
			SimLog::Write(SimLog::Level::Error, "City opened without an advisor scripting context: nothing registered.");
			return;
		}

		lua->GetGlobal("sim");
		const bool exists = lua->IsTable(-1);
		lua->Pop(1);
		if (!exists)
		{
			lua->NewTable();
			lua->SetGlobal("sim");
		}
		const auto status = SCLuaUtil::RegisterLuaFunction(lua, "sim", "version", Version);
		if (status != SCLuaUtil::RegisterLuaFunctionStatus::Ok)
		{
			SimLog::Write(SimLog::Level::Error, "Could not register sim.version (status %d).", static_cast<int>(status));
		}

		lua->GetGlobal("sim_advisor_rewards");
		const bool simLua = lua->IsTable(-1);
		lua->Pop(1);
		if (simLua)
		{
			lua->GetGlobal("sim_advisor_rewards");
			lua->PushString("version");
			lua->GetTable(-2);
			const char* version = lua->IsString(-1) ? lua->ToString(-1) : "?";
			SimLog::Write(SimLog::Level::Info, "SIM Lua advisor rewards %s loaded.", version);
			lua->Pop(2);
		}
		else
		{
			SimLog::Write(SimLog::Level::Warning,
				"SIM Lua advisor rewards not loaded: CAM_LUA_Controller.dat without the package 0x0151A000?");
		}
		for (const char* name : kSimRewardCreators)
		{
			const bool ok = IsGlobalFunction(lua, name);
			SimLog::Write(ok ? SimLog::Level::Info : SimLog::Level::Warning, "%s: %s", name, ok ? "available" : "MISSING");
		}
		for (const char* name : kMaxisRewardCreators)
		{
			if (!IsGlobalFunction(lua, name)) SimLog::Write(SimLog::Level::Warning, "%s (Maxis): MISSING", name);
		}
	}
};

cRZCOMDllDirector* RZGetCOMDllDirector()
{
	static SimDllDirector sDirector;
	return &sDirector;
}
