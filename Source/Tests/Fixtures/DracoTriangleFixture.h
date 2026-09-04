#pragma once

#include <draco/compression/encode.h>
#include <draco/core/encoder_buffer.h>
#include <draco/mesh/triangle_soup_mesh_builder.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Swim::Tests
{

	inline std::filesystem::path WriteDracoTriangleFixture(const std::filesystem::path& root, std::string stem)
	{
		std::filesystem::create_directories(root);

		draco::TriangleSoupMeshBuilder builder;
		builder.Start(1);
		const int positionAttribute = builder.AddAttribute(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32);
		const int normalAttribute = builder.AddAttribute(draco::GeometryAttribute::NORMAL, 3, draco::DT_FLOAT32);
		const int texCoordAttribute = builder.AddAttribute(draco::GeometryAttribute::TEX_COORD, 2, draco::DT_FLOAT32);
		if (positionAttribute < 0 || normalAttribute < 0 || texCoordAttribute < 0)
		{
			throw std::runtime_error("could not create Draco test attributes");
		}

		constexpr std::uint32_t PositionUniqueId = 7;
		constexpr std::uint32_t NormalUniqueId = 11;
		constexpr std::uint32_t TexCoordUniqueId = 13;
		builder.SetAttributeUniqueId(positionAttribute, PositionUniqueId);
		builder.SetAttributeUniqueId(normalAttribute, NormalUniqueId);
		builder.SetAttributeUniqueId(texCoordAttribute, TexCoordUniqueId);

		const std::array<float, 3> p0{ 0.0f, 0.0f, 0.0f };
		const std::array<float, 3> p1{ 1.0f, 0.0f, 0.0f };
		const std::array<float, 3> p2{ 0.0f, 1.0f, 0.0f };
		const std::array<float, 3> normal{ 0.0f, 0.0f, 1.0f };
		const std::array<float, 2> uv0{ 0.0f, 0.0f };
		const std::array<float, 2> uv1{ 1.0f, 0.0f };
		const std::array<float, 2> uv2{ 0.0f, 1.0f };
		builder.SetAttributeValuesForFace(positionAttribute, draco::FaceIndex(0), p0.data(), p1.data(), p2.data());
		builder.SetAttributeValuesForFace(normalAttribute, draco::FaceIndex(0), normal.data(), normal.data(), normal.data());
		builder.SetAttributeValuesForFace(texCoordAttribute, draco::FaceIndex(0), uv0.data(), uv1.data(), uv2.data());

		std::unique_ptr<draco::Mesh> mesh = builder.Finalize();
		if (!mesh)
		{
			throw std::runtime_error("could not finalize Draco test mesh");
		}

		draco::Encoder encoder;
		encoder.SetSpeedOptions(10, 10);
		draco::EncoderBuffer encoded;
		const draco::Status status = encoder.EncodeMeshToBuffer(*mesh, &encoded);
		if (!status.ok())
		{
			throw std::runtime_error("could not encode Draco test mesh: " + status.error_msg_string());
		}

		const std::filesystem::path binaryPath = root / (stem + ".drc");
		{
			std::ofstream file(binaryPath, std::ios::binary | std::ios::trunc);
			file.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
			if (!file)
			{
				throw std::runtime_error("could not write Draco test payload");
			}
		}

		std::ostringstream json;
		json << R"json({
			"asset":{"version":"2.0"},
			"extensionsUsed":["KHR_draco_mesh_compression"],
			"extensionsRequired":["KHR_draco_mesh_compression"],
			"scene":0,
			"scenes":[{"nodes":[0]}],
			"nodes":[{"name":"DracoTriangleNode","mesh":0}],
			"meshes":[{"name":"DracoTriangleMesh","primitives":[{
				"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},
				"indices":3,
				"extensions":{"KHR_draco_mesh_compression":{"bufferView":0,"attributes":{"POSITION":)json"
			<< PositionUniqueId << R"json(,"NORMAL":)json" << NormalUniqueId << R"json(,"TEXCOORD_0":)json" << TexCoordUniqueId << R"json(}}}
			}]}],
			"buffers":[{"byteLength":)json" << encoded.size() << R"json(,"uri":")json" << binaryPath.filename().generic_string() << R"json("}],
			"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":)json" << encoded.size() << R"json(}],
			"accessors":[
				{"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
				{"componentType":5126,"count":3,"type":"VEC3"},
				{"componentType":5126,"count":3,"type":"VEC2"},
				{"componentType":5125,"count":3,"type":"SCALAR"}
			]
		})json";

		const std::filesystem::path gltfPath = root / (stem + ".gltf");
		{
			std::ofstream file(gltfPath, std::ios::binary | std::ios::trunc);
			const std::string text = json.str();
			file.write(text.data(), static_cast<std::streamsize>(text.size()));
			if (!file)
			{
				throw std::runtime_error("could not write Draco glTF test fixture");
			}
		}
		return gltfPath;
	}

}
