#include "Tools/AssetCompiler/GltfImporter.h"
#include "Tests/AssetCompiler/DracoTestFixture.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "glTF importer test failed: " << message << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	static constexpr std::string_view Source = R"json({
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

	const std::filesystem::path path = std::filesystem::temp_directory_path() / "swim-fastgltf-import-test.gltf";
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(Source.data(), static_cast<std::streamsize>(Source.size()));
	}

	Swim::AssetCompiler::GltfImporter importer;
	const Swim::AssetCompiler::GltfImportResult result = importer.Import(path);
	std::error_code ignored;
	std::filesystem::remove(path, ignored);

	Require(static_cast<bool>(result), result.Error.Message.c_str());
	Require(result.Model.Meshes.size() == 1, "one mesh imported");
	Require(result.Model.Meshes[0].Primitives.size() == 1, "one primitive imported");
	Require(result.Model.Meshes[0].Primitives[0].Vertices.size() == 3, "three vertices imported");
	Require(result.Model.Meshes[0].Primitives[0].Indices.size() == 3, "three indices imported");
	Require(result.Model.Meshes[0].Primitives[0].Indices[0] == 0, "first index imported");
	Require(result.Model.Meshes[0].Primitives[0].Indices[1] == 1, "second index imported");
	Require(result.Model.Meshes[0].Primitives[0].Indices[2] == 2, "third index imported");
	Require(result.Model.Materials.size() == 1, "one material imported");
	Require(result.Model.Materials[0].BaseColorFactor[1] == 0.5f, "material base color imported");
	Require(result.Model.Nodes.size() == 1, "one node imported");
	Require(result.Model.Nodes[0].MeshIndex == std::optional<std::uint32_t>(0), "node mesh index imported");
	Require(result.Model.Roots.size() == 1 && result.Model.Roots[0] == 0, "default scene root imported");

	static constexpr std::string_view TextureExtensionSource = R"json({
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

	const std::filesystem::path textureExtensionPath = std::filesystem::temp_directory_path() / "swim-fastgltf-texture-extension-test.gltf";
	{
		std::ofstream file(textureExtensionPath, std::ios::binary | std::ios::trunc);
		file.write(TextureExtensionSource.data(), static_cast<std::streamsize>(TextureExtensionSource.size()));
	}
	const Swim::AssetCompiler::GltfImportResult textureExtensionResult = importer.Import(textureExtensionPath);
	std::filesystem::remove(textureExtensionPath, ignored);
	Require(static_cast<bool>(textureExtensionResult), textureExtensionResult.Error.Message.c_str());
	Require(textureExtensionResult.Model.Images.size() == 2, "Basis/WebP source images imported");
	Require(textureExtensionResult.Model.Textures.size() == 2, "Basis/WebP extension textures imported");
	Require(textureExtensionResult.Model.Images[0].MimeType == Swim::AssetCompiler::SourceImageMimeType::Ktx2, "KHR_texture_basisu image remains KTX2 source data");
	Require(textureExtensionResult.Model.Images[1].MimeType == Swim::AssetCompiler::SourceImageMimeType::WebP, "EXT_texture_webp image remains WebP source data");
	Require(textureExtensionResult.Model.Textures[0].ImageIndex == std::optional<std::uint32_t>(0), "KHR_texture_basisu source index imported");
	Require(textureExtensionResult.Model.Textures[1].ImageIndex == std::optional<std::uint32_t>(1), "EXT_texture_webp source index imported");

	const std::filesystem::path dracoRoot = std::filesystem::temp_directory_path() / "swim-fastgltf-draco-import-test";
	std::filesystem::remove_all(dracoRoot, ignored);
	const std::filesystem::path dracoPath = Swim::Tests::WriteDracoTriangleFixture(dracoRoot, "Triangle");
	const Swim::AssetCompiler::GltfImportResult dracoResult = importer.Import(dracoPath);
	Require(static_cast<bool>(dracoResult), dracoResult.Error.Message.c_str());
	Require(dracoResult.Model.Meshes.size() == 1, "one Draco mesh imported");
	Require(dracoResult.Model.Meshes[0].Primitives.size() == 1, "one Draco primitive imported");
	const auto& dracoPrimitive = dracoResult.Model.Meshes[0].Primitives[0];
	Require(dracoPrimitive.Vertices.size() == 3, "Draco primitive decoded three vertices");
	Require(dracoPrimitive.Indices.size() == 3, "Draco primitive decoded one triangle");
	Require(dracoPrimitive.Vertices[0].HasNormal, "Draco NORMAL attribute decoded");
	Require(dracoPrimitive.Vertices[0].HasTexCoord0, "Draco TEXCOORD_0 attribute decoded");
	Require(dracoPrimitive.Bounds.Max[0] > 0.9f && dracoPrimitive.Bounds.Max[1] > 0.9f, "Draco position bounds decoded");
	std::filesystem::remove_all(dracoRoot, ignored);
	return 0;
}
