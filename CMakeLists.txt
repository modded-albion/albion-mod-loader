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
  src/core/HostTable.h
  src/core/LoaderSettings.h
  src/core/ModRegistry.h
  src/core/MapOverrideSet.h
  src/core/ModCatalogue.h
  src/core/CarriableMerge.h
  src/core/KnackMerge.h
  src/core/Pack.h
  src/core/PackTables.h
  src/core/PackWriter.h
  src/core/ResourceLoads.h
  src/core/TemplatePlan.h
  src/core/TypedMerge.h
  src/core/HookRules.h
  src/core/OverlayRules.h
  src/core/HostTable.cpp
  src/core/LoaderSettings.cpp
  src/core/ModRegistry.cpp
  src/core/MapOverrideSet.cpp
  src/core/ModCatalogue.cpp
  src/core/CarriableMerge.cpp
  src/core/KnackMerge.cpp
  src/core/Pack.cpp
  src/core/PackTables.cpp
  src/core/PackWriter.cpp
  src/core/ResourceLoads.cpp
  src/core/TemplatePlan.cpp
  src/core/TypedMerge.cpp
  src/unitymesh/RuntimeMesh.h
  src/unitymesh/RuntimeMesh.cpp
)
target_include_directories(moa_loader_core PUBLIC src)
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
add_library(moa_loader_dlls STATIC src/dlls/ModDllLoader.h src/dlls/ModDllLoader.cpp)
target_include_directories(moa_loader_dlls PUBLIC src)
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
  src/game/ResourceLoadSlot.cpp
  src/game/ResourceLoadSlot.h
  src/game/loader/Loader.cpp
  src/game/maps/MapsMod.cpp
  src/game/maps/MapsMod.h
  src/game/objects/ObjectsMod.cpp
  src/game/objects/ObjectsMod.h
  src/game/objects/TemplateBuilder.cpp
  src/game/objects/TemplateBuilder.h
  src/game/overlay/DxgiHooks.cpp
  src/game/overlay/DxgiHooks.h
  src/game/overlay/GameMenuProbe.cpp
  src/game/overlay/GameMenuProbe.h
  src/game/overlay/Overlay.cpp
  src/game/overlay/Overlay.h
  src/game/overlay/OverlayRenderer.cpp
  src/game/overlay/OverlayRenderer.h
  src/game/overlay/OverlayUi.cpp
  src/game/overlay/OverlayUi.h
)
set_target_properties(moa_mod_loader PROPERTIES OUTPUT_NAME version)
target_compile_definitions(moa_mod_loader PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(moa_mod_loader PRIVATE src)
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
