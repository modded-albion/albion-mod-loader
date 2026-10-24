# Masters of Albion mod loader

A proxy `version.dll` for Masters of Albion that allows for mod loading.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout of Masters of Albion Reforged.

```bat
git -c protocol.file.allow=always submodule update --init
tools\preset.bat x64-release
```

## Tests

`ctest --preset x64-release` runs the GoogleTest tests

## How to install

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
