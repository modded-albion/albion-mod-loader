# ******************************************************************************
# Copyright (c) 2026 Masters of Albion Reforged developers
# ******************************************************************************

# The mod loader of Masters of Albion: a proxy version.dll that loads the mod DLLs from the folder `mods` of the game.
# The game folder must get no other DLL, so the loader uses only the static C runtime, static libraries, and libraries
# that it compiles itself (Dear ImGui, MinHook, miniz).

cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(
  moa_mod_loader
  VERSION 0.1.0
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MOA_LOADER_BUILD_TESTS "Build the tests of the mod loader" ON)

# The shared code (git submodule).
add_subdirectory(external/common)

find_package(nlohmann_json CONFIG REQUIRED)

# The parts of the loader that do not need the game: the settings, the registry of the mods, the table of a mod, the
# mod zips, the map overrides, the rules of the menu bar and the pack format of the custom objects.
add_library(
  moa_loader_core STATIC
  src/core/include/moa_mod/loader/HostTable.h
  src/core/include/moa_mod/loader/LoaderSettings.h
  src/core/include/moa_mod/loader/ModRegistry.h
  src/core/include/moa_mod/maps/MapOverrideSet.h
  src/core/include/moa_mod/mods/ModCatalogue.h
  src/core/include/moa_mod/objects/CarriableMerge.h
  src/core/include/moa_mod/objects/KnackMerge.h
  src/core/include/moa_mod/objects/Pack.h
  src/core/include/moa_mod/objects/PackTables.h
  src/core/include/moa_mod/objects/PackWriter.h
  src/core/include/moa_mod/objects/ResourceLoads.h
  src/core/include/moa_mod/objects/TemplatePlan.h
  src/core/include/moa_mod/objects/TypedMerge.h
  src/core/include/moa_mod/overlay/HookRules.h
  src/core/include/moa_mod/overlay/OverlayRules.h
  src/core/src/loader/HostTable.cpp
  src/core/src/loader/LoaderSettings.cpp
  src/core/src/loader/ModRegistry.cpp
  src/core/src/maps/MapOverrideSet.cpp
  src/core/src/mods/ModCatalogue.cpp
  src/core/src/objects/CarriableMerge.cpp
  src/core/src/objects/KnackMerge.cpp
  src/core/src/objects/Pack.cpp
  src/core/src/objects/PackTables.cpp
  src/core/src/objects/PackWriter.cpp
  src/core/src/objects/ResourceLoads.cpp
  src/core/src/objects/TemplatePlan.cpp
  src/core/src/objects/TypedMerge.cpp
  src/unitymesh/include/moa/unitymesh/RuntimeMesh.h
  src/unitymesh/src/RuntimeMesh.cpp
)
target_include_directories(moa_loader_core PUBLIC src/core/include src/unitymesh/include)
target_link_libraries(
  moa_loader_core
  PUBLIC moa_common
         moa_mod_core
         moa_mod_api
         moa_modzip
         moa_mapoverride
         nlohmann_json::nlohmann_json
)
moa_set_warnings(moa_loader_core)

# The load of the mod DLLs. Windows only, but it needs no game: the tests load small test mods.
add_library(moa_loader_dlls STATIC src/dlls/include/moa_mod/loader/ModDllLoader.h src/dlls/src/ModDllLoader.cpp)
target_include_directories(moa_loader_dlls PUBLIC src/dlls/include)
target_compile_definitions(moa_loader_dlls PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(moa_loader_dlls PUBLIC moa_loader_core)
moa_set_warnings(moa_loader_dlls)

enable_language(ASM_MASM)
include(cmake/ModImGui.cmake)
include(cmake/ModMinHook.cmake)

add_library(
  moa_mod_loader SHARED
  src/game/proxy/ProxyStubs.asm
  src/game/proxy/VersionExports.h
  src/game/proxy/version.def
  src/game/moa_mod/ResourceLoadSlot.cpp
  src/game/moa_mod/ResourceLoadSlot.h
  src/game/moa_mod/loader/Loader.cpp
  src/game/moa_mod/maps/MapsMod.cpp
  src/game/moa_mod/maps/MapsMod.h
  src/game/moa_mod/objects/ObjectsMod.cpp
  src/game/moa_mod/objects/ObjectsMod.h
  src/game/moa_mod/objects/TemplateBuilder.cpp
  src/game/moa_mod/objects/TemplateBuilder.h
  src/game/moa_mod/overlay/DxgiHooks.cpp
  src/game/moa_mod/overlay/DxgiHooks.h
  src/game/moa_mod/overlay/GameMenuProbe.cpp
  src/game/moa_mod/overlay/GameMenuProbe.h
  src/game/moa_mod/overlay/Overlay.cpp
  src/game/moa_mod/overlay/Overlay.h
  src/game/moa_mod/overlay/OverlayRenderer.cpp
  src/game/moa_mod/overlay/OverlayRenderer.h
  src/game/moa_mod/overlay/OverlayUi.cpp
  src/game/moa_mod/overlay/OverlayUi.h
)
set_target_properties(moa_mod_loader PROPERTIES OUTPUT_NAME version)
target_compile_definitions(moa_mod_loader PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(moa_mod_loader PRIVATE src/game)
target_link_libraries(
  moa_mod_loader
  PRIVATE moa_loader_core
          moa_loader_dlls
          moa_mod_game
          moa_mod_imgui
          moa_mod_minhook
          delayimp
)
# The game loads version.dll before it loads its graphics. The DLLs of the menu bar load only at the first call to
# them, when the game draws. A process that is not the game never loads them.
target_link_options(
  moa_mod_loader
  PRIVATE
  /DELAYLOAD:d3dcompiler_47.dll
  /DELAYLOAD:dxgi.dll
  /DELAYLOAD:dwmapi.dll
)
moa_set_warnings(moa_mod_loader)

if (MOA_LOADER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()
