#include "pch.h"
#include "PrimitiveTest.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Meshes/PrimitiveMeshes.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"

namespace Game
{

	namespace
	{

		const glm::vec3 RED = { 1.0f, 0.0f, 0.0f };
		const glm::vec3 GREEN = { 0.0f, 1.0f, 0.0f };
		const glm::vec3 BLUE = { 0.0f, 0.0f, 1.0f };

		struct Layout
		{
			float startX = -20.0f;
			float stepX = 3.0f;
			float yCenter = 0.5f;
			// rows (front-to-back)
			float z_spheres = -9.0f;
			float z_cones = -3.0f;
			float z_cylinders = 3.0f;
			float z_torus_thin = 9.0f;
			float z_torus_med = 12.0f;
			float z_torus_fat = 15.0f;
		} constexpr kLayout{};

		struct Pools
		{
			Engine::MeshPool& meshPool;
			Engine::MaterialPool& materialPool;
		};

		inline auto MakeSolidSphere(Pools& p, const std::string& name, const glm::vec3& c, int lat = 24, int lon = 48)
		{
			auto data = Engine::MakeSphere(lat, lon, c, c, c);
			auto mesh = p.meshPool.RegisterMesh(name, data.vertices, data.indices);
			return p.materialPool.RegisterMaterialData(name + "_mat", mesh);
		}

		inline auto MakeSolidCylinder(Pools& p, const std::string& name, const glm::vec3& c, float r = 0.25f, float h = 1.0f, uint32_t seg = 64)
		{
			auto data = Engine::MakeCylinder(r, h, seg, c);
			auto mesh = p.meshPool.RegisterMesh(name, data.vertices, data.indices);
			return p.materialPool.RegisterMaterialData(name + "_mat", mesh);
		}

		inline auto MakeSolidCone(Pools& p, const std::string& name, const glm::vec3& c, float r = 0.5f, float h = 1.0f, uint32_t seg = 64)
		{
			auto data = Engine::MakeCone(r, h, seg, c);
			auto mesh = p.meshPool.RegisterMesh(name, data.vertices, data.indices);
			return p.materialPool.RegisterMaterialData(name + "_mat", mesh);
		}

		inline auto MakeSolidTorus(Pools& p, const std::string& name, const glm::vec3& c, float outerR, float thickness, uint32_t segU = 48, uint32_t segV = 24)
		{
			auto data = Engine::MakeTorus(outerR, thickness, segU, segV, c);
			auto mesh = p.meshPool.RegisterMesh(name, data.vertices, data.indices);
			return p.materialPool.RegisterMaterialData(name + "_mat", mesh);
		}

		inline auto MakeSolidArrow(Pools& p, const std::string& name, const glm::vec3& c,
			float shaftR, float shaftL, float headR, float headL, uint32_t seg = 64)
		{
			auto data = Engine::MakeArrow(shaftR, shaftL, headR, headL, seg, c);
			auto mesh = p.meshPool.RegisterMesh(name, data.vertices, data.indices);
			return p.materialPool.RegisterMaterialData(name + "_mat", mesh);
		}

		inline void SpawnAt(Engine::Scene* scene, const glm::vec3& pos, const glm::vec3& scl,
			const glm::quat& rot, decltype(Engine::MaterialPool::GetInstance().RegisterMaterialData("", 0)) matHandle)
		{
			auto e = scene->CreateEntity();
			Engine::Transform t;
			t.GetPositionRef() = pos;
			t.GetScaleRef() = scl;
			t.GetRotationRef() = rot;
			scene->AddComponent<Engine::Transform>(e, t);
			scene->AddComponent<Engine::Material>(e, Engine::Material(matHandle));
		}

		inline void SpawnTripletRow(Engine::Scene* scene, float x0, float stepX, float y, float z,
			decltype(Engine::MaterialPool::GetInstance().RegisterMaterialData("", 0)) m0,
			decltype(Engine::MaterialPool::GetInstance().RegisterMaterialData("", 0)) m1,
			decltype(Engine::MaterialPool::GetInstance().RegisterMaterialData("", 0)) m2)
		{
			SpawnAt(scene, { x0 + 0 * stepX, y, z }, { 1,1,1 }, glm::quat(1, 0, 0, 0), m0);
			SpawnAt(scene, { x0 + 1 * stepX, y, z }, { 1,1,1 }, glm::quat(1, 0, 0, 0), m1);
			SpawnAt(scene, { x0 + 2 * stepX, y, z }, { 1,1,1 }, glm::quat(1, 0, 0, 0), m2);
		}

		inline void AddSpheres(Engine::Scene* scene, Pools& p)
		{
			auto mR = MakeSolidSphere(p, "Prim_Sphere_Red", RED);
			auto mG = MakeSolidSphere(p, "Prim_Sphere_Green", GREEN);
			auto mB = MakeSolidSphere(p, "Prim_Sphere_Blue", BLUE);
			SpawnTripletRow(scene, kLayout.startX, kLayout.stepX, kLayout.yCenter, kLayout.z_spheres, mR, mG, mB);
		}

		inline void AddCones(Engine::Scene* scene, Pools& p)
		{
			auto mR = MakeSolidCone(p, "Prim_Cone_Red", RED);
			auto mG = MakeSolidCone(p, "Prim_Cone_Green", GREEN);
			auto mB = MakeSolidCone(p, "Prim_Cone_Blue", BLUE);
			SpawnTripletRow(scene, kLayout.startX, kLayout.stepX, kLayout.yCenter, kLayout.z_cones, mR, mG, mB);
		}

		inline void AddCylinders(Engine::Scene* scene, Pools& p)
		{
			auto mR = MakeSolidCylinder(p, "Prim_Cyl_Red", RED, 0.25f);
			auto mG = MakeSolidCylinder(p, "Prim_Cyl_Green", GREEN, 0.25f);
			auto mB = MakeSolidCylinder(p, "Prim_Cyl_Blue", BLUE, 0.25f);
			SpawnTripletRow(scene, kLayout.startX, kLayout.stepX, kLayout.yCenter, kLayout.z_cylinders, mR, mG, mB);
		}

		inline void AddToruses(Engine::Scene* scene, Pools& p)
		{
			const float outer = 0.40f;
			const float thin = 0.05f;
			const float med = 0.12f;
			const float fat = 0.20f;

			// thin
			{
				auto mR = MakeSolidTorus(p, "Prim_Torus_Thin_R", RED, outer, thin);
				auto mG = MakeSolidTorus(p, "Prim_Torus_Thin_G", GREEN, outer, thin);
				auto mB = MakeSolidTorus(p, "Prim_Torus_Thin_B", BLUE, outer, thin);
				SpawnTripletRow(scene, kLayout.startX, kLayout.stepX, kLayout.yCenter, kLayout.z_torus_thin, mR, mG, mB);
			}
			// medium
			{
				auto mR = MakeSolidTorus(p, "Prim_Torus_Med_R", RED, outer, med);
				auto mG = MakeSolidTorus(p, "Prim_Torus_Med_G", GREEN, outer, med);
				auto mB = MakeSolidTorus(p, "Prim_Torus_Med_B", BLUE, outer, med);
				SpawnTripletRow(scene, kLayout.startX, kLayout.stepX, kLayout.yCenter, kLayout.z_torus_med, mR, mG, mB);
			}
			// fat
			{
				auto mR = MakeSolidTorus(p, "Prim_Torus_Fat_R", RED, outer, fat);
				auto mG = MakeSolidTorus(p, "Prim_Torus_Fat_G", GREEN, outer, fat);
				auto mB = MakeSolidTorus(p, "Prim_Torus_Fat_B", BLUE, outer, fat);
				SpawnTripletRow(scene, kLayout.startX, kLayout.stepX, kLayout.yCenter, kLayout.z_torus_fat, mR, mG, mB);
			}
		}

		inline void AddGizmoArrows(Engine::Scene* scene, Pools& p)
		{
			// proportions
			const float shaftR = 0.05f;
			const float headR = 0.12f;
			const glm::vec3 gizmoPos(0.0f);

			// lengths
			const float lenX = 1.0f, lenY = 1.5f, lenZ = 2.0f;
			auto split = [](float total)
			{
				const float headL = total * 0.30f;
				const float shaftL = std::max(0.0001f, total - headL);
				return std::pair<float, float>(shaftL, headL);
			};
			const auto [shaftL_X, headL_X] = split(lenX);
			const auto [shaftL_Y, headL_Y] = split(lenY);
			const auto [shaftL_Z, headL_Z] = split(lenZ);

			// build mats
			auto mR = MakeSolidArrow(p, "Prim_Arrow_Red", RED, shaftR, shaftL_X, headR, headL_X);
			auto mG = MakeSolidArrow(p, "Prim_Arrow_Green", GREEN, shaftR, shaftL_Y, headR, headL_Y);
			auto mB = MakeSolidArrow(p, "Prim_Arrow_Blue", BLUE, shaftR, shaftL_Z, headR, headL_Z);

			// rotations: arrow model points +Y
			const glm::quat rotX = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 0, 1)); // +X
			const glm::quat rotY = glm::quat(glm::vec3(0));                                  // +Y
			const glm::quat rotZ = glm::angleAxis(+glm::half_pi<float>(), glm::vec3(1, 0, 0)); // +Z

			SpawnAt(scene, gizmoPos, { 1,1,1 }, rotX, mR); // red +X
			SpawnAt(scene, gizmoPos, { 1,1,1 }, rotY, mG); // green +Y
			SpawnAt(scene, gizmoPos, { 1,1,1 }, rotZ, mB); // blue +Z
		}

		inline void AddDarkPlane(Engine::Scene* scene, Pools& p)
		{
			auto cubeData = Engine::MakeCube();

			std::vector<Engine::Vertex> darkVerts = cubeData.vertices;
			const glm::vec3 DARK_GREY(0.2f, 0.2f, 0.2f);
			for (auto& v : darkVerts) v.color = DARK_GREY;

			auto planeMesh = p.meshPool.RegisterMesh("Prim_DarkGreyPlane", darkVerts, cubeData.indices);
			auto planeMat = p.materialPool.RegisterMaterialData("Prim_DarkGreyPlane_Mat", planeMesh);

			const glm::vec3 planeScale(2.0f, 0.02f, 2.0f);
			const glm::vec3 planePos(-20.0f, 0.0f, 0.0f);

			SpawnAt(scene, planePos, planeScale, glm::quat(1, 0, 0, 0), planeMat);
		}

	} // anon namespace

	// =================== public entry ===================
	void TestPrimitives(Engine::Scene* scene)
	{
		Pools pools{
			Engine::MeshPool::GetInstance(),
			Engine::MaterialPool::GetInstance()
		};

		AddSpheres(scene, pools);
		AddCones(scene, pools);
		AddCylinders(scene, pools);
		AddToruses(scene, pools);
		// AddGizmoArrows(scene, pools);
		// AddDarkPlane(scene, pools);
	}

} // namespace Game
