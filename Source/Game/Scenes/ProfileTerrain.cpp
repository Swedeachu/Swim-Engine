#include "Game/Scenes/ProfileTerrain.h"

#include "Engine/Components/Light.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Game/SandboxContent.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Game
{

	namespace
	{

		float Hash(int x, int z)
		{
			std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(z) * 668265263u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return float(h ^ (h >> 16)) / 4294967295.0f;
		}

		float ValueNoise(float x, float z)
		{
			const int ix = int(std::floor(x));
			const int iz = int(std::floor(z));
			const float fx = x - float(ix);
			const float fz = z - float(iz);
			const float sx = fx * fx * (3.0f - 2.0f * fx);
			const float sz = fz * fz * (3.0f - 2.0f * fz);
			const float a = Hash(ix, iz) + (Hash(ix + 1, iz) - Hash(ix, iz)) * sx;
			const float b = Hash(ix, iz + 1) + (Hash(ix + 1, iz + 1) - Hash(ix, iz + 1)) * sx;
			return a + (b - a) * sz;
		}

		// One mesh per chunk and material: grass tops, dirt sides (the top three blocks) and
		// stone below; faces only where a column is higher than its neighbour.
		enum Layer
		{
			Grass,
			Dirt,
			Stone,
			Layers
		};

		void AddFace(Engine::ProceduralMeshes::MeshData& mesh, const std::array<glm::vec3, 4>& corners, const glm::vec3& normal)
		{
			const std::array<float, 3> n{ normal.x, normal.y, normal.z };
			const std::array<std::array<float, 2>, 4> uv{ { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } } };
			std::array<std::uint32_t, 4> index{};

			for (int i = 0; i < 4; ++i)
			{
				index[i] = mesh.AddVertex({ corners[i].x, corners[i].y, corners[i].z }, n, uv[i]);
			}

			mesh.AddQuad(index[0], index[1], index[2], index[3]);
		}

	} // namespace

	int ProfileTerrain::Height(int x, int z)
	{
		const float fx = float(x);
		const float fz = float(z);
		const float h = 14.0f * ValueNoise(fx / 48.0f, fz / 48.0f) + 6.0f * ValueNoise(fx / 16.0f + 7.3f, fz / 16.0f - 2.1f) +
			2.0f * ValueNoise(fx / 5.0f, fz / 5.0f);
		return 4 + int(h);
	}

	int ProfileTerrain::Init()
	{
		frame = 0;
		auto* render = GetRenderServices();

		if (render && render->Settings)
		{
			// Classic lighting: sun + flat ambient on a clear colour, nothing else.
			auto& settings = *render->Settings;
			settings.Shadows = false;
			settings.Environment = false;
			settings.SkyBackground = false;
			settings.ClearColor = { 0.45f, 0.62f, 0.95f };
			settings.Ambient = { 0.32f, 0.34f, 0.38f };
			settings.ScreenSpace.AmbientOcclusion.Enabled = false;
			settings.ScreenSpace.Reflections.Enabled = false;
			settings.ScreenSpace.Fog.Enabled = false;
			settings.Post.Bloom.Enabled = false;
			settings.Post.Exposure.Mode = Swim::Render::ExposureMode::Manual;
			settings.Post.Exposure.ManualEv100 = 0.0f;
		}

		if (!render || !render->HasRenderer())
		{
			return 0; // Headless (tests): no meshes.
		}

		const std::array<std::uint32_t, Layers> materials{
			Material(*render->Materials, "Terrain grass", SrgbColor(96, 160, 64), 0.0f, 0.9f),
			Material(*render->Materials, "Terrain dirt", SrgbColor(134, 96, 67), 0.0f, 0.95f),
			Material(*render->Materials, "Terrain stone", SrgbColor(125, 125, 125), 0.0f, 0.85f),
		};
		auto height = [](int x, int z)
		{
			return x < 0 || z < 0 || x >= Size || z >= Size ? 0 : Height(x, z);
		};
		const float half = Size * 0.5f;

		for (int cz = 0; cz < Size; cz += Chunk)
		{
			for (int cx = 0; cx < Size; cx += Chunk)
			{
				std::array<Engine::ProceduralMeshes::MeshData, Layers> meshes;

				for (int z = cz; z < cz + Chunk; ++z)
				{
					for (int x = cx; x < cx + Chunk; ++x)
					{
						const int h = height(x, z);
						const glm::vec3 p(float(x) - half, 0.0f, float(z) - half);
						AddFace(meshes[Grass],
							{ p + glm::vec3(0, h, 1), p + glm::vec3(1, h, 1), p + glm::vec3(1, h, 0), p + glm::vec3(0, h, 0) },
							{ 0, 1, 0 });
						constexpr std::array<std::array<int, 2>, 4> sides{ { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } } };

						for (const auto& side : sides)
						{
							const int below = height(x + side[0], z + side[1]);
							const glm::vec3 n(float(side[0]), 0.0f, float(side[1]));
							// The face's bottom edge, counter-clockwise seen from outside.
							const glm::vec3 centre = p + glm::vec3(0.5f, 0.0f, 0.5f) + n * 0.5f;
							const glm::vec3 along(n.z, 0.0f, -n.x);
							const glm::vec3 a = centre - along * 0.5f;
							const glm::vec3 b = centre + along * 0.5f;

							for (int y = below; y < h; ++y)
							{
								const Layer layer = y >= h - 3 ? Dirt : Stone;
								AddFace(meshes[layer],
									{ a + glm::vec3(0, y, 0), b + glm::vec3(0, y, 0), b + glm::vec3(0, y + 1, 0),
										a + glm::vec3(0, y + 1, 0) },
									n);
							}
						}
					}
				}

				for (int layer = 0; layer < Layers; ++layer)
				{
					if (meshes[layer].Indices.empty())
					{
						continue;
					}

					Engine::ProceduralMeshes::GenerateTangents(meshes[layer]);
					const std::string name = "Terrain " + std::to_string(cx) + "," + std::to_string(cz) + "/" + std::to_string(layer);
					auto mesh = render->Meshes->Find(name);

					if (!mesh.IsValid())
					{
						mesh = render->Meshes->Register(name, std::move(meshes[layer]));
					}

					SpawnMesh(*this, { name, mesh, materials[layer] });
				}
			}
		}

		const entt::entity sun = CreateEntity("Sun");
		const glm::vec3 towardSun = glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f));
		AddComponent<Engine::Transform>(
			sun, Engine::Transform(glm::vec3(0.0f), glm::vec3(1.0f), glm::quatLookAt(-towardSun, glm::vec3(0, 1, 0))));
		Engine::Light light;
		light.Kind = Engine::LightKind::Directional;
		light.Color = { 1.0f, 0.97f, 0.9f };
		light.Intensity = 2.2f;
		light.CastShadows = false;
		AddComponent<Engine::Light>(sun, light);
		AddTag(sun, Engine::Tags::Light);
		Update(0.0);
		return 0;
	}

	void ProfileTerrain::Update(double)
	{
		// One revolution every 3600 frames, by frame count (not wall time), so runs match.
		if (auto* cameras = GetCameraSystem())
		{
			const float angle = 6.2831853f * float(frame % 3600) / 3600.0f;
			const glm::vec3 eye(std::cos(angle) * 70.0f, 48.0f, std::sin(angle) * 70.0f);
			cameras->GetCamera().LookAt(eye, glm::vec3(0.0f, 14.0f, 0.0f));
		}

		++frame;
	}

} // namespace Game
