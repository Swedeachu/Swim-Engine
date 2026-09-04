#include "PCH.h"

#include <string_view>
#include "Engine/Systems/Renderer/Core/Ui/UiCoordinates.h"
#include "Scene.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/DoNotSerialize.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/MeshDecorator.h"
#include "Engine/Components/Internal/FrustumCullCache.h"
#include "SceneCommandBuffer.h"
#include "InternalBehaviors/CameraControl/EditorCamera.h"
#include "Engine/Systems/Physics/PhysicsSystem.h"
#include "Engine/Systems/Entity/BehaviorRegistry.h"
#include "Serialization/SceneSerializer.h"
#include "Serialization/SceneStorage.h"
#include "Serialization/SceneSyncTracker.h"
#include "Serialization/SceneToolingBridge.h"

namespace Engine
{

#ifdef _SWIM_DEBUG
	constexpr static bool handleDebugDraw = true;
#else
	constexpr static bool handleDebugDraw = false;
#endif

	constexpr static bool alwaysUseEditorCamera = true;

	Scene::Scene()
		: name("UnnamedScene"), registry(), sceneCommandBuffer(std::make_unique<SceneCommandBuffer>(*this))
	{}

	Scene::Scene(const std::string& name)
		: name(name), registry(), sceneCommandBuffer(std::make_unique<SceneCommandBuffer>(*this))
	{}

	Scene::~Scene() = default;

	template<typename T>
	void Scene::BindSerializationHooksForComponent()
	{
		// Component construction -> entity changed (and possibly created)
		registry.on_construct<T>().connect<&Scene::OnComponentConstruct<T>>(*this);

		// Component destruction -> entity changed (or destroyed if Transform)
		registry.on_destroy<T>().connect<&Scene::OnComponentDestroy<T>>(*this);
	}

	template<typename T>
	void Scene::OnComponentConstruct(entt::registry& reg, entt::entity entity)
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

		if constexpr (
			std::is_same_v<T, Transform>
			|| std::is_same_v<T, Material>
			|| std::is_same_v<T, CompositeMaterial>
			|| std::is_same_v<T, MeshDecorator>
			)
		{
			++renderablesRevision;
		}

		if (sceneSyncTracker)
		{
			sceneSyncTracker->EntityMutated(entity);
		}
	}

	template<typename T>
	void Scene::OnComponentDestroy(entt::registry& reg, entt::entity entity)
	{
		if constexpr (
			std::is_same_v<T, Transform>
			|| std::is_same_v<T, Material>
			|| std::is_same_v<T, CompositeMaterial>
			|| std::is_same_v<T, MeshDecorator>
			)
		{
			++renderablesRevision;
		}

		if (sceneSyncTracker && reg.valid(entity))
		{
			// EnTT on_destroy is emitted before component removal. Record the stable ID
			// as dirty and let SceneSyncTracker serialize the post-mutation state later.
			sceneSyncTracker->EntityMutated(entity);
		}
	}

	entt::entity Scene::CreateEntity()
	{
		entt::entity e = registry.create();
		entityIdentities.Assign(e);

		// All serialization notifications for this entity will be triggered by
		// registry hooks (e.g. first Transform/ObjectTag/etc. attachment).

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

	void Scene::DestroyEntity(entt::entity entity, bool callExit, bool destroyChildren)
	{
		if (!registry.valid(entity))
		{
			return;
		}

		const SerializedEntityId serializedId = GetSerializedEntityId(entity);
		if (sceneSyncTracker)
		{
			sceneSyncTracker->EntityDestroyed(serializedId);
		}

		// If it has a Transform, handle children and unlink from parent
		if (registry.any_of<Transform>(entity))
		{
			auto& tf = registry.get<Transform>(entity);

			// Handle children
			// Copy the list; it will be mutated
			std::vector<entt::entity> kids = tf.children;

			if (destroyChildren)
			{
				// Depth-first destroy of subtree
				for (auto child : kids)
				{
					DestroyEntity(child, callExit, true);
				}
			}
			else
			{
				// Detach children (null their parents)
				for (auto child : kids)
				{
					if (!registry.valid(child) || !registry.any_of<Transform>(child))
					{
						continue;
					}
					auto& ctf = registry.get<Transform>(child);
					// remove child from our list will happen after loop anyway
					ctf.parent = entt::null;
					ctf.MarkWorldDirtyOnly();

					if (sceneSyncTracker)
					{
						sceneSyncTracker->EntityMutated(child);
					}
				}

				tf.children.clear();
			}

			// Unlink from our parent
			if (tf.parent != entt::null && registry.valid(tf.parent) && registry.any_of<Transform>(tf.parent))
			{
				auto& ptf = registry.get<Transform>(tf.parent);
				auto& vec = ptf.children;
				vec.erase(std::remove(vec.begin(), vec.end(), entity), vec.end());
			}

			tf.parent = entt::null;
		}

		EngineState state = GetEngineState();

		// Call Exit() on behaviors if needed
		if (callExit && registry.any_of<BehaviorComponents>(entity))
		{
			auto& bc = registry.get<BehaviorComponents>(entity);
			if (bc.CanExecute(state))
			{
				for (auto& b : bc.behaviors)
				{
					if (b) b->Exit();
				}
			}
		}

		// Finally destroy the entity itself and release its runtime-to-persistent mapping.
		registry.destroy(entity);
		entityIdentities.Forget(entity);
	}

	void Scene::DestroyAllEntities(bool callExit)
	{
		std::vector<entt::entity> toKill;

		for (auto entity : registry.storage<entt::entity>())
		{
			toKill.push_back(entity);
		}

		for (auto e : toKill)
		{
			if (!registry.valid(e))
			{
				continue;
			}

			bool hasTf = registry.any_of<Transform>(e);
			bool hasParent = false;

			if (hasTf)
			{
				hasParent = registry.get<Transform>(e).parent != entt::null;
			}

			if (!hasParent)
			{
				DestroyEntity(e, callExit, true);
			}
		}

		for (auto e : toKill)
		{
			if (registry.valid(e))
			{
				DestroyEntity(e, callExit, true);
			}
		}
	}

	void Scene::SetParent(entt::entity child, entt::entity parent)
	{
		// Avoid self-parenting
		if (child == parent)
		{
			return;
		}

		// Safety
		if (!registry.valid(child) || !registry.any_of<Transform>(child))
		{
			return;
		}

		// Allow nulling by passing entt::null via RemoveParent instead.
		if (!registry.valid(parent) || !registry.any_of<Transform>(parent))
		{
			return;
		}

		// Avoid cycles
		if (WouldCreateCycle(registry, child, parent))
		{
			return;
		}

		auto& childTf = registry.get<Transform>(child);

		// If already same parent, nothing to do
		if (childTf.parent == parent)
		{
			return;
		}

		// Remove from old parent's children list
		if (childTf.parent != entt::null && registry.valid(childTf.parent) && registry.any_of<Transform>(childTf.parent))
		{
			auto& oldParentTf = registry.get<Transform>(childTf.parent);
			auto& vec = oldParentTf.children;
			vec.erase(std::remove(vec.begin(), vec.end(), child), vec.end());
		}

		// Set new parent + register child
		childTf.parent = parent;
		auto& parentTf = registry.get<Transform>(parent);
		parentTf.children.push_back(child);

		// Invalidate child's world and all its descendants (lazy recompute on demand)
		std::vector<entt::entity> stack;
		stack.push_back(child);

		while (!stack.empty())
		{
			entt::entity e = stack.back();
			stack.pop_back();

			if (!registry.valid(e) || !registry.any_of<Transform>(e))
			{
				continue;
			}

			auto& tf = registry.get<Transform>(e);
			tf.MarkWorldDirtyOnly();

			for (auto c : tf.children)
			{
				stack.push_back(c);
			}
		}

		// Notify editor that this entity's parent changed.
		// Parent-child relationships are not purely registry-driven; we keep this explicit.
		if (sceneSyncTracker)
		{
			sceneSyncTracker->EntityMutated(child);
		}
	}

	void Scene::RemoveParent(entt::entity child)
	{
		if (!registry.valid(child) || !registry.any_of<Transform>(child))
		{
			return;
		}

		auto& childTf = registry.get<Transform>(child);

		// Remove from old parent's children list
		if (childTf.parent != entt::null && registry.valid(childTf.parent) && registry.any_of<Transform>(childTf.parent))
		{
			auto& oldParentTf = registry.get<Transform>(childTf.parent);
			auto& vec = oldParentTf.children;
			vec.erase(std::remove(vec.begin(), vec.end(), child), vec.end());
		}

		// Clear parent
		childTf.parent = entt::null;

		// Invalidate subtree world matrices
		std::vector<entt::entity> stack;
		stack.push_back(child);

		while (!stack.empty())
		{
			entt::entity e = stack.back();
			stack.pop_back();

			if (!registry.valid(e) || !registry.any_of<Transform>(e))
			{
				continue;
			}

			auto& tf = registry.get<Transform>(e);
			tf.MarkWorldDirtyOnly();

			for (auto c : tf.children)
			{
				stack.push_back(c);
			}
		}

		// Notify editor about parenting change.
		if (sceneSyncTracker)
		{
			sceneSyncTracker->EntityMutated(child);
		}
	}

	std::vector<entt::entity>* Scene::GetChildren(entt::entity e)
	{
		if (registry.valid(e) && registry.any_of<Transform>(e))
		{
			Transform& tf = registry.get<Transform>(e);
			return &tf.children;
		}

		return nullptr;
	}

	entt::entity Scene::GetParent(entt::entity e) const
	{
		if (registry.valid(e) && registry.any_of<Transform>(e))
		{
			const auto& tf = registry.get<Transform>(e);
			return tf.parent;
		}

		return entt::null;
	}

	bool Scene::WouldCreateCycle(const entt::registry& reg, entt::entity child, entt::entity newParent)
	{
		// climb from newParent up to root; if we see child, that's a cycle
		entt::entity cur = newParent;

		while (cur != entt::null && reg.valid(cur) && reg.any_of<Transform>(cur))
		{
			if (cur == child)
			{
				return true;
			}

			const auto& tf = reg.get<Transform>(cur);

			cur = tf.parent;
		}

		return false;
	}

	bool Scene::ShouldRenderBasedOnState(entt::entity e) const
	{
		const EngineState state = GetEngineState();

		if (registry.any_of<BehaviorComponents>(e))
		{
			const BehaviorComponents& bc = registry.get<BehaviorComponents>(e);
			const bool editingOnly = ShouldRenderOnlyDuringEditingBasedOnState(e);

			if (editingOnly)
			{
				// Bitflag-safe check (covers combined states like Editing|Paused)
				return HasAnyEngineStates(state, EngineState::Editing);
			}
		}

		// Otherwise most things will always render
		return true;
	}

	bool Scene::ShouldRenderOnlyDuringEditingBasedOnState(entt::entity e) const
	{
		if (!registry.any_of<BehaviorComponents>(e))
		{
			return false;
		}

		const BehaviorComponents& bc = registry.get<BehaviorComponents>(e);

		// True only during editing: enabled in Editing, and NOT enabled in any other state.
		const bool enabledEditing = bc.IsEnabledIn(EngineState::Editing);
		const bool enabledElsewhere =
			bc.IsEnabledIn(EngineState::Playing) ||
			bc.IsEnabledIn(EngineState::Paused) ||
			bc.IsEnabledIn(EngineState::Stopped);

		return enabledEditing && !enabledElsewhere;
	}

	// Right now the interal scene base init and update are for caching mesh stuff for the frustum culling.
	// Sooner or later we will have more code here for full on spacial partioning of the scene, 
	// which will be essential for physics and AI and rendering/generic updates of active chunks.
	// We are already doing that now with SceneBVH.

	void Scene::InternalSceneAwake()
	{
		// Transform ownership/hierarchy context must be wired before user Awake() can create entities.
		if (!transformHooksBound)
		{
			registry.on_construct<Transform>().connect<&Scene::OnComponentConstruct<Transform>>(*this);
			registry.on_destroy<Transform>().connect<&Scene::OnComponentDestroy<Transform>>(*this);
			transformHooksBound = true;

			registry.view<Transform>().each([&](entt::entity entity, Transform& transform)
			{
				transform.owner = entity;
				transform.ownerRegistry = &registry;
				transform.transformSystem = &transformSystem;
				transform.lastQueuedDirtyEpoch = 0;
				transform.QueueDirtyEntity();
			});
		}

		ForEachBehavior(&Behavior::Awake); // we might not want to do this actually and let behaviors do this themselves
	}

	void Scene::InternalSceneInit()
	{
		// Watch for updates such as construction or modification of renderable transforms
		frustumCacheObserver.connect(registry, entt::collector
			.group<Engine::Transform, Engine::Material>()
			.group<Engine::Transform, Engine::CompositeMaterial>()
		);

		if (!serializationHooksBound)
		{
			// Auto-remove FrustumCullCache when prerequisites are destroyed.
			registry.on_destroy<Engine::Transform>().connect<&Scene::RemoveFrustumCache>(*this);
			registry.on_destroy<Engine::Material>().connect<&Scene::RemoveFrustumCache>(*this);
			registry.on_destroy<Engine::CompositeMaterial>().connect<&Scene::RemoveFrustumCache>(*this);

			// Always track renderable composition changes, even outside editor mode.
			// Transform hooks are connected in InternalSceneAwake so transforms created by user Awake() are wired too.
			registry.on_construct<Material>().connect<&Scene::OnComponentConstruct<Material>>(*this);
			registry.on_destroy<Material>().connect<&Scene::OnComponentDestroy<Material>>(*this);
			registry.on_construct<CompositeMaterial>().connect<&Scene::OnComponentConstruct<CompositeMaterial>>(*this);
			registry.on_destroy<CompositeMaterial>().connect<&Scene::OnComponentDestroy<CompositeMaterial>>(*this);
			registry.on_construct<MeshDecorator>().connect<&Scene::OnComponentConstruct<MeshDecorator>>(*this);
			registry.on_destroy<MeshDecorator>().connect<&Scene::OnComponentDestroy<MeshDecorator>>(*this);

			// Persistence/tool sync hooks are cheap while tooling is inactive and make
			// switching scene modes safe without reconnecting duplicate EnTT callbacks.
			BindSerializationHooksForComponent<ObjectTag>();
			BindSerializationHooksForComponent<BehaviorComponents>();
			BindSerializationHooksForComponent<DoNotSerialize>();
			serializationHooksBound = true;
		}

		// Initialize SceneBVH grid
		sceneBVH = std::make_unique<SceneBVH>(registry, transformSystem, GetJobSystem());
		sceneBVH->Init();

		if (GetEngineState() == EngineState::Editing)
		{
			// Serialization, storage, and tooling transport are independent modules.
			// None of them requires renderer/presentation resources.
			sceneSerializer = std::make_unique<SceneSerializer>(registry, entityIdentities, name);
			sceneStorage = std::make_unique<SceneStorage>(GetFileSystem());
			SceneToolingBridge::SendCallback toolingSender;
			if (editorMessageSender)
			{
				toolingSender = [this](const std::string& message, std::uintptr_t channel)
				{
					return SendEditorMessage(message, channel);
				};
			}
			sceneToolingBridge = std::make_unique<SceneToolingBridge>(std::move(toolingSender));
			sceneSyncTracker = std::make_unique<SceneSyncTracker>(*sceneSerializer, *sceneToolingBridge);
		}

		if (HasPresentationServices())
		{
			// Presentation/debug/editor facilities are optional so the same Scene core can run headless.
			sceneDebugDraw = std::make_unique<SceneDebugDraw>(GetMeshPool(), GetMaterialPool());
			sceneDebugDraw->Init();
			sceneBVH->SetDebugDrawer(sceneDebugDraw.get());

			gizmoSystem = std::make_unique<GizmoSystem>();
			std::shared_ptr<Scene> self = shared_from_this();
			gizmoSystem->SetScene(self);
			gizmoSystem->Awake();
			gizmoSystem->Init();

			// Give the editing only scripts, for now is just the free cam.
			GetCommandBuffer().CreateWithBehaviors<EditorCamera>(
				[this](entt::entity e, EditorCamera* editorCam)
			{
				entt::registry& reg = GetRegistry();
				if (reg.any_of<Engine::BehaviorComponents>(e))
				{
					Engine::BehaviorComponents& bc = reg.get<Engine::BehaviorComponents>(e);
					if (alwaysUseEditorCamera)
					{
						bc.SetEnabledStates(Engine::EngineState::Editing | Engine::EngineState::Playing);
					}
					else
					{
						bc.SetEnabledStates(Engine::EngineState::Editing);
					}
				}

				// Give this entity a tag to make it as an editor mode object.
				EmplaceComponent<ObjectTag>(e, TagConstants::EDITOR_MODE_OBJECT, "Editor Camera Entity");
				EmplaceComponent<DoNotSerialize>(e);
			});
		}

		ForEachBehavior(&Behavior::InitIfNeeded); // we might not want to do this actually and let behaviors do this themselves
	}

	void Scene::InternalScenePostInit()
	{
		// Send our first state of the scene to the editor right away.
		// After this, it becomes a balancing act of syncing and updating components and entities between the processes when things change (which happens a lot).
		if (sceneSerializer && sceneStorage)
		{
			const SceneStorageResult result = sceneStorage->Save(name, sceneSerializer->SerializeScene());
			if (!result.Success)
			{
				std::cerr << "Failed to save scene '" << name << "': " << result.Message << std::endl;
			}
		}

		if (sceneSyncTracker)
		{
			sceneSyncTracker->SendFullScene();
		}
	}

	void Scene::RemoveFrustumCache(entt::registry& registry, entt::entity entity)
	{
		if (registry.any_of<Engine::FrustumCullCache>(entity))
		{
			registry.remove<Engine::FrustumCullCache>(entity);
		}
	}

	// Stuff we don't need to happen thousands of times a second, or needs to be timed, such as physics scene updates.
	// It might make sense to have sub scene systems be in a data structure that iterates with update inside of here and init and update etc.
	void Scene::InternalFixedUpdate(unsigned int tickThisSecond)
	{
		if (gizmoSystem)
		{
			gizmoSystem->FixedUpdate(tickThisSecond);
		}

		// Call fixed update on all our behaviors
		ForEachBehavior(&Behavior::InitIfNeeded);
		ForEachBehavior(&Behavior::FixedUpdate, tickThisSecond);

		// Add new frustum cache components if needed
		for (auto entity : frustumCacheObserver)
		{
			if (!registry.any_of<Engine::FrustumCullCache>(entity))
			{
				registry.emplace<Engine::FrustumCullCache>(entity);
			}
		}

		// Let BVH manage itself
		if (sceneBVH)
		{
			sceneBVH->UpdateIfNeeded(frustumCacheObserver);
		}

		frustumCacheObserver.clear();
	}

	void Scene::InternalScenePostUpdate(double dt)
	{
		// Do not clear transform dirty state here. The renderer runs after SceneSystem::Update() in the
		// engine system order, so clearing the scene-owned transform dirty queue in post-update makes
		// renderer-side incremental GPU uploads miss the transforms that changed during this frame.
		// That shows up as movement/rotation lag or stutter in the GPU cull path.

		// if constexpr (handleDebugDraw)
		sceneBVH->DebugRender();

		if (sceneSyncTracker)
		{
			// Transform edits happen in-place and therefore do not generate EnTT
			// construct/destroy signals. Feed the scene-owned dirty set into the same
			// stable-ID sync boundary before serializing this frame's deltas.
			for (entt::entity entity : transformSystem.GetDirtyEntities())
			{
				sceneSyncTracker->EntityMutated(entity);
			}
			sceneSyncTracker->Flush();
		}
	}

	void Scene::InternalSceneExit()
	{
		// TODO: don't destroy on load/persist entities 
		ForEachBehavior(&Behavior::Exit);
		GetCommandBuffer().Clear();

		// Tooling/persistence state is lifecycle-local. Reset it before Scene::Exit()
		// destroys runtime entities so an unloaded scene cannot emit stale deltas.
		sceneSyncTracker.reset();
		sceneToolingBridge.reset();
		sceneStorage.reset();
		sceneSerializer.reset();

		// Tear down our physics world during exit 
		DestroyPhysicsWorld();
	}

	void Scene::DestroyPhysicsWorld()
	{
		physicsBridge.reset();
		physicsTimeSinceLastTick = 0.0;
	}

	void Scene::InternalSceneUpdate(double dt)
	{
		// Clear the previous frames debug draw data. 
		// This opens up an opportunity for caching commonly drawn wireframes.

		// We want to keep editor mode objects such as retained gizmos, trash everything else that is immediate mode from the previous frame
		constexpr static std::array<int, 1> keep = { TagConstants::EDITOR_MODE_OBJECT }; // TODO: we might want to use a better tag like immediate mode object
		if (sceneDebugDraw)
		{
			sceneDebugDraw->ClearExceptTags(keep);
		}

		GetCommandBuffer().Flush(); // Apply the previous frame's deferred scene mutations in one deterministic FIFO batch.

		// Editor/game state hotkeys are runtime state controls now; they no longer
		// depend on a compile-time SwimEngine default or global engine type.
		if (StateTestControl())
		{
			return;
		}

		// Ensure BVH is coherent for this frame if any entity was removed/added or forced.
		if (sceneBVH && sceneBVH->ShouldForceUpdate())
		{
			sceneBVH->Update();
		}

		// Call Update(dt) on all Behavior components.
		ForEachBehavior(&Behavior::InitIfNeeded);
		ForEachBehavior(&Behavior::Update, dt);
		if (inputManager)
		{
			UpdateUIBehaviors();
		}

		if (gizmoSystem)
		{
			gizmoSystem->Update(dt);
		}

		// Was doing bvh update here but its more performant to do it in the fixed update.

		// if constexpr (handleDebugDraw)
		if (inputManager && sceneDebugDraw)
		{
			InputManager* input = inputManager;
			// control toggle with G key
			if (input->IsControlDown() && input->IsKeyTriggered(Swim::Platform::KeyCode::G))
			{
				sceneDebugDraw->SetEnabled(!sceneDebugDraw->IsEnabled());
				std::string abled = sceneDebugDraw->IsEnabled() ? "Enabled" : "Disabled";
				std::cout << "Debug wireframe draw " << abled << "\n";
			}
		}
	}

	// Converts the mouse position from window-pixel space -> virtual-canvas space
	// Tests that virtual point against each screen-space entity's AABB
	// Dispatches OnMouseEnter / Exit / Hover / Click events
	void Scene::UpdateUIBehaviors()
	{
		mouseBusyWithUI = false; // reset mouse pointer UI focus status for this frame

		InputManager* inputMgr = GetInputManager();
		if (!inputMgr)
		{
			return;
		}

		// 1. Get raw mouse position in window pixels
		glm::vec2 mouseVirt = UiCoordinates::WindowToVirtualCanvas(inputMgr->GetMousePosition(), inputMgr->GetWindowSize());

		// 2. Iterate over UI entities and run hit-testing in the same space
		entt::registry& registry = GetRegistry();

		// We want the engine state for filtering which behaviors should have callbacks ran on them
		EngineState state = GetEngineState();

		registry.view<Transform, Material, BehaviorComponents>().each(
			[&](entt::entity entity,
			Transform& transform,
			Material&, BehaviorComponents& bc)
		{
			if (transform.GetTransformSpace() != TransformSpace::Screen || !bc.CanExecute(state))
			{
				return; // ignore world-space or non active stuff here
			}

			// World position (center of quad in virtual-canvas units)
			glm::vec3 pos = transform.GetWorldPosition(registry); // xyz from translation column

			// World scale = lengths of basis vectors (handles non-uniform scale).
			// For UI AABB we only care about X/Y; sign doesn't matter for extents.
			glm::vec3 scl = transform.GetWorldScale(registry);

			// Position / size are now in world (screen) virtual-canvas units
			glm::vec2 halfSize{ 0.5f * std::abs(scl.x), 0.5f * std::abs(scl.y) };

			glm::vec2 minRect{ pos.x - halfSize.x, pos.y - halfSize.y };
			glm::vec2 maxRect{ pos.x + halfSize.x, pos.y + halfSize.y };

			bool inside = (mouseVirt.x >= minRect.x && mouseVirt.x <= maxRect.x
				&& mouseVirt.y >= minRect.y && mouseVirt.y <= maxRect.y);

			// 3. Let each attached behaviour react
			for (std::unique_ptr<Behavior>& behavior : bc.behaviors)
			{
				if (!behavior || !behavior->RunMouseCallBacks())
				{
					continue;
				}

				bool wasFocused = behavior->FocusedByMouse();

				if (inside && !wasFocused) // mouse first enter
				{
					mouseBusyWithUI = true;
					behavior->SetFocusedByMouse(true);
					behavior->OnMouseEnter();
				}
				else if (!inside && wasFocused) // mouse exit
				{
					behavior->SetFocusedByMouse(false);
					behavior->OnMouseExit();
				}
				else if (inside) // mouse hover + possible focused input interactions from mouse clicking
				{
					mouseBusyWithUI = true;
					behavior->OnMouseHover();

					if (inputMgr->IsMouseButtonDown(Swim::Platform::MouseButton::Left)) { behavior->OnLeftClickDown(); }
					if (inputMgr->IsMouseButtonDown(Swim::Platform::MouseButton::Right)) { behavior->OnRightClickDown(); }

					if (inputMgr->IsMouseButtonReleased(Swim::Platform::MouseButton::Left)) { behavior->OnLeftClickUp(); }
					if (inputMgr->IsMouseButtonReleased(Swim::Platform::MouseButton::Right)) { behavior->OnRightClickUp(); }

					if (inputMgr->IsMouseButtonTriggered(Swim::Platform::MouseButton::Left)) { behavior->OnLeftClicked(); }
					if (inputMgr->IsMouseButtonTriggered(Swim::Platform::MouseButton::Right)) { behavior->OnRightClicked(); }
				}
			}
		});
	}

	// Returns if we changed state
	bool Scene::StateTestControl()
	{
		InputManager* input = GetInputManager();
		if (!input || !commandDispatcher)
		{
			return false;
		}

		auto send = [this](std::string_view command)
		{
			DispatchCommand(command);
		};
		const EngineState state = GetEngineState();

		// Must be holding shift to do these hotkeys
		bool shifting = input->IsShiftDown();
		if (!shifting)
		{
			return false;
		}

		bool handled = false;

		// Toggle Play / Stop (L)
		if (!handled && input->IsKeyTriggered(Swim::Platform::KeyCode::L))
		{
			const bool playing = HasAnyEngineStates(state, EngineState::Playing);
			if (playing)
			{
				// GoIntoStoppedMode()
				send("stop");
				send("resume");
				send("edit");
			}
			else
			{
				// GoIntoPlayMode()
				send("resume");
				send("game");
				send("play");
			}
			handled = true;
		}
		else if (!handled && input->IsKeyTriggered(Swim::Platform::KeyCode::P)) // Toggle Pause / Resume (P)
		{
			if (HasAnyEngineStates(state, EngineState::Paused))
			{
				send("resume");
			}
			else
			{
				send("pause");
			}
			handled = true;
		}
		else if (!handled && input->IsKeyTriggered(Swim::Platform::KeyCode::E)) // Toggle Edit / Game (E)
		{
			if (HasAnyEngineStates(state, EngineState::Editing))
			{
				send("game");
			}
			else
			{
				send("edit");
			}
			handled = true;
		}
		else if (!handled && input->IsKeyTriggered(Swim::Platform::KeyCode::O)) // Hard Stop (O)
		{
			send("stop");
			send("resume");
			send("edit");
			handled = true;
		}
		else if (!handled && input->IsKeyTriggered(Swim::Platform::KeyCode::R)) // Restart stub (R)
		{
			send("restart");
			handled = true;
		}

		return handled;
	}

	Behavior* Scene::EmplaceBehaviorByName(entt::entity e, const std::string& behaviorName)
	{
		if (!registry.valid(e))
		{
			return nullptr;
		}

		if (!behaviorRegistry || !behaviorRegistry->Contains(behaviorName))
		{
			std::cout << "Scene::EmplaceBehaviorByName | Unknown behavior: " << behaviorName << std::endl;
			return nullptr;
		}

		std::unique_ptr<Behavior> behavior = behaviorRegistry->Create(behaviorName, this, e);
		if (!behavior)
		{
			return nullptr;
		}

		// Attach to BehaviorComponents just like EmplaceBehavior<T>
		BehaviorComponents& bc = registry.get_or_emplace<BehaviorComponents>(e);
		Behavior* rawPtr = behavior.get();
		bc.behaviors.push_back(std::move(behavior));

		rawPtr->RefreshFieldCache();

		return rawPtr;
	}

	bool Scene::RemoveBehaviorByName(entt::entity e, const std::string& behaviorName, bool callExit)
	{
		if (!registry.valid(e) || !behaviorRegistry || !registry.any_of<BehaviorComponents>(e))
		{
			return false;
		}

		BehaviorComponents& components = registry.get<BehaviorComponents>(e);
		EngineState state = GetEngineState();
		auto& behaviors = components.behaviors;
		const auto oldSize = behaviors.size();

		behaviors.erase(std::remove_if(behaviors.begin(), behaviors.end(),
			[&](std::unique_ptr<Behavior>& behavior)
		{
			if (!behavior || !behaviorRegistry->Matches(behaviorName, *behavior))
			{
				return false;
			}

			if (callExit && components.CanExecute(state))
			{
				behavior->Exit();
			}
			return true;
		}), behaviors.end());

		return behaviors.size() != oldSize;
	}

	void Scene::RefreshBehaviorFieldCacheForEntity(entt::entity e)
	{
		if (registry.valid(e))
		{
			if (registry.any_of<BehaviorComponents>(e))
			{
				BehaviorComponents& bc = registry.get<BehaviorComponents>(e);
				for (auto& b : bc.behaviors)
				{
					b->RefreshFieldCache();
				}
			}
		}
	}

	bool Scene::IsTopFocusedElement(entt::entity target)
	{
		InputManager* inputMgr = GetInputManager();
		if (!inputMgr)
		{
			return false;
		}

		glm::vec2 mouseVirt = UiCoordinates::WindowToVirtualCanvas(inputMgr->GetMousePosition(), inputMgr->GetWindowSize());
		return IsTopMostUiAtScreenPoint(target, mouseVirt);
	}

	// This can get very expensive to call
	bool Scene::IsTopMostUiAtScreenPoint(entt::entity target, const glm::vec2& point)
	{
		entt::registry& registry = GetRegistry();

		// Basic validity checks
		if (!registry.valid(target) || !registry.any_of<Transform>(target))
		{
			return false;
		}

		const Transform& myTf = registry.get<Transform>(target);
		if (myTf.GetTransformSpace() != TransformSpace::Screen)
		{
			return false;
		}

		// Target AABB (same convention as UpdateUIBehaviors)
		const glm::vec3 myPos = myTf.GetWorldPosition(registry);
		const glm::vec3 myScale = myTf.GetWorldScale(registry);
		const glm::vec2 myHalf{ 0.5f * std::abs(myScale.x), 0.5f * std::abs(myScale.y) };

		const glm::vec2 myMin{ myPos.x - myHalf.x, myPos.y - myHalf.y };
		const glm::vec2 myMax{ myPos.x + myHalf.x, myPos.y + myHalf.y };

		// If the target doesn't actually cover the point, it's not top-most UI at that point, nor is any UI
		if (!(point.x >= myMin.x && point.x <= myMax.x && point.y >= myMin.y && point.y <= myMax.y))
		{
			return false;
		}

		// const float myZ = myTf.GetPosition().z; // local Z 
		const float myZ = myTf.readableLayer; // we use the readable layer that is agnostic of render pipeline for layering

		bool coveredByFront = false;

		// Iterate all screen-space transforms and see if any overlapping AABB is in front of the target
		registry.view<Transform>().each([&](entt::entity e, Transform& tf)
		{
			if (coveredByFront) { return; } // early-out if already found something in front
			if (e == target) { return; }    // skip self
			if (tf.GetTransformSpace() != TransformSpace::Screen) { return; }

			const glm::vec3 pos = tf.GetWorldPosition(registry);
			const glm::vec3 scale = tf.GetWorldScale(registry);

			const glm::vec2 half{ 0.5f * std::abs(scale.x), 0.5f * std::abs(scale.y) };
			const glm::vec2 minRect{ pos.x - half.x, pos.y - half.y };
			const glm::vec2 maxRect{ pos.x + half.x, pos.y + half.y };

			const bool inside = (point.x >= minRect.x && point.x <= maxRect.x &&
				point.y >= minRect.y && point.y <= maxRect.y);

			if (!inside) { return; }

			// Compare with local Z for direct Z layer, our render contexts do screen space NDC's differently in Z layering
			// We instead fix this with the Transform::readableLayer float field

			if (tf.readableLayer <= myZ)
			{
				coveredByFront = true;
				return;
			}
		});

		return !coveredByFront;
	}

	// Point is in screen pixels, (0,0) = top-left.
	Ray Scene::ScreenPointToRay(const glm::vec2& point) const
	{
		CameraSystem* camera = GetCameraSystem();
		InputManager* input = GetInputManager();
		if (!camera || !input)
		{
			throw std::runtime_error("Scene::ScreenPointToRay requires presentation camera/input services.");
		}

		Camera& cam = camera->GetCamera();

		const Swim::Platform::Extent2D windowSize = input->GetWindowSize();
		const float width = static_cast<float>(windowSize.Width);
		const float height = static_cast<float>(windowSize.Height);

		// top-left-origin pixels -> NDC
		float ndcX = (2.0f * point.x) / width - 1.0f;  // [-1,+1], left->right
		float ndcY = 1.0f - (2.0f * point.y) / height;  // [-1,+1], top->bottom

		// Camera params
		const float fovY = glm::radians(cam.GetFOV());
		const float tanHalfFovY = tanf(fovY * 0.5f);
		float aspect = cam.GetAspect();
		if (aspect <= 0.0f && height > 0.0f) aspect = width / height;
		const float zNear = cam.GetNearClip();

		// View-space direction (RH, forward = -Z)
		const glm::vec3 dirVS(ndcX * tanHalfFovY * aspect, ndcY * tanHalfFovY, -1.0f);

		// Point on the near plane for this screen pixel (z = -zNear in view space)
		const glm::vec3 nearVS = dirVS * (zNear / -dirVS.z);

		// Rotate into world space & build ray
		const glm::quat q = cam.GetRotation();
		const glm::vec3 origin = cam.GetPosition() + (q * nearVS);
		const glm::vec3 dir = glm::normalize(q * dirVS);

		return Ray(origin, dir);
	}

	void Scene::SetEnabledStates(entt::entity entity, EngineState states)
	{
		if (registry.valid(entity))
		{
			BehaviorComponents& bc = registry.get_or_emplace<BehaviorComponents>(entity);
			bc.SetEnabledStates(states);
		}
	}

	void Scene::AddEnabledStates(entt::entity entity, EngineState states)
	{
		if (registry.valid(entity))
		{
			BehaviorComponents& bc = registry.get_or_emplace<BehaviorComponents>(entity);
			bc.AddEnabledStates(states);
		}
	}

	void Scene::RemoveEnabledStates(entt::entity entity, EngineState states)
	{
		if (registry.valid(entity))
		{
			BehaviorComponents& bc = registry.get_or_emplace<BehaviorComponents>(entity);
			bc.RemoveEnabledStates(states);
		}
	}

	ObjectTag* Scene::GetTag(entt::entity entity)
	{
		if (registry.valid(entity) && registry.any_of<ObjectTag>(entity))
		{
			return &registry.get<ObjectTag>(entity);
		}

		return nullptr;
	}

	// Under the hood attempts to get the name of entity via ObjectTag. By default this usually will be "Entity 12" for example.
	const std::string Scene::GetEntityName(entt::entity e) const
	{
		if (registry.valid(e))
		{
			if (registry.any_of<ObjectTag>(e))
			{
				const ObjectTag& t = registry.get<ObjectTag>(e);
				return t.name;
			}
		}

		const SerializedEntityId id = GetSerializedEntityId(e);
		return id ? "Entity " + std::to_string(id.Value) : "Entity Untracked";
	}

	void Scene::SetTag(entt::entity entity, unsigned int tag, const std::string& name)
	{
		if (registry.valid(entity))
		{
			if (registry.any_of<ObjectTag>(entity))
			{
				auto& t = registry.get<ObjectTag>(entity);
				t.tag = tag;
				t.name = name;
				if (sceneSyncTracker)
				{
					sceneSyncTracker->EntityMutated(entity);
				}
			}
			else
			{
				registry.emplace<ObjectTag>(entity, tag, name);
			}
		}
	}

	void Scene::RemoveTag(entt::entity entity)
	{
		if (registry.valid(entity) && registry.any_of<ObjectTag>(entity))
		{
			registry.remove<ObjectTag>(entity);
		}
	}

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

	void Scene::UpdatePhysics(PhysicsSystem& physicsSystem, double dt)
	{
		if (!HasAnyEngineStates(GetEngineState(), EngineState::Playing))
		{
			physicsTimeSinceLastTick = 0.0;
			return;
		}

		if (!physicsBridge)
		{
			return;
		}

		physicsTimeSinceLastTick += dt;

		float alpha = 1.0f;
		const float fixedDeltaSeconds = physicsSystem.GetFixedDeltaSeconds();
		if (fixedDeltaSeconds > 0.0f)
		{
			alpha = static_cast<float>(physicsTimeSinceLastTick / static_cast<double>(fixedDeltaSeconds));
		}

		physicsBridge->Interpolate(std::clamp(alpha, 0.0f, 1.0f));
	}

	void Scene::FixedUpdatePhysics(PhysicsSystem& physicsSystem)
	{
		if (!HasAnyEngineStates(GetEngineState(), EngineState::Playing))
		{
			physicsTimeSinceLastTick = 0.0;
			return;
		}

		GetOrCreatePhysicsWorld(physicsSystem);

		physicsBridge->Interpolate(1.0f);
		physicsTimeSinceLastTick = 0.0;

		const float fixedDeltaSeconds = physicsSystem.GetFixedDeltaSeconds();
		physicsBridge->PreSimulateSync(fixedDeltaSeconds);
		physicsBridge->Step(fixedDeltaSeconds);
		physicsBridge->FetchResults(true);
		physicsBridge->PostSimulateSync();
	}

	void Scene::InternalFixedPostUpdate(unsigned int tickThisSecond)
	{
		// nothing to do here
	}

}
