#!/usr/bin/env python3
# ******************************************************************************
# Copyright (c) 2026 Masters of Albion Reforged developers
# ******************************************************************************
"""Put the mod loader (version.dll) and the mod DLLs in the Steam game, remove them, or show their state.

The loader is one file: version.dll next to moa.exe. The mod DLLs go into <game>\\mods\\, next to the mod zips. The
script changes no other file of the game. It refuses to replace or remove a version.dll that is not the mod loader
(also the research mod: remove that one with its own install.py first). It backs up a mod DLL that it did not put
there before it replaces or removes it. It never writes to the original game (the reference copy). It refuses to
change a file while the game runs, because Windows locks a loaded DLL.

    python tools/install.py status
    python tools/install.py install [--dll build/x64-release/version.dll]
    python tools/install.py uninstall
    python tools/install.py mods list
    python tools/install.py mods add <dll>        (copies the DLL to <game>\\mods\\<file name>)
    python tools/install.py mods remove <file name>
    python tools/install.py --self-test
"""

import argparse
import datetime
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_GAME = Path(r"F:\SteamLibrary\steamapps\common\Masters of Albion")
ORIGINAL_GAME = Path(r"D:\masters_of_Albion_original")
DEFAULT_DLL = Path(__file__).resolve().parents[1] / "build" / "x64-release" / "version.dll"
LOADER_MARKER = b"MoaModLoaderVersion"
RESEARCH_MARKER = b"MoaResearchModVersion"
# A mod DLL exports these functions, so their names are in the file.
MOD_MARKERS = (b"MoaModApiVersion", b"MoaModGetInfo", b"MoaModInit", b"MoaModShutdown")
NOTES = Path(r"D:\masters_of_albion")
# The mod DLLs that this script put into the folder of the mods, with their SHA-1. A DLL with another hash is not ours.
LEDGER = NOTES / "cache" / "mod-dlls-installed.json"
BACKUP = NOTES / "cache" / "mod-dlls-backup"


def game_is_running() -> bool:
    """Return True if a moa.exe process runs. Windows locks a loaded DLL, and the game must not be stopped."""
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq moa.exe", "/NH"], capture_output=True, text=True,
                             timeout=30)
    except (OSError, subprocess.SubprocessError):
        return False
    return "moa.exe" in out.stdout.lower()


def sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def is_loader(path: Path) -> bool:
    return LOADER_MARKER in path.read_bytes()


def is_mod_dll(path: Path) -> bool:
    data = path.read_bytes()
    return data[:2] == b"MZ" and all(marker in data for marker in MOD_MARKERS)


def refuse_original(game: Path) -> None:
    if game.resolve() == ORIGINAL_GAME.resolve():
        sys.exit(f"{game} is the original game; nothing writes to it. Use the Steam game.")


def game_folder(game: Path) -> Path:
    refuse_original(game)
    if not (game / "moa.exe").exists():
        sys.exit(f"{game} has no moa.exe")
    return game


def status(game: Path) -> int:
    target = game / "version.dll"
    if not target.exists():
        print(f"loader not installed: {target} does not exist")
    elif is_loader(target):
        print(f"loader installed: {target} sha1 {sha1(target)}")
    elif RESEARCH_MARKER in target.read_bytes():
        print(f"RESEARCH MOD: {target} is the research mod, not the loader; this script will not touch it")
    else:
        print(f"FOREIGN: {target} is not the mod loader; this script will not touch it")
        return 1
    return 0


def install(game: Path, dll: Path) -> int:
    game_folder(game)
    if not dll.exists() or not is_loader(dll):
        sys.exit(f"{dll} is not a build of the mod loader")
    target = game / "version.dll"
    if target.exists() and not is_loader(target):
        sys.exit(f"{target} exists and is not the mod loader; remove it with its own tool first")
    shutil.copy2(dll, target)
    print(f"installed {dll} -> {target} sha1 {sha1(target)}")
    return 0


def uninstall(game: Path) -> int:
    game_folder(game)
    target = game / "version.dll"
    if not target.exists():
        print("nothing to remove")
        return 0
    if not is_loader(target):
        sys.exit(f"{target} is not the mod loader; the script will not remove it")
    target.unlink()
    print(f"removed {target}")
    return 0


# --- Mod DLLs ---------------------------------------------------------------------------------------------------------


def mods_folder(game: Path) -> Path:
    """Return the folder of the mods. Refuse the original game, and a folder that is a link to another place."""
    game_folder(game)
    folder = game / "mods"
    if folder.is_symlink() or (folder.exists() and folder.resolve() != folder.absolute().resolve()):
        sys.exit(f"{folder} is a link; the script writes only into a plain folder")
    if folder.exists() and not folder.is_dir():
        sys.exit(f"{folder} is not a folder")
    return folder


def inside(folder: Path, path: Path) -> Path:
    """Return the path if it is directly in the folder. Stop the script if it is not."""
    resolved = path.resolve()
    if resolved.parent != folder.resolve():
        sys.exit(f"{path} is not in {folder}; the script writes nothing else")
    return resolved


def load_ledger(ledger: Path) -> dict:
    try:
        data = json.loads(ledger.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_ledger(ledger: Path, data: dict) -> None:
    ledger.parent.mkdir(parents=True, exist_ok=True)
    ledger.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def is_ours(data: dict, path: Path) -> bool:
    return path.exists() and data.get("dlls", {}).get(path.name.lower()) == sha1(path)


def back_up(path: Path, backup_root: Path) -> Path:
    """Copy a DLL that is not ours to the backup folder before the script replaces or removes it."""
    stamp = datetime.datetime.now().strftime("%Y-%m-%d-%H%M%S-%f")
    target = backup_root / f"{stamp}-{path.name}"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, target)
    print(f"backed up {path} (not ours) -> {target} sha1 {sha1(target)}")
    return target


def mods_list(game: Path, ledger: Path = LEDGER) -> int:
    folder = mods_folder(game)
    if not folder.exists():
        print(f"no folder {folder}: no mod DLLs")
        return 0
    data = load_ledger(ledger)
    dlls = sorted(path for path in folder.iterdir() if path.suffix.lower() == ".dll")
    if not dlls:
        print(f"{folder} has no mod DLLs")
    for path in dlls:
        kind = "mod DLL" if is_mod_dll(path) else "NOT A MOD DLL"
        owner = "ours" if is_ours(data, path) else "not ours"
        print(f"{path.name}: {kind}, {path.stat().st_size} bytes, sha1 {sha1(path)}, {owner}")
    return 0


def mods_add(game: Path, source: Path, ledger: Path = LEDGER, backup_root: Path = BACKUP) -> int:
    folder = mods_folder(game)
    if not source.is_file():
        sys.exit(f"{source} is not a file")
    if source.suffix.lower() != ".dll" or not is_mod_dll(source):
        sys.exit(f"{source} is not a mod DLL: it must export {', '.join(m.decode() for m in MOD_MARKERS)}")
    if folder.exists() and source.resolve().parent == folder.resolve():
        sys.exit(f"{source} is already in {folder}")
    data = load_ledger(ledger)
    if not folder.exists():
        folder.mkdir()
        data["createdFolder"] = True
        print(f"made {folder}")
    target = inside(folder, folder / source.name)
    if target.exists() and not is_ours(data, target):
        back_up(target, backup_root)
    shutil.copy2(source, target)
    data.setdefault("dlls", {})[target.name.lower()] = sha1(target)
    save_ledger(ledger, data)
    print(f"added {source} -> {target} sha1 {sha1(target)}")
    return 0


def mods_remove(game: Path, name: str, ledger: Path = LEDGER, backup_root: Path = BACKUP) -> int:
    folder = mods_folder(game)
    if not folder.exists():
        print("nothing to remove")
        return 0
    matches = [path for path in sorted(folder.iterdir())
               if path.suffix.lower() == ".dll" and path.name.lower() == name.lower()]
    if not matches:
        print(f"no mod DLL {name} in {folder}")
        return 0
    data = load_ledger(ledger)
    for path in matches:
        target = inside(folder, path)
        if not is_ours(data, target):
            back_up(target, backup_root)
        target.unlink()
        data.get("dlls", {}).pop(target.name.lower(), None)
        print(f"removed {target}")
    if data.get("createdFolder") and not any(folder.iterdir()):
        folder.rmdir()
        data.pop("createdFolder")
        print(f"removed the empty folder {folder}, which the script made")
    save_ledger(ledger, data)
    return 0


def self_test() -> int:
    """Check all commands on a fake game folder in a temporary folder. The real game folders stay as they are."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        game = root / "game"
        game.mkdir()
        (game / "moa.exe").write_bytes(b"exe")
        ledger = root / "ledger.json"
        backups = root / "backups"

        def refused(call) -> bool:
            try:
                call()
            except SystemExit:
                return True
            return False

        def fake_dll(path: Path, *markers: bytes) -> Path:
            path.write_bytes(b"MZ" + b"\0".join(markers) + b"\0" + path.name.encode())
            return path

        # The loader.
        loader = fake_dll(root / "version.dll", LOADER_MARKER)
        assert install(game, loader) == 0 and is_loader(game / "version.dll")
        assert uninstall(game) == 0 and not (game / "version.dll").exists()
        (game / "version.dll").write_bytes(b"MZ" + RESEARCH_MARKER)
        assert refused(lambda: install(game, loader)), "the research mod was replaced"
        assert refused(lambda: uninstall(game)), "the research mod was removed"
        (game / "version.dll").write_bytes(b"MZ other")
        assert refused(lambda: install(game, loader)), "a foreign version.dll was replaced"
        assert status(game) == 1
        (game / "version.dll").unlink()
        assert refused(lambda: install(game, fake_dll(root / "plain.dll"))), "a DLL that is not the loader was installed"

        # The mod DLLs.
        mod = fake_dll(root / "improved_camera.dll", *MOD_MARKERS)
        assert mods_add(game, mod, ledger, backups) == 0
        installed = game / "mods" / "improved_camera.dll"
        assert installed.exists() and is_ours(load_ledger(ledger), installed)
        assert load_ledger(ledger)["createdFolder"] is True
        # A DLL that changed after the add is not ours: it goes to the backup before the replacement.
        installed.write_bytes(installed.read_bytes() + b"changed")
        assert mods_add(game, mod, ledger, backups) == 0
        assert len(list(backups.iterdir())) == 1
        assert is_ours(load_ledger(ledger), installed)
        assert refused(lambda: mods_add(game, fake_dll(root / "not_mod.dll", b"MoaModInit"), ledger, backups)), \
            "a DLL with no mod exports was added"
        text = root / "mod.txt"
        text.write_bytes(b"MZ" + b"".join(MOD_MARKERS))
        assert refused(lambda: mods_add(game, text, ledger, backups)), "a file that is not a DLL was added"
        (game / "mods" / "other.dll").write_bytes(b"MZ user")
        assert mods_remove(game, "other.dll", ledger, backups) == 0
        assert len(list(backups.iterdir())) == 2, "a DLL that is not ours goes to the backup before the removal"
        (game / "mods" / "keep.zip").write_bytes(b"zip")
        assert mods_remove(game, "improved_camera.dll", ledger, backups) == 0
        assert (game / "mods").exists(), "the folder stays while it has other files"
        (game / "mods" / "keep.zip").unlink()
        assert mods_remove(game, "improved_camera.dll", ledger, backups) == 0
        assert refused(lambda: inside(game / "mods", game / "version.dll")), "a path outside the folder was accepted"
        assert refused(lambda: inside(game / "mods", game / "mods" / ".." / "moa.exe")), "'..' was accepted"
        assert refused(lambda: mods_folder(ORIGINAL_GAME)), "the original game was accepted"
        assert refused(lambda: install(ORIGINAL_GAME, loader)), "the original game was accepted"
        assert refused(lambda: mods_folder(root)), "a folder with no moa.exe was accepted"
    print("self-test: OK")
    return 0


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        return self_test()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["status", "install", "uninstall", "mods"])
    parser.add_argument("action", nargs="?", choices=["list", "add", "remove"], help="the action of the mods command")
    parser.add_argument("target", nargs="?", help="mods add: the DLL; mods remove: the file name")
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    args = parser.parse_args()
    if args.command == "mods":
        if args.action is None or (args.action != "list" and not args.target):
            sys.exit("use: mods list | mods add <dll> | mods remove <file name>")
        if args.action == "list":
            return mods_list(args.game)
        # The loader reads the DLLs when the game starts. Change them only when the game does not run.
        if game_is_running():
            sys.exit("the game runs: quit it at the main menu first, then run the command again")
        if args.action == "add":
            return mods_add(args.game, Path(args.target))
        return mods_remove(args.game, args.target)
    if args.command in ("install", "uninstall") and game_is_running():
        sys.exit("the game runs: quit it at the main menu first, then run the command again")
    if args.command == "status":
        return status(args.game)
    if args.command == "install":
        return install(args.game, args.dll)
    return uninstall(args.game)


if __name__ == "__main__":
    sys.exit(main())
