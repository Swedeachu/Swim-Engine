#include "Engine/Systems/Replay/Runtime/SceneReplayPresenter.h"

#include "Engine/Commands/CommandRegistry.h"
#include "Engine/Components/Light.h"
#include "Engine/Components/MeshRenderer.h"
#include "Engine/Components/ParticleEmitter.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Audio/AudioSystem.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Features/CameraEffects.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Engine/Systems/Replay/Runtime/SceneReplaySource.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Tooling/Json.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_map>

namespace Engine
{

	using namespace Swim::Replay;
	using Swim::Render::RenderObjectFlags;
	using Swim::Tooling::Json;

	namespace
	{

		glm::vec3 ToGlm(const Vec3& v)
		{
			return { v[0], v[1], v[2] };
		}

		glm::quat ToGlm(const Quat& q)
		{
			return glm::quat(q[3], q[0], q[1], q[2]);
		}

		std::string CacheKey(const AssetRecord& record)
		{
			char hash[24];
			std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(record.Hash));
			return std::string(ToString(record.Kind)) + "|" + hash + "|" + record.Key;
		}

		std::string HashName(const char* prefix, std::uint64_t hash)
		{
			char text[24];
			std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
			return std::string(prefix) + text;
		}

		float ScalarOf(const Value& value)
		{
			if (const float* f = std::get_if<float>(&value))
			{
				return *f;
			}

			if (const std::int64_t* i = std::get_if<std::int64_t>(&value))
			{
				return static_cast<float>(*i);
			}

			if (const bool* b = std::get_if<bool>(&value))
			{
				return *b ? 1.0f : 0.0f;
			}

			return 0.0f;
		}

		std::array<float, 3> ColorOf(const Value& value)
		{
			if (const Vec3* v = std::get_if<Vec3>(&value))
			{
				return *v;
			}

			const float f = ScalarOf(value);
			return { f, f, f };
		}

		// Environment keys that drive post-processing directly (the rest go to console
		// variables). False for any other name.
		bool ApplyPost(Swim::Render::PostProcessSettings& post, const std::string& name, const Value& value)
		{
			if (name == "post.exposure")
			{
				post.Exposure.Compensation = ScalarOf(value);
			}
			else if (name == "post.ev100")
			{
				post.Exposure.Mode = Swim::Render::ExposureMode::Manual;
				post.Exposure.ManualEv100 = ScalarOf(value);
			}
			else if (name == "grade.temperature")
			{
				post.Grading.Temperature = std::clamp(ScalarOf(value), -100.0f, 100.0f);
			}
			else if (name == "grade.tint")
			{
				post.Grading.Tint = std::clamp(ScalarOf(value), -100.0f, 100.0f);
			}
			else if (name == "grade.contrast")
			{
				post.Grading.Contrast = std::max(ScalarOf(value), 0.01f);
			}
			else if (name == "grade.saturation")
			{
				post.Grading.Saturation = std::max(ScalarOf(value), 0.0f);
			}
			else if (name == "grade.slope")
			{
				post.Grading.Slope = ColorOf(value);
			}
			else if (name == "grade.offset")
			{
				post.Grading.Offset = ColorOf(value);
			}
			else if (name == "grade.power")
			{
				const std::array<float, 3> power = ColorOf(value);
				post.Grading.Power = { std::max(power[0], 0.01f), std::max(power[1], 0.01f), std::max(power[2], 0.01f) };
			}
			else if (name == "bloom.intensity")
			{
				post.Bloom.Intensity = std::max(ScalarOf(value), 0.0f);
			}
			else
			{
				return false;
			}

			return true;
		}

		bool IsPostName(std::string_view name)
		{
			return name.starts_with("post.") || name.starts_with("grade.") || name.starts_with("bloom.");
		}

		std::string ValueText(const Value& value)
		{
			return std::visit(
				[](const auto& v) -> std::string
				{
					using T = std::decay_t<decltype(v)>;

					if constexpr (std::is_same_v<T, bool>)
					{
						return v ? "1" : "0";
					}
					else if constexpr (std::is_same_v<T, std::string>)
					{
						return v;
					}
					else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, AssetId>)
					{
						return std::to_string(v);
					}
					else if constexpr (std::is_same_v<T, float>)
					{
						return std::to_string(v);
					}
					else
					{
						std::string text;

						for (float f : v)
						{
							text += (text.empty() ? "" : " ") + std::to_string(f);
						}

						return text;
					}
				},
				value);
		}

	} // namespace

	struct SceneReplayPresenter::State
	{
		SceneReplayPresenterDesc Desc;
		std::unordered_map<NodeId, entt::entity> Entities;
		std::unordered_map<std::string, IReplayPresenter*> Kinds;

		// Resolved assets by kind, key and hash (stable across recordings).
		std::unordered_map<std::string, MeshLibrary::MeshHandle> Meshes;
		std::unordered_map<std::string, MeshLibrary::TextureHandle> Textures;
		std::unordered_map<std::string, std::uint32_t> Materials;
		std::unordered_map<std::string, std::optional<Swim::Render::ParticleEmitterDesc>> Emitters;
		std::vector<std::string> RegisteredMeshes;
		std::vector<std::string> RegisteredTextures;
		std::vector<std::uint32_t> CreatedMaterials;

		struct SavedCamera
		{
			glm::vec3 Position;
			glm::quat Rotation;
			float Fov;
		};

		std::optional<SavedCamera> Camera;
		std::optional<std::pair<bool, DepthOfField::SettingsData>> Focus;
		std::map<std::string, std::string> Variables;		   // Original values of changed console variables.
		std::optional<Swim::Render::PostProcessSettings> Post; // Original post-processing while keys drive it.

		void RestorePost()
		{
			if (Post && Desc.Render.Settings)
			{
				Desc.Render.Settings->Post = *Post;
			}

			Post.reset();
		}

		const std::string& KindName(NameId kind) const { return Desc.Player->GetNames().Get(kind); }

		IReplayPresenter* Forward(NameId kind) const
		{
			const auto found = Kinds.find(KindName(kind));
			return found == Kinds.end() ? nullptr : found->second;
		}

		const Value* Prop(const NodeState& node, std::string_view name) const
		{
			const auto id = Desc.Player->GetNames().Find(name);
			return id ? node.Find(*id) : nullptr;
		}

		float Float(const NodeState& node, std::string_view name, float fallback) const
		{
			const Value* value = Prop(node, name);

			if (const float* f = value ? std::get_if<float>(value) : nullptr)
			{
				return *f;
			}

			if (const std::int64_t* i = value ? std::get_if<std::int64_t>(value) : nullptr)
			{
				return static_cast<float>(*i);
			}

			return fallback;
		}

		bool Bool(const NodeState& node, std::string_view name, bool fallback) const
		{
			const Value* value = Prop(node, name);
			const bool* b = value ? std::get_if<bool>(value) : nullptr;
			return b ? *b : fallback;
		}

		std::int64_t Int(const NodeState& node, std::string_view name, std::int64_t fallback) const
		{
			const Value* value = Prop(node, name);
			const std::int64_t* i = value ? std::get_if<std::int64_t>(value) : nullptr;
			return i ? *i : fallback;
		}

		MeshLibrary::MeshHandle ResolveMesh(AssetId id)
		{
			const AssetRecord* record = Desc.Player->GetAsset(id);

			if (!record)
			{
				return {};
			}

			const std::string key = CacheKey(*record);

			if (const auto found = Meshes.find(key); found != Meshes.end())
			{
				return found->second;
			}

			MeshLibrary::MeshHandle mesh;

			if (Desc.ResolveMesh)
			{
				mesh = Desc.ResolveMesh(*record);
			}

			if (!mesh.IsValid() && record->HasData)
			{
				ProceduralMeshes::MeshData data;

				if (ReplayAssets::DecodeMesh(Desc.Player->LoadAssetData(id), data))
				{
					const std::string name = HashName("Replay/mesh/", record->Hash);
					mesh = Desc.Render.Meshes->Find(name);

					if (!mesh.IsValid())
					{
						mesh = Desc.Render.Meshes->Register(name, data);
						RegisteredMeshes.push_back(name);
					}
				}
			}

			if (!mesh.IsValid() && !record->Key.empty())
			{
				mesh = Desc.Render.Meshes->Find(record->Key);

				if (!mesh.IsValid() && !record->Key.starts_with("Procedural/"))
				{
					mesh = Desc.Render.Meshes->Load(record->Key);
				}
			}

			Meshes.emplace(key, mesh);
			return mesh;
		}

		MeshLibrary::TextureHandle ResolveTexture(AssetId id)
		{
			const AssetRecord* record = Desc.Player->GetAsset(id);

			if (!record)
			{
				return {};
			}

			const std::string key = CacheKey(*record);

			if (const auto found = Textures.find(key); found != Textures.end())
			{
				return found->second;
			}

			MeshLibrary::TextureHandle texture;
			ReplayAssets::TextureData data;

			if (record->HasData && ReplayAssets::DecodeTexture(Desc.Player->LoadAssetData(id), data))
			{
				const std::string name = HashName("Replay/texture/", record->Hash);
				texture = Desc.Render.Meshes->FindTexture(name);

				if (!texture.IsValid())
				{
					texture = Desc.Render.Meshes->RegisterTexture(name, data.Width, data.Height, data.Rgba8, data.Srgb, data.Mips);
					RegisteredTextures.push_back(name);
				}
			}

			if (!texture.IsValid() && !record->Key.empty())
			{
				texture = Desc.Render.Meshes->FindTexture(record->Key);
			}

			Textures.emplace(key, texture);
			return texture;
		}

		std::uint32_t ResolveMaterial(AssetId id)
		{
			const AssetRecord* record = Desc.Player->GetAsset(id);

			if (!record)
			{
				return Swim::Render::GpuInstanceRecord::InvalidIndex;
			}

			const std::string key = CacheKey(*record);

			if (const auto found = Materials.find(key); found != Materials.end())
			{
				return found->second;
			}

			std::uint32_t set = Swim::Render::GpuInstanceRecord::InvalidIndex;

			if (Desc.ResolveMaterial)
			{
				set = Desc.ResolveMaterial(*record);
			}

			if (set == Swim::Render::GpuInstanceRecord::InvalidIndex && record->HasData)
			{
				const auto bytes = Desc.Player->LoadAssetData(id);
				const auto parsed = Json::TryParse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));

				if (parsed && parsed->IsObject())
				{
					const Json& j = *parsed;
					MaterialDesc desc;
					// Its own name: a live material of the recorded name may differ.
					desc.Name = HashName("Replay/material/", record->Hash);

					for (std::size_t i = 0; i < 4 && i < j["baseColor"].Size(); ++i)
					{
						desc.BaseColor[i] = j["baseColor"][i].AsFloat(desc.BaseColor[i]);
					}

					for (std::size_t i = 0; i < 3 && i < j["emissive"].Size(); ++i)
					{
						desc.Emissive[i] = j["emissive"][i].AsFloat(desc.Emissive[i]);
					}

					desc.Metallic = j["metallic"].AsFloat(desc.Metallic);
					desc.Roughness = j["roughness"].AsFloat(desc.Roughness);
					desc.Blend = static_cast<MaterialBlend>(j["blend"].AsInt(static_cast<int>(desc.Blend)));
					desc.AlphaCutoff = j["alphaCutoff"].AsFloat(desc.AlphaCutoff);
					desc.DoubleSided = j["doubleSided"].AsBool(desc.DoubleSided);
					desc.PointSampled = j["pointSampled"].AsBool(desc.PointSampled);
					desc.OcclusionStrength = j["occlusionStrength"].AsFloat(desc.OcclusionStrength);
					desc.NormalScale = j["normalScale"].AsFloat(desc.NormalScale);
					const std::pair<const char*, MeshLibrary::TextureHandle*> textures[] = { { "baseColorTexture", &desc.BaseColorTexture },
						{ "metallicRoughnessTexture", &desc.MetallicRoughnessTexture }, { "normalTexture", &desc.NormalTexture },
						{ "emissiveTexture", &desc.EmissiveTexture }, { "occlusionTexture", &desc.OcclusionTexture } };

					for (const auto& [field, handle] : textures)
					{
						if (const Json* texture = j.Find(field); texture && texture->IsNumber())
						{
							*handle = ResolveTexture(static_cast<AssetId>(texture->AsInt()));
						}
					}

					const std::uint32_t existing = Desc.Render.Materials->FindByName(desc.Name);
					set = existing != 0 ? existing : Desc.Render.Materials->Create(desc);

					if (existing == 0)
					{
						CreatedMaterials.push_back(set);
					}
				}
			}

			if (set == Swim::Render::GpuInstanceRecord::InvalidIndex && !record->Key.empty())
			{
				std::string_view name = record->Key;

				if (name.starts_with("material:"))
				{
					name.remove_prefix(9);
				}

				set = Desc.Render.Materials->FindByName(name);
			}

			Materials.emplace(key, set);
			return set;
		}

		const Swim::Render::ParticleEmitterDesc* ResolveEmitter(AssetId id)
		{
			const AssetRecord* record = Desc.Player->GetAsset(id);

			if (!record)
			{
				return nullptr;
			}

			auto [found, added] = Emitters.try_emplace(CacheKey(*record));

			if (added && record->HasData)
			{
				Swim::Render::ParticleEmitterDesc desc;

				if (ReplayAssets::DecodeEmitter(Desc.Player->LoadAssetData(id), desc))
				{
					found->second = desc;
				}
			}

			return found->second ? &*found->second : nullptr;
		}

		MeshRenderer Renderer(const NodeState& node)
		{
			MeshRenderer renderer;
			MeshRendererPart part;
			part.Mesh = node.Assets.size() > 0 ? ResolveMesh(node.Assets[0]) : MeshLibrary::MeshHandle{};
			part.MaterialSet = node.Assets.size() > 1 ? ResolveMaterial(node.Assets[1]) : Swim::Render::GpuInstanceRecord::InvalidIndex;

			if (part.Mesh.IsValid())
			{
				renderer.Parts.push_back(part);
			}

			renderer.Flags = RenderObjectFlags::None;

			if (node.Visible)
			{
				renderer.Flags = renderer.Flags | RenderObjectFlags::Visible;
			}

			if (Bool(node, "castShadows", true))
			{
				renderer.Flags = renderer.Flags | RenderObjectFlags::CastShadows;
			}

			return renderer;
		}

		Light MakeLight(const NodeState& node) const
		{
			Light light;
			const Value* type = Prop(node, "type");
			const std::string* name = type ? std::get_if<std::string>(type) : nullptr;
			light.Kind = !name			 ? LightKind::Point
				: *name == "directional" ? LightKind::Directional
				: *name == "spot"		 ? LightKind::Spot
										 : LightKind::Point;

			if (const Value* color = Prop(node, "color"))
			{
				if (const Vec3* c = std::get_if<Vec3>(color))
				{
					light.Color = ToGlm(*c);
				}
			}

			light.Intensity = Float(node, "intensity", light.Intensity);
			light.Range = Float(node, "range", light.Range);
			light.InnerCone = Float(node, "innerCone", light.InnerCone);
			light.OuterCone = Float(node, "outerCone", light.OuterCone);
			light.CastShadows = Bool(node, "castShadows", light.CastShadows);
			light.ShadowPriority = Float(node, "shadowPriority", light.ShadowPriority);
			light.Enabled = node.Visible;
			return light;
		}

		DepthOfField* FindFocus() const
		{
			if (!Desc.Render.Renderer)
			{
				return nullptr;
			}

			for (const auto& feature : Desc.Render.Renderer->GetFeatures())
			{
				if (auto* focus = dynamic_cast<DepthOfField*>(feature.get()))
				{
					return focus;
				}
			}

			return nullptr;
		}

		void RestoreFocus()
		{
			if (Focus)
			{
				if (DepthOfField* focus = FindFocus())
				{
					focus->Enabled = Focus->first;
					focus->Settings = Focus->second;
				}

				Focus.reset();
			}
		}

		void RestoreVariable(const std::string& name, const std::string& text)
		{
			if (Desc.Commands)
			{
				if (Swim::Commands::ConsoleVariable* variable = Desc.Commands->FindVariable(name))
				{
					variable->SetFromText(text);
				}
			}
		}
	};

	SceneReplayPresenter::SceneReplayPresenter(SceneReplayPresenterDesc desc) : state(std::make_unique<State>())
	{
		state->Desc = std::move(desc);
	}

	SceneReplayPresenter::~SceneReplayPresenter()
	{
		ReleaseAssets();
	}

	void SceneReplayPresenter::ReleaseAssets()
	{
		Clear();
		State& s = *state;

		if (s.Desc.Render.Meshes)
		{
			for (const std::string& name : s.RegisteredMeshes)
			{
				s.Desc.Render.Meshes->Unregister(name);
			}

			for (const std::string& name : s.RegisteredTextures)
			{
				s.Desc.Render.Meshes->UnregisterTexture(name);
			}
		}

		if (s.Desc.Render.Materials)
		{
			for (const std::uint32_t set : s.CreatedMaterials)
			{
				s.Desc.Render.Materials->Release(set);
			}
		}

		s.RegisteredMeshes.clear();
		s.RegisteredTextures.clear();
		s.CreatedMaterials.clear();
		s.Meshes.clear();
		s.Textures.clear();
		s.Materials.clear();
		s.Emitters.clear();
	}

	void SceneReplayPresenter::RegisterKind(std::string_view kind, IReplayPresenter& presenter)
	{
		state->Kinds[std::string(kind)] = &presenter;
	}

	void SceneReplayPresenter::UnregisterKind(std::string_view kind)
	{
		state->Kinds.erase(std::string(kind));
	}

	void SceneReplayPresenter::Clear()
	{
		State& s = *state;

		if (s.Desc.Scene)
		{
			const entt::registry& registry = s.Desc.Scene->GetRegistry();

			for (const auto& [id, entity] : s.Entities)
			{
				if (registry.valid(entity))
				{
					s.Desc.Scene->DestroyEntity(entity);
				}
			}
		}

		s.Entities.clear();

		if (s.Camera && s.Desc.Cameras)
		{
			Camera& camera = s.Desc.Cameras->GetCamera();
			camera.SetPosition(s.Camera->Position);
			camera.SetRotation(s.Camera->Rotation);
			camera.SetFieldOfView(s.Camera->Fov);
		}

		s.Camera.reset();
		s.RestoreFocus();

		for (const auto& [name, text] : s.Variables)
		{
			s.RestoreVariable(name, text);
		}

		s.Variables.clear();
		s.RestorePost();
	}

	entt::entity SceneReplayPresenter::FindEntity(NodeId id) const
	{
		const auto found = state->Entities.find(id);
		return found == state->Entities.end() ? entt::null : found->second;
	}

	void SceneReplayPresenter::OnAdd(const NodeState& node)
	{
		State& s = *state;

		if (IReplayPresenter* presenter = s.Forward(node.Kind))
		{
			presenter->OnAdd(node);
			return;
		}

		if (!s.Desc.Scene || !s.Desc.Player || !s.Desc.Render.Meshes || !s.Desc.Render.Materials)
		{
			return;
		}

		const std::string& kind = s.KindName(node.Kind);
		const bool mesh = kind == Names::Mesh;
		const bool light = kind == Names::Light;
		const bool emitter = kind == Names::Emitter;

		if (!mesh && !light && !emitter)
		{
			return;
		}

		Scene& scene = *s.Desc.Scene;
		const entt::entity entity = scene.CreateEntity();
		s.Entities[node.Id] = entity;
		scene.AddComponent(entity, Transform(ToGlm(node.Pose.Position), ToGlm(node.Pose.Scale), ToGlm(node.Pose.Rotation)));

		if (mesh)
		{
			scene.AddComponent(entity, s.Renderer(node));
		}
		else if (light)
		{
			scene.AddComponent(entity, s.MakeLight(node));
		}
		else
		{
			ParticleEmitter particles;

			if (const auto* desc = node.Assets.empty() ? nullptr : s.ResolveEmitter(node.Assets[0]))
			{
				particles.Desc = *desc;
			}

			particles.Emitting = node.Visible && s.Bool(node, "emitting", true);
			scene.AddComponent(entity, std::move(particles));
		}
	}

	void SceneReplayPresenter::OnUpdate(const NodeState& node, const NodeState& before)
	{
		State& s = *state;

		if (node.Kind != before.Kind)
		{
			OnRemove(before);
			OnAdd(node);
			return;
		}

		if (IReplayPresenter* presenter = s.Forward(node.Kind))
		{
			presenter->OnUpdate(node, before);
			return;
		}

		const auto found = s.Entities.find(node.Id);

		if (found == s.Entities.end() || !s.Desc.Scene)
		{
			return;
		}

		entt::registry& registry = s.Desc.Scene->GetRegistry();
		const entt::entity entity = found->second;

		if (!registry.valid(entity))
		{
			return;
		}

		if (Transform* transform = registry.try_get<Transform>(entity))
		{
			transform->SetPosition(ToGlm(node.Pose.Position));
			transform->SetRotation(ToGlm(node.Pose.Rotation));
			transform->SetScale(ToGlm(node.Pose.Scale));
		}

		const bool changed = node.Visible != before.Visible || node.Assets != before.Assets || node.Props != before.Props;

		if (!changed)
		{
			return;
		}

		if (registry.all_of<MeshRenderer>(entity))
		{
			MeshRenderer renderer = s.Renderer(node);
			const MeshRenderer& current = registry.get<MeshRenderer>(entity);

			if (renderer.Parts != current.Parts || renderer.Flags != current.Flags)
			{
				registry.replace<MeshRenderer>(entity, std::move(renderer));
			}
		}
		else if (registry.all_of<Light>(entity))
		{
			registry.replace<Light>(entity, s.MakeLight(node));
		}
		else if (registry.all_of<ParticleEmitter>(entity))
		{
			registry.patch<ParticleEmitter>(entity,
				[&](ParticleEmitter& particles)
				{
					if (node.Assets != before.Assets || s.Int(node, "revision", 0) != s.Int(before, "revision", 0))
					{
						if (const auto* desc = node.Assets.empty() ? nullptr : s.ResolveEmitter(node.Assets[0]))
						{
							particles.Desc = *desc;
						}

						++particles.Revision;
					}

					particles.Emitting = node.Visible && s.Bool(node, "emitting", true);
				});
		}
	}

	void SceneReplayPresenter::OnRemove(const NodeState& node)
	{
		State& s = *state;

		if (IReplayPresenter* presenter = s.Forward(node.Kind))
		{
			presenter->OnRemove(node);
			return;
		}

		const auto found = s.Entities.find(node.Id);

		if (found == s.Entities.end())
		{
			return;
		}

		if (s.Desc.Scene && s.Desc.Scene->GetRegistry().valid(found->second))
		{
			s.Desc.Scene->DestroyEntity(found->second);
		}

		s.Entities.erase(found);
	}

	void SceneReplayPresenter::OnEvent(const ReplayEvent& event)
	{
		State& s = *state;

		if (IReplayPresenter* presenter = s.Forward(event.Kind))
		{
			presenter->OnEvent(event);
			return;
		}

		if (!s.Desc.Audio || !s.Desc.Player || s.KindName(event.Kind) != Names::Sound)
		{
			return;
		}

		const AssetRecord* record = s.Desc.Player->GetAsset(event.Asset);

		if (!record || record->Key.empty())
		{
			return;
		}

		NodeState props;
		props.Props = event.Props;
		PlayParams params;
		params.Volume = s.Float(props, "volume", 1.0f);
		params.Pitch = s.Float(props, "pitch", 1.0f);

		if (s.Bool(props, "positional", false))
		{
			params.Position = ToGlm(event.Position);
		}

		s.Desc.Audio->Play(record->Key, params);
	}

	void SceneReplayPresenter::OnCamera(const CameraPose& pose)
	{
		State& s = *state;

		if (!pose.Valid || !s.Desc.Cameras)
		{
			return;
		}

		Camera& camera = s.Desc.Cameras->GetCamera();

		if (!s.Camera)
		{
			s.Camera = State::SavedCamera{ camera.GetPosition(), camera.GetRotation(), camera.GetFieldOfView() };
		}

		camera.SetPosition(ToGlm(pose.Position));
		camera.SetRotation(ToGlm(pose.Rotation));
		camera.SetFieldOfView(pose.Fov);

		if (pose.Aperture <= 0.0f)
		{
			s.RestoreFocus();
			return;
		}

		if (DepthOfField* focus = s.FindFocus())
		{
			if (!s.Focus)
			{
				s.Focus.emplace(focus->Enabled, focus->Settings);
			}

			focus->Enabled = true;
			focus->Settings.FNumber = pose.Aperture;
			focus->Settings.FocusDistance = pose.FocusDistance;
		}
	}

	void SceneReplayPresenter::OnEnvironment(const std::map<std::string, Value>& environment)
	{
		State& s = *state;

		// Post-processing keys apply on top of the settings found before the first one,
		// which come back when no such key is left (or on Clear).
		if (RenderSettings* settings = s.Desc.Render.Settings)
		{
			bool any = false;

			for (const auto& [name, value] : environment)
			{
				any = any || IsPostName(name);
			}

			if (any)
			{
				if (!s.Post)
				{
					s.Post = settings->Post;
				}

				settings->Post = *s.Post;

				for (const auto& [name, value] : environment)
				{
					ApplyPost(settings->Post, name, value);
				}
			}
			else
			{
				s.RestorePost();
			}
		}

		if (s.Desc.Environment)
		{
			s.Desc.Environment(environment);
			return;
		}

		if (!s.Desc.Commands)
		{
			return;
		}

		for (auto it = s.Variables.begin(); it != s.Variables.end();)
		{
			if (!environment.contains(it->first))
			{
				s.RestoreVariable(it->first, it->second);
				it = s.Variables.erase(it);
			}
			else
			{
				++it;
			}
		}

		for (const auto& [name, value] : environment)
		{
			Swim::Commands::ConsoleVariable* variable = s.Desc.Commands->FindVariable(name);

			if (!variable || std::holds_alternative<AssetId>(value))
			{
				continue;
			}

			s.Variables.try_emplace(name, variable->ToString());
			variable->SetFromText(ValueText(value));
		}
	}

} // namespace Engine
