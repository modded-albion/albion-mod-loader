/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/mods/ModCatalogue.h"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <utility>

#include <moa/mapoverride/MapOverride.h>

namespace moa_mod::mods
{

namespace mz = moa::modzip;

namespace
{
[[nodiscard]] std::string_view AsText(std::span<const std::byte> bytes)
{
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] bool Loads(const CatalogueMod& mod)
{
	return mod.state == ModState::On && mod.info.has_value();
}

/// Check the settings that an edit makes, as the menu bar does for a field.
[[nodiscard]] moa::Result<void> CheckSettings(const loader::LoaderSettings& settings)
{
	if (auto checked = loader::ParseLoaderSettings(loader::LoaderSettingsJson(settings).dump()); !checked)
	{
		return checked.GetError();
	}
	return {};
}
} // namespace

std::string_view ModStateName(ModState state)
{
	switch (state)
	{
	case ModState::On:
		return "on";
	case ModState::Off:
		return "off";
	case ModState::Refused:
		return "refused";
	}
	return "unknown";
}

std::vector<const CatalogueMod*> ModCatalogue::Loaded() const
{
	std::vector<const CatalogueMod*> loaded;
	for (const auto& mod : mods)
	{
		if (Loads(mod))
		{
			loaded.push_back(&mod);
		}
	}
	return loaded;
}

const CatalogueMod* ModCatalogue::FindLoaded(std::string_view id) const
{
	const auto found = std::ranges::find_if(mods, [id](const CatalogueMod& mod) { return Loads(mod) && mod.info->id == id; });
	return found == mods.end() ? nullptr : &*found;
}

CatalogueMod OpenModZip(std::string file, std::vector<std::byte> bytes, std::string_view buildGuid, const mz::ZipLimits& limits)
{
	CatalogueMod mod {.file = std::move(file)};
	auto archive = mz::ZipArchive::Open(std::move(bytes), limits);
	if (!archive)
	{
		mod.error = archive.GetError().message;
		return mod;
	}
	auto text = archive->Read(mz::k_ModFile);
	if (!text)
	{
		mod.error = std::format("the zip has no {} at its root", mz::k_ModFile);
		return mod;
	}
	auto info = mz::ParseModInfo(AsText(*text));
	if (!info)
	{
		mod.error = std::format("{}: {}", mz::k_ModFile, info.GetError().message);
		return mod;
	}
	mod.info = std::move(*info);
	if (auto contents = mz::CheckModContents(*mod.info, archive->Entries()); !contents)
	{
		mod.error = contents.GetError().message;
		return mod;
	}
	if (auto build = mz::CheckModBuild(*mod.info, buildGuid); !build)
	{
		mod.error = build.GetError().message;
		return mod;
	}
	mod.archive = std::make_shared<const mz::ZipArchive>(std::move(*archive));
	mod.state = ModState::On;
	return mod;
}

ModCatalogue BuildCatalogue(std::vector<CatalogueMod> mods, const loader::LoaderSettings& settings)
{
	// The load order: the ids, then the file names. Zips with no id go last.
	std::ranges::sort(mods, [](const CatalogueMod& a, const CatalogueMod& b) {
		const auto keyA = a.info ? a.info->id : std::string();
		const auto keyB = b.info ? b.info->id : std::string();
		if (a.info.has_value() != b.info.has_value())
		{
			return a.info.has_value();
		}
		return keyA != keyB ? keyA < keyB : a.file < b.file;
	});
	// Of the zips with the same id that pass their checks, the newest version loads. A zip that failed a check (for
	// example, a zip for another game build) does not hide an older zip that works.
	std::map<std::string, std::vector<size_t>, std::less<>> filesOfId;
	for (size_t index = 0; index < mods.size(); ++index)
	{
		if (mods[index].state == ModState::On && mods[index].info)
		{
			filesOfId[mods[index].info->id].push_back(index);
		}
	}
	const auto refuse = [&mods](size_t index, std::string reason) {
		auto& mod = mods[index];
		mod.state = ModState::Refused;
		mod.archive.reset();
		mod.error = std::move(reason);
	};
	for (const auto& [id, indices] : filesOfId)
	{
		if (indices.size() < 2)
		{
			continue;
		}
		const auto newest = *std::ranges::max_element(indices, [&mods](size_t a, size_t b) {
			return mz::CompareModVersions(mods[a].info->version, mods[b].info->version) < 0;
		});
		const auto& newestVersion = mods[newest].info->version;
		std::vector<size_t> tied;
		for (const auto index : indices)
		{
			if (mz::CompareModVersions(mods[index].info->version, newestVersion) == 0)
			{
				tied.push_back(index);
			}
		}
		std::string tiedFiles;
		for (const auto index : tied)
		{
			tiedFiles += (tiedFiles.empty() ? "" : ", ") + mods[index].file;
		}
		for (const auto index : indices)
		{
			if (tied.size() > 1 && std::ranges::find(tied, index) != tied.end())
			{
				refuse(index,
				       std::format("the zips {} have the same id '{}' and the same version {}; remove all of them but one",
				                   tiedFiles, id, newestVersion));
			}
			else if (std::ranges::find(tied, index) == tied.end())
			{
				refuse(index, std::format("the zip {} has a newer version {} of '{}'; this version {} does not load",
				                          tied.size() == 1 ? mods[newest].file : tiedFiles, newestVersion, id,
				                          mods[index].info->version));
			}
		}
	}
	for (auto& mod : mods)
	{
		if (mod.state == ModState::On &&
		    std::ranges::find(settings.mods.disabled, mod.info->id) != settings.mods.disabled.end())
		{
			mod.state = ModState::Off;
		}
	}

	ModCatalogue catalogue {.mods = std::move(mods)};
	const auto firstMap = [&catalogue]() -> const CatalogueMod* {
		const auto loaded = catalogue.Loaded();
		const auto found = std::ranges::find_if(loaded, [](const CatalogueMod* mod) { return mod->info->map; });
		return found == loaded.end() ? nullptr : *found;
	};
	const CatalogueMod* mapMod = nullptr;
	if (!settings.maps.active.empty())
	{
		catalogue.map = MapChoice {.kind = MapChoice::Kind::Folder, .name = settings.maps.active};
	}
	else if (settings.mods.map && settings.mods.map->empty())
	{
		catalogue.map = MapChoice {};
	}
	else if (settings.mods.map)
	{
		mapMod = catalogue.FindLoaded(*settings.mods.map);
		if (mapMod == nullptr || !mapMod->info->map)
		{
			catalogue.mapNote = std::format("the mod '{}' of the map setting does not load or has no map", *settings.mods.map);
			mapMod = firstMap();
		}
	}
	else
	{
		mapMod = firstMap();
	}
	if (mapMod != nullptr)
	{
		catalogue.map = MapChoice {.kind = MapChoice::Kind::Mod, .name = mapMod->info->id};
		for (auto& mod : catalogue.mods)
		{
			mod.mapActive = &mod == mapMod;
		}
	}
	return catalogue;
}

FileReader ZipFolderReader(std::shared_ptr<const mz::ZipArchive> archive, std::string prefix)
{
	return [archive = std::move(archive),
	        prefix = std::move(prefix)](const std::string& relative) -> moa::Result<std::vector<std::byte>> {
		auto name = mz::CheckEntryName(relative);
		if (!name)
		{
			return name.GetError();
		}
		if (*name != relative)
		{
			return moa::MakeError("'{}' is a folder, not a file", relative);
		}
		return archive->Read(prefix + relative);
	};
}

moa::Result<moa_mod::settings::SettingsEdit> EditModEnabled(loader::LoaderSettings& settings, std::string_view id, bool enabled)
{
	if (!mz::IsValidModId(id))
	{
		return moa::MakeError("'{}' is not a mod id", id);
	}
	auto next = settings;
	auto& disabled = next.mods.disabled;
	std::erase(disabled, id);
	if (!enabled)
	{
		disabled.emplace_back(id);
	}
	std::ranges::sort(disabled);
	if (auto checked = CheckSettings(next); !checked)
	{
		return checked.GetError();
	}
	nlohmann::json patch;
	patch["mods"]["disabled"] = disabled;
	settings = std::move(next);
	return moa_mod::settings::SettingsEdit {
	    .message = std::format("Mod {}: {} after a restart of the game", id, enabled ? "on" : "off"),
	    .patch = std::move(patch)};
}

moa::Result<moa_mod::settings::SettingsEdit> EditMapChoice(loader::LoaderSettings& settings, const MapChoice& choice)
{
	auto next = settings;
	nlohmann::json patch;
	std::string message;
	switch (choice.kind)
	{
	case MapChoice::Kind::Folder:
		if (!moa::mapoverride::IsValidMapName(choice.name))
		{
			return moa::MakeError("'{}' is not a name of a map override", choice.name);
		}
		next.maps.active = choice.name;
		next.mods.map.reset();
		patch["maps"]["active"] = choice.name;
		patch["mods"]["map"] = nullptr;
		message = std::format("Map: the folder {}", choice.name);
		break;
	case MapChoice::Kind::Mod:
		if (!mz::IsValidModId(choice.name))
		{
			return moa::MakeError("'{}' is not a mod id", choice.name);
		}
		next.maps.active.clear();
		next.mods.map = choice.name;
		patch["maps"]["active"] = "";
		patch["mods"]["map"] = choice.name;
		message = std::format("Map: the mod {}", choice.name);
		break;
	case MapChoice::Kind::Automatic:
		next.maps.active.clear();
		next.mods.map.reset();
		patch["maps"]["active"] = "";
		patch["mods"]["map"] = nullptr;
		message = "Map: the first mod with a map";
		break;
	case MapChoice::Kind::None:
		next.maps.active.clear();
		next.mods.map = std::string();
		patch["maps"]["active"] = "";
		patch["mods"]["map"] = "";
		message = "Map: none";
		break;
	}
	if (auto checked = CheckSettings(next); !checked)
	{
		return checked.GetError();
	}
	settings = std::move(next);
	return moa_mod::settings::SettingsEdit {.message = message + " after a restart of the game", .patch = std::move(patch)};
}

nlohmann::json DescribeCatalogue(const ModCatalogue& catalogue)
{
	nlohmann::json mods = nlohmann::json::array();
	for (const auto& mod : catalogue.mods)
	{
		nlohmann::json item {
		    {"file", mod.file},
		    {"state", ModStateName(mod.state)},
		    {"error", mod.error},
		    {"mapActive", mod.mapActive},
		};
		if (mod.info)
		{
			item["id"] = mod.info->id;
			item["name"] = mod.info->name;
			item["version"] = mod.info->version;
			item["author"] = mod.info->author;
			item["description"] = mod.info->description;
			item["buildGuids"] = mod.info->buildGuids;
			item["map"] = mod.info->map;
			item["packs"] = mod.info->packs;
		}
		mods.push_back(std::move(item));
	}
	constexpr std::array k_KindNames {"none", "folder", "mod", "automatic"};
	return {
	    {"mods", std::move(mods)},
	    {"map", {{"kind", k_KindNames.at(static_cast<size_t>(catalogue.map.kind))}, {"name", catalogue.map.name}}},
	    {"mapNote", catalogue.mapNote},
	};
}

} // namespace moa_mod::mods
