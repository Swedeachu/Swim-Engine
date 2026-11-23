#include "Engine/Systems/Scene/Scene.h"

#include "Engine/Components/Transform.h"
#include "Engine/Runtime/EngineStateMachine.h"
#include "Engine/Systems/Entity/BehaviorRegistry.h"
#include "Engine/Systems/Physics/PhysicsSystem.h"
#include "Engine/Systems/Scene/SceneCommandBuffer.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace Engine
{

	namespace
	{

		const SimulationFrame& DefaultFrame()
		{
			static const SimulationFrame frame{};
			return frame;
		}

	} // namespace

	Scene::Scene() : Scene("UnnamedScene")
	{
	}

	Scene::Scene(const std::string& sceneName)
		: name(sceneName), registry(), sceneCommandBuffer(std::make_unique<SceneCommandBuffer>(*this))
	{
		// Every way a behaviour's storage goes (DestroyEntity, RemoveComponent, registry
		// clears) takes it off the run lists first.
		registry.on_destroy<BehaviorComponents>().connect<&Scene::OnBehaviorsDestroyed>(*this);
	}

	void Scene::OnBehaviorsDestroyed(entt::registry& reg, entt::entity entity)
	{
		for (auto& behavior : reg.get<BehaviorComponents>(entity).behaviors)
		{
			if (behavior)
			{
				behaviorScheduler.Remove(*behavior);
			}
		}
	}

	Scene::~Scene()
	{
		// The physics bridge holds registry signal connections; drop it before the registry.
		physicsBridge.reset();
	}

	template <typename T> void Scene::OnComponentConstruct(entt::registry& reg, entt::entity entity)
	{
		if constexpr (std::is_same_v<T, Transform>)
		{
			Transform& tf = reg.get<Transform>(entity);
			tf.owner = entity;
			tf.ownerRegistry = &reg;
			tf.transformSystem = &transformSystem;
			tf.lastQueuedDirtyEpoch = 0;
			tf.QueueDirtyEntity();
		}
	}

	void Scene::SetServices(SceneServices value)
	{
		services = std::move(value);

		if (!services.Tags && !ownTags)
		{
			ownTags = std::make_unique<TagRegistry>();
		}

		// Behaviours created before injection cache input/camera pointers.
		behaviorScheduler.ForEach(
			[](Behavior& behavior)
			{
				behavior.RefreshFieldCache();
			});
	}

	TagRegistry& Scene::GetTagRegistry() const
	{
		if (services.Tags)
		{
			return *services.Tags;
		}

		if (!ownTags)
		{
			const_cast<Scene*>(this)->ownTags = std::make_unique<TagRegistry>();
		}

		return *ownTags;
	}

	EngineState Scene::GetEngineState() const
	{
		return services.State ? services.State->Get() : EngineState::Playing;
	}

	EngineState Scene::GetExecutionState() const
	{
		const EngineState state = GetEngineState();
		// A single step taken while paused runs the frame as if playing.
		return state == EngineState::Paused && GetTime().Stepped ? EngineState::Playing : state;
	}

	const SimulationFrame& Scene::GetTime() const
	{
		return services.Time ? *services.Time : DefaultFrame();
	}

	// --- Entities -------------------------------------------------------------------

	entt::entity Scene::CreateEntity()
	{
		entt::entity e = registry.create();
		entityIdentities.Assign(e);
		return e;
	}

	entt::entity Scene::CreateEntity(std::string_view entityName)
	{
		const entt::entity e = CreateEntity();
		SetEntityName(e, entityName);
		return e;
	}

	entt::entity Scene::CreateEntityWithSerializedId(SerializedEntityId id)
	{
		if (!id)
		{
			throw std::invalid_argument("Scene::CreateEntityWithSerializedId requires a nonzero persistent ID.");
		}

		entt::entity entity = registry.create();

		if (!entityIdentities.Bind(entity, id))
		{
			registry.destroy(entity);
			throw std::runtime_error("Scene::CreateEntityWithSerializedId received a duplicate persistent ID.");
		}

		return entity;
	}

	SerializedEntityId Scene::GetSerializedEntityId(entt::entity entity) const
	{
		const auto id = entityIdentities.FindId(entity);
		return id ? *id : SerializedEntityId{};
	}

	entt::entity Scene::FindEntityBySerializedId(SerializedEntityId id) const
	{
		const auto entity = entityIdentities.FindEntity(id);

		if (!entity || !registry.valid(*entity))
		{
			return entt::null;
		}

		return *entity;
	}

	std::size_t Scene::GetEntityCount() const
	{
		return (registry.storage<entt::entity>() ? registry.storage<entt::entity>()->free_list() : 0u);
	}

	void Scene::DestroyEntity(entt::entity entity, bool callExit, bool destroyChildren)
	{
		if (!registry.valid(entity))
		{
			return;
		}

		if (registry.any_of<Transform>(entity))
		{
			auto& tf = registry.get<Transform>(entity);
			std::vector<entt::entity> kids = tf.children;

			if (destroyChildren)
			{
				for (auto child : kids)
				{
					DestroyEntity(child, callExit, true);
				}
			}
			else
			{
				for (auto child : kids)
				{
					if (!registry.valid(child) || !registry.any_of<Transform>(child))
					{
						continue;
					}

					auto& ctf = registry.get<Transform>(child);
					ctf.parent = entt::null;
					ctf.MarkWorldDirtyOnly();
				}

				tf.children.clear();
			}

			// Recursion may have moved storage: re-fetch before unlinking.
			auto& self = registry.get<Transform>(entity);

			if (self.parent != entt::null && registry.valid(self.parent) && registry.any_of<Transform>(self.parent))
			{
				auto& vec = registry.get<Transform>(self.parent).children;
				vec.erase(std::remove(vec.begin(), vec.end(), entity), vec.end());
			}

			self.parent = entt::null;
		}

		if (callExit && registry.any_of<BehaviorComponents>(entity))
		{
			auto& bc = registry.get<BehaviorComponents>(entity);

			for (auto& b : bc.behaviors)
			{
				if (b && b->HasInited())
				{
					b->Exit();
				}
			}
		}

		registry.destroy(entity);
		entityIdentities.Forget(entity);
	}

	void Scene::DestroyAllEntities(bool callExit)
	{
		std::vector<entt::entity> all;

		for (auto [entity] : registry.storage<entt::entity>().each())
		{
			all.push_back(entity);
		}

		for (auto e : all)
		{
			if (registry.valid(e) && GetParent(e) == entt::null)
			{
				DestroyEntity(e, callExit, true);
			}
		}

		for (auto e : all)
		{
			if (registry.valid(e))
			{
				DestroyEntity(e, callExit, true);
			}
		}

		tagIndex.clear();
	}

	void Scene::SetParent(entt::entity child, entt::entity parent)
	{
		if (child == parent || !registry.valid(child) || !registry.any_of<Transform>(child) || !registry.valid(parent) ||
			!registry.any_of<Transform>(parent) || WouldCreateCycle(registry, child, parent))
		{
			return;
		}

		auto& childTf = registry.get<Transform>(child);

		if (childTf.parent == parent)
		{
			return;
		}

		if (childTf.parent != entt::null && registry.valid(childTf.parent) && registry.any_of<Transform>(childTf.parent))
		{
			auto& vec = registry.get<Transform>(childTf.parent).children;
			vec.erase(std::remove(vec.begin(), vec.end(), child), vec.end());
		}

		childTf.parent = parent;
		registry.get<Transform>(parent).children.push_back(child);

		std::vector<entt::entity> stack{ child };

		while (!stack.empty())
		{
			const entt::entity e = stack.back();
			stack.pop_back();

			if (!registry.valid(e) || !registry.any_of<Transform>(e))
			{
				continue;
			}

			auto& tf = registry.get<Transform>(e);
			tf.MarkWorldDirtyOnly();
			stack.insert(stack.end(), tf.children.begin(), tf.children.end());
		}
	}

	void Scene::RemoveParent(entt::entity child)
	{
		if (!registry.valid(child) || !registry.any_of<Transform>(child))
		{
			return;
		}

		auto& childTf = registry.get<Transform>(child);

		if (childTf.parent != entt::null && registry.valid(childTf.parent) && registry.any_of<Transform>(childTf.parent))
		{
			auto& vec = registry.get<Transform>(childTf.parent).children;
			vec.erase(std::remove(vec.begin(), vec.end(), child), vec.end());
		}

		childTf.parent = entt::null;

		std::vector<entt::entity> stack{ child };

		while (!stack.empty())
		{
			const entt::entity e = stack.back();
			stack.pop_back();

			if (!registry.valid(e) || !registry.any_of<Transform>(e))
			{
				continue;
			}

			auto& tf = registry.get<Transform>(e);
			tf.MarkWorldDirtyOnly();
			stack.insert(stack.end(), tf.children.begin(), tf.children.end());
		}
	}

	std::vector<entt::entity>* Scene::GetChildren(entt::entity e)
	{
		if (registry.valid(e) && registry.any_of<Transform>(e))
		{
			return &registry.get<Transform>(e).children;
		}

		return nullptr;
	}

	entt::entity Scene::GetParent(entt::entity e) const
	{
		if (registry.valid(e) && registry.any_of<Transform>(e))
		{
			return registry.get<Transform>(e).parent;
		}

		return entt::null;
	}

	bool Scene::WouldCreateCycle(const entt::registry& reg, entt::entity child, entt::entity newParent)
	{
		entt::entity cur = newParent;

		while (cur != entt::null && reg.valid(cur) && reg.any_of<Transform>(cur))
		{
			if (cur == child)
			{
				return true;
			}

			cur = reg.get<Transform>(cur).parent;
		}

		return false;
	}

	// --- Names and tags -------------------------------------------------------------

	void Scene::SetEntityName(entt::entity entity, std::string_view value)
	{
		if (registry.valid(entity))
		{
			registry.emplace_or_replace<EntityName>(entity, EntityName{ std::string(value) });
		}
	}

	std::string Scene::GetEntityName(entt::entity entity) const
	{
		if (registry.valid(entity))
		{
			if (const auto* entityName = registry.try_get<EntityName>(entity); entityName && !entityName->Value.empty())
			{
				return entityName->Value;
			}
		}

		const SerializedEntityId id = GetSerializedEntityId(entity);
		return id ? "Entity " + std::to_string(id.Value) : "Entity (untracked)";
	}

	entt::entity Scene::FindByName(std::string_view value) const
	{
		for (auto [entity, entityName] : registry.view<const EntityName>().each())
		{
			if (entityName.Value == value)
			{
				return entity;
			}
		}

		return entt::null;
	}

	TagId Scene::AddTag(entt::entity entity, std::string_view tag)
	{
		const TagId id = GetTagRegistry().Register(tag);
		AddTag(entity, id);
		return id;
	}

	bool Scene::AddTag(entt::entity entity, TagId tag)
	{
		if (!tag || !registry.valid(entity))
		{
			return false;
		}

		if (!tagHooksBound)
		{
			registry.on_destroy<TagSet>().connect<&Scene::OnTagSetDestroyed>(*this);
			tagHooksBound = true;
		}

		auto& set = registry.get_or_emplace<TagSet>(entity);

		if (!set.Add(tag))
		{
			return false;
		}

		tagIndex[tag.Value].insert(entity);
		return true;
	}

	bool Scene::RemoveTag(entt::entity entity, TagId tag)
	{
		if (!registry.valid(entity))
		{
			return false;
		}

		auto* set = registry.try_get<TagSet>(entity);

		if (!set || !set->Remove(tag))
		{
			return false;
		}

		if (const auto it = tagIndex.find(tag.Value); it != tagIndex.end())
		{
			it->second.erase(entity);
		}

		return true;
	}

	bool Scene::HasTag(entt::entity entity, TagId tag) const
	{
		if (!registry.valid(entity))
		{
			return false;
		}

		const auto* set = registry.try_get<TagSet>(entity);
		return set && set->Has(tag);
	}

	const TagSet* Scene::GetTags(entt::entity entity) const
	{
		return registry.valid(entity) ? registry.try_get<TagSet>(entity) : nullptr;
	}

	std::vector<entt::entity> Scene::GetEntitiesWithTag(TagId tag) const
	{
		const auto it = tagIndex.find(tag.Value);

		if (it == tagIndex.end())
		{
			return {};
		}

		return { it->second.begin(), it->second.end() };
	}

	std::size_t Scene::CountWithTag(TagId tag) const
	{
		const auto it = tagIndex.find(tag.Value);
		return it == tagIndex.end() ? 0 : it->second.size();
	}

	entt::entity Scene::FindFirstWithTag(TagId tag) const
	{
		const auto it = tagIndex.find(tag.Value);

		if (it == tagIndex.end() || it->second.empty())
		{
			return entt::null;
		}

		return *it->second.begin();
	}

	void Scene::OnTagSetDestroyed(entt::registry& reg, entt::entity entity)
	{
		const auto& set = reg.get<TagSet>(entity);

		for (const TagId tag : set.Values)
		{
			if (const auto it = tagIndex.find(tag.Value); it != tagIndex.end())
			{
				it->second.erase(entity);
			}
		}
	}

	// --- Lifecycle ------------------------------------------------------------------

	void Scene::InternalSceneAwake()
	{
		if (!transformHooksBound)
		{
			registry.on_construct<Transform>().connect<&Scene::OnComponentConstruct<Transform>>(*this);
			transformHooksBound = true;
			registry.view<Transform>().each(
				[&](entt::entity entity, Transform& transform)
				{
					transform.owner = entity;
					transform.ownerRegistry = &registry;
					transform.transformSystem = &transformSystem;
					transform.lastQueuedDirtyEpoch = 0;
					transform.QueueDirtyEntity();
				});
		}

		if (!tagHooksBound)
		{
			registry.on_destroy<TagSet>().connect<&Scene::OnTagSetDestroyed>(*this);
			tagHooksBound = true;
		}
	}

	void Scene::InternalSceneInit()
	{
	}

	void Scene::InternalScenePostInit()
	{
		// Entities Init created through the command buffer exist before the first frame.
		GetCommandBuffer().Flush();
	}

	void Scene::InternalSceneUpdate(double dt)
	{
		// Apply the previous frame's deferred mutations in one deterministic FIFO batch.
		GetCommandBuffer().Flush();
		behaviorScheduler.RunUpdate(GetExecutionState(), dt, GetTime().RealDelta);
	}

	void Scene::InternalScenePostUpdate(double dt)
	{
		timers.Tick(dt);
	}

	void Scene::InternalFixedUpdate(unsigned int tickThisSecond)
	{
		behaviorScheduler.RunFixedUpdate(GetExecutionState(), tickThisSecond);
	}

	void Scene::InternalFixedPostUpdate(unsigned int tickThisSecond)
	{
		(void)tickThisSecond;
	}

	void Scene::InternalSceneExit()
	{
		// Runs after the scene's own Exit(): every remaining behaviour receives Exit()
		// exactly once as its entity is destroyed, then pending commands and the physics
		// world go. The scene can be re-entered (Awake is not repeated; Init is).
		GetCommandBuffer().Clear();
		DestroyAllEntities(true);
		GetCommandBuffer().Clear();
		DestroyPhysicsWorld();
		activeTriggers.clear();
		timers.Clear();
	}

	void Scene::InternalStateChanged(EngineState previous, EngineState current)
	{
		behaviorScheduler.ForEach(
			[&](Behavior& behavior)
			{
				if (!behavior.HasInited())
				{
					return;
				}

				if (current == EngineState::Paused)
				{
					behavior.OnPause();
				}
				else if (current == EngineState::Stopped)
				{
					behavior.OnStop();
				}
				else if (current == EngineState::Playing && previous == EngineState::Paused)
				{
					behavior.OnResume();
				}
				else if (current == EngineState::Playing)
				{
					behavior.OnPlay();
				}

			});

		OnStateChanged(previous, current);
	}

	// --- Behaviours -----------------------------------------------------------------

	Behavior* Scene::EmplaceBehaviorByName(entt::entity e, const std::string& behaviorName)
	{
		if (!registry.valid(e))
		{
			return nullptr;
		}

		const auto* descriptor = services.Behaviors ? services.Behaviors->Find(behaviorName) : nullptr;

		if (!descriptor)
		{
			std::cerr << "Scene::EmplaceBehaviorByName | Unknown behavior: " << behaviorName << std::endl;
			return nullptr;
		}

		std::unique_ptr<Behavior> behavior = descriptor->Create(this, e);

		if (!behavior)
		{
			return nullptr;
		}

		return Attach(e, std::move(behavior), descriptor->Traits);
	}

	bool Scene::RemoveBehaviorByName(entt::entity e, const std::string& behaviorName, bool callExit)
	{
		if (!registry.valid(e) || !services.Behaviors || !registry.any_of<BehaviorComponents>(e))
		{
			return false;
		}

		auto& behaviors = registry.get<BehaviorComponents>(e).behaviors;
		const auto oldSize = behaviors.size();
		behaviors.erase(std::remove_if(behaviors.begin(), behaviors.end(),
							[&](std::unique_ptr<Behavior>& behavior)
							{
								if (!behavior || !services.Behaviors->Matches(behaviorName, *behavior))
								{
									return false;
								}

								if (callExit && behavior->HasInited())
								{
									behavior->Exit();
								}

								behaviorScheduler.Remove(*behavior);
								return true;
							}),
			behaviors.end());
		return behaviors.size() != oldSize;
	}

	void Scene::RefreshBehaviorFieldCacheForEntity(entt::entity e)
	{
		if (registry.valid(e))
		{
			if (auto* bc = registry.try_get<BehaviorComponents>(e))
			{
				for (auto& b : bc->behaviors)
				{
					if (b)
					{
						b->RefreshFieldCache();
					}
				}
			}
		}
	}

	void Scene::SetEnabledStates(entt::entity entity, EngineState states)
	{
		if (!registry.valid(entity))
		{
			return;
		}

		auto& bc = registry.get_or_emplace<BehaviorComponents>(entity);
		bc.SetEnabledStates(states);

		for (auto& behavior : bc.behaviors)
		{
			if (behavior)
			{
				behaviorScheduler.SetEnabledStates(*behavior, bc.GetEnabledStates());
			}
		}
	}

	void Scene::AddEnabledStates(entt::entity entity, EngineState states)
	{
		if (registry.valid(entity))
		{
			SetEnabledStates(entity, registry.get_or_emplace<BehaviorComponents>(entity).GetEnabledStates() | states);
		}
	}

	void Scene::RemoveEnabledStates(entt::entity entity, EngineState states)
	{
		if (registry.valid(entity))
		{
			SetEnabledStates(entity, registry.get_or_emplace<BehaviorComponents>(entity).GetEnabledStates() & ~states);
		}
	}

	// --- Physics --------------------------------------------------------------------

	PhysicsWorld& Scene::GetOrCreatePhysicsWorld(PhysicsSystem& physicsSystem)
	{
		if (!physicsBridge)
		{
			physicsBridge = std::make_unique<ScenePhysicsBridge>(physicsSystem, registry);

			if (!physicsBridge->Init())
			{
				physicsBridge.reset();
				throw std::runtime_error("Scene::GetOrCreatePhysicsWorld | Failed to initialize PhysicsWorld!");
			}
		}

		return physicsBridge->GetWorld();
	}

	PhysicsWorld* Scene::GetPhysicsWorld() const
	{
		return physicsBridge ? &physicsBridge->GetWorld() : nullptr;
	}

	void Scene::DestroyPhysicsWorld()
	{
		physicsBridge.reset();
		physicsSteps = 0;
	}

	void Scene::UpdatePhysics(PhysicsSystem& physicsSystem, float alpha)
	{
		(void)physicsSystem;

		if (physicsBridge)
		{
			physicsBridge->Interpolate(std::clamp(alpha, 0.0f, 1.0f));
		}
	}

	void Scene::FixedUpdatePhysics(PhysicsSystem& physicsSystem, float dt)
	{
		GetOrCreatePhysicsWorld(physicsSystem);
		physicsBridge->Interpolate(1.0f);
		physicsBridge->PreSimulateSync(dt);
		physicsBridge->Step(dt);
		physicsBridge->FetchResults(true);
		physicsBridge->PostSimulateSync();
		++physicsSteps;
		DispatchCollisionEvents();
		DispatchTriggerEvents();
	}

	entt::entity Scene::FindEntityByBody(BodyHandle body) const
	{
		return physicsBridge ? physicsBridge->FindEntity(body) : entt::entity{ entt::null };
	}

	void Scene::DispatchCollisionEvents()
	{
		PhysicsWorld* world = GetPhysicsWorld();

		if (!world)
		{
			return;
		}

		const EngineState state = GetExecutionState();
		const auto notify = [&](entt::entity self, entt::entity other, const CollisionEvent& event, float normalSign)
		{
			auto* bc = registry.valid(self) ? registry.try_get<BehaviorComponents>(self) : nullptr;

			if (!bc || !bc->CanExecute(state))
			{
				return;
			}

			BehaviorCollision collision;
			collision.Other = other;
			collision.Position = event.Position;
			collision.Normal = event.Normal * normalSign;
			collision.Impulse = event.Impulse;

			for (std::size_t i = 0; bc && i < bc->behaviors.size(); ++i)
			{
				Behavior* behavior = bc->behaviors[i].get();

				if (!behavior || !behavior->RunCollisionCallBacks() || !behavior->HasInited())
				{
					continue;
				}

				switch (event.Type)
				{
				case CollisionEventType::Started:
					behavior->OnCollisionEnter(collision);
					break;
				case CollisionEventType::Persisted:
					behavior->OnCollisionStay(collision);
					break;
				case CollisionEventType::Ended:
					behavior->OnCollisionExit(collision);
					break;
				}

				// The callback may have destroyed components; re-fetch.
				bc = registry.valid(self) ? registry.try_get<BehaviorComponents>(self) : nullptr;
			}
		};

		// Index loop over the world's own event list (no copy). A callback that destroys an
		// entity directly can append "ended" events (the backend reports the lost contacts),
		// so the list is re-read every step and those are dispatched too.
		for (std::size_t i = 0;; ++i)
		{
			world = GetPhysicsWorld();

			if (!world)
			{
				break;
			}

			const auto events = world->GetCollisionEvents();

			if (i >= events.size())
			{
				break;
			}

			const CollisionEvent event = events[i];
			const entt::entity a = FindEntityByBody(event.BodyA);
			const entt::entity b = FindEntityByBody(event.BodyB);
			notify(a, b, event, 1.0f);
			notify(b, a, event, -1.0f);
			this->events.Publish(SceneCollisionEvent{ event.Type, a, b, event.Position, event.Normal, event.Impulse });
		}
	}

	void Scene::NotifyTrigger(entt::entity self, const BehaviorTrigger& trigger, int phase)
	{
		auto* bc = registry.valid(self) ? registry.try_get<BehaviorComponents>(self) : nullptr;
		const EngineState state = GetExecutionState();

		if (!bc || !bc->CanExecute(state))
		{
			return;
		}

		for (std::size_t i = 0; bc && i < bc->behaviors.size(); ++i)
		{
			Behavior* behavior = bc->behaviors[i].get();

			if (!behavior || !behavior->RunCollisionCallBacks() || !behavior->HasInited())
			{
				continue;
			}

			if (phase == 0)
			{
				behavior->OnTriggerEnter(trigger);
			}
			else if (phase == 1)
			{
				behavior->OnTriggerStay(trigger);
			}
			else
			{
				behavior->OnTriggerExit(trigger);
			}

			bc = registry.valid(self) ? registry.try_get<BehaviorComponents>(self) : nullptr;
		}
	}

	void Scene::DispatchTriggerEvents()
	{
		PhysicsWorld* world = GetPhysicsWorld();

		if (!world)
		{
			return;
		}

		const auto dispatch = [&](const TriggerPair& pair, int phase)
		{
			const BehaviorTrigger trigger{ pair.Trigger, pair.Other };
			NotifyTrigger(pair.Trigger, trigger, phase);
			NotifyTrigger(pair.Other, trigger, phase);

			if (phase != 1)
			{
				events.Publish(SceneTriggerEvent{ phase == 0, pair.Trigger, pair.Other });
			}
		};

		// Copied: callbacks may destroy bodies, which can report more events.
		const auto reported = world->GetTriggerEvents();
		const std::vector<TriggerEvent> triggerEvents(reported.begin(), reported.end());
		std::vector<std::size_t> entered;

		for (const auto& event : triggerEvents)
		{
			const TriggerPair key{ FindEntityByBody(event.TriggerBody), FindEntityByBody(event.OtherBody), 0 };

			if (key.Trigger == entt::null || key.Other == entt::null)
			{
				continue;
			}

			auto found = std::find_if(activeTriggers.begin(), activeTriggers.end(),
				[&](const TriggerPair& pair)
				{
					return pair.Trigger == key.Trigger && pair.Other == key.Other;
				});

			if (event.Entered)
			{
				// Compound bodies overlap through several shapes: one Enter per entity pair.
				if (found != activeTriggers.end())
				{
					++found->Count;
					continue;
				}

				activeTriggers.push_back({ key.Trigger, key.Other, 1 });
				entered.push_back(activeTriggers.size() - 1);
				dispatch(activeTriggers.back(), 0);
			}
			else if (found != activeTriggers.end() && --found->Count == 0)
			{
				const TriggerPair pair = *found;
				activeTriggers.erase(found);
				dispatch(pair, 2);
			}
		}

		// Stay for overlaps older than this step; pairs whose entity is gone end here (a
		// destroyed body may not report its exit).
		for (std::size_t i = 0; i < activeTriggers.size();)
		{
			const TriggerPair pair = activeTriggers[i];

			if (!registry.valid(pair.Trigger) || !registry.valid(pair.Other))
			{
				activeTriggers.erase(activeTriggers.begin() + static_cast<std::ptrdiff_t>(i));
				dispatch(pair, 2);
				continue;
			}

			if (std::find(entered.begin(), entered.end(), i) == entered.end())
			{
				dispatch(pair, 1);
			}

			++i;
		}
	}

	std::optional<SceneRaycastHit> Scene::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const
	{
		return Raycast(origin, direction, maxDistance, CollisionLayer{});
	}

	entt::entity Scene::RaycastEntity(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, CollisionLayer filter) const
	{
		const auto hit = Raycast(origin, direction, maxDistance, filter);
		return hit ? hit->Entity : entt::entity{ entt::null };
	}

	std::vector<entt::entity> Scene::OverlapSphere(const glm::vec3& center, float radius, CollisionLayer filter) const
	{
		ShapeDesc shape;
		shape.Type = ShapeType::Sphere;
		shape.Sphere.Radius = radius;
		PhysicsPose pose;
		pose.Position = center;
		return Overlap(shape, pose, filter);
	}

	std::vector<entt::entity> Scene::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
		CollisionLayer filter) const
	{
		ShapeDesc shape;
		shape.Type = ShapeType::Box;
		shape.Box.HalfExtents = halfExtents;
		PhysicsPose pose;
		pose.Position = center;
		pose.Rotation = rotation;
		return Overlap(shape, pose, filter);
	}

	std::vector<entt::entity> Scene::Overlap(const ShapeDesc& shape, const PhysicsPose& pose, CollisionLayer filter) const
	{
		std::vector<entt::entity> result;
		PhysicsWorld* world = GetPhysicsWorld();

		if (!world)
		{
			return result;
		}

		std::array<OverlapHit, 256> hits{};
		const auto count = world->Overlap(shape, pose, hits, filter);

		for (std::size_t i = 0; i < std::min(count, hits.size()); ++i)
		{
			const auto entity = FindEntityByBody(hits[i].Body);

			if (entity != entt::null && std::find(result.begin(), result.end(), entity) == result.end())
			{
				result.push_back(entity);
			}
		}

		return result;
	}

	std::optional<SceneRaycastHit> Scene::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
		CollisionLayer filter) const
	{
		PhysicsWorld* world = GetPhysicsWorld();

		if (!world || glm::length(direction) <= 0.0f)
		{
			return std::nullopt;
		}

		RaycastHit hit;

		if (!world->Raycast(origin, glm::normalize(direction), maxDistance, hit, filter))
		{
			return std::nullopt;
		}

		SceneRaycastHit result;
		result.Entity = FindEntityByBody(hit.Body);
		result.Position = hit.Position;
		result.Normal = hit.Normal;
		result.Distance = hit.Distance;
		return result;
	}

} // namespace Engine
