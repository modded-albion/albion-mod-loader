/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <string>

#include <core/Pack.h>

namespace moa_mod::objects
{

/// Write a pack as the text of a `pack.json` file, with two spaces for each level. A pack with only carried resources
/// gets format 1; a pack with typed items gets format 2. The keys are in a fixed order. The writer leaves out a value
/// that is the default of the parser, so that `ParsePack` of the text gives the same pack. The folder of the pack is
/// not in the file.
[[nodiscard]] std::string WritePack(const Pack& pack);

/// Put a resource into a pack. A resource with the same name (the case of ASCII letters is ignored) changes to the new
/// resource and keeps its place. Else the function adds the resource at the end. Return true if the resource replaced
/// another resource. The writer puts each item in the list of its kind.
bool PutResource(Pack& pack, PackResource resource);

} // namespace moa_mod::objects
