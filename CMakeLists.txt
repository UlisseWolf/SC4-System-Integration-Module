# SC4SIM.dll - alternative to SC4SIM.sln. Build with Visual Studio 2022, 32-bit (SimCity 4 is a 32-bit game):
#   cmake -S . -B build -A Win32
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.20)
project(SC4SIM LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "SimCity 4 is 32-bit: configure with -A Win32")
endif()

set(CMAKE_CONFIGURATION_TYPES Release CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(GZCOM ${CMAKE_SOURCE_DIR}/vendor/gzcom-dll/gzcom-dll)

set(SC4SIM_SOURCES
  src/SimDllDirector.cpp
  src/SimLog.cpp
  src/powerpoles/PowerPoleCustomization.cpp
  src/powerpoles/RelativeCallPatch.cpp
  src/powerpoles/VersionDetection.cpp
  src/water/AdvancedWater.cpp
  src/water/SC4NotificationDialog.cpp
  src/water/WaterBuildingOccupantFilter.cpp
  src/water/WaterBuildingPropertyUtil.cpp
  src/mixed/MixedOccupancy.cpp
  src/zones/ManufacturingZone.cpp
  src/fixes/TripCapacityFix.cpp
  src/fixes/BudgetFixes.cpp
  src/fixes/PortUpgradeFix.cpp
  src/fixes/SeaportDemandCapFix.cpp
  src/advisors/UnderfundedNotices.cpp
  src/afterlife/Afterlife.cpp
  src/afterlife/AfterlifeModel.cpp
  src/afterlife/Deathcare.cpp
  src/power/PowerGraphHistory.cpp
  src/water/WaterGraphHistory.cpp
  src/safety/CrimeGraphHistory.cpp
  src/safety/FireGraphHistory.cpp
  src/safety/HealthGraphHistory.cpp
  src/population/PopulationGraphHistory.cpp
  src/population/EducationGraphHistory.cpp
  src/economy/AdvisorGraphHistory.cpp
  src/ui/GraphButtons.cpp
  src/ui/GraphLegendPatch.cpp
)
set(GZCOM_SOURCES
  ${GZCOM}/src/cRZBaseString.cpp
  ${GZCOM}/src/cRZBaseVariant.cpp
  ${GZCOM}/src/cRZCOMDllDirector.cpp
  ${GZCOM}/src/SCLuaUtil.cpp
  ${GZCOM}/src/SCPropertyUtil.cpp
  ${GZCOM}/src/StringResourceManager.cpp
  ${GZCOM}/src/cRZCellMap.cpp
)
add_library(SC4SIM SHARED ${SC4SIM_SOURCES} ${GZCOM_SOURCES})
target_include_directories(SC4SIM PRIVATE src src/powerpoles src/water src/mixed src/zones src/afterlife vendor/mINI/src ${GZCOM}/include)
target_link_libraries(SC4SIM PRIVATE version)
target_compile_definitions(SC4SIM PRIVATE WIN32 _WINDOWS _USRDLL NOMINMAX)
target_compile_options(SC4SIM PRIVATE /permissive- /utf-8)
# All warnings for SC4SIM's own code; none for the gzcom-dll submodule, which is not changed here.
set_source_files_properties(${SC4SIM_SOURCES} PROPERTIES COMPILE_OPTIONS $<$<CXX_COMPILER_ID:MSVC>:/W4>)
set_source_files_properties(${GZCOM_SOURCES} PROPERTIES COMPILE_OPTIONS $<$<CXX_COMPILER_ID:MSVC>:/W0>)
# Static runtime: players do not need to install the Visual C++ redistributable.
set_property(TARGET SC4SIM PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
