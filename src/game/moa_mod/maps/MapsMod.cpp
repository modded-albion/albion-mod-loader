/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/maps/MapsMod.h"

#include <cmath>
#include <cstring>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <tuple>
#include <unordered_map>

#include <moa/mapoverride/MapOverride.h>
#include <moa/mapoverride/Sha1.h>
#include <spdlog/spdlog.h>

#include "moa_mod/ResourceLoadSlot.h"

namespace moa_mod::maps
{

namespace mo = moa::mapoverride;
using Decision = MapOverrideSet::Decision;

namespace
{
/// The mod that the entry of the loader calls. A code pointer cannot carry data, so the function finds the mod here.
std::atomic<MapsMod*> s_mod = nullptr;

/// The resource path of the camera floor. The game loads it after it made its land data at run time, so the mod
/// changes the land at this load.
constexpr std::string_view k_CameraFloorPath = "camerabaseheightmap";

/// The option of the text asset constructor that makes the asset in the engine too.
constexpr int32_t k_CreateNativeObject = 1;

/// The largest height sample of Unity. A height in the land data is the sample divided by this value.
constexpr float k_MaxSample = 32766.0f;

/// The managed object header before the fields of a value type in a boxed object: two pointers.
constexpr size_t k_ObjectHeader = 2 * sizeof(void*);

/// The sea level of the game in metres. The game marks the samples at this height or lower as water.
constexpr float k_SeaLevel = 90.0f;

/// The largest difference in metres between a run-time height of the game and the height of its sample.
constexpr float k_HeightTolerance = 0.01f;

/// A `NativeArray` of Unity in memory: the address of the data, then the count of items.
struct NativeArrayView
{
	std::byte* data = nullptr;
	size_t count = 0;
};

[[nodiscard]] NativeArrayView ReadNativeArray(const void* object, size_t offset)
{
	const auto data = ReadField<std::byte*>(object, offset);
	const auto count = ReadField<int32_t>(object, offset + sizeof(void*));
	return {.data = data, .count = count < 0 ? 0 : static_cast<size_t>(count)};
}
} // namespace

moa::Result<MapsBindings> MapsBindings::Bind(const Managed& managed)
{
	Binder binder(managed);
	MapsBindings b;
	b.resourcesApi = binder.Class("UnityEngine", "ResourcesAPI");
	b.textAsset = binder.Class("UnityEngine", "TextAsset");
	b.texture = binder.Class("UnityEngine", "Texture2D");
	auto* unityObject = binder.Class("UnityEngine", "Object");
	b.load = binder.Method(b.resourcesApi, "Load", {"System.String", "System.Type"}, "UnityEngine.Object");
	b.activeApi = binder.Method(b.resourcesApi, "get_ActiveAPI", {}, "UnityEngine.ResourcesAPI", true);
	b.textAssetMake =
	    binder.Method(b.textAsset, ".ctor", {"UnityEngine.TextAsset.CreateOptions", "System.String"}, "System.Void");
	b.textDataPointer = binder.Method(b.textAsset, "GetDataPtr", {}, "System.IntPtr");
	b.textDataSize = binder.Method(b.textAsset, "GetDataSize", {}, "System.Int64");
	b.textureData = binder.Method(b.texture, "GetWritableImageData", {"System.Int32"}, "System.IntPtr");
	b.textureDataSize = binder.Method(b.texture, "GetDataSize", {}, "System.UInt64");
	b.textureLoadRaw = binder.Method(b.texture, "LoadRawTextureDataImpl", {"System.IntPtr", "System.UInt64"}, "System.Boolean");
	b.getName = binder.Method(unityObject, "get_name", {}, "System.String");
	b.setName = binder.Method(unityObject, "set_name", {"System.String"}, "System.Void");
	if (!binder.Errors().empty())
	{
		return moa::MakeError("{}", binder.Errors());
	}
	if (managed.il2cpp_object_new == nullptr || managed.il2cpp_object_get_virtual_method == nullptr)
	{
		return moa::MakeError("GameAssembly.dll does not export il2cpp_object_new or il2cpp_object_get_virtual_method");
	}
	return b;
}

moa::Result<LandBindings> LandBindings::Bind(const Managed& managed)
{
	Binder binder(managed);
	LandBindings b;
	auto* globalData = binder.Class("", "GlobalData");
	auto* terrainData = binder.Class("UnityEngine", "TerrainData");
	auto* tree = binder.Class("UnityEngine", "TreeInstance");
	auto* unityObject = binder.Class("UnityEngine", "Object");
	auto* component = binder.Class("UnityEngine", "Component");
	auto* transform = binder.Class("UnityEngine", "Transform");
	b.treeHolder = binder.Class("", "TreeHolder");
	b.globalTerrainData = binder.Field(globalData, "m_terrainData", "UnityEngine.TerrainData");
	b.originalTerrainData = binder.Field(globalData, "m_originalTerrainData", "UnityEngine.TerrainData");
	b.heights = binder.Field(globalData, "m_heights", "Unity.Collections.NativeArray<System.Single>");
	b.heightsOriginal = binder.Field(globalData, "m_heightsOriginal", "Unity.Collections.NativeArray<System.Single>");
	b.waterPresence = binder.Field(globalData, "m_waterPresence", "Unity.Collections.NativeArray<System.UInt32>");
	b.initialWaterPresence = binder.Field(globalData, "m_initialWaterPresence", "Unity.Collections.NativeArray<System.UInt32>");
	b.resolution = binder.Method(terrainData, "get_heightmapResolution", {}, "System.Int32");
	b.terrainSize = binder.Method(terrainData, "get_size", {}, "UnityEngine.Vector3");
	b.getHeights = binder.Method(terrainData, "GetHeights", {"System.Int32", "System.Int32", "System.Int32", "System.Int32"},
	                             "System.Single[,]");
	b.setHeights =
	    binder.Method(terrainData, "SetHeights", {"System.Int32", "System.Int32", "System.Single[,]"}, "System.Void");
	b.getTrees = binder.Method(terrainData, "get_treeInstances", {}, "UnityEngine.TreeInstance[]");
	b.setTrees =
	    binder.Method(terrainData, "SetTreeInstances", {"UnityEngine.TreeInstance[]", "System.Boolean"}, "System.Void");
	const auto position = binder.Field(tree, "position", "UnityEngine.Vector3");
	b.findObjects =
	    binder.Method(unityObject, "FindObjectsOfType", {"System.Type", "System.Boolean"}, "UnityEngine.Object[]", true);
	b.componentGameObject = binder.Method(component, "get_gameObject", {}, "UnityEngine.GameObject");
	b.componentTransform = binder.Method(component, "get_transform", {}, "UnityEngine.Transform");
	b.getLocalPosition = binder.Method(transform, "get_localPosition", {}, "UnityEngine.Vector3");
	b.setLocalPosition = binder.Method(transform, "set_localPosition", {"UnityEngine.Vector3"}, "System.Void");
	b.getName = binder.Method(unityObject, "get_name", {}, "System.String");
	if (!binder.Errors().empty())
	{
		return moa::MakeError("{}", binder.Errors());
	}
	if (managed.il2cpp_class_array_element_size == nullptr || managed.il2cpp_array_length == nullptr ||
	    managed.il2cpp_array_object_header_size == nullptr)
	{
		return moa::MakeError("GameAssembly.dll does not export the array functions");
	}
	// The field offset of a value type counts the object header; an item of an array has no header.
	const auto size = managed.il2cpp_class_array_element_size(tree);
	if (position < k_ObjectHeader || size <= 0 || position - k_ObjectHeader + 3 * sizeof(float) > static_cast<size_t>(size))
	{
		return moa::MakeError("the layout of TreeInstance is not known (position {}, size {})", position, size);
	}
	b.treeSize = static_cast<size_t>(size);
	b.treeHeight = position - k_ObjectHeader + sizeof(float);
	return b;
}

MapsMod::MapsMod(const Managed& managed, MapsBindings bindings, MapOverrideSet set, ModStatus& status)
    : _managed(managed)
    , _bindings(bindings)
    , _status(status)
    , _set(std::move(set))
{
}

MapsMod::~MapsMod()
{
	Remove(false);
}

moa::Result<std::unique_ptr<MapsMod>> MapsMod::Install(const Managed& managed, MapOverrideSet set, ModStatus& status)
{
	if (s_mod.load() != nullptr)
	{
		return moa::MakeError("the map override is already installed");
	}
	auto bindings = MapsBindings::Bind(managed);
	if (!bindings)
	{
		return bindings.GetError();
	}
	// The entry holds the code of the game, or the function of the objects mod in this DLL. The map override calls it
	// for each load that it does not serve.
	auto found = FindResourceLoadSlot(managed, bindings->resourcesApi, bindings->activeApi, bindings->load, true);
	if (!found)
	{
		return found.GetError();
	}
	auto mod = std::unique_ptr<MapsMod>(new MapsMod(managed, *bindings, std::move(set), status));
	if (auto land = LandBindings::Bind(managed); land)
	{
		mod->_land = *land;
	}
	else
	{
		mod->_landError = land.GetError().message;
		spdlog::error("maps: the land items are off: {}", mod->_landError);
	}
	mod->_original = reinterpret_cast<LoadFunction>(found->code);
	s_mod = mod.get();
	mod->_swap = SlotSwap::Install(found->slot, found->code, reinterpret_cast<void*>(&MapsMod::HookLoad));
	if (!mod->_swap.Installed())
	{
		s_mod = nullptr;
		return moa::MakeError("the entry for Load changed during the install");
	}
	spdlog::info("maps: the hook on {}.Load is installed (after {}); the override '{}' has {} items", found->className,
	             found->gameCode ? "the game" : "the objects mod", mod->_set.Name(), mod->_set.Items().size());
	return mod;
}

bool MapsMod::Remove(bool log)
{
	if (!_swap.Installed())
	{
		return true;
	}
	const auto restored = _swap.Restore();
	if (log)
	{
		spdlog::info("maps: the hook on Load is {}", restored ? "removed" : "kept by another program");
	}
	return restored;
}

Il2CppObject* MapsMod::HookLoad(Il2CppObject* self, Il2CppObject* path, Il2CppObject* type, const MethodInfo* method)
{
	auto* mod = s_mod.load();
	Il2CppObject* served = nullptr;
	std::string text;
	// No C++ exception may go into the game. A managed exception of the next loader goes to the game unchanged, thus
	// that call is outside the try block.
	try
	{
		text = mod->_managed.Text(path);
		served = mod->Serve(self, text, type, method);
	}
	catch (const std::exception& error)
	{
		mod->_status.SetError(std::format("load of '{}': {}", text, error.what()));
	}
	if (served != nullptr)
	{
		return served;
	}
	return mod->_original(self, path, type, method);
}

Il2CppObject* MapsMod::Serve(Il2CppObject* self, const std::string& path, Il2CppObject* type, const MethodInfo* method)
{
	{
		const std::scoped_lock lock(_mutex);
		if (_mainThread == std::thread::id {})
		{
			_mainThread = std::this_thread::get_id();
		}
		if (_mainThread != std::this_thread::get_id())
		{
			// Unity objects change only on the main thread.
			++_otherThreadLoads;
			return nullptr;
		}
	}
	// The inspector reads the state of the override on its own thread.
	const std::scoped_lock lock(_mutex);
	const auto normal = mo::NormalResourcePath(path);
	const auto index = _set.FindPath(normal);
	const bool cameraFloor = normal == k_CameraFloorPath;
	const bool land =
	    std::ranges::any_of(_set.Items(), [](const OverrideItem& item) { return IsLandKind(item.manifest.kind); });
	if (!index && !(cameraFloor && land))
	{
		return nullptr;
	}
	auto* original = _original(self, _managed.String(path.c_str()), type, method);
	if (original == nullptr)
	{
		if (index)
		{
			Refuse(*index, std::format("the game has no object at '{}'", path));
		}
		return nullptr;
	}
	if (cameraFloor)
	{
		ChangeLand(self, original, method);
		return original;
	}
	switch (_set.Items()[*index].manifest.kind)
	{
	case mo::ItemKind::Text:
		return ServeText(*index, original);
	case mo::ItemKind::CoastMap:
		ChangeCoastMap(*index, original);
		return original;
	default:
		return original;
	}
}

moa::Result<std::span<std::byte>> MapsMod::TextBytes(Il2CppObject* asset) const
{
	auto pointer = _managed.Call<intptr_t>(_bindings.textDataPointer, asset);
	auto size = _managed.Call<int64_t>(_bindings.textDataSize, asset);
	if (!pointer || !size)
	{
		return moa::MakeError("the data of the text asset is not readable: {}",
		                      !pointer ? pointer.GetError().message : size.GetError().message);
	}
	if (*size < 0 || (*size > 0 && *pointer == 0))
	{
		return moa::MakeError("the text asset has no data");
	}
	auto* data = reinterpret_cast<std::byte*>(*pointer);
	if (*size > 0 && ReadableBytes(data) < static_cast<size_t>(*size))
	{
		return moa::MakeError("the data of the text asset is not in readable memory");
	}
	return std::span<std::byte>(data, static_cast<size_t>(*size));
}

Il2CppObject* MapsMod::ServeText(size_t index, Il2CppObject* original)
{
	auto bytes = TextBytes(original);
	if (!bytes)
	{
		Refuse(index, bytes.GetError().message);
		return original;
	}
	if (_set.Decide(index, *bytes) == Decision::Refuse)
	{
		spdlog::error("maps: '{}' is not used: {}", _set.Items()[index].manifest.file, Why(index));
		_status.SetError(Why(index));
		return original;
	}
	if (const auto found = _texts.find(index); found != _texts.end())
	{
		// The game can destroy a text asset after it reads it (MOADistricts). Then the mod makes a new asset.
		if (TextBytes(found->second))
		{
			_set.Served(index, "the game got the text of the override again");
			return found->second;
		}
		spdlog::info("maps: the text asset of '{}' was destroyed; the mod makes a new one",
		             _set.Items()[index].manifest.paths.front());
		_texts.erase(found);
	}
	const auto& item = _set.Items()[index];
	auto* asset = _managed.il2cpp_object_new(_bindings.textAsset);
	if (asset == nullptr)
	{
		Refuse(index, "the mod cannot make a text asset");
		return original;
	}
	std::string text(reinterpret_cast<const char*>(item.bytes.data()), item.bytes.size());
	if (_filter)
	{
		// The rows of the packs go into the text of the map too.
		if (auto filtered = _filter(item.manifest.paths.front(), text))
		{
			spdlog::info("maps: the objects mod added the rows of its packs to '{}'", item.manifest.paths.front());
			text = std::move(*filtered);
		}
	}
	auto options = k_CreateNativeObject;
	if (auto made = _managed.Invoke(_bindings.textAssetMake, asset, {&options, _managed.String(text.c_str())}); !made)
	{
		Refuse(index, made.GetError().message);
		return original;
	}
	// The new asset has the name of the original asset, as the game can show or compare it.
	if (auto name = _managed.Invoke(_bindings.getName, original); name && *name != nullptr)
	{
		(void)_managed.Invoke(_bindings.setName, asset, {*name});
	}
	Keep(asset);
	_texts.emplace(index, asset);
	_set.Served(index, std::format("the game got {} bytes of text in place of {} bytes", item.bytes.size(), bytes->size()));
	spdlog::info("maps: the game loads '{}' from the override ({} bytes, SHA-1 {})", item.manifest.paths.front(),
	             item.bytes.size(), item.manifest.sha1);
	return asset;
}

void MapsMod::ChangeLand(Il2CppObject* self, Il2CppObject* floor, const MethodInfo* method)
{
	++_landChanges;
	std::vector<size_t> items;
	for (size_t index = 0; index < _set.Items().size(); ++index)
	{
		if (IsLandKind(_set.Items()[index].manifest.kind))
		{
			items.push_back(index);
		}
	}
	if (_set.LandStopped())
	{
		spdlog::info("maps: the land group was refused before; the game keeps its land");
		return;
	}
	if (!_land)
	{
		Refuse(items.front(), "the land names are not bound: " + _landError);
		return;
	}
	auto globalData = _managed.SingletonInstance("GlobalData");
	auto* terrainData = globalData ? ReadField<Il2CppObject*>(*globalData, _land->globalTerrainData) : nullptr;

	// First the checks of all the land items, with no change: the game gets all of them or none.
	std::vector<LandWork> work;
	for (const auto index : items)
	{
		moa::Result<LandWork> prepared = moa::MakeError("the kind of the item is not a land kind");
		switch (_set.Items()[index].manifest.kind)
		{
		case mo::ItemKind::Heights:
		case mo::ItemKind::TerrainTrees:
			if (terrainData == nullptr)
			{
				prepared = moa::MakeError("the game has no run-time land data now{}",
				                          globalData ? std::string() : ": " + globalData.GetError().message);
			}
			else if (_set.Items()[index].manifest.kind == mo::ItemKind::Heights)
			{
				prepared = PrepareHeights(index, *globalData, terrainData);
			}
			else
			{
				prepared = PrepareTerrainTrees(index, terrainData);
			}
			break;
		case mo::ItemKind::TreeHolders:
			prepared = PrepareTreeHolders(index);
			break;
		case mo::ItemKind::CameraFloor:
			prepared = PrepareCameraFloor(index, floor);
			break;
		case mo::ItemKind::CoastMap:
		{
			const auto& path = _set.Items()[index].manifest.paths.front();
			auto* coast = _original(self, _managed.String(path.c_str()), _managed.TypeObject(_bindings.textAsset), method);
			prepared = coast != nullptr ? PrepareCoastMap(index, coast)
			                            : moa::Result<LandWork>(moa::MakeError("the game has no object at '{}'", path));
			break;
		}
		case mo::ItemKind::Text:
			break;
		}
		if (!prepared)
		{
			Refuse(index, prepared.GetError().message);
			return;
		}
		std::string reason;
		if (_set.Check(index, prepared->current, &reason) == Decision::Refuse)
		{
			Refuse(index, reason);
			return;
		}
		work.push_back(std::move(*prepared));
	}

	// Then the changes. The heights go first: Unity can move the trees when the heights change, and the mod then
	// gives the trees their heights.
	std::ranges::stable_partition(
	    work, [this](const LandWork& entry) { return _set.Items()[entry.index].manifest.kind == mo::ItemKind::Heights; });
	for (auto& entry : work)
	{
		if (_set.Check(entry.index, entry.current) == Decision::AlreadyApplied)
		{
			_set.Served(entry.index, "the game had the data of the override already");
			continue;
		}
		if (auto applied = entry.apply(); !applied)
		{
			Refuse(entry.index, applied.GetError().message);
			spdlog::error("maps: the land items before this one are changed; the game can have a part of the land");
			return;
		}
		_set.Served(entry.index, "the mod changed the data of the game");
	}
	spdlog::info("maps: the land group of the override is in the game ({} items)", work.size());
}

void MapsMod::ChangeCoastMap(size_t index, Il2CppObject* original)
{
	auto prepared = PrepareCoastMap(index, original);
	if (!prepared)
	{
		Refuse(index, prepared.GetError().message);
		return;
	}
	const auto decision = _set.Decide(index, prepared->current);
	if (decision == Decision::Refuse)
	{
		spdlog::error("maps: the coast map is not used: {}", Why(index));
		return;
	}
	if (decision == Decision::Apply)
	{
		if (auto applied = prepared->apply(); !applied)
		{
			Refuse(index, applied.GetError().message);
			return;
		}
	}
	_set.Served(index, decision == Decision::Apply ? "the mod changed the coast map" : "the coast map was already changed");
}

moa::Result<MapsMod::LandWork> MapsMod::PrepareCoastMap(size_t index, Il2CppObject* asset)
{
	auto bytes = TextBytes(asset);
	if (!bytes)
	{
		return bytes.GetError();
	}
	const auto& item = _set.Items()[index];
	if (bytes->size() != item.bytes.size())
	{
		return moa::MakeError("the coast map has {} bytes, not {}", bytes->size(), item.bytes.size());
	}
	auto target = *bytes;
	return LandWork {
	    .index = index, .current = {bytes->begin(), bytes->end()}, .apply = [this, index, target]() -> moa::Result<void> {
		    const auto& data = _set.Items()[index].bytes;
		    if (!IsWritableMemory(target.data(), target.size()))
		    {
			    return moa::MakeError("the data of the coast map is not in writable memory");
		    }
		    std::memcpy(target.data(), data.data(), data.size());
		    spdlog::info("maps: the coast map has the {} bytes of the override", data.size());
		    return {};
	    }};
}

moa::Result<MapsMod::LandWork> MapsMod::PrepareCameraFloor(size_t index, Il2CppObject* texture)
{
	const auto& item = _set.Items()[index];
	int32_t frame = 0;
	auto pointer = _managed.Call<intptr_t>(_bindings.textureData, texture, {&frame});
	auto size = _managed.Call<uint64_t>(_bindings.textureDataSize, texture);
	if (!pointer || !size || *pointer == 0)
	{
		return moa::MakeError("the image data of the camera floor is not readable (the texture must be readable)");
	}
	if (*size != item.bytes.size() || ReadableBytes(reinterpret_cast<const void*>(*pointer)) < *size)
	{
		return moa::MakeError("the camera floor has {} bytes of image data, not {}", *size, item.bytes.size());
	}
	const auto* data = reinterpret_cast<const std::byte*>(*pointer);
	return LandWork {.index = index, .current = {data, data + *size}, .apply = [this, index, texture]() -> moa::Result<void> {
		                 const auto& bytes = _set.Items()[index].bytes;
		                 auto source = reinterpret_cast<intptr_t>(bytes.data());
		                 uint64_t count = bytes.size();
		                 auto loaded = _managed.Call<bool>(_bindings.textureLoadRaw, texture, {&source, &count});
		                 if (!loaded || !*loaded)
		                 {
			                 return moa::MakeError("Unity did not take the image data of the camera floor{}",
			                                       loaded ? std::string() : ": " + loaded.GetError().message);
		                 }
		                 spdlog::info("maps: the camera floor has the {} bytes of image data of the override", count);
		                 return {};
	                 }};
}

moa::Result<MapsMod::LandWork> MapsMod::PrepareHeights(size_t index, Il2CppObject* globalData, Il2CppObject* terrainData)
{
	const auto& item = _set.Items()[index];
	const auto& rect = item.manifest.rect;
	auto resolution = _managed.Call<int32_t>(_land->resolution, terrainData);
	if (!resolution || *resolution != item.manifest.resolution)
	{
		return moa::MakeError("the land data has a resolution of {}, not {}", resolution ? *resolution : -1,
		                      item.manifest.resolution);
	}
	auto size = _managed.Call<std::array<float, 3>>(_land->terrainSize, terrainData);
	if (!size || !((*size)[1] > 0.0f))
	{
		return moa::MakeError("the height scale of the land data is not readable");
	}
	const float scale = (*size)[1];
	auto column = rect.column;
	auto row = rect.row;
	auto width = rect.width;
	auto height = rect.height;
	auto array = _managed.Invoke(_land->getHeights, terrainData, {&column, &row, &width, &height});
	if (!array || *array == nullptr || _managed.ArrayLength(*array) != rect.Count())
	{
		return moa::MakeError("the heights of the land data are not readable{}",
		                      array ? std::string() : ": " + array.GetError().message);
	}
	const auto* values = reinterpret_cast<const float*>(_managed.ArrayData(*array));
	std::vector<int16_t> current(rect.Count());
	for (size_t i = 0; i < current.size(); ++i)
	{
		current[i] = static_cast<int16_t>(std::lround(values[i] * k_MaxSample));
	}

	// The game keeps its own copy of the heights in metres, a square of samples with the first rows and columns of
	// the land, and a bit for each sample under the sea. The copy must agree with the land data now.
	const auto heights = ReadNativeArray(globalData, _land->heights);
	const auto heightsOriginal = ReadNativeArray(globalData, _land->heightsOriginal);
	const auto water = ReadNativeArray(globalData, _land->waterPresence);
	const auto initialWater = ReadNativeArray(globalData, _land->initialWaterPresence);
	const auto side = static_cast<size_t>(std::lround(std::sqrt(static_cast<double>(heights.count))));
	if (heights.data == nullptr || side == 0 || side * side != heights.count || side > static_cast<size_t>(*resolution) ||
	    heightsOriginal.count != heights.count || heightsOriginal.data == nullptr || water.count * 32 < heights.count ||
	    initialWater.count != water.count || water.data == nullptr || initialWater.data == nullptr)
	{
		return moa::MakeError("the run-time heights of the game have an unknown layout ({} heights, {} water words)",
		                      heights.count, water.count);
	}
	for (const auto& [view, items, name] :
	     {std::tuple {heights, sizeof(float), "heights"}, std::tuple {heightsOriginal, sizeof(float), "original heights"},
	      std::tuple {water, sizeof(uint32_t), "water bits"}, std::tuple {initialWater, sizeof(uint32_t), "first water bits"}})
	{
		if (!IsWritableMemory(view.data, view.count * items))
		{
			return moa::MakeError("the run-time {} of the game are not in writable memory", name);
		}
	}
	// The game copies heights from its copy of the first land data back under the roads when it makes them (a new
	// game). The copy gets the heights of the override too, so it must have the same heights now.
	auto* originalData = ReadField<Il2CppObject*>(globalData, _land->originalTerrainData);
	Il2CppObject* originalArray = nullptr;
	if (originalData != nullptr && originalData != terrainData)
	{
		auto copy = _managed.Invoke(_land->getHeights, originalData, {&column, &row, &width, &height});
		if (!copy || *copy == nullptr || _managed.ArrayLength(*copy) != rect.Count())
		{
			return moa::MakeError("the heights of the first land data are not readable{}",
			                      copy ? std::string() : ": " + copy.GetError().message);
		}
		const auto* copyValues = reinterpret_cast<const float*>(_managed.ArrayData(*copy));
		for (size_t i = 0; i < current.size(); ++i)
		{
			if (std::lround(copyValues[i] * k_MaxSample) != current[i])
			{
				return moa::MakeError(
				    "the first land data of the game differs from the land data at sample {} of the rectangle", i);
			}
		}
		originalArray = *copy;
		Keep(originalArray);
	}
	const auto* metres = reinterpret_cast<const float*>(heights.data);
	for (int32_t r = 0; r < rect.height; ++r)
	{
		for (int32_t c = 0; c < rect.width; ++c)
		{
			const auto x = static_cast<size_t>(rect.column + c);
			const auto z = static_cast<size_t>(rect.row + r);
			if (x >= side || z >= side)
			{
				continue;
			}
			const float expected =
			    values[static_cast<size_t>(r) * static_cast<size_t>(rect.width) + static_cast<size_t>(c)] * scale;
			const float actual = metres[z * side + x];
			if (std::fabs(actual - expected) > k_HeightTolerance)
			{
				return moa::MakeError("the run-time height of the game at ({}, {}) is {:.3f} m, but the land data has {:.3f} m",
				                      x, z, actual, expected);
			}
		}
	}
	return LandWork {
	    .index = index,
	    .current = mo::HeightBytes(current),
	    .apply = [this, index, terrainData, array = *array, scale, side, heights, heightsOriginal, water, initialWater,
	              originalData, originalArray]() mutable -> moa::Result<void> {
		    const auto& item = _set.Items()[index];
		    const auto& rect = item.manifest.rect;
		    auto* values = reinterpret_cast<float*>(_managed.ArrayData(array));
		    for (size_t i = 0; i < item.heights.size(); ++i)
		    {
			    values[i] = static_cast<float>(item.heights[i]) / k_MaxSample;
		    }
		    auto column = rect.column;
		    auto row = rect.row;
		    if (auto set = _managed.Invoke(_land->setHeights, terrainData, {&column, &row, array}); !set)
		    {
			    return moa::MakeError("Unity did not take the heights: {}", set.GetError().message);
		    }
		    if (originalArray != nullptr)
		    {
			    std::memcpy(_managed.ArrayData(originalArray), _managed.ArrayData(array), item.heights.size() * sizeof(float));
			    if (auto set = _managed.Invoke(_land->setHeights, originalData, {&column, &row, originalArray}); !set)
			    {
				    return moa::MakeError("Unity did not take the heights in the first land data: {}", set.GetError().message);
			    }
			    spdlog::info("maps: the first land data has the heights of the override too");
		    }
		    auto* metres = reinterpret_cast<float*>(heights.data);
		    auto* metresOriginal = reinterpret_cast<float*>(heightsOriginal.data);
		    auto* bits = reinterpret_cast<uint32_t*>(water.data);
		    auto* initialBits = reinterpret_cast<uint32_t*>(initialWater.data);
		    const float seaSample = k_SeaLevel / scale;
		    for (int32_t r = 0; r < rect.height; ++r)
		    {
			    for (int32_t c = 0; c < rect.width; ++c)
			    {
				    const auto x = static_cast<size_t>(rect.column + c);
				    const auto z = static_cast<size_t>(rect.row + r);
				    if (x >= side || z >= side)
				    {
					    continue;
				    }
				    const float normal =
				        values[static_cast<size_t>(r) * static_cast<size_t>(rect.width) + static_cast<size_t>(c)];
				    const auto at = z * side + x;
				    metres[at] = normal * scale;
				    metresOriginal[at] = normal * scale;
				    const uint32_t bit = 1u << (at & 31u);
				    if (normal <= seaSample)
				    {
					    bits[at >> 5] |= bit;
					    initialBits[at >> 5] |= bit;
				    }
				    else
				    {
					    bits[at >> 5] &= ~bit;
					    initialBits[at >> 5] &= ~bit;
				    }
			    }
		    }
		    spdlog::info("maps: the land data and the run-time heights have the heights of the override in columns {} to {} "
		                 "and rows {} to {}",
		                 rect.column, rect.column + rect.width - 1, rect.row, rect.row + rect.height - 1);
		    return {};
	    }};
}

moa::Result<MapsMod::LandWork> MapsMod::PrepareTerrainTrees(size_t index, Il2CppObject* terrainData)
{
	const auto& item = _set.Items()[index];
	auto array = _managed.Invoke(_land->getTrees, terrainData);
	if (!array || *array == nullptr)
	{
		return moa::MakeError("the trees of the land data are not readable");
	}
	const auto count = _managed.ArrayLength(*array);
	auto* data = _managed.ArrayData(*array);
	std::vector<mo::TerrainTreeMove> current;
	for (const auto& move : item.terrainTrees)
	{
		if (move.index >= count)
		{
			return moa::MakeError("the land data has {} trees; the override moves the tree {}", count, move.index);
		}
		const auto height = ReadField<float>(data, move.index * _land->treeSize + _land->treeHeight);
		if (height != move.before && height != move.after)
		{
			spdlog::error("maps: the tree {} has the height {:.9g} in the game; the editor read {:.9g}", move.index, height,
			              move.before);
		}
		current.push_back({.index = move.index, .before = height, .after = move.after});
	}
	return LandWork {.index = index,
	                 .current = mo::TerrainTreeOriginalBytes(current),
	                 .apply = [this, index, terrainData, array = *array]() -> moa::Result<void> {
		                 const auto& moves = _set.Items()[index].terrainTrees;
		                 auto* data = _managed.ArrayData(array);
		                 for (const auto& move : moves)
		                 {
			                 WriteField(data, move.index * _land->treeSize + _land->treeHeight, move.after);
		                 }
		                 bool snap = false;
		                 if (auto set = _managed.Invoke(_land->setTrees, terrainData, {array, &snap}); !set)
		                 {
			                 return moa::MakeError("Unity did not take the trees: {}", set.GetError().message);
		                 }
		                 spdlog::info("maps: {} trees of the land data have the heights of the override", moves.size());
		                 return {};
	                 }};
}

moa::Result<MapsMod::LandWork> MapsMod::PrepareTreeHolders(size_t index)
{
	const auto& item = _set.Items()[index];
	bool inactive = true;
	auto objects = _managed.Invoke(_land->findObjects, nullptr, {_managed.TypeObject(_land->treeHolder), &inactive});
	if (!objects || *objects == nullptr)
	{
		return moa::MakeError("the mod cannot find the TreeHolder objects");
	}
	std::unordered_map<std::string, Il2CppObject*> wanted;
	for (const auto& move : item.holders)
	{
		wanted.emplace(move.name, nullptr);
	}
	for (size_t i = 0; i < _managed.ArrayLength(*objects); ++i)
	{
		auto* component = _managed.ArrayItem(*objects, i);
		auto name = _managed.Invoke(_land->getName, component);
		if (!name || *name == nullptr)
		{
			continue;
		}
		const auto found = wanted.find(_managed.Text(*name));
		if (found == wanted.end())
		{
			continue;
		}
		if (found->second != nullptr)
		{
			return moa::MakeError("the scene has two TreeHolder objects with the name {}", found->first);
		}
		auto transform = _managed.Invoke(_land->componentTransform, component);
		if (!transform || *transform == nullptr)
		{
			return moa::MakeError("the TreeHolder {} has no transform", found->first);
		}
		found->second = *transform;
	}
	std::vector<mo::HolderMove> current;
	std::vector<std::pair<Il2CppObject*, std::array<float, 3>>> targets;
	for (const auto& move : item.holders)
	{
		auto* transform = wanted.at(move.name);
		if (transform == nullptr)
		{
			return moa::MakeError("the scene has no TreeHolder with the name {}", move.name);
		}
		auto position = _managed.Call<std::array<float, 3>>(_land->getLocalPosition, transform);
		if (!position)
		{
			return position.GetError();
		}
		if ((*position)[1] != move.before && (*position)[1] != move.after)
		{
			spdlog::error("maps: {} has the local height {:.9g} in the game; the editor read {:.9g}", move.name, (*position)[1],
			              move.before);
		}
		current.push_back({.name = move.name, .before = (*position)[1], .after = move.after});
		targets.emplace_back(transform, *position);
	}
	return LandWork {
	    .index = index, .current = mo::HolderOriginalBytes(current), .apply = [this, index, targets]() -> moa::Result<void> {
		    const auto& moves = _set.Items()[index].holders;
		    for (size_t i = 0; i < moves.size(); ++i)
		    {
			    auto position = targets[i].second;
			    position[1] = moves[i].after;
			    if (auto set = _managed.Invoke(_land->setLocalPosition, targets[i].first, {position.data()}); !set)
			    {
				    return moa::MakeError("Unity did not move a TreeHolder: {}", set.GetError().message);
			    }
		    }
		    spdlog::info("maps: {} TreeHolder objects have the heights of the override", moves.size());
		    return {};
	    }};
}

std::string MapsMod::Why(size_t index) const
{
	const auto& message = _set.Items()[index].message;
	return message.empty() ? std::string("an item of its group was refused, so the group stopped") : message;
}

void MapsMod::Refuse(size_t index, const std::string& message)
{
	_set.Fail(index, message);
	spdlog::error("maps: '{}' is not used, and its group stops: {}", _set.Items()[index].manifest.file, message);
	_status.SetError(message);
}

void MapsMod::Keep(Il2CppObject* object)
{
	if (_managed.il2cpp_gchandle_new != nullptr)
	{
		_handles.push_back(_managed.il2cpp_gchandle_new(object, false));
	}
}

nlohmann::json MapsMod::State() const
{
	const std::scoped_lock lock(_mutex);
	auto state = _set.Describe();
	state["installed"] = _swap.Installed();
	state["otherThreadLoads"] = _otherThreadLoads;
	state["landChanges"] = _landChanges;
	state["landBindings"] = _land.has_value();
	if (!_landError.empty())
	{
		state["landError"] = _landError;
	}
	state["status"] = _status.Describe();
	return state;
}

} // namespace moa_mod::maps
