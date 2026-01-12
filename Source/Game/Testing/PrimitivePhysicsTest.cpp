#include "pch.h"
#include "PrimitivePhysicsTest.h"

#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Meshes/PrimitiveMeshes.h"

#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/ObjectTag.h"

#include "Engine/Systems/Physics/RigidBody.h"

namespace Game
{

	namespace
	{

		const glm::vec3 CUBE_DARK = { 0.10f, 0.00f, 0.00f };
		const glm::vec3 CUBE_LIGHT = { 1.00f, 0.05f, 0.05f };

		const glm::vec3 SPH_DARK = { 0.05f, 0.10f, 0.22f };
		const glm::vec3 SPH_LIGHT = { 0.25f, 0.75f, 1.00f };

		const glm::vec3 PLANE_DARK = { 0.08f, 0.08f, 0.09f };
		const glm::vec3 PLANE_LIGHT = { 0.22f, 0.22f, 0.24f };

		struct Layout
		{
			glm::vec3 planePos = { 50.0f, 0.0f, 0.0f };
			glm::vec3 planeScale = { 50.0f, 0.05f, 50.0f };

			glm::vec3 startPos = { 50.0f, 2.0f, -10.0f };
			int countX = 7;
			int countY = 7;
			int countZ = 7;

			float step = 2.5f;

			float cubeScale = 1.0f;
			float sphereScale = 1.0f;
		} constexpr kLayout{};

		struct Pools
		{
			Engine::MeshPool& meshPool;
			Engine::MaterialPool& materialPool;
		};

		inline float Saturate(float v)
		{
			if (v < 0.0f) return 0.0f;
			if (v > 1.0f) return 1.0f;
			return v;
		}

		inline glm::vec3 Lerp3(const glm::vec3& a, const glm::vec3& b, float t)
		{
			t = Saturate(t);
			return a + (b - a) * t;
		}

		inline void ApplyAxisGradient(std::vector<Engine::Vertex>& verts,
			const glm::vec3& c0, const glm::vec3& c1, const glm::vec3& axisWeights)
		{
			for (auto& v : verts)
			{
				// Cube from MakeCube is typically in [-0.5, +0.5]. Normalize to [0,1] per axis.
				const glm::vec3 p = v.position;
				const glm::vec3 u = glm::vec3(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);

				// Blend axis contributions to get a smooth 3D-ish gradient.
				const float t =
					Saturate(u.x) * axisWeights.x +
					Saturate(u.y) * axisWeights.y +
					Saturate(u.z) * axisWeights.z;

				v.color = Lerp3(c0, c1, t);
			}
		}

		inline std::shared_ptr<Engine::MaterialData> EnsureGradientCubeMaterial(Pools& p,
			const std::string& meshName, const std::string& matName,
			const glm::vec3& c0, const glm::vec3& c1, const glm::vec3& axisWeights)
		{
			if (!p.meshPool.GetMesh(meshName))
			{
				auto cubeData = Engine::MakeCube();

				std::vector<Engine::Vertex> verts = cubeData.vertices;
				ApplyAxisGradient(verts, c0, c1, axisWeights);

				p.meshPool.RegisterMesh(meshName, verts, cubeData.indices);
			}

			if (p.materialPool.MaterialExists(matName))
			{
				return p.materialPool.GetMaterialData(matName);
			}

			auto mesh = p.meshPool.GetMesh(meshName);
			return p.materialPool.RegisterMaterialData(matName, mesh);
		}

		inline std::shared_ptr<Engine::MaterialData> EnsureGradientSphereMaterial(Pools& p,
			const std::string& meshName, const std::string& matName,
			const glm::vec3& cSouth, const glm::vec3& cEquator, const glm::vec3& cNorth,
			int lat = 24, int lon = 48)
		{
			if (!p.meshPool.GetMesh(meshName))
			{
				// Build with a 3-stop vertical gradient using MakeSphere's 3 color parameters
				auto sphereData = Engine::MakeSphere(lat, lon, cSouth, cEquator, cNorth);
				p.meshPool.RegisterMesh(meshName, sphereData.vertices, sphereData.indices);
			}

			if (p.materialPool.MaterialExists(matName))
			{
				return p.materialPool.GetMaterialData(matName);
			}

			auto mesh = p.meshPool.GetMesh(meshName);
			return p.materialPool.RegisterMaterialData(matName, mesh);
		}

		inline void SpawnStaticPlane(Engine::Scene* scene, const glm::vec3& pos, const glm::vec3& scl, std::shared_ptr<Engine::MaterialData> mat)
		{
			entt::entity e = scene->CreateEntity();
			scene->SetTag(e, Engine::TagConstants::WORLD, "physics plane");

			Engine::Transform t;
			t.GetPositionRef() = pos;
			t.GetScaleRef() = scl;
			t.GetRotationRef() = glm::quat(1, 0, 0, 0);
			scene->AddComponent<Engine::Transform>(e, t);

			scene->AddComponent<Engine::Material>(e, Engine::Material(mat));

			Engine::Rigidbody rb = Engine::Rigidbody();
			rb.type = Engine::RigidbodyType::Static;
			rb.useGravity = false;
			rb.startAwake = true;
			rb.isTrigger = false;

			rb.mass = 1.0f;
			rb.linearDamping = 0.01f;
			rb.angularDamping = 0.01f;

			rb.collider.type = Engine::ColliderType::Box;
			rb.collider.box.halfExtents = glm::vec3(0.5f, 0.5f, 0.5f); // unit cube; Transform scale applies actual size

			rb.dirty = true;
			scene->AddComponent<Engine::Rigidbody>(e, rb);
		}

		inline void SpawnDynamicCube(Engine::Scene* scene, const glm::vec3& pos, float uniformScale, std::shared_ptr<Engine::MaterialData> mat)
		{
			entt::entity e = scene->CreateEntity();
			scene->SetTag(e, Engine::TagConstants::WORLD, "physics cube");

			Engine::Transform t;
			t.GetPositionRef() = pos;
			t.GetScaleRef() = glm::vec3(uniformScale);
			t.GetRotationRef() = glm::quat(1, 0, 0, 0);
			scene->AddComponent<Engine::Transform>(e, t);

			scene->AddComponent<Engine::Material>(e, Engine::Material(mat));

			Engine::Rigidbody rb = Engine::Rigidbody();
			rb.type = Engine::RigidbodyType::Dynamic;
			rb.useGravity = true;
			rb.startAwake = true;
			rb.isTrigger = false;

			rb.mass = 1.0f;
			rb.linearDamping = 0.01f;
			rb.angularDamping = 0.01f;

			rb.collider.type = Engine::ColliderType::Box;
			rb.collider.box.halfExtents = glm::vec3(0.5f, 0.5f, 0.5f); // unit cube; Transform scale applies actual size

			rb.dirty = true;
			scene->AddComponent<Engine::Rigidbody>(e, rb);
		}

		inline void SpawnDynamicSphere(Engine::Scene* scene, const glm::vec3& pos, float uniformScale, std::shared_ptr<Engine::MaterialData> mat)
		{
			entt::entity e = scene->CreateEntity();
			scene->SetTag(e, Engine::TagConstants::WORLD, "physics sphere");

			Engine::Transform t;
			t.GetPositionRef() = pos;
			t.GetScaleRef() = glm::vec3(uniformScale);
			t.GetRotationRef() = glm::quat(1, 0, 0, 0);
			scene->AddComponent<Engine::Transform>(e, t);

			scene->AddComponent<Engine::Material>(e, Engine::Material(mat));

			Engine::Rigidbody rb = Engine::Rigidbody();
			rb.type = Engine::RigidbodyType::Dynamic;
			rb.useGravity = true;
			rb.startAwake = true;
			rb.isTrigger = false;

			rb.mass = 1.0f;
			rb.linearDamping = 0.01f;
			rb.angularDamping = 0.01f;

			rb.collider.type = Engine::ColliderType::Sphere;
			rb.collider.sphere.radius = 0.5f; // unit sphere radius; Transform scale applies actual size

			rb.dirty = true;
			scene->AddComponent<Engine::Rigidbody>(e, rb);
		}

	} // anon namespace

	// =================== public entry ===================
	void TestPrimitivePhysics(Engine::Scene* scene)
	{
		if (!scene)
		{
			return;
		}

		Pools pools{
			Engine::MeshPool::GetInstance(),
			Engine::MaterialPool::GetInstance()
		};

		// Shared gradient materials:
		// - cubes: warm red/orange gradient with slight diagonal bias
		// - spheres: blue gradient (darker bottom -> bright top)
		// - plane: subtle dark grey gradient
		auto redCubeMat = EnsureGradientCubeMaterial(
			pools,
			"PhysTest_Cube_RedGrad",
			"PhysTest_Cube_RedGrad_Mat",
			CUBE_DARK,
			CUBE_LIGHT,
			glm::normalize(glm::vec3(0.35f, 0.55f, 0.10f))
		);

		auto blueSphereMat = EnsureGradientSphereMaterial(
			pools,
			"PhysTest_Sphere_BlueGrad",
			"PhysTest_Sphere_BlueGrad_Mat",
			SPH_DARK,
			Lerp3(SPH_DARK, SPH_LIGHT, 0.45f),
			SPH_LIGHT,
			24, 48
		);

		auto planeMat = EnsureGradientCubeMaterial(
			pools,
			"PhysTest_Plane_Grad",
			"PhysTest_Plane_Grad_Mat",
			PLANE_DARK,
			PLANE_LIGHT,
			glm::normalize(glm::vec3(0.15f, 0.85f, 0.00f))
		);

		// Static collision plane.
		SpawnStaticPlane(scene, kLayout.planePos, kLayout.planeScale, planeMat);

		// Grid of alternating cubes and spheres.
		for (int y = 0; y < kLayout.countY; y++)
		{
			for (int z = 0; z < kLayout.countZ; z++)
			{
				for (int x = 0; x < kLayout.countX; x++)
				{
					const glm::vec3 pos = kLayout.startPos + glm::vec3(x * kLayout.step, (float)y, z * kLayout.step);

					const bool spawnCube = ((x + z) % 2) == 0;
					if (spawnCube)
					{
						SpawnDynamicCube(scene, pos, kLayout.cubeScale, redCubeMat);
					}
					else
					{
						SpawnDynamicSphere(scene, pos, kLayout.sphereScale, blueSphereMat);
					}
				}
			}
		}
	}

} // namespace Game
