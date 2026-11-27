#include "Game/Scenes/SandboxShared.h"

// The playgrounds' content: gallery, hall, glass, physics, particles, tentacles, world UI,
// the HUD, waves, ocean and the spawners.
namespace Game
{

	using namespace SandboxDetail;

	void Sandbox::BuildPbrGallery()
	{
		// Rows: metallic 0 .. 1 (bottom to top); columns: roughness 0.05 .. 1.
		constexpr int columns = 7;
		constexpr int rows = 4;
		const glm::vec3 base = SrgbColor(230, 190, 120);

		for (int r = 0; r < rows; ++r)
		{
			for (int c = 0; c < columns; ++c)
			{
				const float metallic = static_cast<float>(r) / static_cast<float>(rows - 1);
				const float roughness = 0.05f + 0.95f * static_cast<float>(c) / static_cast<float>(columns - 1);
				const std::string name = "PBR m" + std::to_string(r) + " r" + std::to_string(c);
				const glm::vec3 color = metallic > 0.5f ? base : glm::mix(SrgbColor(200, 40, 40), base, metallic * 2.0f);
				SpawnMesh(*this,
					{ name, palette.Mesh(Engine::BuiltinMesh::Sphere), Mat(name, color, metallic, roughness),
						{ -17.0f + 1.3f * static_cast<float>(c), 0.8f + 1.3f * static_cast<float>(r), -9.0f }, glm::vec3(1.0f),
						glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static,
						{ GameTags::PbrGallery, Engine::Tags::Static } });
			}
		}

		// A backdrop wall.
		const entt::entity wall = SpawnMesh(*this,
			{ "Gallery wall", palette.Mesh(Engine::BuiltinMesh::Cube), Mat("Gallery wall", SrgbColor(60, 64, 72), 0.0f, 0.9f),
				{ -13.1f, 3.0f, -10.3f }, { 10.5f, 6.0f, 0.4f }, glm::quat(1, 0, 0, 0),
				Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static, { Engine::Tags::Static } });
		AddBoxBody(*this, wall, Engine::RigidbodyType::Static, glm::vec3(0.5f));
		// One static area probe for the whole gallery (28 spheres share it).
		const entt::entity probe = CreateEntity("PBR gallery probe");
		AddComponent<Engine::Transform>(probe, Engine::Transform(glm::vec3(-13.1f, 2.5f, -7.5f), glm::vec3(1.0f)));
		Engine::ReflectionProbe area;
		area.ObjectProbe = false;
		area.Dynamic = false;
		area.InfluenceRadius = 7.0f;
		area.BlendDistance = 2.0f;
		AddComponent<Engine::ReflectionProbe>(probe, area);
	}

	void Sandbox::BuildInstanceHall()
	{
		constexpr int side = 24;
		const std::array<std::uint32_t, 6> materials{ Mat("Hall slate", SrgbColor(70, 78, 92), 0.0f, 0.6f),
			Mat("Hall steel", SrgbColor(170, 175, 185), 1.0f, 0.35f), Mat("Hall copper", SrgbColor(215, 120, 80), 1.0f, 0.3f),
			Mat("Hall ivory", SrgbColor(225, 220, 205), 0.0f, 0.5f), Mat("Hall teal", SrgbColor(40, 150, 150), 0.0f, 0.4f),
			Mat("Hall graphite", SrgbColor(35, 36, 40), 0.0f, 0.25f) };

		for (int z = 0; z < side; ++z)
		{
			for (int x = 0; x < side; ++x)
			{
				const float fx = static_cast<float>(x) - (side - 1) * 0.5f;
				const float fz = static_cast<float>(z) - (side - 1) * 0.5f;
				const float height = 0.3f + 1.4f * (0.5f + 0.5f * std::sin(fx * 0.45f) * std::cos(fz * 0.38f));
				const std::uint32_t material = materials[static_cast<std::size_t>((x / 4 + z / 4) % 6)];
				const entt::entity cube = SpawnMesh(*this,
					{ "Hall cube", palette.Mesh(Engine::BuiltinMesh::Cube), material,
						HallCenter + glm::vec3(fx * 0.55f, height * 0.5f, fz * 0.55f), { 0.4f, height, 0.4f }, glm::quat(1, 0, 0, 0),
						Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static,
						{ GameTags::InstanceHall, Engine::Tags::Static } });

				if ((x * 7 + z * 3) % 29 == 0)
				{
					EmplaceBehavior<Spin>(cube, glm::vec3(0, 1, 0), 45.0f + 10.0f * static_cast<float>(x % 5));
				}
			}
		}
	}

	void Sandbox::BuildGlassAndEmissive()
	{
		const std::array<std::pair<const char*, glm::vec3>, 4> glass{ { { "Red glass", { 0.9f, 0.15f, 0.12f } },
			{ "Green glass", { 0.15f, 0.85f, 0.3f } }, { "Blue glass", { 0.15f, 0.35f, 0.95f } },
			{ "Amber glass", { 0.95f, 0.65f, 0.1f } } } };

		for (std::size_t i = 0; i < glass.size(); ++i)
		{
			SpawnMesh(*this,
				{ glass[i].first, palette.Mesh(Engine::BuiltinMesh::Cube),
					Mat(glass[i].first, glass[i].second, 0.0f, 0.08f, glm::vec3(0.0f), true, 0.35f),
					{ -13.0f + 1.9f * static_cast<float>(i), 1.4f, 2.0f + 0.6f * static_cast<float>(i) }, { 1.6f, 2.4f, 0.06f },
					glm::angleAxis(0.25f * static_cast<float>(i), glm::vec3(0, 1, 0)), Swim::Render::RenderObjectFlags::Visible,
					{ GameTags::Glass } });
		}

		const std::array<std::pair<const char*, glm::vec3>, 3> emissive{ { { "Ember ring", { 6.0f, 2.2f, 0.4f } },
			{ "Ice ring", { 0.6f, 3.5f, 7.0f } }, { "Violet ring", { 4.5f, 0.8f, 6.0f } } } };

		for (std::size_t i = 0; i < emissive.size(); ++i)
		{
			const entt::entity ring = SpawnMesh(*this,
				{ emissive[i].first, palette.Mesh(Engine::BuiltinMesh::Torus),
					Mat(emissive[i].first, glm::vec3(0.1f), 0.2f, 0.4f, emissive[i].second),
					{ -14.0f + 3.0f * static_cast<float>(i), 1.6f, 6.0f }, glm::vec3(1.4f), glm::angleAxis(Pi * 0.5f, glm::vec3(1, 0, 0)),
					Swim::Render::RenderObjectFlags::Default, { GameTags::Emissive, Engine::Tags::Effect } });
			EmplaceBehavior<Spin>(ring, glm::vec3(0.3f, 1.0f, 0.2f), 40.0f + 25.0f * static_cast<float>(i));
			EmplaceBehavior<Bob>(ring, 0.25f, 0.25f, static_cast<float>(i));
		}
	}

	void Sandbox::BuildPhysicsPlayground()
	{
		const std::uint32_t crate = Mat("Crate", SrgbColor(190, 140, 90), 0.0f, 0.7f);
		const std::uint32_t crateDark = Mat("Crate dark", SrgbColor(120, 90, 60), 0.0f, 0.75f);
		// A five-level box pyramid.
		constexpr float size = 0.8f;
		int index = 0;

		for (int level = 0; level < 5; ++level)
		{
			const int count = 5 - level;

			for (int i = 0; i < count; ++i)
			{
				const float x = (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f) * (size + 0.02f);
				const entt::entity box = SpawnMesh(*this,
					{ "Crate " + std::to_string(++index), palette.Mesh(Engine::BuiltinMesh::Cube), (level + i) % 2 ? crate : crateDark,
						PhysicsCenter + glm::vec3(x, size * 0.5f + static_cast<float>(level) * size, 0.0f), glm::vec3(size),
						glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Default, { GameTags::PhysicsToy, Engine::Tags::Dynamic } });
				AddBoxBody(*this, box, Engine::RigidbodyType::Dynamic, glm::vec3(0.5f), 4.0f);
			}
		}

		// A ramp and a few capsules and spheres to knock around.
		const glm::quat tilt = glm::angleAxis(glm::radians(-18.0f), glm::vec3(1, 0, 0));
		const entt::entity ramp = SpawnMesh(*this,
			{ "Ramp", palette.Mesh(Engine::BuiltinMesh::Cube), Mat("Ramp", SrgbColor(90, 100, 115), 0.2f, 0.5f),
				PhysicsCenter + glm::vec3(-5.5f, 1.0f, -1.0f), { 2.5f, 0.3f, 6.0f }, tilt, Swim::Render::RenderObjectFlags::Default,
				{ GameTags::PhysicsToy, Engine::Tags::Static } });
		AddBoxBody(*this, ramp, Engine::RigidbodyType::Static, glm::vec3(0.5f));

		for (int i = 0; i < 4; ++i)
		{
			const entt::entity capsule = SpawnMesh(*this,
				{ "Capsule " + std::to_string(i + 1), palette.Mesh(Engine::BuiltinMesh::Capsule),
					Mat("Capsule", SrgbColor(80, 170, 220), 0.0f, 0.3f),
					PhysicsCenter + glm::vec3(-5.5f, 3.0f + static_cast<float>(i), -3.0f + 0.4f * static_cast<float>(i)), glm::vec3(1.0f),
					glm::angleAxis(Pi * 0.5f, glm::vec3(0, 0, 1)), Swim::Render::RenderObjectFlags::Default,
					{ GameTags::PhysicsToy, Engine::Tags::Dynamic } });
			AddCapsuleBody(*this, capsule, Engine::RigidbodyType::Dynamic, 0.25f, 0.25f, 1.5f);
		}

		for (int i = 0; i < 6; ++i)
		{
			const entt::entity ball = SpawnMesh(*this,
				{ "Toy sphere " + std::to_string(i + 1), palette.Mesh(Engine::BuiltinMesh::Sphere),
					Mat("Toy sphere", SrgbColor(230, 230, 235), 1.0f, 0.03f),
					PhysicsCenter + glm::vec3(3.0f + 0.9f * static_cast<float>(i % 3), 0.45f, -1.5f + 1.2f * static_cast<float>(i / 3)),
					glm::vec3(0.9f), glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Default,
					{ GameTags::PhysicsToy, Engine::Tags::Dynamic } });
			AddSphereBody(*this, ball, Engine::RigidbodyType::Dynamic, 0.5f, 3.0f);
			// Chrome: the ball's own probe is its whole reflection - the floor, the other balls and
			// whatever is behind the camera (screen-space reflections alone cannot).
			AddComponent<Engine::ReflectionProbe>(ball, Engine::ReflectionProbe{});
		}
	}

	void Sandbox::BuildParticles()
	{
		using namespace Swim::Render;
		// A fountain of glowing droplets that bounce on the ground.
		{
			const entt::entity fountain = CreateEntity("Fountain particles");
			AddComponent<Engine::Transform>(fountain, Engine::Transform(FountainCenter + glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(1.0f)));
			Engine::ParticleEmitter emitter;
			auto& d = emitter.Desc;
			d.Capacity = 6000;
			d.Blend = ParticleBlendMode::Additive;
			d.Rate = 1400.0f;
			d.Shape = ParticleShape::Sphere;
			d.ShapeExtent = { 0.08f, 0.0f, 0.0f };
			d.Direction = { 0.0f, 1.0f, 0.0f };
			d.ConeAngle = 0.22f;
			d.SpeedMin = 5.5f;
			d.SpeedMax = 7.0f;
			d.LifetimeMin = 1.6f;
			d.LifetimeMax = 2.4f;
			d.SizeMin = 0.05f;
			d.SizeMax = 0.09f;
			d.Collision = true;
			d.GroundHeight = 0.02f;
			d.Restitution = 0.35f;
			d.Friction = 0.2f;
			d.SizeOverLife = Curve(1.0f, 0.4f);
			d.ColorOverLife = Gradient({ 0.7f, 1.0f, 2.2f, 1.0f }, { 0.2f, 0.5f, 1.4f, 0.0f });
			d.Seed = 7;
			AddComponent<Engine::ParticleEmitter>(fountain, emitter);
			AddTag(fountain, Engine::Tags::Effect);
		}
		// Slow smoke above the emissive rings (alpha blended, sorted).
		{
			const entt::entity smoke = CreateEntity("Smoke particles");
			AddComponent<Engine::Transform>(smoke, Engine::Transform(glm::vec3(-11.0f, 0.4f, 6.0f), glm::vec3(1.0f)));
			Engine::ParticleEmitter emitter;
			auto& d = emitter.Desc;
			d.Capacity = 512;
			d.Blend = ParticleBlendMode::AlphaBlend;
			d.Rate = 45.0f;
			d.Shape = ParticleShape::Box;
			d.ShapeExtent = { 3.5f, 0.2f, 0.6f };
			d.Direction = { 0.0f, 1.0f, 0.0f };
			d.ConeAngle = 0.35f;
			d.SpeedMin = 0.3f;
			d.SpeedMax = 0.7f;
			d.LifetimeMin = 4.0f;
			d.LifetimeMax = 7.0f;
			d.SizeMin = 0.7f;
			d.SizeMax = 1.3f;
			d.RotationMax = 6.28f;
			d.AngularVelocityMin = -0.4f;
			d.AngularVelocityMax = 0.4f;
			d.Gravity = { 0.0f, 0.25f, 0.0f };
			d.Drag = 0.25f;
			d.SizeOverLife = Curve(0.6f, 2.2f);
			d.ColorOverLife = Gradient({ 0.35f, 0.35f, 0.4f, 0.45f }, { 0.5f, 0.5f, 0.55f, 0.0f });
			d.Seed = 11;
			AddComponent<Engine::ParticleEmitter>(smoke, emitter);
			AddTag(smoke, Engine::Tags::Effect);
		}
		// Looping spark bursts from the fountain's rim.
		{
			const entt::entity sparks = CreateEntity("Spark particles");
			AddComponent<Engine::Transform>(sparks, Engine::Transform(FountainCenter + glm::vec3(0.0f, 0.6f, 0.0f), glm::vec3(1.0f)));
			EmplaceBehavior<Spin>(sparks, glm::vec3(0, 1, 0), 60.0f);
			Engine::ParticleEmitter emitter;
			auto& d = emitter.Desc;
			d.Capacity = 1024;
			d.Blend = ParticleBlendMode::Additive;
			d.Rate = 0.0f;
			d.Duration = 1.2f;
			d.Looping = true;
			d.Bursts = { { 0.0f, 160 }, { 0.6f, 90 } };
			d.Shape = ParticleShape::Point;
			d.Direction = { 1.0f, 1.2f, 0.0f };
			d.ConeAngle = 0.5f;
			d.SpeedMin = 3.0f;
			d.SpeedMax = 6.0f;
			d.LifetimeMin = 0.6f;
			d.LifetimeMax = 1.1f;
			d.SizeMin = 0.025f;
			d.SizeMax = 0.05f;
			d.Collision = true;
			d.Restitution = 0.5f;
			d.ColorOverLife = Gradient({ 6.0f, 3.0f, 0.8f, 1.0f }, { 2.0f, 0.3f, 0.05f, 0.0f });
			d.Seed = 3;
			AddComponent<Engine::ParticleEmitter>(sparks, emitter);
			AddTag(sparks, Engine::Tags::Effect);
		}
		// The fountain basin.
		SpawnMesh(*this,
			{ "Fountain basin", palette.Mesh(Engine::BuiltinMesh::Cylinder), Mat("Basin stone", SrgbColor(140, 135, 125), 0.0f, 0.8f),
				FountainCenter + glm::vec3(0.0f, 0.3f, 0.0f), { 1.6f, 0.6f, 1.6f }, glm::quat(1, 0, 0, 0),
				Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static, { Engine::Tags::Static } });
	}

	void Sandbox::BuildTentacles()
	{
		auto* render = GetRenderServices();
		constexpr std::uint32_t joints = 5;
		constexpr float height = 2.6f;

		if (render && render->Bridge && !render->Bridge->HasSkinnedMesh("Tentacle"))
		{
			render->Bridge->RegisterSkinnedMesh("Tentacle", Engine::ProceduralMeshes::MakeSkinnedColumn(0.22f, height, joints, 20, 5));
		}

		const std::uint32_t material = Mat("Tentacle", SrgbColor(170, 60, 120), 0.1f, 0.35f, glm::vec3(0.05f, 0.0f, 0.03f));

		for (int i = 0; i < 6; ++i)
		{
			const float angle = static_cast<float>(i) * (2.0f * Pi / 6.0f) + 0.3f;
			const entt::entity tentacle = CreateEntity("Tentacle " + std::to_string(i + 1));
			AddComponent<Engine::Transform>(tentacle,
				Engine::Transform(FountainCenter + glm::vec3(std::cos(angle) * 3.0f, 0.0f, std::sin(angle) * 3.0f), glm::vec3(1.0f)));
			Engine::SkinnedMeshRenderer skinned;
			skinned.Mesh = "Tentacle";
			skinned.MaterialSet = material;
			AddComponent<Engine::SkinnedMeshRenderer>(tentacle, skinned);
			AddTag(tentacle, GameTags::Tentacle);
			EmplaceBehavior<TentacleAnimator>(tentacle, joints, height, static_cast<float>(i) * 1.1f);
		}
	}

	void Sandbox::BuildWorldUi()
	{
		auto* render = GetRenderServices();

		if (!render || !render->Ui)
		{
			return;
		}

		auto& ui = *render->Ui;
		// The info panel: a world-space document facing the start camera.
		infoDocument = ui.CreateDocument();
		{
			using namespace Swim::UI;
			UiStyle rootStyle;
			rootStyle.Flow = UiFlow::Column;
			rootStyle.Padding = { 18, 14, 18, 14 };
			rootStyle.Gap = 6;
			rootStyle.Background = UiSrgbHex(0x0a0d13, 0.93f);
			rootStyle.CornerRadius = 14;
			rootStyle.BorderWidth = 3;
			rootStyle.BorderColor = UiSrgbHex(0x1f7aff);
			rootStyle.Width = UiLength::Percent(1.0f);
			rootStyle.Height = UiLength::Percent(1.0f);
			const auto frame = CreateStyledNode(*infoDocument, infoDocument->GetRoot(), rootStyle);
			const auto title = CreateLabel(*infoDocument, frame, ui.GetBoldFonts(), "Swim Engine sandbox", 30.0f);
			(void)title;
			UiStyle bodyStyle;
			bodyStyle.Width = UiLength::Percent(1.0f);
			bodyStyle.TextWrap = Swim::Text::TextWrap::Word;
			infoBody = CreateLabel(*infoDocument, frame, ui.GetFonts(), "Starting...", 20.0f, bodyStyle);
			const auto button = CreateButton(*infoDocument, frame, "Fire a ball from here");
			CreateTooltip(*infoDocument, button, "World-space UI takes the mouse through the camera ray.");
			const entt::entity panel = CreateEntity("Info panel");
			AddComponent<Engine::Transform>(panel,
				Engine::Transform(glm::vec3(4.5f, 3.4f, -3.0f), glm::vec3(1.0f), glm::angleAxis(glm::radians(-15.0f), glm::vec3(0, 1, 0))));
			Engine::UiCanvas canvas;
			canvas.Document = infoDocument;
			canvas.Mode = UiCanvasMode::WorldPanel;
			canvas.Size = { 560.0f, 300.0f };
			canvas.UnitsPerPixel = 0.0085f;
			canvas.BlocksPointer = true;
			AddComponent<Engine::UiCanvas>(panel, canvas);
			AddTag(panel, Engine::Tags::Ui);
			infoButton = button;
		}
		// Billboard zone labels.
		const std::array<std::pair<const char*, glm::vec3>, 5> zones{ { { "PBR gallery", { -13.1f, 6.7f, -9.0f } },
			{ "Instance hall  (576 objects)", HallCenter + glm::vec3(0.0f, 3.2f, 0.0f) },
			{ "Physics playground", PhysicsCenter + glm::vec3(0.0f, 5.2f, 0.0f) },
			{ "Particles + GPU skinning", FountainCenter + glm::vec3(0.0f, 4.4f, 0.0f) },
			{ "Glass + emission", { -10.5f, 4.0f, 4.0f } } } };

		for (const auto& [text, position] : zones)
		{
			using namespace Swim::UI;
			auto document = ui.CreateDocument();
			UiStyle style;
			style.Padding = { 14, 6, 14, 6 };
			style.Background = UiSrgbHex(0x07090d, 0.85f);
			style.CornerRadius = 10;
			style.AlignSelf = UiAlign::Center;
			const auto box = CreateStyledNode(*document, document->GetRoot(), style);
			CreateLabel(*document, box, ui.GetBoldFonts(), text, 26.0f);
			const entt::entity label = CreateEntity(std::string("Label: ") + text);
			AddComponent<Engine::Transform>(label, Engine::Transform(position, glm::vec3(1.0f)));
			Engine::UiCanvas canvas;
			canvas.Document = document;
			canvas.Mode = UiCanvasMode::Billboard;
			canvas.Size = { 460.0f, 60.0f };
			canvas.UnitsPerPixel = 0.009f;
			canvas.ConstantScreenSize = true;
			canvas.ScreenPixelsPerCanvasPixel = 0.65f;
			canvas.Interactive = false;
			canvas.FadeStart = 45.0f;
			canvas.FadeEnd = 70.0f;
			AddComponent<Engine::UiCanvas>(label, canvas);
			AddTag(label, Engine::Tags::Ui);
		}
	}

	void Sandbox::BuildHud()
	{
		auto* render = GetRenderServices();

		if (!render || !render->Ui)
		{
			return;
		}

		const entt::entity hud = CreateEntity("HUD");
		auto* behavior = EmplaceBehavior<SandboxHud>(hud);
		SetEnabledStates(hud, AllStates);
		(void)behavior;
		AddTag(hud, Engine::Tags::Ui);
	}

	void Sandbox::RecordImpact(float impulse, const glm::vec3& position)
	{
		++impacts;
		strongestImpact = std::max(strongestImpact, impulse);
		PlayImpactSound(impulse, position);
	}

	void Sandbox::SpawnBallAt(const glm::vec3& position, const glm::vec3& velocity)
	{
		auto* render = GetRenderServices();
		const auto mesh = palette.Mesh(Engine::BuiltinMesh::Sphere);
		const std::uint32_t material = Mat("Rain ball", SrgbColor(250, 180, 70), 0.0f, 0.3f, glm::vec3(0.0f));
		(void)render;
		++spawned;
		GetCommandBuffer().Create(
			[position, velocity, mesh, material](Engine::Scene& owner, entt::entity ball)
			{
				owner.SetEntityName(ball, "Rain ball");
				owner.AddComponent<Engine::Transform>(ball, Engine::Transform(position, glm::vec3(0.5f)));
				Engine::MeshRenderer renderer;
				renderer.Parts.push_back({ mesh, material });
				owner.AddComponent<Engine::MeshRenderer>(ball, std::move(renderer));
				AddSphereBody(owner, ball, Engine::RigidbodyType::Dynamic, 0.5f, 2.0f);
				owner.GetRegistry().get<Engine::Rigidbody>(ball).SetInitialLinearVelocity(velocity);
				owner.AddTag(ball, Engine::Tags::Projectile);
				owner.AddTag(ball, GameTags::Spawned);
				owner.EmplaceBehavior<Lifetime>(ball, 10.0f);
				owner.EmplaceBehavior<Projectile>(ball);
			});
	}

	void Sandbox::SpawnBalls(std::uint32_t count)
	{
		std::uniform_real_distribution<float> spread(-2.5f, 2.5f);

		for (std::uint32_t i = 0; i < count; ++i)
		{
			SpawnBallAt(PhysicsCenter + glm::vec3(spread(random), 7.0f + 0.4f * static_cast<float>(i), spread(random)),
				glm::vec3(0.0f, -1.0f, 0.0f));
		}
	}

	void Sandbox::SetWaves(bool on, glm::vec2 center, float halfSize, float height, float amplitude)
	{
		if (!waveWater)
		{
			return;
		}

		auto* world = GetPhysicsWorld();

		if (world && waveFluid)
		{
			world->RemoveFluidVolume(waveFluid);
			waveFluid = {};
		}

		waveWater->Enabled = on;

		if (!on)
		{
			return;
		}

		if (ocean)
		{
			ocean->Enabled = false;
		}

		auto& settings = waveWater->Settings;
		const float inf = std::numeric_limits<float>::max();
		settings.Waves.Height = height;
		settings.Waves.Amplitude = std::max(amplitude, 0.0f);
		settings.BoundsMin =
			halfSize > 0.0f ? std::array<float, 2>{ center.x - halfSize, center.y - halfSize } : std::array<float, 2>{ -inf, -inf };
		settings.BoundsMax =
			halfSize > 0.0f ? std::array<float, 2>{ center.x + halfSize, center.y + halfSize } : std::array<float, 2>{ inf, inf };

		if (!world)
		{
			return;
		}

		// The water and the physics share the waves and the clock (simulated seconds).
		waveWater->Clock = [world]
		{
			return world->GetSimulationTime();
		};
		Engine::FluidVolumeDesc fluid;
		fluid.Surface = Engine::MakeFluidSurface(waveWater->GetWaves());
		fluid.SurfaceHeight = height;
		fluid.BoundsMin = glm::vec3(settings.BoundsMin[0], -inf, settings.BoundsMin[1]);
		fluid.BoundsMax = glm::vec3(settings.BoundsMax[0], inf, settings.BoundsMax[1]);
		waveFluid = world->AddFluidVolume(std::move(fluid));
	}

	void Sandbox::SetOcean(bool on, float height, float windSpeed)
	{
		if (!ocean)
		{
			return;
		}

		auto* world = GetPhysicsWorld();

		if (world && waveFluid)
		{
			world->RemoveFluidVolume(waveFluid);
			waveFluid = {};
		}

		ocean->Enabled = on;

		if (!on)
		{
			return;
		}

		if (waveWater)
		{
			waveWater->Enabled = false;
		}

		ocean->Settings.Height = height;
		ocean->Simulation->Settings.WindSpeed = std::max(windSpeed, 0.1f);

		if (world)
		{
			Engine::FluidVolumeDesc fluid;
			fluid.Surface = Engine::MakeOceanFluidSurface(ocean->Simulation, height);
			fluid.SurfaceHeight = height;
			waveFluid = world->AddFluidVolume(std::move(fluid));
		}
	}

	void Sandbox::SpawnCutouts(float far)
	{
		auto* render = GetRenderServices();
		const auto* cameras = GetCameraSystem();

		if (!render || !render->HasRenderer() || !cameras)
		{
			return;
		}

		auto texture = render->Meshes->FindTexture("SandboxFlower");

		if (!texture.IsValid())
		{
			// 16 x 16: a green stem and leaves, a red bloom; clear texels white.
			std::vector<std::uint8_t> rgba(16 * 16 * 4, 255);

			for (int y = 0; y < 16; ++y)
			{
				for (int x = 0; x < 16; ++x)
				{
					auto* t = &rgba[(y * 16 + x) * 4];
					const bool stem = x == 7 && y >= 6;
					const bool leaf = (y == 10 && x >= 4 && x <= 6) || (y == 12 && x >= 8 && x <= 10);
					const bool bloom = (x - 7) * (x - 7) + (y - 4) * (y - 4) <= 5;
					t[3] = stem || leaf || bloom ? 255 : 0;

					if (bloom)
					{
						t[0] = 200, t[1] = 30, t[2] = 30;
					}
					else if (stem || leaf)
					{
						t[0] = 40, t[1] = 130, t[2] = 40;
					}
				}
			}

			texture = render->Meshes->RegisterTexture("SandboxFlower", 16, 16, rgba);
		}

		Engine::MaterialDesc desc;
		desc.Name = "Sandbox flower";
		desc.Blend = Engine::MaterialBlend::Masked;
		desc.DoubleSided = true;
		desc.PointSampled = true;
		desc.Roughness = 0.4f;
		desc.BaseColorTexture = texture;
		const std::uint32_t material = render->Materials->GetOrCreate(desc);
		auto mesh = render->Meshes->Find("SandboxCard");

		if (!mesh.IsValid())
		{
			mesh = render->Meshes->Register("SandboxCard", Engine::ProceduralMeshes::MakePlane(1.0f, 1, 1.0f));
		}

		const auto& camera = cameras->GetCamera();
		glm::vec3 forward = camera.GetForward();
		forward.y = 0.0f;
		forward = glm::length(forward) > 1.0e-3f ? glm::normalize(forward) : glm::vec3(0, 0, -1);
		const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
		const glm::quat upright = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));

		for (float d = 8.0f; d <= far; d *= 1.06f)
		{
			for (int i = -6; i <= 6; ++i)
			{
				const float spread = d * 0.05f * float(i);
				const glm::vec3 at = camera.GetPosition() + forward * d + right * spread;

				for (int card = 0; card < 2; ++card)
				{
					const glm::quat turn =
						glm::angleAxis(glm::radians(45.0f + 90.0f * card + 7.0f * float(i)), glm::vec3(0, 1, 0)) * upright;
					SpawnMesh(*this,
						{ "Flower card", mesh, material, glm::vec3(at.x, 0.5f, at.z), glm::vec3(1.0f), turn,
							Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static, { Engine::Tags::Static } });
				}
			}
		}
	}

	void Sandbox::SpawnFloaters(std::uint32_t count, glm::vec3 around)
	{
		std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
		std::uniform_real_distribution<float> hue(0.0f, 1.0f);

		for (std::uint32_t i = 0; i < count; ++i)
		{
			const int kind = static_cast<int>(spawned % 3);
			const auto mesh = kind == 0 ? Engine::BuiltinMesh::Cube
				: kind == 1				? Engine::BuiltinMesh::Sphere
										: Engine::BuiltinMesh::Capsule;
			const float h = hue(random);
			const glm::vec3 color{ 0.5f + 0.5f * std::cos(6.2831f * h), 0.5f + 0.5f * std::cos(6.2831f * (h + 0.33f)),
				0.5f + 0.5f * std::cos(6.2831f * (h + 0.67f)) };
			const std::uint32_t material = Mat("Floater " + std::to_string(spawned % 8), color, 0.0f, 0.5f);
			const glm::vec3 position = around + glm::vec3(unit(random) * 4.0f, 1.5f + 0.6f * float(i % 5), unit(random) * 4.0f);
			const glm::vec3 scale = kind == 2 ? glm::vec3(0.6f, 1.2f, 0.6f) : glm::vec3(0.8f);
			const entt::entity entity = SpawnMesh(*this,
				{ "Floater", palette.Mesh(mesh), material, position, scale,
					glm::angleAxis(unit(random) * 3.14159f, glm::normalize(glm::vec3(unit(random), 1.0f, unit(random)))),
					Swim::Render::RenderObjectFlags::Default, { GameTags::Spawned, Engine::Tags::Dynamic } });
			Engine::Rigidbody body;
			body.type = Engine::RigidbodyType::Dynamic;
			body.buoyant = true;

			// Half to two thirds as dense as water: they float, the figures low in it.
			if (kind == 0)
			{
				body.collider.type = Engine::ColliderType::Box;
				body.collider.box.halfExtents = glm::vec3(0.5f);
				body.mass = 0.512f * 450.0f;
			}
			else if (kind == 1)
			{
				body.collider.type = Engine::ColliderType::Sphere;
				body.collider.sphere.radius = 0.5f;
				body.mass = 0.268f * 500.0f;
			}
			else
			{
				body.collider.type = Engine::ColliderType::Capsule;
				body.collider.capsule.radius = 0.5f;
				body.collider.capsule.halfHeight = 0.25f;
				body.mass = 0.283f * 650.0f;
			}

			// Thrown in: a toss toward the middle with some spin.
			body.SetInitialLinearVelocity(glm::vec3(unit(random) * 3.0f, 2.0f + 2.0f * hue(random), unit(random) * 3.0f));
			body.SetInitialAngularVelocity(glm::vec3(unit(random), unit(random), unit(random)) * 3.0f);
			AddComponent<Engine::Rigidbody>(entity, body);
			AddTag(entity, Engine::Tags::Physics);
			++spawned;
		}
	}

	entt::entity Sandbox::SpawnPrimitive(Engine::BuiltinMesh mesh)
	{
		glm::vec3 position = PhysicsCenter + glm::vec3(0.0f, 6.0f, 2.0f);

		if (const auto* cameras = GetCameraSystem())
		{
			const auto& camera = cameras->GetCamera();
			position = camera.GetPosition() + camera.GetForward() * 4.0f;
			position.y = std::max(position.y, 1.0f);
		}

		std::uniform_real_distribution<float> hue(0.0f, 1.0f);
		const float h = hue(random);
		const glm::vec3 color{ 0.5f + 0.5f * std::cos(6.2831f * h), 0.5f + 0.5f * std::cos(6.2831f * (h + 0.33f)),
			0.5f + 0.5f * std::cos(6.2831f * (h + 0.67f)) };
		const std::uint32_t material = Mat("Spawned " + std::to_string(spawned % 8), color, (spawned % 2) ? 0.0f : 1.0f, 0.35f);
		const entt::entity entity = SpawnMesh(*this,
			{ "Spawned " + std::string(mesh == Engine::BuiltinMesh::Cube ? "cube" : "shape"), palette.Mesh(mesh), material, position,
				glm::vec3(0.8f), glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Default,
				{ GameTags::Spawned, Engine::Tags::Dynamic } });

		switch (mesh)
		{
		case Engine::BuiltinMesh::Sphere:
			AddSphereBody(*this, entity, Engine::RigidbodyType::Dynamic, 0.5f, 2.0f);
			break;
		case Engine::BuiltinMesh::Capsule:
			AddCapsuleBody(*this, entity, Engine::RigidbodyType::Dynamic, 0.25f, 0.25f, 1.5f);
			break;
		default:
			AddBoxBody(*this, entity, Engine::RigidbodyType::Dynamic, glm::vec3(0.5f), 2.0f);
			break;
		}

		++spawned;
		return entity;
	}

	void Sandbox::Update(double dt)
	{
		if (!rainBalls || dt <= 0.0)
		{
			return;
		}

		rainTimer += static_cast<float>(dt);

		while (rainTimer >= 0.15f)
		{
			rainTimer -= 0.15f;
			SpawnBalls(1);
		}
	}

} // namespace Game
