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

add_library(SC4SIM SHARED
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
  src/afterlife/Afterlife.cpp
  src/afterlife/AfterlifeModel.cpp
  ${GZCOM}/src/cRZBaseString.cpp
  ${GZCOM}/src/cRZBaseVariant.cpp
  ${GZCOM}/src/cRZCOMDllDirector.cpp
  ${GZCOM}/src/SCLuaUtil.cpp
  ${GZCOM}/src/SCPropertyUtil.cpp
  ${GZCOM}/src/StringResourceManager.cpp
  ${GZCOM}/src/cRZCellMap.cpp
)
target_include_directories(SC4SIM PRIVATE src src/powerpoles src/water src/mixed src/zones src/afterlife vendor/mINI/src ${GZCOM}/include)
target_link_libraries(SC4SIM PRIVATE version)
target_compile_definitions(SC4SIM PRIVATE WIN32 _WINDOWS _USRDLL NOMINMAX)
target_compile_options(SC4SIM PRIVATE /W4 /permissive- /utf-8)
# Static runtime: players do not need to install the Visual C++ redistributable.
set_property(TARGET SC4SIM PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
