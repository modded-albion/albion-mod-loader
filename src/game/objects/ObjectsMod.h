/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <common/Result.h>
#include <core/CarriableMerge.h>
#include <core/LoaderSettings.h>
#include <core/Pack.h>
#include <core/PackTables.h>
#include <core/ResourceLoads.h>
#include <core/TemplatePlan.h>
#include <mod_core/SlotSwap.h>
#include <nlohmann/json.hpp>

#include "game/objects/TemplateBuilder.h"
#include "mod_game/Managed.h"
#include "mod_game/ModStatus.h"

namespace moa_mod::objects
{

/// The names of Unity that the objects mod uses.
struct ObjectsBindings
{
	Il2CppClass* resourcesApi = nullptr;
	const MethodInfo* load = nullptr;      ///< `ResourcesAPI.Load(string, Type)`, the virtual method.
	const MethodInfo* activeApi = nullptr; ///< `ResourcesAPI.ActiveAPI`, the object that `Resources.Load` calls.
	Il2CppClass* textAsset = nullptr;
	const MethodInfo* textAssetMake = nullptr; ///< The constructor of a text asset from a text.
	const MethodInfo* textAssetText = nullptr;
	Il2CppClass* gameObject = nullptr;
	Il2CppClass* unityObject = nullptr;
	const MethodInfo* gameObjectMake = nullptr; ///< The constructor of a game object with a name.
	const MethodInfo* setActive = nullptr;
	const MethodInfo* gameObjectTransform = nullptr;
	const MethodInfo* instantiate = nullptr; ///< `Object.Instantiate(Object, Transform)`.
	const MethodInfo* dontDestroyOnLoad = nullptr;
	const MethodInfo* setName = nullptr;
	const MethodInfo* getName = nullptr;
	const MethodInfo* destroyImmediate = nullptr;

	[[nodiscard]] static moa::Result<ObjectsBindings> Bind(const Managed& managed);
};

/// One pack for the objects mod: a folder in the development folder of the packs, or a pack folder in a mod zip.
struct PackOrigin
{
	/// The name of the pack in the logs and the status: `packs/<folder>` for a development folder, `mods/<mod id>/<folder>`
	/// for a mod zip. The packs load in the order of these names, so the packs of the mod zips come first.
	std::string key;
	std::string text;    ///< The text of `pack.json`.
	PackFileReader read; ///< Reads a file of the pack folder.
};

/// Read the `pack.json` of each folder in a development folder of the packs, in the order of the folder names.
[[nodiscard]] std::vector<PackOrigin> ReadPackFolder(const std::filesystem::path& folder);

/// Custom objects. The mod puts its function into the entry of `ResourcesAPI.Load` in the table of virtual methods of
/// the resource loader of Unity. Each `Resources.Load` of the game then calls the mod first:
///
/// - The mod writes each resource path and type that the game loads to a trace file, one time each.
/// - For the tables that the packs change (the carried resources, the decorations, the blocks, the hand powers and the
///   research), the mod gives a text asset with the rows of the packs added.
/// - For the path of the prefab of a custom resource, throwable or weapon part, the mod gives a copy of the prefab of
///   its template with a new name and the model of the pack.
///
/// All other loads go to Unity with no change. The mod is off by default (`objects.enabled`).
class ObjectsMod
{
public:
	~ObjectsMod();
	ObjectsMod(const ObjectsMod&) = delete;
	ObjectsMod& operator=(const ObjectsMod&) = delete;

	/// Find the entry of the loader and put the function of the mod into it. Call it on a thread that IL2CPP
	/// attached, before the game loads its tables. Return an error, and change nothing, if a name is missing or the
	/// entry is not found exactly one time.
	[[nodiscard]] static moa::Result<std::unique_ptr<ObjectsMod>> Install(const Managed& managed,
	                                                                      loader::LoaderSettings::Objects settings,
	                                                                      const std::filesystem::path& traceFile,
	                                                                      std::vector<PackOrigin> packs, ModStatus& status);

	/// Put the original entry back. Return false if another program changed the entry after the install. With `log`
	/// false, the function only does atomic writes, for `DllMain`.
	bool Remove(bool log = true);

	/// The state of the mod and the loads, for the log. `filter` keeps the paths that contain it
	/// (in lower case).
	[[nodiscard]] nlohmann::json State(const std::string& filter) const;

	/// Return the text of a table with the rows of the packs added, or nothing if the packs do not change the table.
	/// The map override calls it for a table that it gives in place of the game text, so that the rows of the packs
	/// are also in the text of the map. Call it on the main thread.
	[[nodiscard]] std::optional<std::string> FilterTable(const std::string& path, const std::string& text);

	/// The packs that load (`packs`: their keys) and why packs or resources do not load (`packErrors`: messages by key),
	/// for the Mod list.
	[[nodiscard]] nlohmann::json PackStatus() const;

private:
	ObjectsMod(const Managed& managed, ObjectsBindings bindings, loader::LoaderSettings::Objects settings, ModStatus& status);

	using LoadFunction = Il2CppObject* (*)(Il2CppObject * self, Il2CppObject* path, Il2CppObject* type,
	                                       const MethodInfo* method);

	static Il2CppObject* HookLoad(Il2CppObject* self, Il2CppObject* path, Il2CppObject* type, const MethodInfo* method);

	/// Return the object that the mod gives for this load, or null to let Unity load it.
	[[nodiscard]] Il2CppObject* Serve(Il2CppObject* self, const std::string& path, Il2CppObject* type,
	                                  const MethodInfo* method);
	void Trace(const std::string& path, Il2CppObject* type, bool found);
	[[nodiscard]] std::string TypeName(Il2CppObject* type);
	[[nodiscard]] moa::Result<Il2CppObject*> MergedTable(PackTable table, Il2CppObject* original);
	/// Merge the text of a table and keep the items and the errors. `loadOriginal` reads the text of another table.
	[[nodiscard]] std::optional<std::string> MergeText(PackTable table, const std::string& text,
	                                                   const PackTableMerger::TextLoader& loadOriginal);
	/// Read the text of a table of the game through the original loader.
	[[nodiscard]] std::optional<std::string> OriginalText(PackTable table, Il2CppObject* self, Il2CppObject* type,
	                                                      const MethodInfo* method);
	[[nodiscard]] moa::Result<Il2CppObject*> Prefab(const MergedResource& resource, Il2CppObject* self, Il2CppObject* type,
	                                                const MethodInfo* method);
	[[nodiscard]] moa::Result<Il2CppObject*> Holder();
	void Keep(Il2CppObject* object);
	/// Read the mesh and the textures of each pack resource that has a mesh. Remove a resource whose files are bad.
	void LoadModels();
	/// Record why a pack, or a resource of a pack, is not used, for the status of the mods.
	void PackError(const std::string& key, std::string message);
	/// Return the key of a resource in `_models`: the pack id and the resource name.
	[[nodiscard]] static std::string ModelKey(const Pack& pack, const PackResource& resource);
	const Managed& _managed;
	ObjectsBindings _bindings;
	loader::LoaderSettings::Objects _settings;
	ModStatus& _status;
	SlotSwap _swap;
	LoadFunction _original = nullptr;
	std::chrono::steady_clock::time_point _start = std::chrono::steady_clock::now();

	mutable std::mutex _mutex;
	LoadTrace _trace;
	std::ofstream _traceFile;
	std::unordered_map<Il2CppObject*, std::string> _typeNames;
	std::thread::id _mainThread;
	uint64_t _otherThreadLoads = 0;
	uint64_t _tableServed = 0;
	uint64_t _prefabServed = 0;
	/// The packs, and the resources that the last table merge added. Only the main thread changes them.
	std::vector<Pack> _packs;
	/// The reader of the files of each pack, by `PackOrigin::key` (the folder of the pack).
	std::map<std::string, PackFileReader> _readers;
	/// Why a pack or a resource of it is not used, by `PackOrigin::key`.
	std::map<std::string, std::vector<std::string>> _packErrors;
	std::vector<MergedResource> _merged;
	/// The merges of the tables. Made after the models load, with the packs that remain.
	std::optional<PackTableMerger> _tables;
	/// The resource loader of Unity (`ResourcesAPI.ActiveAPI`), for the loads of the mod outside the hook.
	Il2CppObject* _api = nullptr;
	/// The meshes and textures of the pack resources, by `ModelKey`. They are read at the install.
	std::map<std::string, ResourceModel> _models;
	/// The builder of the custom visuals. Empty if Unity does not have a name that it needs.
	std::optional<TemplateBuilder> _builder;
	/// What the builder made for each prefab, or the error, by prefab path, for the status.
	std::map<std::string, nlohmann::json> _built;

	/// The objects of the mod: the hidden holder and the custom prefabs. The garbage collector keeps them.
	Il2CppObject* _holder = nullptr;
	std::map<std::string, Il2CppObject*> _prefabs;
	std::vector<uintptr_t> _handles;
};

} // namespace moa_mod::objects
