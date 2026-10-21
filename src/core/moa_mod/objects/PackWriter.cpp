/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/objects/PackWriter.h"

#include <cstdint>

#include <algorithm>
#include <type_traits>
#include <variant>

#include <nlohmann/json.hpp>

namespace moa_mod::objects
{
namespace
{

using Json = nlohmann::ordered_json;

constexpr int k_Indent = 2;

[[nodiscard]] Json PivotValue(const PackResource& resource)
{
	switch (resource.pivot)
	{
	case PivotMode::BottomCentre:
		return "bottom-centre";
	case PivotMode::Centre:
		return "centre";
	case PivotMode::Origin:
		return "origin";
	case PivotMode::Explicit:
		break;
	}
	return Json::array({resource.pivotPoint[0], resource.pivotPoint[1], resource.pivotPoint[2]});
}

[[nodiscard]] const char* ColliderValue(ColliderMode mode)
{
	switch (mode)
	{
	case ColliderMode::Box:
		return "box";
	case ColliderMode::Mesh:
		return "mesh";
	case ColliderMode::Template:
		break;
	}
	return "template";
}

[[nodiscard]] Json MaterialValue(const PackMaterial& material)
{
	Json value = Json::object();
	if (!material.name.empty())
	{
		value["name"] = material.name;
	}
	for (const auto& [key, file] :
	     {std::pair {"base", &material.base}, std::pair {"normal", &material.normal}, std::pair {"mask", &material.mask}})
	{
		if (*file)
		{
			value[key] = **file;
		}
	}
	return value;
}

void WriteTypedValues(Json& value, const PackResource& resource);

[[nodiscard]] Json ResourceValue(const PackResource& resource)
{
	Json value = Json::object();
	value["name"] = resource.name;
	value["template"] = resource.templateName;
	if (resource.prefab != resource.name)
	{
		value["prefab"] = resource.prefab;
	}
	if (resource.id)
	{
		value["id"] = *resource.id;
	}
	if (resource.model)
	{
		value["model"] = *resource.model;
	}
	if (resource.mesh)
	{
		value["mesh"] = *resource.mesh;
	}
	if (!resource.materials.empty())
	{
		Json materials = Json::array();
		for (const auto& material : resource.materials)
		{
			materials.push_back(MaterialValue(material));
		}
		value["materials"] = std::move(materials);
	}
	value["scale"] = resource.scale;
	value["pivot"] = PivotValue(resource);
	value["collider"] = ColliderValue(resource.collider);
	if (!resource.row.empty())
	{
		Json row = Json::object();
		for (const auto& [key, text] : resource.row)
		{
			row[key] = text;
		}
		value["row"] = std::move(row);
	}
	if (!resource.credits.Empty())
	{
		value["credits"] = Json {{"title", resource.credits.title},
		                         {"author", resource.credits.author},
		                         {"source", resource.credits.source},
		                         {"licence", resource.credits.licence}};
	}
	if (resource.grip && !PackPointKey(resource.kind).empty())
	{
		value[std::string(PackPointKey(resource.kind))] =
		    Json::array({(*resource.grip)[0], (*resource.grip)[1], (*resource.grip)[2]});
	}
	if (resource.upZ)
	{
		value["up"] = "z";
	}
	WriteTypedValues(value, resource);
	return value;
}

/// A spell has no model: only its power, its id and its typed values.
[[nodiscard]] Json SpellValue(const PackResource& spell)
{
	Json value = Json::object();
	value["power"] = spell.templateName;
	if (spell.id)
	{
		value["id"] = *spell.id;
	}
	WriteTypedValues(value, spell);
	return value;
}

[[nodiscard]] Json ItemValue(const PackResource& item)
{
	return item.kind == PackKind::Spell ? SpellValue(item) : ResourceValue(item);
}

} // namespace

namespace
{

void WriteTypedValues(Json& value, const PackResource& resource)
{
	// The typed values go in the order of the fields of the kind.
	for (const auto& spec : PackFields(resource.kind))
	{
		const auto found = std::ranges::find(resource.values, spec.key, &std::pair<std::string, KnackValue>::first);
		if (found == resource.values.end())
		{
			continue;
		}
		std::visit(
		    [&](const auto& item) {
			    using T = std::decay_t<decltype(item)>;
			    if constexpr (std::is_same_v<T, double>)
			    {
				    if (spec.type == PackFieldType::Integer)
				    {
					    value[std::string(spec.key)] = static_cast<int64_t>(item);
					    return;
				    }
			    }
			    value[std::string(spec.key)] = item;
		    },
		    found->second);
	}
}

} // namespace

std::string WritePack(const Pack& pack)
{
	Json value = Json::object();
	const bool typed =
	    std::ranges::any_of(pack.resources, [](const PackResource& item) { return item.kind != PackKind::Resource; });
	value["format"] = typed ? k_PackFormatTyped : k_PackFormat;
	value["id"] = pack.id;
	value["name"] = pack.name;
	if (!pack.version.empty())
	{
		value["version"] = pack.version;
	}
	if (!pack.description.empty())
	{
		value["description"] = pack.description;
	}
	for (const auto kind : {PackKind::Resource, PackKind::Throwable, PackKind::Weapon, PackKind::Spell})
	{
		Json list = Json::array();
		for (const auto& resource : pack.resources)
		{
			if (resource.kind == kind)
			{
				list.push_back(ItemValue(resource));
			}
		}
		// Format 1 always has the list of the resources; format 2 has only the lists with items.
		if (!list.empty() || (kind == PackKind::Resource && !typed))
		{
			value[std::string(PackListName(kind))] = std::move(list);
		}
	}
	return value.dump(k_Indent) + "\n";
}

bool PutResource(Pack& pack, PackResource resource)
{
	const auto found = std::ranges::find_if(
	    pack.resources, [&resource](const PackResource& other) { return EqualsIgnoreCase(other.name, resource.name); });
	if (found != pack.resources.end())
	{
		*found = std::move(resource);
		std::ranges::stable_sort(pack.resources, {}, &PackResource::kind);
		return true;
	}
	pack.resources.push_back(std::move(resource));
	// The items stay in the order of the lists of the file, so that a write and a parse give the same order.
	std::ranges::stable_sort(pack.resources, {}, &PackResource::kind);
	return false;
}

} // namespace moa_mod::objects
