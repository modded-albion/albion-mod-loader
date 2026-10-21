/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <common/Result.h>
#include <core/MapOverrideSet.h>
#include <mod_core/SlotSwap.h>
#include <nlohmann/json.hpp>

#include "mod_game/Managed.h"
#include "mod_game/ModStatus.h"

namespace moa_mod::maps
{

/// The names of Unity that the map override uses for the loads of the game.
struct MapsBindings
{
	Il2CppClass* resourcesApi = nullptr;
	const MethodInfo* load = nullptr;      ///< `ResourcesAPI.Load(string, Type)`, the virtual method.
	const MethodInfo* activeApi = nullptr; ///< `ResourcesAPI.ActiveAPI`.
	Il2CppClass* textAsset = nullptr;
	const MethodInfo* textAssetMake = nullptr; ///< The constructor of a text asset from a text.
	const MethodInfo* textDataPointer = nullptr;
	const MethodInfo* textDataSize = nullptr;
	Il2CppClass* texture = nullptr;
	const MethodInfo* textureData = nullptr;     ///< The address of the image data of a readable texture.
	const MethodInfo* textureDataSize = nullptr; ///< The size of the image data of all mip levels.
	const MethodInfo* textureLoadRaw = nullptr;  ///< Copy new image data into the texture.
	const MethodInfo* getName = nullptr;
	const MethodInfo* setName = nullptr;

	[[nodiscard]] static moa::Result<MapsBindings> Bind(const Managed& managed);
};

/// The names of Unity and of the game that the map override uses for the land. A missing name stops only the land
/// items.
struct LandBindings
{
	size_t globalTerrainData = 0; ///< The land data that the game made at run time, in `GlobalData`.
	/// The copy of the first land data in `GlobalData`. The game copies heights from it back into the land under the
	/// roads when it makes the roads.
	size_t originalTerrainData = 0;
	const MethodInfo* resolution = nullptr;
	const MethodInfo* getHeights = nullptr;
	const MethodInfo* setHeights = nullptr;
	const MethodInfo* getTrees = nullptr;
	const MethodInfo* setTrees = nullptr;
	size_t treeSize = 0;   ///< The size of one tree in an array.
	size_t treeHeight = 0; ///< The position of the height of the tree in an item of the array.
	Il2CppClass* treeHolder = nullptr;
	const MethodInfo* findObjects = nullptr; ///< `Object.FindObjectsOfType(Type, bool)`.
	const MethodInfo* componentGameObject = nullptr;
	const MethodInfo* componentTransform = nullptr;
	const MethodInfo* getLocalPosition = nullptr;
	const MethodInfo* setLocalPosition = nullptr;
	const MethodInfo* getName = nullptr;
	const MethodInfo* terrainSize = nullptr; ///< `TerrainData.size`: the height scale is its y.
	/// The run-time heights of `GlobalData` in metres, and the bits of the samples under the sea. Each is a
	/// `NativeArray`: the address of the data, then the count of items.
	size_t heights = 0;
	size_t heightsOriginal = 0;
	size_t waterPresence = 0;
	size_t initialWaterPresence = 0;

	[[nodiscard]] static moa::Result<LandBindings> Bind(const Managed& managed);
};

/// The map override. The mod puts its function into the entry of `ResourcesAPI.Load` in the table of virtual methods
/// of the resource loader of Unity, after the entry of the objects mod if that mod is on. Then:
///
/// - For a TextAsset of the override (the start map, the districts, a flow script, a table), the game gets a new text
///   asset with the text of the override.
/// - For the coast map and the camera floor, the mod changes the data of the asset in memory.
/// - When the game loads the camera floor, the land data is ready: the mod changes the heights and the trees of the
///   land data, and the heights of the TreeHolder objects.
///
/// The mod compares the original data of each item with the manifest first. All other loads go to Unity with no change.
class MapsMod
{
public:
	~MapsMod();
	MapsMod(const MapsMod&) = delete;
	MapsMod& operator=(const MapsMod&) = delete;

	/// Find the entry of the loader and put the function of the mod into it. Call it on a thread that IL2CPP
	/// attached, before the game loads its data. Return an error, and change nothing, if a name is missing or the
	/// entry is not found exactly one time.
	[[nodiscard]] static moa::Result<std::unique_ptr<MapsMod>> Install(const Managed& managed, MapOverrideSet set,
	                                                                   ModStatus& status);

	/// A function that changes the text of a table before the game gets it, or gives nothing to keep the text. The
	/// objects mod adds the rows of its packs with it.
	using TextFilter = std::function<std::optional<std::string>(const std::string& path, const std::string& text)>;

	/// Set the filter of the texts of the override. Call it on the thread that installed the mod, before the game
	/// loads the tables.
	void SetTextFilter(TextFilter filter) { _filter = std::move(filter); }

	/// Put the original entry back. Return false if another program changed the entry after the install. With `log`
	/// false, the function only does atomic writes, for `DllMain`.
	bool Remove(bool log = true);

	/// The state of the override and the loads, for the menu bar and the log.
	[[nodiscard]] nlohmann::json State() const;

private:
	MapsMod(const Managed& managed, MapsBindings bindings, MapOverrideSet set, ModStatus& status);

	using LoadFunction = Il2CppObject* (*)(Il2CppObject * self, Il2CppObject* path, Il2CppObject* type,
	                                       const MethodInfo* method);

	static Il2CppObject* HookLoad(Il2CppObject* self, Il2CppObject* path, Il2CppObject* type, const MethodInfo* method);

	/// Return the object that the mod gives for this load, or null to let the next loader load it.
	[[nodiscard]] Il2CppObject* Serve(Il2CppObject* self, const std::string& path, Il2CppObject* type,
	                                  const MethodInfo* method);
	[[nodiscard]] Il2CppObject* ServeText(size_t index, Il2CppObject* original);

	/// The change of one land item: the data that the game has now (in the form of `CheckOriginal`) and the function
	/// that changes it.
	struct LandWork
	{
		size_t index = 0;
		std::vector<std::byte> current;
		std::function<moa::Result<void>()> apply;
	};

	/// Check all the land items, then change all of them, at the load of the camera floor (`floor` is the texture that
	/// the game gets). A refusal of one land item keeps the whole land of the game.
	void ChangeLand(Il2CppObject* self, Il2CppObject* floor, const MethodInfo* method);
	/// Change the coast map at a later load of it, if the land group is not refused.
	void ChangeCoastMap(size_t index, Il2CppObject* original);
	[[nodiscard]] moa::Result<LandWork> PrepareHeights(size_t index, Il2CppObject* globalData, Il2CppObject* terrainData);
	[[nodiscard]] moa::Result<LandWork> PrepareTerrainTrees(size_t index, Il2CppObject* terrainData);
	[[nodiscard]] moa::Result<LandWork> PrepareTreeHolders(size_t index);
	[[nodiscard]] moa::Result<LandWork> PrepareCameraFloor(size_t index, Il2CppObject* texture);
	[[nodiscard]] moa::Result<LandWork> PrepareCoastMap(size_t index, Il2CppObject* asset);
	/// The bytes of a TextAsset in memory, or an error.
	[[nodiscard]] moa::Result<std::span<std::byte>> TextBytes(Il2CppObject* asset) const;
	void Refuse(size_t index, const std::string& message);
	/// The reason that an item was not used, for the log.
	[[nodiscard]] std::string Why(size_t index) const;
	void Keep(Il2CppObject* object);

	const Managed& _managed;
	MapsBindings _bindings;
	std::optional<LandBindings> _land;
	std::string _landError;
	ModStatus& _status;
	SlotSwap _swap;
	LoadFunction _original = nullptr;

	/// The main thread holds the lock while it serves a load. The lock is recursive, because Unity can load an object
	/// in a load.
	mutable std::recursive_mutex _mutex;
	MapOverrideSet _set;
	std::thread::id _mainThread;
	uint64_t _otherThreadLoads = 0;
	uint64_t _landChanges = 0;
	/// The text assets that the mod made, by item. The garbage collector keeps them.
	std::map<size_t, Il2CppObject*> _texts;
	TextFilter _filter;
	std::vector<uintptr_t> _handles;
};

} // namespace moa_mod::maps
