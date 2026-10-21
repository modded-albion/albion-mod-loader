/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "game/objects/ObjectsMod.h"

#include <array>
#include <atomic>
#include <format>
#include <iterator>

#include <spdlog/spdlog.h>
#include <windows.h>

#include "game/ResourceLoadSlot.h"

namespace moa_mod::objects
{

namespace
{
/// The mod that the entry of the loader calls. A code pointer cannot carry data, so the function finds the mod here.
std::atomic<ObjectsMod*> s_mod = nullptr;

/// The file of a pack in its folder.
constexpr std::string_view k_PackFile = "pack.json";

/// The largest file of a pack that the mod reads.
constexpr uintmax_t k_MaxPackFileBytes = 256ull * 1024 * 1024;

/// The option of the text asset constructor that makes the asset in the engine too.
constexpr int32_t k_CreateNativeObject = 1;

/// The test pack of the setting `testClone`: a copy of the raw cotton with the name `Custom_TestCotton`.
Pack TestPack()
{
	return Pack {
	    .folder = "(built in)",
	    .id = "moar-test",
	    .name = "Test resource of the mod",
	    .version = "1",
	    .description = {},
	    .resources = {PackResource {.name = "Custom_TestCotton",
	                                .templateName = "Cotton",
	                                .prefab = "Custom_TestCotton",
	                                .id = std::nullopt,
	                                .model = std::nullopt,
	                                .mesh = std::nullopt,
	                                .materials = {},
	                                .scale = 1.0f,
	                                .pivot = PivotMode::BottomCentre,
	                                .pivotPoint = {0.0f, 0.0f, 0.0f},
	                                .collider = ColliderMode::Template,
	                                .row = {{"m_title", "Test Cotton"}},
	                                .credits = {}}},
	};
}

/// Read a file of a pack folder on the disk.
moa::Result<std::vector<std::byte>> ReadFolderFile(const std::filesystem::path& folder, const std::string& relative)
{
	const auto normal = NormalisePackPath(relative);
	if (!normal)
	{
		return moa::MakeError("the path '{}' is not in the pack folder", relative);
	}
	const auto file = folder / std::filesystem::path(std::u8string(normal->begin(), normal->end()));
	std::error_code error;
	const auto size = std::filesystem::file_size(file, error);
	if (error)
	{
		return moa::MakeError("the file cannot be read: {}", error.message());
	}
	if (size > k_MaxPackFileBytes)
	{
		return moa::MakeError("the file has {} bytes; the mod reads not more than {}", size, k_MaxPackFileBytes);
	}
	std::vector<std::byte> bytes(static_cast<size_t>(size));
	std::ifstream stream(file, std::ios::binary);
	if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
	{
		return moa::MakeError("the file cannot be read");
	}
	return bytes;
}
} // namespace

std::vector<PackOrigin> ReadPackFolder(const std::filesystem::path& folder)
{
	std::vector<PackOrigin> packs;
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(folder, error))
	{
		const auto file = entry.path() / k_PackFile;
		if (!entry.is_directory(error) || !std::filesystem::is_regular_file(file, error))
		{
			continue;
		}
		std::ifstream stream(file, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		const auto name = entry.path().filename().u8string();
		packs.push_back(PackOrigin {
		    .key = "packs/" + std::string(name.begin(), name.end()),
		    .text = std::move(text),
		    .read = [path = entry.path()](const std::string& relative) { return ReadFolderFile(path, relative); },
		});
	}
	std::ranges::sort(packs, {}, &PackOrigin::key);
	return packs;
}

moa::Result<ObjectsBindings> ObjectsBindings::Bind(const Managed& managed)
{
	Binder binder(managed);
	ObjectsBindings b;
	b.resourcesApi = binder.Class("UnityEngine", "ResourcesAPI");
	b.textAsset = binder.Class("UnityEngine", "TextAsset");
	b.gameObject = binder.Class("UnityEngine", "GameObject");
	b.unityObject = binder.Class("UnityEngine", "Object");
	auto* transform = binder.Class("UnityEngine", "Transform");
	b.load = binder.Method(b.resourcesApi, "Load", {"System.String", "System.Type"}, "UnityEngine.Object");
	b.activeApi = binder.Method(b.resourcesApi, "get_ActiveAPI", {}, "UnityEngine.ResourcesAPI", true);
	b.textAssetMake =
	    binder.Method(b.textAsset, ".ctor", {"UnityEngine.TextAsset.CreateOptions", "System.String"}, "System.Void");
	b.textAssetText = binder.Method(b.textAsset, "get_text", {}, "System.String");
	b.gameObjectMake = binder.Method(b.gameObject, ".ctor", {"System.String"}, "System.Void");
	b.setActive = binder.Method(b.gameObject, "SetActive", {"System.Boolean"}, "System.Void");
	b.gameObjectTransform = binder.Method(b.gameObject, "get_transform", {}, "UnityEngine.Transform");
	b.instantiate = binder.Method(b.unityObject, "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform"},
	                              "UnityEngine.Object", true);
	b.dontDestroyOnLoad = binder.Method(b.unityObject, "DontDestroyOnLoad", {"UnityEngine.Object"}, "System.Void", true);
	b.setName = binder.Method(b.unityObject, "set_name", {"System.String"}, "System.Void");
	b.getName = binder.Method(b.unityObject, "get_name", {}, "System.String");
	b.destroyImmediate = binder.Method(b.unityObject, "DestroyImmediate", {"UnityEngine.Object"}, "System.Void", true);
	(void)transform;
	if (!binder.Errors().empty())
	{
		return moa::MakeError("{}", binder.Errors());
	}
	if (managed.il2cpp_object_new == nullptr || managed.il2cpp_class_from_system_type == nullptr ||
	    managed.il2cpp_object_get_virtual_method == nullptr)
	{
		return moa::MakeError("GameAssembly.dll does not export il2cpp_object_new, il2cpp_class_from_system_type or "
		                      "il2cpp_object_get_virtual_method");
	}
	return b;
}

ObjectsMod::ObjectsMod(const Managed& managed, ObjectsBindings bindings, loader::LoaderSettings::Objects settings,
                       ModStatus& status)
    : _managed(managed)
    , _bindings(bindings)
    , _settings(std::move(settings))
    , _status(status)
{
}

ObjectsMod::~ObjectsMod()
{
	Remove(false);
}

moa::Result<std::unique_ptr<ObjectsMod>> ObjectsMod::Install(const Managed& managed, loader::LoaderSettings::Objects settings,
                                                             const std::filesystem::path& traceFile,
                                                             std::vector<PackOrigin> origins, ModStatus& status)
{
	if (s_mod.load() != nullptr)
	{
		return moa::MakeError("the objects mod is already installed");
	}
	auto bindings = ObjectsBindings::Bind(managed);
	if (!bindings)
	{
		return bindings.GetError();
	}
	auto found = FindResourceLoadSlot(managed, bindings->resourcesApi, bindings->activeApi, bindings->load, false);
	if (!found)
	{
		return found.GetError();
	}
	const auto& className = found->className;
	if (!found->unityLoader)
	{
		spdlog::warn("objects: the game uses its own resource loader {}", className);
	}
	auto** slot = found->slot;
	auto* code = found->code;
	spdlog::info("objects: the entry for Load is in the class {}; it {} the code of the method", className,
	             found->methodCode ? "has" : "does not have");

	auto mod = std::unique_ptr<ObjectsMod>(new ObjectsMod(managed, *bindings, std::move(settings), status));
	if (mod->_settings.trace)
	{
		std::error_code error;
		std::filesystem::create_directories(traceFile.parent_path(), error);
		mod->_traceFile.open(traceFile, std::ios::out | std::ios::trunc);
		if (!mod->_traceFile)
		{
			spdlog::warn("objects: the mod cannot write the trace {}", traceFile.string());
		}
		else
		{
			mod->_traceFile << "seconds\ttype\tpath\tresult\n" << std::flush;
			spdlog::info("objects: the trace of the loads goes to {}", traceFile.string());
		}
	}
	std::vector<PackSource> sources;
	for (auto& origin : origins)
	{
		sources.push_back(PackSource {.folder = origin.key, .text = std::move(origin.text)});
		mod->_readers.insert_or_assign(origin.key, std::move(origin.read));
	}
	auto packs = ParsePacks(sources);
	for (const auto& error : packs.errors)
	{
		spdlog::error("objects: the pack {} is not used: {}", error.pack, error.message);
		mod->PackError(error.pack, error.message);
	}
	if (mod->_settings.testClone)
	{
		packs.packs.push_back(TestPack());
	}
	for (const auto& pack : packs.packs)
	{
		spdlog::info("objects: pack {} ({} {}) from {}: {} resources", pack.id, pack.name, pack.version, pack.folder,
		             pack.resources.size());
	}
	mod->_packs = std::move(packs.packs);
	if (auto templateBindings = TemplateBindings::Bind(managed))
	{
		mod->_builder.emplace(managed, *templateBindings, [raw = mod.get()](Il2CppObject* object) { raw->Keep(object); });
	}
	else
	{
		spdlog::error("objects: the custom models are off: {}", templateBindings.GetError().message);
		status.SetError(std::format("custom models: {}", templateBindings.GetError().message));
	}
	mod->LoadModels();
	mod->_tables.emplace(mod->_packs);
	if (auto api = managed.Invoke(bindings->activeApi, nullptr); api && *api != nullptr)
	{
		mod->_api = *api;
		mod->Keep(mod->_api);
	}
	mod->_original = reinterpret_cast<LoadFunction>(code);
	s_mod = mod.get();
	mod->_swap = SlotSwap::Install(slot, code, reinterpret_cast<void*>(&ObjectsMod::HookLoad));
	if (!mod->_swap.Installed())
	{
		s_mod = nullptr;
		return moa::MakeError("the entry for Load changed during the install");
	}
	spdlog::info("objects: the hook on {}.Load is installed; {} packs", className, mod->_packs.size());
	return mod;
}

bool ObjectsMod::Remove(bool log)
{
	if (!_swap.Installed())
	{
		return true;
	}
	const auto restored = _swap.Restore();
	// The function stays callable: a call that started before the restore can still use the mod. The mod lives
	// until the process ends.
	if (log)
	{
		spdlog::info("objects: the hook on Load is {}", restored ? "removed" : "kept by another program");
	}
	return restored;
}

Il2CppObject* ObjectsMod::HookLoad(Il2CppObject* self, Il2CppObject* path, Il2CppObject* type, const MethodInfo* method)
{
	auto* mod = s_mod.load();
	Il2CppObject* served = nullptr;
	std::string text;
	// No C++ exception may go into the game. A managed exception of the original load goes to the game unchanged,
	// thus the call of the original is outside the try blocks.
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
	auto* result = mod->_original(self, path, type, method);
	try
	{
		mod->Trace(text, type, result != nullptr);
	}
	catch (const std::exception& error)
	{
		mod->_status.SetError(std::format("trace of '{}': {}", text, error.what()));
	}
	return result;
}

Il2CppObject* ObjectsMod::Serve(Il2CppObject* self, const std::string& path, Il2CppObject* type, const MethodInfo* method)
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
	if (_packs.empty())
	{
		return nullptr;
	}
	if (const auto table = PackTableOfPath(NormalResourcePath(path)); table && PacksChangeTable(_packs, *table))
	{
		auto* original = _original(self, _managed.String(path.c_str()), type, method);
		Trace(path, type, original != nullptr);
		if (original == nullptr)
		{
			return nullptr;
		}
		auto merged = MergedTable(*table, original);
		if (!merged)
		{
			_status.SetError(std::format("table {}: {}", path, merged.GetError().message));
			return original;
		}
		return *merged;
	}
	const MergedResource* resource = nullptr;
	{
		const std::scoped_lock lock(_mutex);
		resource = MatchResourcePath(path, _merged);
	}
	if (resource == nullptr)
	{
		return nullptr;
	}
	Trace(path, type, true);
	auto prefab = Prefab(*resource, self, type, method);
	if (!prefab)
	{
		_status.SetError(std::format("prefab {}: {}", path, prefab.GetError().message));
		return nullptr;
	}
	++_prefabServed;
	return *prefab;
}

void ObjectsMod::Trace(const std::string& path, Il2CppObject* type, bool found)
{
	auto typeName = TypeName(type);
	const std::scoped_lock lock(_mutex);
	if (!_trace.Add(path, typeName) || !_traceFile)
	{
		return;
	}
	const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - _start).count();
	_traceFile << std::format("{:.3f}\t{}\t{}\n", seconds, LoadTrace::Line(path, typeName), found ? "found" : "null")
	           << std::flush;
}

std::string ObjectsMod::TypeName(Il2CppObject* type)
{
	if (type == nullptr)
	{
		return "(no type)";
	}
	{
		const std::scoped_lock lock(_mutex);
		if (const auto found = _typeNames.find(type); found != _typeNames.end())
		{
			return found->second;
		}
	}
	auto* klass = _managed.il2cpp_class_from_system_type(type);
	auto name = klass == nullptr ? std::string("(unknown type)") : _managed.Api().FullName(klass);
	const std::scoped_lock lock(_mutex);
	_typeNames.emplace(type, name);
	return name;
}

std::optional<std::string> ObjectsMod::OriginalText(PackTable table, Il2CppObject* self, Il2CppObject* type,
                                                    const MethodInfo* method)
{
	auto* asset = _original(self, _managed.String(std::string(PackTablePath(table)).c_str()), type, method);
	if (asset == nullptr)
	{
		return std::nullopt;
	}
	auto text = _managed.Invoke(_bindings.textAssetText, asset);
	if (!text || *text == nullptr)
	{
		return std::nullopt;
	}
	return _managed.Text(*text);
}

std::optional<std::string> ObjectsMod::MergeText(PackTable table, const std::string& text,
                                                 const PackTableMerger::TextLoader& loadOriginal)
{
	if (!_tables)
	{
		return std::nullopt;
	}
	auto merged = _tables->Merge(table, text, loadOriginal);
	for (const auto& error : _tables->TakeErrors())
	{
		_status.SetError(std::format("{} is not in the table {}: {}", error.pack, PackTablePath(table), error.message));
		const auto pack = std::ranges::find(_packs, error.pack, &Pack::id);
		PackError(pack != _packs.end() ? pack->folder : error.pack,
		          std::format("not in the table {}: {}", PackTablePath(table), error.message));
	}
	if (!merged)
	{
		return std::nullopt;
	}
	{
		const std::scoped_lock lock(_mutex);
		_merged = _tables->Merged();
		++_tableServed;
	}
	if (table == PackTable::HandPower || table == PackTable::Research)
	{
		for (const auto& spell : _tables->Spells())
		{
			spdlog::info("objects: the table {} has the level {} of {} (research {}, unlock {})", PackTablePath(table),
			             spell.level, spell.power, spell.researchKey, spell.unlock);
		}
	}
	// Log only the items of this table. Each other table logs its items when the game loads it.
	for (const auto& resource : _tables->MergedIn(table))
	{
		spdlog::info("objects: the table {} has the item {} (id {}, prefab {}, copy of {})", PackTablePath(table),
		             resource.name, resource.id, resource.prefabPath, resource.templatePrefabPath);
	}
	return merged;
}

std::optional<std::string> ObjectsMod::FilterTable(const std::string& path, const std::string& text)
{
	const auto table = PackTableOfPath(NormalResourcePath(path));
	if (!table || !PacksChangeTable(_packs, *table) || _api == nullptr)
	{
		return std::nullopt;
	}
	auto* type = _managed.TypeObject(_bindings.textAsset);
	return MergeText(*table, text, [&](PackTable other) { return OriginalText(other, _api, type, _bindings.load); });
}

moa::Result<Il2CppObject*> ObjectsMod::MergedTable(PackTable table, Il2CppObject* original)
{
	auto text = _managed.Invoke(_bindings.textAssetText, original);
	if (!text)
	{
		return text.GetError();
	}
	auto* type = _managed.TypeObject(_bindings.textAsset);
	auto merged = MergeText(table, _managed.Text(*text), [&](PackTable other) {
		return _api != nullptr ? OriginalText(other, _api, type, _bindings.load) : std::nullopt;
	});
	if (!merged)
	{
		return original;
	}
	auto* asset = _managed.il2cpp_object_new(_bindings.textAsset);
	if (asset == nullptr)
	{
		return moa::MakeError("the mod cannot make a text asset");
	}
	auto options = k_CreateNativeObject;
	if (auto made = _managed.Invoke(_bindings.textAssetMake, asset, {&options, _managed.String(merged->c_str())}); !made)
	{
		return made.GetError();
	}
	// The name of the asset is the name of the original asset, as the game can show it.
	if (auto name = _managed.Invoke(_bindings.getName, original); name && *name != nullptr)
	{
		(void)_managed.Invoke(_bindings.setName, asset, {*name});
	}
	Keep(asset);
	return asset;
}

moa::Result<Il2CppObject*> ObjectsMod::Holder()
{
	if (_holder != nullptr)
	{
		return _holder;
	}
	auto* holder = _managed.il2cpp_object_new(_bindings.gameObject);
	if (holder == nullptr)
	{
		return moa::MakeError("the mod cannot make a game object");
	}
	if (auto made = _managed.Invoke(_bindings.gameObjectMake, holder, {_managed.String("MOAR custom objects")}); !made)
	{
		return made.GetError();
	}
	// The holder is not active, so the prefabs in it do not start and are not in the world.
	bool active = false;
	if (auto result = _managed.Invoke(_bindings.setActive, holder, {&active}); !result)
	{
		return result.GetError();
	}
	if (auto result = _managed.Invoke(_bindings.dontDestroyOnLoad, nullptr, {holder}); !result)
	{
		return result.GetError();
	}
	Keep(holder);
	_holder = holder;
	return _holder;
}

moa::Result<Il2CppObject*> ObjectsMod::Prefab(const MergedResource& resource, Il2CppObject* self, Il2CppObject* type,
                                              const MethodInfo* method)
{
	const auto key = NormalResourcePath(resource.prefabPath);
	// A prefab that the mod could not build has a null entry. Unity then gives null for the path, as for a path that
	// does not exist.
	if (const auto found = _prefabs.find(key); found != _prefabs.end())
	{
		return found->second;
	}
	auto* source = _original(self, _managed.String(resource.templatePrefabPath.c_str()), type, method);
	if (source == nullptr)
	{
		return moa::MakeError("Unity has no prefab {}", resource.templatePrefabPath);
	}
	auto holder = Holder();
	if (!holder)
	{
		return holder.GetError();
	}
	auto transform = _managed.Invoke(_bindings.gameObjectTransform, *holder);
	if (!transform)
	{
		return transform.GetError();
	}
	auto copy = _managed.Invoke(_bindings.instantiate, nullptr, {source, *transform});
	if (!copy)
	{
		return copy.GetError();
	}
	if (*copy == nullptr)
	{
		return moa::MakeError("the copy of {} is null", resource.templatePrefabPath);
	}
	const auto name = resource.prefabPath.substr(resource.prefabPath.find_last_of('/') + 1);
	if (auto named = _managed.Invoke(_bindings.setName, *copy, {_managed.String(name.c_str())}); !named)
	{
		return named.GetError();
	}
	const auto& pack = _packs.at(resource.pack);
	const auto& packResource = pack.resources.at(resource.resource);
	if (const auto model = _models.find(ModelKey(pack, packResource)); model != _models.end())
	{
		auto built = _builder ? _builder->Build(*copy, packResource, model->second)
		                      : moa::Result<nlohmann::json>(moa::MakeError("the builder of the custom models is off"));
		if (!built)
		{
			// Fail closed: the game gets no prefab that the mod built only in part.
			(void)_managed.Invoke(_bindings.destroyImmediate, nullptr, {*copy});
			spdlog::error("objects: the mod did not build the prefab {}: {}", resource.prefabPath, built.GetError().message);
			_status.SetError(std::format("prefab {}: {}", resource.prefabPath, built.GetError().message));
			const std::scoped_lock lock(_mutex);
			_built[key] = {{"error", built.GetError().message}};
			_prefabs.emplace(key, nullptr);
			return static_cast<Il2CppObject*>(nullptr);
		}
		spdlog::info("objects: the mod built the model of {}: {}", resource.prefabPath, built->dump());
		const std::scoped_lock lock(_mutex);
		_built[key] = std::move(*built);
	}
	Keep(*copy);
	{
		const std::scoped_lock lock(_mutex);
		_prefabs.emplace(key, *copy);
	}
	spdlog::info("objects: the mod made the prefab {} from {}", resource.prefabPath, resource.templatePrefabPath);
	return *copy;
}

std::string ObjectsMod::ModelKey(const Pack& pack, const PackResource& resource)
{
	return std::format("{}/{}", pack.id, resource.name);
}

void ObjectsMod::LoadModels()
{
	for (auto& pack : _packs)
	{
		const auto found = _readers.find(pack.folder);
		const PackFileReader read = found != _readers.end() ? found->second : PackFileReader();
		std::erase_if(pack.resources, [&](const PackResource& resource) {
			if (!resource.mesh)
			{
				if (resource.model)
				{
					spdlog::warn("objects: {} has the model {} but no mesh; make the mesh with moa_modeltool new-object. "
					             "The resource looks like its template {}",
					             resource.name, *resource.model, resource.templateName);
				}
				return false;
			}
			if (!_builder)
			{
				spdlog::error("objects: the resource {} of the pack {} is not used: the custom models are off", resource.name,
				              pack.id);
				return true;
			}
			auto model = read ? LoadResourceModel(resource, read)
			                  : moa::Result<ResourceModel>(moa::MakeError("the pack has no reader of its files"));
			if (!model)
			{
				spdlog::error("objects: the resource {} of the pack {} is not used: {}", resource.name, pack.id,
				              model.GetError().message);
				_status.SetError(std::format("{}/{}: {}", pack.id, resource.name, model.GetError().message));
				PackError(pack.folder, std::format("{}: {}", resource.name, model.GetError().message));
				return true;
			}
			spdlog::info("objects: the resource {} of the pack {} has the mesh {} ({} vertices, {} sub-meshes, {} "
			             "materials)",
			             resource.name, pack.id, *resource.mesh, model->mesh.vertices.size(),
			             model->mesh.subMeshTriangles.size(), model->materials.size());
			_models.insert_or_assign(ModelKey(pack, resource), std::move(*model));
			return false;
		});
	}
}

void ObjectsMod::PackError(const std::string& key, std::string message)
{
	auto& errors = _packErrors[key];
	if (std::ranges::find(errors, message) == errors.end())
	{
		errors.push_back(std::move(message));
	}
}

void ObjectsMod::Keep(Il2CppObject* object)
{
	if (_managed.il2cpp_gchandle_new != nullptr)
	{
		_handles.push_back(_managed.il2cpp_gchandle_new(object, false));
	}
}

nlohmann::json ObjectsMod::PackStatus() const
{
	const std::scoped_lock lock(_mutex);
	nlohmann::json packs = nlohmann::json::array();
	for (const auto& pack : _packs)
	{
		packs.push_back(pack.folder);
	}
	return {{"packs", packs}, {"packErrors", _packErrors}};
}

nlohmann::json ObjectsMod::State(const std::string& filter) const
{
	const auto normalFilter = NormalResourcePath(filter);
	const std::scoped_lock lock(_mutex);
	nlohmann::json loads = nlohmann::json::array();
	for (const auto& [line, count] : _trace.Entries())
	{
		if (normalFilter.empty() || NormalResourcePath(line).find(normalFilter) != std::string::npos)
		{
			loads.push_back({{"load", line}, {"count", count}});
		}
	}
	nlohmann::json packs = nlohmann::json::array();
	for (const auto& pack : _packs)
	{
		packs.push_back({{"id", pack.id}, {"folder", pack.folder}, {"resources", pack.resources.size()}});
	}
	nlohmann::json resources = nlohmann::json::array();
	for (const auto& resource : _merged)
	{
		nlohmann::json item {{"name", resource.name}, {"id", resource.id}, {"prefab", resource.prefabPath}};
		if (const auto built = _built.find(NormalResourcePath(resource.prefabPath)); built != _built.end())
		{
			item["model"] = built->second;
		}
		resources.push_back(std::move(item));
	}
	return {
	    {"installed", _swap.Installed()},
	    {"testClone", _settings.testClone},
	    {"packs", packs},
	    {"packErrors", _packErrors},
	    {"resources", resources},
	    {"paths", _trace.Size()},
	    {"loads", _trace.Loads()},
	    {"otherThreadLoads", _otherThreadLoads},
	    {"tableServed", _tableServed},
	    {"prefabServed", _prefabServed},
	    {"prefabs", _prefabs.size()},
	    {"models", _models.size()},
	    {"builder", _builder.has_value()},
	    {"status", _status.Describe()},
	    {"matching", loads},
	};
}

} // namespace moa_mod::objects
