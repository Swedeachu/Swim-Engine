#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Testing
{
	// A small skinned, morphing, animated glTF written to a scratch file:
	//
	//   node 0 "Armature" (translation 0,0,1; not a joint)
	//     node 1 "Hip" (joint)          <- skin joints listed children first:
	//       node 2 "Spine" (joint)          [Head, Spine, Hip]
	//         node 3 "Head" (joint)
	//   node 4 "Body" (mesh 0, skin 0)
	//
	// Node-level morph weights are left out: the pinned fastgltf 0.9 rejects every
	// node "weights" array as InvalidGltf (an inverted error check in its node
	// parser), so tests set them on the intermediate model instead.
	//
	// Mesh 0: four vertices, two triangles, JOINTS_0 (u16) + WEIGHTS_0 (float,
	// deliberately unnormalized and unsorted), one POSITION morph target, mesh
	// weights [0.5]. Animation "Wave": linear rotation on Spine, step translation
	// on Hip, cubic-spline morph weights on Body, linear scale on Armature.
	class SkinnedGltfFixture
	{
	  public:
		// Vertex i uses skin joints JointsOf(i) with weights WeightsOf(i).
		static constexpr std::array<std::array<std::uint16_t, 4>, 4> Joints{ { { 2, 1, 0, 0 }, { 1, 0, 0, 0 }, { 0, 1, 2, 0 },
			{ 2, 0, 0, 0 } } };
		static constexpr std::array<std::array<float, 4>, 4> Weights{ { { 0.2f, 0.6f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f },
			{ 0.1f, 0.1f, 0.2f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } } };
		static constexpr std::array<std::array<float, 3>, 4> Positions{ { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } } };
		static constexpr std::array<std::array<float, 3>, 4> MorphDeltas{ { { 0, 0, 0.5f }, { 0, 0, 0 }, { 0.25f, 0, 0 }, { 0, 0, 1 } } };

		explicit SkinnedGltfFixture(std::string_view fileName) : path(std::filesystem::temp_directory_path() / fileName)
		{
			const std::string json = BuildJson();
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(json.data(), static_cast<std::streamsize>(json.size()));
		}

		~SkinnedGltfFixture()
		{
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

		SkinnedGltfFixture(const SkinnedGltfFixture&) = delete;
		SkinnedGltfFixture& operator=(const SkinnedGltfFixture&) = delete;

		const std::filesystem::path& Path() const { return path; }

		// Inverse bind matrix of skin joint j (column-major): a translation by -j on y.
		static std::array<float, 16> InverseBind(std::size_t joint)
		{
			return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -static_cast<float>(joint), 0, 1 };
		}

	  private:
		struct View
		{
			std::size_t Offset = 0;
			std::size_t Length = 0;
		};

		static std::string Base64(const std::vector<unsigned char>& bytes)
		{
			static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string out;
			for (std::size_t i = 0; i < bytes.size(); i += 3)
			{
				const std::uint32_t b0 = bytes[i];
				const std::uint32_t b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0u;
				const std::uint32_t b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0u;
				const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
				out += Alphabet[(triple >> 18) & 63];
				out += Alphabet[(triple >> 12) & 63];
				out += i + 1 < bytes.size() ? Alphabet[(triple >> 6) & 63] : '=';
				out += i + 2 < bytes.size() ? Alphabet[triple & 63] : '=';
			}
			return out;
		}

		template <typename T> static View Append(std::vector<unsigned char>& buffer, const std::vector<T>& values)
		{
			while (buffer.size() % 4 != 0)
			{
				buffer.push_back(0);
			}
			View view{ buffer.size(), values.size() * sizeof(T) };
			buffer.resize(buffer.size() + view.Length);
			std::memcpy(buffer.data() + view.Offset, values.data(), view.Length);
			return view;
		}

		static std::string BuildJson()
		{
			std::vector<unsigned char> buffer;
			std::vector<float> positions;
			std::vector<float> deltas;
			std::vector<std::uint16_t> joints;
			std::vector<float> weights;
			for (std::size_t v = 0; v < 4; ++v)
			{
				positions.insert(positions.end(), Positions[v].begin(), Positions[v].end());
				deltas.insert(deltas.end(), MorphDeltas[v].begin(), MorphDeltas[v].end());
				joints.insert(joints.end(), Joints[v].begin(), Joints[v].end());
				weights.insert(weights.end(), Weights[v].begin(), Weights[v].end());
			}
			std::vector<std::uint16_t> indices{ 0, 1, 2, 0, 2, 3 };
			std::vector<float> inverseBinds;
			for (std::size_t joint = 0; joint < 3; ++joint)
			{
				const auto matrix = InverseBind(joint);
				inverseBinds.insert(inverseBinds.end(), matrix.begin(), matrix.end());
			}
			const std::vector<float> times{ 0.0f, 1.0f };
			// Spine rotation: identity -> 90 degrees about z.
			const float h = 0.70710678f;
			const std::vector<float> rotations{ 0, 0, 0, 1, 0, 0, h, h };
			const std::vector<float> translations{ 0, 0, 0, 0, 2, 0 };
			// Cubic spline morph weights: (in, value, out) per key, one target.
			const std::vector<float> morphWeights{ 0, 0, 1, 0, 1, 0 };
			const std::vector<float> scales{ 1, 1, 1, 2, 2, 2 };

			const View vPositions = Append(buffer, positions);
			const View vDeltas = Append(buffer, deltas);
			const View vJoints = Append(buffer, joints);
			const View vWeights = Append(buffer, weights);
			const View vIndices = Append(buffer, indices);
			const View vInverse = Append(buffer, inverseBinds);
			const View vTimes = Append(buffer, times);
			const View vRotations = Append(buffer, rotations);
			const View vTranslations = Append(buffer, translations);
			const View vMorph = Append(buffer, morphWeights);
			const View vScales = Append(buffer, scales);

			const auto view = [](const View& v)
			{
				return "{\"buffer\":0,\"byteOffset\":" + std::to_string(v.Offset) + ",\"byteLength\":" + std::to_string(v.Length) + "}";
			};
			std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,4]}],)";
			json += R"("nodes":[)"
					R"({"name":"Armature","translation":[0,0,1],"children":[1]},)"
					R"({"name":"Hip","children":[2]},)"
					R"({"name":"Spine","translation":[0,1,0],"children":[3]},)"
					R"({"name":"Head","translation":[0,1,0]},)"
					R"({"name":"Body","mesh":0,"skin":0}],)";
			json += R"("skins":[{"name":"Rig","joints":[3,2,1],"inverseBindMatrices":5,"skeleton":1}],)";
			json +=
				R"("meshes":[{"name":"BodyMesh","weights":[0.5],"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":4,"targets":[{"POSITION":1}]}]}],)";
			json += R"("animations":[{"name":"Wave","samplers":[)"
					R"({"input":6,"output":7,"interpolation":"LINEAR"},)"
					R"({"input":6,"output":8,"interpolation":"STEP"},)"
					R"({"input":6,"output":9,"interpolation":"CUBICSPLINE"},)"
					R"({"input":6,"output":10,"interpolation":"LINEAR"}],)"
					R"("channels":[)"
					R"({"sampler":0,"target":{"node":2,"path":"rotation"}},)"
					R"({"sampler":1,"target":{"node":1,"path":"translation"}},)"
					R"({"sampler":2,"target":{"node":4,"path":"weights"}},)"
					R"({"sampler":3,"target":{"node":0,"path":"scale"}}]}],)";
			json += "\"buffers\":[{\"byteLength\":" + std::to_string(buffer.size()) + ",\"uri\":\"data:application/octet-stream;base64," +
				Base64(buffer) + "\"}],";
			json += "\"bufferViews\":[" + view(vPositions) + "," + view(vDeltas) + "," + view(vJoints) + "," + view(vWeights) + "," +
				view(vIndices) + "," + view(vInverse) + "," + view(vTimes) + "," + view(vRotations) + "," + view(vTranslations) + "," +
				view(vMorph) + "," + view(vScales) + "],";
			json += R"("accessors":[)"
					R"({"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
					R"({"bufferView":1,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[0.25,0,1]},)"
					R"({"bufferView":2,"componentType":5123,"count":4,"type":"VEC4"},)"
					R"({"bufferView":3,"componentType":5126,"count":4,"type":"VEC4"},)"
					R"({"bufferView":4,"componentType":5123,"count":6,"type":"SCALAR"},)"
					R"({"bufferView":5,"componentType":5126,"count":3,"type":"MAT4"},)"
					R"({"bufferView":6,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},)"
					R"({"bufferView":7,"componentType":5126,"count":2,"type":"VEC4"},)"
					R"({"bufferView":8,"componentType":5126,"count":2,"type":"VEC3"},)"
					R"({"bufferView":9,"componentType":5126,"count":6,"type":"SCALAR"},)"
					R"({"bufferView":10,"componentType":5126,"count":2,"type":"VEC3"}]})";
			return json;
		}

		std::filesystem::path path;
	};
} // namespace Swim::Testing
