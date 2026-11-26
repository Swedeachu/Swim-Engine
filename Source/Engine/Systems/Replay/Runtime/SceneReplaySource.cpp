#include "Engine/Systems/Replay/Runtime/SceneReplaySource.h"

#include "Engine/Components/Light.h"
#include "Engine/Components/MeshRenderer.h"
#include "Engine/Components/ParticleEmitter.h"
#include "Engine/Components/ReplayAnnotation.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Audio/AudioSystem.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Tooling/Json.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace Engine
{

	using namespace Swim::Replay;
	using Swim::Tooling::Json;

	namespace
	{

		constexpr std::uint32_t MeshMagic = 0x444D5253;	   // "SRMD"
		constexpr std::uint32_t TextureMagic = 0x58545253; // "SRTX"
		constexpr std::uint32_t EmitterMagic = 0x4D455253; // "SREM"

		static_assert(std::is_trivially_copyable_v<Swim::Render::StandardVertex>);

		template <typename T> void Put(std::vector<std::uint8_t>& out, const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
			out.insert(out.end(), p, p + sizeof(T));
		}

		template <typename T> bool Get(std::span<const std::uint8_t>& in, T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>);

			if (in.size() < sizeof(T))
			{
				return false;
			}

			std::memcpy(&value, in.data(), sizeof(T));
			in = in.subspan(sizeof(T));
			return true;
		}

		// Every serialized field of an emitter, for reading and writing alike. Bursts are
		// handled separately. TextureIndex/SamplerIndex are bindless indices of this run.
		template <typename D, typename F> void VisitEmitter(D& d, F&& f)
		{
			f(d.Capacity), f(d.Space), f(d.Blend), f(d.Rate), f(d.Duration), f(d.Looping), f(d.Shape), f(d.ShapeExtent), f(d.SurfaceOnly);
			f(d.DirectionMode), f(d.Direction), f(d.ConeAngle), f(d.SpeedMin), f(d.SpeedMax), f(d.LifetimeMin), f(d.LifetimeMax);
			f(d.SizeMin), f(d.SizeMax), f(d.RotationMin), f(d.RotationMax), f(d.AngularVelocityMin), f(d.AngularVelocityMax);
			f(d.Gravity), f(d.Drag), f(d.Collision), f(d.GroundHeight), f(d.Restitution), f(d.Friction), f(d.ExpireOnContact);
			f(d.SizeOverLife), f(d.ColorOverLife), f(d.Facing), f(d.Stretch), f(d.AspectMin), f(d.AspectMax), f(d.TextureIndex);
			f(d.SamplerIndex), f(d.FlipbookColumns), f(d.FlipbookRows), f(d.FlipbookFrames), f(d.FlipbookFrameRate), f(d.RandomFrame);
			f(d.FlipbookLoop), f(d.UvRect), f(d.Seed);
		}

		Value LightKindName(LightKind kind)
		{
			return std::string(kind == LightKind::Directional ? "directional" : kind == LightKind::Spot ? "spot" : "point");
		}

		Vec3 ToVec3(const glm::vec3& v)
		{
			return { v.x, v.y, v.z };
		}

		Quat ToQuat(const glm::quat& q)
		{
			return { q.x, q.y, q.z, q.w };
		}

		Json Array(const float* v, std::size_t n)
		{
			Json a = Json::MakeArray();

			for (std::size_t i = 0; i < n; ++i)
			{
				a.Push(v[i]);
			}

			return a;
		}

	} // namespace

	namespace ReplayAssets
	{

		std::vector<std::uint8_t> EncodeMesh(const ProceduralMeshes::MeshData& mesh)
		{
			std::vector<std::uint8_t> out;
			out.reserve(12 + mesh.Vertices.size() * sizeof(Swim::Render::StandardVertex) + mesh.Indices.size() * 4);
			Put(out, MeshMagic);
			Put(out, static_cast<std::uint32_t>(mesh.Vertices.size()));
			Put(out, static_cast<std::uint32_t>(mesh.Indices.size()));
			const auto* v = reinterpret_cast<const std::uint8_t*>(mesh.Vertices.data());
			out.insert(out.end(), v, v + mesh.Vertices.size() * sizeof(Swim::Render::StandardVertex));
			const auto* i = reinterpret_cast<const std::uint8_t*>(mesh.Indices.data());
			out.insert(out.end(), i, i + mesh.Indices.size() * sizeof(std::uint32_t));
			return out;
		}

		bool DecodeMesh(std::span<const std::uint8_t> bytes, ProceduralMeshes::MeshData& mesh)
		{
			std::uint32_t magic = 0, vertices = 0, indices = 0;

			if (!Get(bytes, magic) || magic != MeshMagic || !Get(bytes, vertices) || !Get(bytes, indices))
			{
				return false;
			}

			const std::size_t vertexBytes = std::size_t(vertices) * sizeof(Swim::Render::StandardVertex);

			if (bytes.size() != vertexBytes + std::size_t(indices) * 4)
			{
				return false;
			}

			mesh.Vertices.resize(vertices);
			mesh.Indices.resize(indices);
			std::memcpy(mesh.Vertices.data(), bytes.data(), vertexBytes);
			std::memcpy(mesh.Indices.data(), bytes.data() + vertexBytes, std::size_t(indices) * 4);
			return true;
		}

		std::vector<std::uint8_t> EncodeTexture(
			std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8, bool srgb, bool mips)
		{
			std::vector<std::uint8_t> out;
			out.reserve(14 + rgba8.size());
			Put(out, TextureMagic);
			Put(out, width);
			Put(out, height);
			Put(out, static_cast<std::uint8_t>(srgb));
			Put(out, static_cast<std::uint8_t>(mips));
			out.insert(out.end(), rgba8.begin(), rgba8.end());
			return out;
		}

		bool DecodeTexture(std::span<const std::uint8_t> bytes, TextureData& texture)
		{
			std::uint32_t magic = 0;
			std::uint8_t srgb = 0, mips = 0;

			if (!Get(bytes, magic) || magic != TextureMagic || !Get(bytes, texture.Width) || !Get(bytes, texture.Height) ||
				!Get(bytes, srgb) || !Get(bytes, mips) || bytes.size() != std::size_t(texture.Width) * texture.Height * 4)
			{
				return false;
			}

			texture.Srgb = srgb != 0;
			texture.Mips = mips != 0;
			texture.Rgba8.assign(bytes.begin(), bytes.end());
			return true;
		}

		std::vector<std::uint8_t> EncodeEmitter(const Swim::Render::ParticleEmitterDesc& desc)
		{
			std::vector<std::uint8_t> out;
			Put(out, EmitterMagic);
			VisitEmitter(desc,
				[&](const auto& field)
				{
					Put(out, field);
				});
			Put(out, static_cast<std::uint32_t>(desc.Bursts.size()));

			for (const auto& burst : desc.Bursts)
			{
				Put(out, burst.Time);
				Put(out, burst.Count);
			}

			return out;
		}

		bool DecodeEmitter(std::span<const std::uint8_t> bytes, Swim::Render::ParticleEmitterDesc& desc)
		{
			std::uint32_t magic = 0;
			bool ok = Get(bytes, magic) && magic == EmitterMagic;
			VisitEmitter(desc,
				[&](auto& field)
				{
					ok = ok && Get(bytes, field);
				});
			std::uint32_t bursts = 0;
			ok = ok && Get(bytes, bursts) && bursts <= 4096;
			desc.Bursts.clear();

			for (std::uint32_t i = 0; ok && i < bursts; ++i)
			{
				Swim::Render::ParticleBurst burst;
				ok = Get(bytes, burst.Time) && Get(bytes, burst.Count);
				desc.Bursts.push_back(burst);
			}

			return ok && bytes.empty();
		}

	} // namespace ReplayAssets

	struct SceneReplaySource::State
	{
		SceneReplaySourceDesc Desc;
		// Source data of procedural content, by asset id; a re-registered path drops its old data.
		std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> MeshBytes;
		std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> TextureBytes;
		std::unordered_map<std::string, std::uint64_t> MeshPaths;
		std::unordered_map<std::string, std::uint64_t> TexturePaths;
		std::vector<AudioPlayRecord> Sounds;
		std::vector<std::string> SoundKeys; // Owns AudioPlayRecord::Key.

		// Per recording session.
		std::uint64_t Session = 0;
		std::unordered_map<std::uint64_t, AssetId> MeshAssets;
		std::unordered_map<std::uint64_t, AssetId> TextureAssets;
		std::unordered_map<std::uint32_t, std::pair<std::uint64_t, AssetId>> MaterialAssets; // Set -> (desc hash, asset).
		std::unordered_map<std::uint64_t, AssetId> EmitterAssets;							 // Desc hash -> asset.

		AssetId MeshAsset(ReplayCapture& capture, const MeshLibrary::MeshHandle& mesh)
		{
			if (!mesh.IsValid())
			{
				return NoAsset;
			}

			const std::uint64_t id = mesh.GetId().Value;

			if (const auto found = MeshAssets.find(id); found != MeshAssets.end())
			{
				return found->second;
			}

			const std::string_view path = Desc.Render.Meshes->FindName(mesh);
			const auto bytes = MeshBytes.find(id);
			const std::span<const std::uint8_t> data = bytes == MeshBytes.end() ? std::span<const std::uint8_t>{} : bytes->second;
			const AssetId asset = path.empty() && data.empty() ? NoAsset : capture.Asset(AssetKind::Mesh, path, data);
			MeshAssets.emplace(id, asset);
			return asset;
		}

		AssetId TextureAsset(ReplayCapture& capture, const MeshLibrary::TextureHandle& texture)
		{
			if (!texture.IsValid())
			{
				return NoAsset;
			}

			const std::uint64_t id = texture.GetId().Value;

			if (const auto found = TextureAssets.find(id); found != TextureAssets.end())
			{
				return found->second;
			}

			const std::string_view path = Desc.Render.Meshes->FindTextureName(texture);
			const auto bytes = TextureBytes.find(id);
			const std::span<const std::uint8_t> data = bytes == TextureBytes.end() ? std::span<const std::uint8_t>{} : bytes->second;
			const AssetId asset = path.empty() && data.empty() ? NoAsset : capture.Asset(AssetKind::Texture, path, data);
			TextureAssets.emplace(id, asset);
			return asset;
		}

		AssetId MaterialAsset(ReplayCapture& capture, std::uint32_t set)
		{
			const MaterialDesc* desc = Desc.Render.Materials->Find(set);

			if (!desc)
			{
				return NoAsset;
			}

			Json j = Json::MakeObject();
			j["name"] = desc->Name;
			j["baseColor"] = Array(desc->BaseColor.data(), 4);
			j["metallic"] = desc->Metallic;
			j["roughness"] = desc->Roughness;
			j["emissive"] = Array(desc->Emissive.data(), 3);
			j["blend"] = static_cast<int>(desc->Blend);
			j["alphaCutoff"] = desc->AlphaCutoff;
			j["doubleSided"] = desc->DoubleSided;
			j["pointSampled"] = desc->PointSampled;
			j["occlusionStrength"] = desc->OcclusionStrength;
			j["normalScale"] = desc->NormalScale;
			const std::pair<const char*, const MeshLibrary::TextureHandle*> textures[] = { { "baseColorTexture", &desc->BaseColorTexture },
				{ "metallicRoughnessTexture", &desc->MetallicRoughnessTexture }, { "normalTexture", &desc->NormalTexture },
				{ "emissiveTexture", &desc->EmissiveTexture }, { "occlusionTexture", &desc->OcclusionTexture } };

			for (const auto& [field, handle] : textures)
			{
				if (const AssetId texture = TextureAsset(capture, *handle); texture != NoAsset)
				{
					j[field] = texture;
				}
			}

			const std::string text = j.Dump();
			const std::uint64_t hash = HashString(text);

			if (const auto found = MaterialAssets.find(set); found != MaterialAssets.end() && found->second.first == hash)
			{
				return found->second.second;
			}

			const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
			const AssetId asset = capture.Asset(AssetKind::Material, "material:" + desc->Name, bytes, hash);
			MaterialAssets[set] = { hash, asset };
			return asset;
		}

		AssetId EmitterAsset(ReplayCapture& capture, const Swim::Render::ParticleEmitterDesc& desc)
		{
			const auto bytes = ReplayAssets::EncodeEmitter(desc);
			const std::uint64_t hash = HashBytes(bytes);

			if (const auto found = EmitterAssets.find(hash); found != EmitterAssets.end())
			{
				return found->second;
			}

			const AssetId asset = capture.Asset(AssetKind::Effect, "emitter", bytes, hash);
			EmitterAssets.emplace(hash, asset);
			return asset;
		}
	};

	SceneReplaySource::SceneReplaySource(SceneReplaySourceDesc desc) : state(std::make_unique<State>())
	{
		state->Desc = std::move(desc);
		State* s = state.get();

		if (s->Desc.Render.Meshes)
		{
			MeshLibrary::SourceObserver observer;
			observer.Mesh = [s](MeshLibrary::MeshHandle handle, std::string_view path, const ProceduralMeshes::MeshData& mesh)
			{
				auto& previous = s->MeshPaths[std::string(path)];
				s->MeshBytes.erase(previous);
				previous = handle.GetId().Value;
				s->MeshBytes[previous] = ReplayAssets::EncodeMesh(mesh);
			};
			observer.Texture = [s](MeshLibrary::TextureHandle handle, std::string_view path, std::uint32_t width, std::uint32_t height,
								   std::span<const std::uint8_t> rgba8, bool srgb, bool mips)
			{
				auto& previous = s->TexturePaths[std::string(path)];
				s->TextureBytes.erase(previous);
				previous = handle.GetId().Value;
				s->TextureBytes[previous] = ReplayAssets::EncodeTexture(width, height, rgba8, srgb, mips);
			};
			s->Desc.Render.Meshes->SetSourceObserver(std::move(observer));
		}

		if (s->Desc.Audio)
		{
			s->Desc.Audio->SetPlayObserver(
				[s](const AudioPlayRecord& record)
				{
					if (s->Sounds.size() < 256)
					{
						s->SoundKeys.emplace_back(record.Key);
						s->Sounds.push_back(record);
					}
				});
		}
	}

	SceneReplaySource::~SceneReplaySource()
	{
		if (state->Desc.Render.Meshes)
		{
			state->Desc.Render.Meshes->SetSourceObserver({});
		}

		if (state->Desc.Audio)
		{
			state->Desc.Audio->SetPlayObserver({});
		}
	}

	void SceneReplaySource::SetScene(Scene* scene)
	{
		state->Desc.Scene = scene;
	}

	NodeId SceneReplaySource::EntityNode(entt::entity entity, std::uint32_t slot)
	{
		return ((static_cast<NodeId>(entt::to_integral(entity)) + 1) << 16) | (slot & 0xFFFFu);
	}

	void SceneReplaySource::Capture(ReplayCapture& capture)
	{
		State& s = *state;

		if (s.Session != capture.GetSession())
		{
			s.Session = capture.GetSession();
			s.MeshAssets.clear();
			s.TextureAssets.clear();
			s.MaterialAssets.clear();
			s.EmitterAssets.clear();
		}

		if (s.Desc.Cameras)
		{
			const Camera& camera = s.Desc.Cameras->GetCamera();
			NodeState& node = capture.Node(CameraNode);
			node.Kind = capture.Name(Names::Camera);
			node.Pose.Position = ToVec3(camera.GetPosition());
			node.Pose.Rotation = ToQuat(camera.GetRotation());
			node.Set(capture.Name("fov"), camera.GetFieldOfView());
			node.Set(capture.Name("near"), camera.GetNearPlane());
			node.Set(capture.Name("far"), camera.GetFarPlane());
			node.Set(capture.Name("active"), true);
		}

		for (std::size_t i = 0; i < s.Sounds.size(); ++i)
		{
			const auto& record = s.Sounds[i];
			ReplayEvent event;
			event.Kind = capture.Name(Names::Sound);
			event.Asset = capture.Asset(AssetKind::Sound, s.SoundKeys[i]);

			if (record.Position)
			{
				event.Position = ToVec3(*record.Position);
				event.Props.push_back({ capture.Name("positional"), true });
			}

			event.Props.push_back({ capture.Name("volume"), record.Volume });
			event.Props.push_back({ capture.Name("pitch"), record.Pitch });
			std::sort(event.Props.begin(), event.Props.end(),
				[](const Property& a, const Property& b)
				{
					return a.Key < b.Key;
				});
			capture.Event(std::move(event));
		}

		s.Sounds.clear();
		s.SoundKeys.clear();

		if (!s.Desc.Scene || !s.Desc.Render.Meshes || !s.Desc.Render.Materials)
		{
			return;
		}

		Scene& scene = *s.Desc.Scene;
		entt::registry& registry = scene.GetRegistry();

		// Ignored subtrees and annotated owners, resolved by walking up the hierarchy. Listed
		// members belong to their annotation as if they were descendants.
		std::unordered_map<entt::entity, entt::entity> members;

		for (auto [entity, annotation] : registry.view<ReplayAnnotation>().each())
		{
			for (entt::entity member : annotation.Members)
			{
				members.try_emplace(member, entity);
			}
		}

		auto ignoredOrOwner = [&](entt::entity entity, entt::entity& owner) -> bool
		{
			owner = entt::null;

			for (entt::entity e = entity; e != entt::null && registry.valid(e); e = scene.GetParent(e))
			{
				if (registry.all_of<ReplayIgnore>(e))
				{
					return true;
				}

				if (owner == entt::null)
				{
					if (registry.all_of<ReplayAnnotation>(e))
					{
						owner = e;
					}
					else if (const auto member = members.find(e); member != members.end() && registry.valid(member->second))
					{
						owner = member->second;
					}
				}
			}

			return false;
		};

		const NameId meshKind = capture.Name(Names::Mesh);
		const NameId ownerKey = capture.Name("owner");
		const NameId shadowKey = capture.Name("castShadows");

		for (const auto [entity, transform, renderer] : registry.view<Transform, MeshRenderer>().each())
		{
			entt::entity owner;

			if (ignoredOrOwner(entity, owner) || (s.Desc.SkipOverlay && HasAny(renderer.Flags, Swim::Render::RenderObjectFlags::Overlay)))
			{
				continue;
			}

			Swim::Replay::Transform world;
			world.Position = ToVec3(transform.GetWorldPosition(registry));
			world.Rotation = ToQuat(transform.GetWorldRotation(registry));
			world.Scale = ToVec3(transform.GetWorldScale(registry));

			const bool visible =
				HasAny(renderer.Flags, Swim::Render::RenderObjectFlags::Visible) || registry.all_of<ReplayShowHidden>(entity);

			for (std::uint32_t part = 0; part < renderer.Parts.size() && part < LightSlot; ++part)
			{
				NodeState& node = capture.Node(EntityNode(entity, part));
				node.Kind = meshKind;
				node.Parent = NoNode;
				node.Pose = world;
				node.Visible = visible;
				node.Assets = { s.MeshAsset(capture, renderer.Parts[part].Mesh),
					s.MaterialAsset(capture, renderer.Parts[part].MaterialSet) };
				node.Props.clear();
				node.Set(shadowKey, HasAny(renderer.Flags, Swim::Render::RenderObjectFlags::CastShadows));

				if (owner != entt::null)
				{
					node.Set(ownerKey, static_cast<std::int64_t>(EntityNode(owner, AnnotationSlot)));
				}
			}
		}

		const NameId lightKind = capture.Name(Names::Light);

		for (const auto [entity, transform, light] : registry.view<Transform, Light>().each())
		{
			entt::entity owner;

			if (ignoredOrOwner(entity, owner))
			{
				continue;
			}

			NodeState& node = capture.Node(EntityNode(entity, LightSlot));
			node.Kind = lightKind;
			node.Pose.Position = ToVec3(transform.GetWorldPosition(registry));
			node.Pose.Rotation = ToQuat(transform.GetWorldRotation(registry));
			node.Visible = light.Enabled;
			node.Props.clear();
			node.Set(capture.Name("type"), LightKindName(light.Kind));
			node.Set(capture.Name("color"), ToVec3(light.Color));
			node.Set(capture.Name("intensity"), light.Intensity);
			node.Set(capture.Name("range"), light.Range);
			node.Set(capture.Name("innerCone"), light.InnerCone);
			node.Set(capture.Name("outerCone"), light.OuterCone);
			node.Set(capture.Name("castShadows"), light.CastShadows);
			node.Set(capture.Name("shadowPriority"), light.ShadowPriority);
		}

		const NameId emitterKind = capture.Name(Names::Emitter);

		for (const auto [entity, transform, emitter] : registry.view<Transform, ParticleEmitter>().each())
		{
			entt::entity owner;

			if (ignoredOrOwner(entity, owner))
			{
				continue;
			}

			NodeState& node = capture.Node(EntityNode(entity, EmitterSlot));
			node.Kind = emitterKind;
			node.Pose.Position = ToVec3(transform.GetWorldPosition(registry));
			node.Pose.Rotation = ToQuat(transform.GetWorldRotation(registry));
			node.Pose.Scale = ToVec3(transform.GetWorldScale(registry));
			node.Assets = { s.EmitterAsset(capture, emitter.Desc) };
			node.Props.clear();
			node.Set(capture.Name("emitting"), emitter.Emitting);
			node.Set(capture.Name("revision"), static_cast<std::int64_t>(emitter.Revision));
		}

		for (const auto [entity, transform, annotation] : registry.view<Transform, ReplayAnnotation>().each())
		{
			entt::entity owner;

			if (ignoredOrOwner(entity, owner))
			{
				continue;
			}

			NodeState& node = capture.Node(EntityNode(entity, AnnotationSlot));
			node.Kind = capture.Name(annotation.Kind);
			node.Pose.Position = ToVec3(transform.GetWorldPosition(registry));
			node.Pose.Rotation = ToQuat(transform.GetWorldRotation(registry));
			node.Pose.Scale = ToVec3(transform.GetWorldScale(registry));
			node.Props.clear();

			for (const auto& [name, value] : annotation.Props)
			{
				node.Set(capture.Name(name), value);
			}
		}
	}

} // namespace Engine
