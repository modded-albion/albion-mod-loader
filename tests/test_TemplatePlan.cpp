/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cstddef>

#include <map>
#include <string>
#include <vector>

#include <core/TemplatePlan.h>
#include <gtest/gtest.h>
#include <unitymesh/RuntimeMesh.h>

using namespace moa_mod::objects;

namespace
{

/// A box of 2 x 1 x 4 metres that stands on the ground, as `MakeRuntimeMesh` gives it with the pivot at the bottom.
moa::unitymesh::RuntimeMesh MakeBox()
{
	moa::unitymesh::RuntimeMesh mesh;
	mesh.name = "Box";
	mesh.vertices = {{-1.0f, 0.0f, -2.0f}, {1.0f, 0.0f, -2.0f}, {1.0f, 1.0f, 2.0f}, {-1.0f, 1.0f, 2.0f}};
	mesh.subMeshTriangles = {{0, 1, 2, 0, 2, 3}};
	mesh.subMeshMaterials = {0};
	mesh.boundsCentre = {0.0f, 0.5f, 0.0f};
	mesh.boundsExtents = {1.0f, 0.5f, 2.0f};
	return mesh;
}

std::vector<std::byte> Bytes(std::string_view text)
{
	std::vector<std::byte> bytes;
	for (const char c : text)
	{
		bytes.push_back(static_cast<std::byte>(c));
	}
	return bytes;
}

std::vector<std::byte> FakePng()
{
	return Bytes("\x89PNG\r\n\x1a\nrest of the image");
}

/// A pack folder in memory.
struct FakeFolder
{
	std::map<std::string, std::vector<std::byte>> files;

	[[nodiscard]] PackFileReader Reader() const
	{
		return [this](const std::string& path) -> moa::Result<std::vector<std::byte>> {
			const auto found = files.find(path);
			if (found == files.end())
			{
				return moa::MakeError("no file");
			}
			return found->second;
		};
	}
};

PackResource MakeResource()
{
	PackResource resource;
	resource.name = "BrokenCoffin";
	resource.templateName = "Wood";
	resource.prefab = "BrokenCoffin";
	resource.mesh = "models/coffin.moamesh";
	resource.materials = {
	    PackMaterial {.name = "Coffin", .base = "models/base.png", .normal = std::nullopt, .mask = "models/mask.png"}};
	return resource;
}

FakeFolder MakeFolder()
{
	FakeFolder folder;
	folder.files["models/coffin.moamesh"] = *moa::unitymesh::WriteRuntimeMesh(MakeBox());
	folder.files["models/base.png"] = FakePng();
	folder.files["models/mask.png"] = FakePng();
	return folder;
}

} // namespace

TEST(ObjectsTemplatePlan, LoadsTheMeshAndTheImages)
{
	const auto folder = MakeFolder();
	auto model = LoadResourceModel(MakeResource(), folder.Reader());
	ASSERT_TRUE(model) << model.GetError().message;
	EXPECT_EQ(model->mesh.vertices.size(), 4u);
	ASSERT_EQ(model->materials.size(), 1u);
	EXPECT_EQ(model->materials[0].name, "Coffin");
	EXPECT_EQ(model->materials[0].base, FakePng());
	EXPECT_TRUE(model->materials[0].normal.empty());
	EXPECT_EQ(model->materials[0].mask, FakePng());

	const auto slots = TextureSlots(model->materials[0]);
	ASSERT_EQ(slots.size(), 2u);
	EXPECT_EQ(slots[0].property, "_BaseMap");
	EXPECT_FALSE(slots[0].linear);
	EXPECT_EQ(slots[1].property, "_MaskMap");
	EXPECT_TRUE(slots[1].linear);
}

TEST(ObjectsTemplatePlan, RefusesBadFiles)
{
	auto noMesh = MakeResource();
	noMesh.mesh.reset();
	EXPECT_FALSE(LoadResourceModel(noMesh, MakeFolder().Reader()));

	auto missing = MakeFolder();
	missing.files.erase("models/mask.png");
	auto model = LoadResourceModel(MakeResource(), missing.Reader());
	ASSERT_FALSE(model);
	EXPECT_NE(model.GetError().message.find("models/mask.png"), std::string::npos);

	auto notPng = MakeFolder();
	notPng.files["models/base.png"] = Bytes("GIF89a");
	model = LoadResourceModel(MakeResource(), notPng.Reader());
	ASSERT_FALSE(model);
	EXPECT_NE(model.GetError().message.find("not a PNG"), std::string::npos);

	auto badMesh = MakeFolder();
	badMesh.files["models/coffin.moamesh"] = Bytes("MOARMESH");
	EXPECT_FALSE(LoadResourceModel(MakeResource(), badMesh.Reader()));

	auto twoMaterials = MakeResource();
	twoMaterials.materials.push_back({});
	model = LoadResourceModel(twoMaterials, MakeFolder().Reader());
	ASSERT_FALSE(model);
	EXPECT_NE(model.GetError().message.find("2 materials"), std::string::npos);

	// No materials: each sub-mesh keeps the material of the template.
	auto noMaterials = MakeResource();
	noMaterials.materials.clear();
	model = LoadResourceModel(noMaterials, MakeFolder().Reader());
	ASSERT_TRUE(model) << model.GetError().message;
	EXPECT_TRUE(model->materials.empty());
}

TEST(ObjectsTemplatePlan, FitsTheBoxAndTheGrip)
{
	const auto box = MakeBox();
	const auto fit = FitBox(box, 2.0f);
	EXPECT_EQ(fit.centre, (std::array {0.0f, 1.0f, 0.0f}));
	EXPECT_EQ(fit.size, (std::array {4.0f, 2.0f, 8.0f}));
	EXPECT_EQ(GripPoint(box, 2.0f), (std::array {0.0f, 2.0f, 0.0f}));

	// A flat mesh still gets a box with a volume.
	auto flat = box;
	flat.boundsExtents[1] = 0.0f;
	EXPECT_FLOAT_EQ(FitBox(flat, 1.0f).size[1], k_MinColliderSide);
}

TEST(ObjectsTemplatePlan, ChecksTheIndices)
{
	const std::vector<uint32_t> indices {0, 1, 2, 2, 3, 0};
	auto managed = ManagedIndices(indices, 4);
	ASSERT_TRUE(managed) << managed.GetError().message;
	EXPECT_EQ(*managed, (std::vector<int32_t> {0, 1, 2, 2, 3, 0}));
	EXPECT_FALSE(ManagedIndices(indices, 3));
	EXPECT_FALSE(ManagedIndices(std::vector<uint32_t> {0, 1}, 4));
}

TEST(ObjectsTemplatePlan, KnowsAPngSignature)
{
	EXPECT_TRUE(IsPng(FakePng()));
	EXPECT_FALSE(IsPng(Bytes("\x89PNG")));
	EXPECT_FALSE(IsPng({}));
}

TEST(ObjectsTemplatePlan, UsesThePointOfThePackOrTheTop)
{
	const auto box = MakeBox();
	PackResource item;
	item.scale = 2.0f;
	EXPECT_EQ(ItemPoint(item, box), GripPoint(box, 2.0f));
	item.grip = std::array {0.5f, 1.0f, -0.25f};
	EXPECT_EQ(ItemPoint(item, box), (std::array {0.5f, 1.0f, -0.25f}));
}
