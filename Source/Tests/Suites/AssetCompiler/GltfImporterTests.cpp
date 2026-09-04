#include "Tests/Fixtures/DracoTriangleFixture.h"
#include "Tests/Framework/Test.h"
#include "Tools/AssetCompiler/GltfImporter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace
{

	// Writes a glTF fixture to a uniquely named scratch file and removes it once
	// the case finishes, including when a requirement aborts the case.
	class ScopedGltfFixture
	{

	public:

		ScopedGltfFixture(std::string_view fileName, std::string_view json)
			: path(std::filesystem::temp_directory_path() / fileName)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(json.data(), static_cast<std::streamsize>(json.size()));
		}

		~ScopedGltfFixture()
		{
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

		ScopedGltfFixture(const ScopedGltfFixture&) = delete;
		ScopedGltfFixture& operator=(const ScopedGltfFixture&) = delete;

		const std::filesystem::path& Path() const
		{
			return path;
		}

	private:

		std::filesystem::path path;

	};

	constexpr std::string_view TriangleGltf = R"json({
		"asset":{"version":"2.0"},
		"extensionsUsed":["KHR_texture_transform"],
		"extensionsRequired":["KHR_texture_transform"],
		"scene":0,
		"scenes":[{"nodes":[0]}],
		"nodes":[{"name":"TriangleNode","mesh":0}],
		"materials":[{"name":"TriangleMaterial","pbrMetallicRoughness":{"baseColorFactor":[0.25,0.5,0.75,1.0],"metallicFactor":0.2,"roughnessFactor":0.8}}],
		"meshes":[{"name":"TriangleMesh","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],
		"buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"}],
		"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
		"accessors":[
			{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
			{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}
		]
	})json";

	constexpr std::string_view TextureExtensionGltf = R"json({
		"asset":{"version":"2.0"},
		"extensionsUsed":["KHR_texture_basisu","EXT_texture_webp"],
		"extensionsRequired":["KHR_texture_basisu","EXT_texture_webp"],
		"images":[
			{"name":"BasisImage","mimeType":"image/ktx2","uri":"data:image/ktx2;base64,q0tUWCAyMLsNChoK"},
			{"name":"WebPImage","mimeType":"image/webp","uri":"data:image/webp;base64,UklGRiAAAABXRUJQVlA4TBMAAAAvAQAAEA8w//sfD/wPBxWI6H8AAA=="}
		],
		"textures":[
			{"name":"BasisTexture","extensions":{"KHR_texture_basisu":{"source":0}}},
			{"name":"WebPTexture","extensions":{"EXT_texture_webp":{"source":1}}}
		]
	})json";

}

SWIM_TEST("AssetCompiler.GltfImporter", "ImportsGeometryMaterialsAndSceneRoots")
{
	const ScopedGltfFixture fixture("swim-fastgltf-import-test.gltf", TriangleGltf);

	Swim::AssetCompiler::GltfImporter importer;
	const Swim::AssetCompiler::GltfImportResult result = importer.Import(fixture.Path());
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(result), result.Error.Message);

	SWIM_REQUIRE(result.Model.Meshes.size() == 1);
	SWIM_REQUIRE(result.Model.Meshes[0].Primitives.size() == 1);

	const auto& primitive = result.Model.Meshes[0].Primitives[0];
	SWIM_CHECK_EQUAL(primitive.Vertices.size(), std::size_t{ 3 });
	SWIM_REQUIRE(primitive.Indices.size() == 3);
	SWIM_CHECK_EQUAL(primitive.Indices[0], 0u);
	SWIM_CHECK_EQUAL(primitive.Indices[1], 1u);
	SWIM_CHECK_EQUAL(primitive.Indices[2], 2u);

	SWIM_REQUIRE(result.Model.Materials.size() == 1);
	SWIM_CHECK_EQUAL(result.Model.Materials[0].BaseColorFactor[1], 0.5f);

	SWIM_REQUIRE(result.Model.Nodes.size() == 1);
	SWIM_CHECK(result.Model.Nodes[0].MeshIndex == std::optional<std::uint32_t>(0));
	SWIM_REQUIRE(result.Model.Roots.size() == 1);
	SWIM_CHECK_EQUAL(result.Model.Roots[0], 0u);
}

SWIM_TEST("AssetCompiler.GltfImporter", "PreservesBasisAndWebPTextureExtensionSources")
{
	const ScopedGltfFixture fixture("swim-fastgltf-texture-extension-test.gltf", TextureExtensionGltf);

	Swim::AssetCompiler::GltfImporter importer;
	const Swim::AssetCompiler::GltfImportResult result = importer.Import(fixture.Path());
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(result), result.Error.Message);

	SWIM_REQUIRE(result.Model.Images.size() == 2);
	SWIM_REQUIRE(result.Model.Textures.size() == 2);
	SWIM_CHECK(result.Model.Images[0].MimeType == Swim::AssetCompiler::SourceImageMimeType::Ktx2);
	SWIM_CHECK(result.Model.Images[1].MimeType == Swim::AssetCompiler::SourceImageMimeType::WebP);
	SWIM_CHECK(result.Model.Textures[0].ImageIndex == std::optional<std::uint32_t>(0));
	SWIM_CHECK(result.Model.Textures[1].ImageIndex == std::optional<std::uint32_t>(1));
}

SWIM_TEST("AssetCompiler.GltfImporter", "DecodesDracoCompressedPrimitives")
{
	const std::filesystem::path dracoRoot = std::filesystem::temp_directory_path() / "swim-fastgltf-draco-import-test";
	std::error_code ignored;
	std::filesystem::remove_all(dracoRoot, ignored);

	const std::filesystem::path dracoPath = Swim::Tests::WriteDracoTriangleFixture(dracoRoot, "Triangle");

	Swim::AssetCompiler::GltfImporter importer;
	const Swim::AssetCompiler::GltfImportResult result = importer.Import(dracoPath);
	std::filesystem::remove_all(dracoRoot, ignored);

	SWIM_REQUIRE_MESSAGE(static_cast<bool>(result), result.Error.Message);
	SWIM_REQUIRE(result.Model.Meshes.size() == 1);
	SWIM_REQUIRE(result.Model.Meshes[0].Primitives.size() == 1);

	const auto& primitive = result.Model.Meshes[0].Primitives[0];
	SWIM_REQUIRE(primitive.Vertices.size() == 3);
	SWIM_CHECK_EQUAL(primitive.Indices.size(), std::size_t{ 3 });
	SWIM_CHECK(primitive.Vertices[0].HasNormal);
	SWIM_CHECK(primitive.Vertices[0].HasTexCoord0);
	SWIM_CHECK(primitive.Bounds.Max[0] > 0.9f);
	SWIM_CHECK(primitive.Bounds.Max[1] > 0.9f);
}
