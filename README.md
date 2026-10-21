# Masters of Albion mod loader

A proxy `version.dll` for Masters of Albion (22cans, Unity 6000.5.7f1, IL2CPP). The game loads it from its own
folder. It forwards the 17 functions of the system `version.dll`, and in the game process (`moa.exe`) it loads the
mod DLLs from the folder `mods` of the game.

The code comes from the research mod of Masters of Albion Reforged (`tools/research_mod`). That repository stays as
it is. The research parts (the dump, the inspector, the tests with the virtual input, the research queries) are not
in this project. The inspector is the mod DLL of the project `inspector`; the loader gives it the queries of the
loader and of the mods (see below).

## What the loader owns

Only one module may own these, so the loader owns them and the mods use them through the loader:

- **The hooks.** A mod asks for a callback before or after a managed method. The loader installs one hook for each
  method (`GameManager.Update` always) and calls the callbacks of all mods in the order of the mods.
- **The menu bar.** The Dear ImGui menu bar on the D3D11 or D3D12 swap chain (the `Present` hook, MinHook). Each mod
  gets a menu with its fields, buttons and readouts. The menus Mod list, Mods and Help belong to the loader.
- **The settings file** `%LOCALAPPDATA%\MastersOfAlbionReforged\mods.json`. The loader reads the top-level keys
  `frameHook`, `liveReload`, `hotkeys`, `overlay`, `objects`, `maps`, `mods` and `modDlls`. It gives every other
  object to the mod that registered it (for example `camera` to improved_camera, `moon` and `sky` to better_skybox),
  watches the file, and writes the changes of the menu bar and of the hotkeys.
- **The hotkeys** (`hotkeys` in mods.json, by the name of each action) and the short messages of the game.
- **The frame callbacks and the main-thread tasks.**
- **The loading features:** the mod zips in `<game>\mods\*.zip`, the map overrides, and the custom content packs.
- **The start-up guard.** A marker file shows a start that did not complete. The next start then turns off the menu
  bar and each mod DLL that the failed start used (`overlay.enabled` false, `modDlls.disabled` in mods.json).

## The interface between the loader and a mod

The interface is the C header `external/common/src/api/moa_mod/api/ModApi.h` (version
`MOA_MOD_API_VERSION`). Only plain C types go across it, because each module has its own C runtime. In short:

- A mod DLL exports `MoaModApiVersion`, `MoaModGetInfo`, `MoaModInit(const MoaHostApi*, uint32_t)` and
  `MoaModShutdown`.
- The loader refuses a DLL with a missing export, another interface version, a bad id or the id of a loaded mod.
- `MoaModInit` registers settings sections, menu fields, actions (buttons and hotkeys), readouts, a status reader and
  method callbacks. The registrations are open only during `MoaModInit`.
- A mod that returns 0 from `MoaModInit` is unloaded and its registrations go. An access violation in a mod (in the
  init or later) stops all calls to that mod; the game continues. The log tells why.
- Two mods cannot own the same settings section or the same hotkey name, and all callbacks on one method agree on its
  parameters.
- `MoaModShutdown` runs when the game quits.

The C++ side of the interface for a mod is `moa_mod_sdk` in common (`ModHost`, `BindFields`, `HostLogSink`).

## Inspector queries

The loader keeps a list of named queries: each mod adds its own queries in `MoaModInit` (`addQuery`), and each query
name has one owner. The inspector mod lists them (`listQueries`, any thread) and calls one (`callQuery`). The loader
runs a query only on the main thread, with the same guard as the other calls into a mod: an access violation stops
the calls to that mod. A query of a mod that failed gives an error.

The queries of the loader itself:

| Query | What it gives |
|---|---|
| `mods.status` | The state of the loader, the status of each mod and of the objects and the maps, the mod DLLs, the menu bar |
| `mods.list` | The mod zips (as the Mod list shows them) and the mod DLLs (`dlls`) |
| `mods.settings` | mods.json: the path, the settings in use, the settings that the user asked for, the check of the file, the last reload, the messages |
| `mods.reload` | Reads mods.json again and gives each mod its objects |
| `maps.status` | The state of the map override |
| `objects.status` | The state of the custom objects (parameter `filter`) |
| `hooks.status` | The method hooks of the loader and the tasks that wait |
| `overlay.pin` | Pins the menu bar (parameter `pinned`) |
| `overlay.edit` | Changes a field as the menu bar does (`section`, `key`, `value`, `save`), or turns a mod zip (`mod`) or a mod DLL (`dll`) on or off (`enabled`) |
| `overlay.probe` | Makes a new swap chain, as the game does when it changes the display mode |

In `mods.status`, a status keeps its own name (for example `camera`) and gets the name of its mod as `mod`.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout of Masters of Albion Reforged.

```bat
git -c protocol.file.allow=always submodule update --init
tools\preset.bat x64-release
```

The result is `build\x64-release\version.dll`. The script prepares the MSVC x64 environment, then configures,
compiles and tests the preset `x64-release` (Ninja, MSVC, RelWithDebInfo).

Dependencies: vcpkg in manifest mode (`vcpkg.json`: nlohmann-json, spdlog, GoogleTest) with the triplet
`x64-windows-static`. The presets use `VCPKG_ROOT` = `D:/masters_of_albion/masters_of_albion_reforged/vcpkg` (the
vcpkg checkout of the reforged repository; this repository has no vcpkg submodule), the binary cache
`D:/masters_of_albion/cache/vcpkg-archives` and the downloads `D:/masters_of_albion/cache/vcpkg-downloads`. Dear
ImGui, MinHook and miniz compile from their source archives (`cmake/ModImGui.cmake`, `cmake/ModMinHook.cmake`,
common's `cmake/Miniz.cmake`) with fixed SHA-512 values, from the vcpkg download cache.

## The link to common

`external/common` is a git submodule with the URL `D:/masters_of_albion/projects/common` (an absolute local path).
Git 2.43 refuses local file submodules by default, so each submodule command needs `-c protocol.file.allow=always`.
`CMakeLists.txt` includes it with `add_subdirectory(external/common)`.

## Tests

`ctest --preset x64-release` runs the GoogleTest tests: the settings of the loader, the registry of the mods, the table
of a mod and its queries, the DLL loader (with six small test mods that the build makes: correct, failed init, old
interface, access violation, same id, bad id), the mod zips, the map overrides, the rules of the menu bar and the pack format.

## Install (not done by the build)

The install changes the Steam game, so only the user starts it. `tools/install.py` does it:

```bat
python tools\install.py status
python tools\install.py install                      (build\x64-release\version.dll -> <game>\version.dll)
python tools\install.py mods add <path>\improved_camera.dll
python tools\install.py mods list
python tools\install.py mods remove improved_camera.dll
python tools\install.py uninstall
python tools\install.py --self-test                  (a test on a temporary folder; no game folder changes)
```

The tool writes only `<game>\version.dll` and DLLs in `<game>\mods\`. It refuses a `version.dll` that is not the
loader (also the research mod: remove that one with its own `install.py` first), backs up a mod DLL that it did not
put there before it replaces or removes it (`D:\masters_of_albion\cache\mod-dlls-backup`), never writes to the
original game `D:\masters_of_Albion_original`, and refuses to change a file while `moa.exe` runs.

The loader and the research mod are both a `version.dll`, so only one of them can be in the game at a time.

## Logs and folders

- Log: `%LOCALAPPDATA%\MastersOfAlbionReforged\logs\mod-loader-<time>-<pid>.log` (`MOA_LOADER_LOG_FOLDER` changes it).
- Crash dumps: `%LOCALAPPDATA%\MastersOfAlbionReforged\crash-reports` (`MOA_CRASH_DUMPS=0` turns them off).
- Settings: `%LOCALAPPDATA%\MastersOfAlbionReforged\mods.json` (`MOA_MODS_SETTINGS` changes it).
- `MOA_MOD_LOADER=0` keeps only the proxy: the game runs as if the loader were not there.
