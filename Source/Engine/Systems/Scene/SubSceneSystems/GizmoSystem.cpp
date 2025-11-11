#include "PCH.h"
#include "GizmoSystem.h"
#include "Engine/Systems/Renderer/Core/Meshes/PrimitiveMeshes.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/MeshDecorator.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/TextComponent.h"
#include "Engine/Components/ObjectTag.h"
#include "Engine/Systems/Entity/CommonBehaviors/DragUiBehavior.h"
#include "Engine/Systems/Renderer/Core/Font/FontPool.h"
#include "Engine/Systems/Scene/InternalBehaviors/ChangeGizmoTypeButtonBehavior.h"

namespace Engine
{

	// On initial awake we will create all the meshes we use to render gizmos with
	int GizmoSystem::Awake()
	{
		MeshPool& mp = MeshPool::GetInstance();
		MaterialPool& mtp = MaterialPool::GetInstance();

		glm::vec3 white = GetDebugColorValue(DebugColor::White);

		auto getOrCreateMesh = [&](const char* name, auto&& makeData)
		{
			if (auto m = mp.GetMesh(name))
			{
				return m;
			}

			VertexesIndexesPair data = makeData();
			return mp.RegisterMesh(name, data);
		};

		// Helper to get or create a material bound to a mesh.
		auto getOrCreateMaterial = [&](const char* matName, const std::shared_ptr<Mesh>& mesh)
		{
			if (auto md = mtp.GetMaterialData(matName))
			{
				return md;
			}
			// No albedo map for gizmos; they rely on vertex colors / decorators
			return mtp.RegisterMaterialData(matName, mesh, nullptr);
		};

		// All of these will have mesh decorators on them for fill color

		sphereMesh = getOrCreateMesh("GizmoBall", [&]
		{
			return MakeSphere(24, 48, white, white, white);
		});

		arrowMesh = getOrCreateMesh("GizmoArrow", [&]
		{
			return MakeArrow(0.05f, 1.5f, 0.12f, 0.5f, 64, white);
		});

		ringMesh = getOrCreateMesh("GizmoRing", [&]
		{
			return MakeTorus(0.40f, 0.05f, 48, 24, white);
		});

		cubeMesh = getOrCreateMesh("GizmoCube", [&]
		{
			return MakeCube();
		});

		quadMesh = getOrCreateMesh("GizmoQuad", [&]
		{
			return MakeQuad();
		});

		circleMesh = getOrCreateMesh("GizmoCircle", [&]
		{
			return MakeCircle(0.5f, 128, white);
		});

		ballArrowMesh = getOrCreateMesh("GizmoBallArrow", [&]
		{
			return MakeBallArrow(0.05f, 1.5f, 0.12f, 64, white);
		});

		// Register or fetch materials for each gizmo mesh
		sphereMatData = getOrCreateMaterial("GizmoBallMat", sphereMesh);
		arrowMatData = getOrCreateMaterial("GizmoArrowMat", arrowMesh);
		ringMatData = getOrCreateMaterial("GizmoRingMat", ringMesh);
		cubeMatData = getOrCreateMaterial("GizmoCubeMat", cubeMesh);
		quadMatData = getOrCreateMaterial("GizmoQuadData", quadMesh);
		circleMatData = getOrCreateMaterial("GizmoCircleData", circleMesh);
		ballArrowMatData = getOrCreateMaterial("GizmoBallArrowData", ballArrowMesh);

		return 0;
	}

	int GizmoSystem::Init()
	{
		glm::vec4 gray = { 0.188f, 0.176f, 0.176f, 1.0f };
		glm::vec4 black = { 0.0f,   0.0f,   0.0f,   1.0f };
		glm::vec4 white = { 1.0f,   1.0f,   1.0f,   1.0f };

		// Common colors for all gizmo buttons
		glm::vec4 hoverColor = { 0.0509804f, 0.3450980f, 0.8196079f, 1.0f }; // blueish
		glm::vec4 activeColor = { 0.0f, 0.5f, 0.0f, 1.0f }; // green

		gizmoUI = activeScene->CreateEntity();
		activeScene->SetEnabledStates(gizmoUI, EngineState::Editing);

		// Base container box properties of the gizmo selection UI (in px)
		glm::vec3 position = { 100.0f, 700.0f, 0.0f };
		glm::vec3 scale = { 100.0f, 300.0f, 1.0f };
		glm::vec2 strokeWidth = { 6.0f, 6.0f };
		glm::vec2 cornerRadius = { 12.0f, 12.0f };
		glm::vec2 pad = { 0.0f, 0.0f };
		bool rounded = true, stroke = true, fill = true, useTex = false;

		activeScene->EmplaceComponent<Transform>(gizmoUI, position, scale, glm::quat(), TransformSpace::Screen);
		activeScene->EmplaceComponent<Material>(gizmoUI, Material(quadMatData));
		activeScene->EmplaceComponent<MeshDecorator>(gizmoUI, black, gray, strokeWidth, cornerRadius, pad, rounded, stroke, fill, useTex);
		activeScene->EmplaceComponent<ObjectTag>(gizmoUI, TagConstants::EDITOR_MODE_UI);
		activeScene->EmplaceBehavior<DragUiBehavior>(gizmoUI);

		// Create buttons parented to gizmoUI
		Engine::FontPool& fontPool = Engine::FontPool::GetInstance();
		std::shared_ptr<Engine::FontInfo> roboto = fontPool.GetFontInfo("roboto_bold");

		bool buttonRounded = false, buttonStroke = false, buttonFill = true, buttonUseTex = false;

		glm::vec3 buttonScale = { 0.70f, 0.25f, 1.0f };
		glm::vec2 buttonStrokeWidth = { 0.05f, 0.05f };
		glm::vec2 buttonCornerRadius = { 0.05f, 0.05f };
		glm::vec2 buttonPad = { 0.0f, 0.0f };
		glm::vec4 buttonStrokeColor = white;
		glm::vec4 buttonFillColor = gray;

		auto makeButton = [&](const char* label, float textY, const glm::vec3& pos, GizmoType type)
		{
			entt::entity e = activeScene->CreateEntity();
			activeScene->SetEnabledStates(e, EngineState::Editing);
			Transform& tf = activeScene->EmplaceComponent<Transform>(e, pos, buttonScale, glm::quat(), TransformSpace::Screen);
			activeScene->EmplaceComponent<Material>(e, Material(quadMatData));
			activeScene->EmplaceComponent<MeshDecorator>(e, buttonFillColor, buttonStrokeColor,
				buttonStrokeWidth, buttonCornerRadius, buttonPad, buttonRounded, buttonStroke, buttonFill, buttonUseTex);
			activeScene->EmplaceComponent<ObjectTag>(e, TagConstants::EDITOR_MODE_UI);

			ChangeGizmoTypeButtonBehavior* beh = activeScene->EmplaceBehavior<ChangeGizmoTypeButtonBehavior>(e);
			beh->SetGizmoType(type);
			beh->SetHoverColor(hoverColor);
			beh->SetActiveColor(activeColor);
			beh->SetRegularColor(gray);

			activeScene->SetParent(e, gizmoUI);
			tf.SetScreenSpaceLayerRelativeToParent(true);

			// Text
			entt::entity txt = activeScene->CreateEntity();
			activeScene->SetEnabledStates(txt, EngineState::Editing);
			glm::vec3 textPos = { 0.0f, textY, 0.0f };
			Transform& txtTf = activeScene->EmplaceComponent<Transform>(txt, textPos, buttonScale * 2.f, glm::quat(), TransformSpace::Screen);
			activeScene->EmplaceComponent<ObjectTag>(txt, TagConstants::EDITOR_MODE_UI);

			Engine::TextComponent tc;
			tc.fillColor = white;
			tc.strokeColor = black;
			tc.strokeWidth = 2.0f;
			tc.SetAlignment(Engine::TextAllignemt::Center);
			tc.SetText(label);
			tc.SetFont(roboto);

			activeScene->AddComponent<Engine::TextComponent>(txt, tc);
			activeScene->SetParent(txt, e);
			txtTf.SetScreenSpaceLayerRelativeToParent(true);

			return e;
		};

		makeButton("T", 75, { 0.0f,  0.3f, 0.0f }, GizmoType::Translate);
		makeButton("R", -15, { 0.0f,  0.0f, 0.0f }, GizmoType::Rotate);
		makeButton("S", -105, { 0.0f, -0.3f, 0.0f }, GizmoType::Scale); 

		return 0;
	}

	void GizmoSystem::Update(double dt)
	{
		EngineState state = SwimEngine::GetInstance()->GetEngineState(); 
		if (!activeScene || !HasAny(state, EngineState::Editing)) return; // only active during editing

		// If nothing is selected, we will call the method to do mouse click ray cast checks for if we are selecting anything in the scene.
		if (activeGizmoType != GizmoType::Inactive && (rootGizmoControl == entt::null || focusedEntity == entt::null))
		{
			NothingSelectedYetBehavior();
			return;
		}

		entt::registry& reg = activeScene->GetRegistry();

		// If focused entity or their transform was destroyed, destroy the root gizmo control entity 
		if (!reg.valid(focusedEntity) || !reg.any_of<Transform>(focusedEntity))
		{
			LoseFocus();
			return;
		} 
		// We are kinda boned if something happens to root control entity, we can set the engine scene policy of:
		// DO NOT EVER destroy editor tagged objects that should not be destroyed (duh).

		// If we have something selected, we will call the method to control the root gizmo based on gizmo type
		if (activeGizmoType != GizmoType::Inactive && focusedEntity != entt::null && rootGizmoControl != entt::null)
		{
			if (!activeScene->IsMouseBusyWithUI())
			{
				GizmoRootControl();
			}

			// Update size
			ScaleGizmoBasedOnCameraDistance(reg);
		}
	}

	// This will delete the current root gizmo control and replaces it with the new gizmo type 
	void GizmoSystem::SetGizmoType(GizmoType type)
	{
		activeGizmoType = type;

		LoseFocus(/*bool setFocusedEntityNull=*/ activeGizmoType == GizmoType::Inactive);

		entt::registry& reg = activeScene->GetRegistry();

		if (reg.valid(focusedEntity) && reg.any_of<Transform>(focusedEntity) && activeGizmoType != GizmoType::Inactive)
		{
			SelectedEntityToControlWithGizmo(focusedEntity);
		}
	}

	void GizmoSystem::LoseFocus(bool setFocusedEntityNull)
	{
		if (setFocusedEntityNull) // true by default
		{
			focusedEntity = entt::null;
		}

		activeScene->DestroyEntity(rootGizmoControl, true, true);
		rootGizmoControl = entt::null;

		axisX = axisY = axisZ = entt::null;
		hoveredAxis = Axis::None;
		activeAxisDrag = Axis::None;
		isDragging = false;
	}

	void GizmoSystem::GizmoRootControl()
	{
		auto input = activeScene->GetInputManager();
		bool lDown = input->IsKeyDown(VK_LBUTTON);
		bool lPressed = input->IsKeyTriggered(VK_LBUTTON);
		bool lReleased = !lDown && isDragging; // basic release detection

		// Build current mouse ray
		glm::vec2 mousePos = input->GetMousePosition(false);
		Engine::Ray ray = activeScene->ScreenPointToRay(mousePos);
		const glm::vec3 rayOrigin = ray.origin;
		const glm::vec3 rayDirN = glm::normalize(ray.dir);

		// If currently dragging, update or end
		if (isDragging)
		{
			if (lReleased)
			{
				EndDrag();
				return;
			}

			UpdateDrag(rayOrigin, rayDirN);
			return;
		}

		// Not dragging: update hover state over gizmo axes
		entt::entity hoverHit = RayCastUnderMouse();
		Axis hoverAxis = AxisFromTagEntity(hoverHit);
		hoveredAxis = hoverAxis;
		SetAxisHighlight(hoveredAxis, Axis::None);

		entt::registry& reg = activeScene->GetRegistry();

		// Begin drag if pressed on an axis
		if (lPressed && hoverAxis != Axis::None)
		{
			BeginDrag(hoverAxis, rayOrigin, rayDirN);
			return;
		}

		// Otherwise, if clicked and not over gizmo, change focus
		if (lPressed && hoverAxis == Axis::None)
		{
			entt::entity hit = LeftClickCheck();

			LoseFocus();

			// Check if we clicked something else, jump focus to it instead if so:
			if (hit != entt::null && hit != focusedEntity && reg.valid(hit))
			{
				SelectedEntityToControlWithGizmo(hit);
			}
		}
	}

	// Scales gizmo to be larger the further camera is away from the focused entity
	void GizmoSystem::ScaleGizmoBasedOnCameraDistance(entt::registry& reg)
	{
		if (reg.valid(rootGizmoControl) && reg.any_of<Transform>(rootGizmoControl))
		{
			const glm::vec3& camPos = activeScene->GetCameraSystem()->GetCamera().GetPosition();
			Transform& tf = reg.get<Transform>(rootGizmoControl);
			float distance = glm::distance(tf.GetWorldPosition(reg), camPos);
			float scale = std::max(1.0f, distance / 10.f);
			tf.GetScaleRef() = glm::vec3(scale);
		}
	}

	void GizmoSystem::NothingSelectedYetBehavior()
	{
		entt::entity hit = LeftClickCheck();

		if (hit != entt::null)
		{
			SelectedEntityToControlWithGizmo(hit);
		}
	}

	// We'll gather all hits along the ray in front-to-back order for left click check.
	struct Hit
	{
		entt::entity e{ entt::null };
		float t{ std::numeric_limits<float>::infinity() };
		AABB aabb{};
	};

	// Ray casts in world for if we left clicked anything
	entt::entity GizmoSystem::LeftClickCheck()
	{
		auto input = activeScene->GetInputManager();
		bool leftClicked = input->IsKeyTriggered(VK_LBUTTON) || input->IsKeyDown(VK_LBUTTON);

		if (!leftClicked || activeScene->IsMouseBusyWithUI())
		{
			return entt::null;
		}

		glm::vec2 mousePos = input->GetMousePosition(false);
		mousePos.y += 14; // window title bar adjustment hack
		Engine::Ray ray = activeScene->ScreenPointToRay(mousePos);

		const glm::vec3 camPos = activeScene->GetCameraSystem()->GetCamera().GetPosition();

		entt::registry& reg = activeScene->GetRegistry();

		static auto isEditorOnly = [&](entt::entity e) -> bool
		{
			if (!reg.valid(e) || !reg.any_of<ObjectTag>(e))
			{
				return false;
			}
			const ObjectTag& tag = reg.get<ObjectTag>(e);
			return tag.tag == TagConstants::EDITOR_MODE_OBJECT;
		};

		std::vector<Hit> hits;
		hits.reserve(32);

		activeScene->GetSceneBVH()->RayCastCallback(ray, [&](entt::entity e, float t, const AABB& aabb)
		{
			// Collect everything; we'll decide after. Keep order (front-to-back).
			if (e != entt::null && reg.valid(e))
			{
				hits.push_back({ e, t, aabb });
			}
			return true; // continue collecting
		}, 0.0f, std::numeric_limits<float>::infinity());

		if (hits.empty())
		{
			return entt::null;
		}

		std::vector<Hit> sceneHits;
		sceneHits.reserve(hits.size());

		// Filter out editor-only objects (gizmos, etc.) if present
		for (const Hit& h : hits)
		{
			if (!isEditorOnly(h.e))
			{
				sceneHits.push_back(h);
			}
		}

		if (sceneHits.empty())
		{
			return entt::null;
		}

		// Find all container entities whose AABB currently contains the camera.
		std::vector<Hit> containers;
		containers.reserve(sceneHits.size());

		for (const Hit& h : sceneHits)
		{
			if (PointInsideAABB(camPos, h.aabb))
			{
				containers.push_back(h);
			}
		}

		// If we are inside any container, prefer selecting a child hit within any of those containers
		if (!containers.empty())
		{
			// First: nearest enclosed child (entity NOT equal to the container), whose AABB is inside any container AABB.
			Hit bestChild{};
			bool foundChild = false;

			for (const Hit& h : sceneHits)
			{
				// Skip if this hit itself is a container (we'll consider them second).
				bool isContainerItself = false;
				for (const Hit& c : containers)
				{
					if (h.e == c.e)
					{
						isContainerItself = true;
						break;
					}
				}

				if (isContainerItself)
				{
					continue;
				}

				// Check if this hit is enclosed by ANY container's AABB
				bool enclosedByAny = false;

				for (const Hit& c : containers)
				{
					if (AABBInsideAABB(h.aabb, c.aabb))
					{
						enclosedByAny = true;
						break;
					}
				}

				if (!enclosedByAny)
				{
					continue;
				}

				// Keep nearest enclosed child
				if (!foundChild || h.t < bestChild.t)
				{
					bestChild = h;
					foundChild = true;
				}
			}

			if (foundChild && bestChild.e != entt::null && reg.valid(bestChild.e))
			{
				return bestChild.e; // Prefer child (e.g., cube) inside container (e.g., bigger cube)
			}

			// Second: no suitable child; choose nearest container hit itself
			Hit bestContainer = containers.front();

			for (const Hit& c : containers)
			{
				if (c.t < bestContainer.t)
				{
					bestContainer = c;
				}
			}

			if (bestContainer.e != entt::null && reg.valid(bestContainer.e))
			{
				return bestContainer.e; // Select the container 
			}
		}

		// No container scenario or no valid container selection: pick the nearest general hit.
		const Hit* nearest = nullptr;
		for (const Hit& h : sceneHits)
		{
			if (!nearest || h.t < nearest->t)
			{
				nearest = &h;
			}
		}

		if (nearest && nearest->e != entt::null && reg.valid(nearest->e))
		{
			return nearest->e;
		}

		return entt::null;
	}

	void GizmoSystem::SelectedEntityToControlWithGizmo(entt::entity hit)
	{
		focusedEntity = hit;
		if (activeGizmoType == GizmoType::Translate || activeGizmoType == GizmoType::Scale)
		{
			CreateTranslationGizmo(/*bool useBallArrow=*/activeGizmoType == GizmoType::Scale);
		}
	}

	// 3 arrows for each axis from root, red for X, blue for Z, green for Y
	void GizmoSystem::CreateTranslationGizmo(bool useBallArrow)
	{
		entt::registry& reg = activeScene->GetRegistry();

		// Focused entity must have a transform (how would this ever happen!?)
		if (!reg.any_of<Transform>(focusedEntity))
		{
			return;
		}

		// Focused entity transform
		Transform& tf = reg.get<Transform>(focusedEntity);
		const glm::vec3 pos = tf.GetWorldPosition(reg); // we have crashed here before

		// World-axis rotations (arrow model points +Y) 
		const glm::quat rotX = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 0, 1)); // +X
		const glm::quat rotY = glm::quat(glm::vec3(0));                                    // +Y
		const glm::quat rotZ = glm::angleAxis(+glm::half_pi<float>(), glm::vec3(1, 0, 0)); // +Z

		// Colors
		const glm::vec4 RED(1.0f, 0.0f, 0.0f, 1.0f);
		const glm::vec4 GREEN(0.0f, 1.0f, 0.0f, 1.0f);
		const glm::vec4 BLUE(0.0f, 0.0f, 1.0f, 1.0f);

		// Root control
		rootGizmoControl = activeScene->CreateEntity();
		{
			Transform rootT;
			rootT.GetPositionRef() = pos;
			rootT.GetScaleRef() = tf.GetWorldScale(reg);
			rootT.GetRotationRef() = glm::quat(1, 0, 0, 0);
			activeScene->EmplaceComponent<Transform>(rootGizmoControl, rootT);
			activeScene->EmplaceComponent<ObjectTag>(rootGizmoControl, TagConstants::EDITOR_MODE_OBJECT, "gizmo root");
			activeScene->SetEnabledStates(rootGizmoControl, EngineState::Editing);
			// Just to show the parent root gizmo control object (we know it works):
			// auto color = GetDebugColorValue(DebugColor::Gray);
			// activeScene->EmplaceComponent<Material>(rootGizmoControl, Material(sphereMatData));
			// activeScene->EmplaceComponent<MeshDecorator>(rootGizmoControl, glm::vec4(color, 1));
		}

		auto spawnAxis = [&](const glm::quat& r, const glm::vec4& color, const std::string& tagName)
		{
			entt::entity e = activeScene->CreateEntity();
			activeScene->SetEnabledStates(e, EngineState::Editing);

			// tweak: gap away from root so arrow ends don't touch at (0,0,0) 
			constexpr float kGap = 0.1f;        // small world-space gap to avoid z-fighting (tweak as needed)
			constexpr float kArrowHalfLen = 0.0f; // set to half the arrow length if the mesh is centered

			// Offset along the arrow's local +Y, expressed in parent (root) space
			const glm::vec3 localOffset = r * glm::vec3(0.0f, kArrowHalfLen + kGap, 0.0f);

			// Local to root (so (0,0,0) would be at root position)
			Transform tLocal;
			tLocal.GetPositionRef() = localOffset;
			tLocal.GetScaleRef() = glm::vec3(1.0f);
			tLocal.GetRotationRef() = r;
			activeScene->EmplaceComponent<Transform>(e, tLocal);

			// Filled mesh that always renders on top to ignore depth buffer
			MeshDecorator dec{};
			dec.fillColor = color;
			dec.renderOnTop = 1;

			// Attach the material and decorator color
			std::shared_ptr<MaterialData> matData = useBallArrow ? ballArrowMatData : arrowMatData;
			activeScene->EmplaceComponent<Material>(e, matData);
			activeScene->AddComponent<MeshDecorator>(e, dec);
			activeScene->EmplaceComponent<ObjectTag>(e, TagConstants::EDITOR_MODE_OBJECT, "gizmo " + tagName);

			// Parent under the root control
			activeScene->SetParent(e, rootGizmoControl);

			return e;
		};

		// X (red), Y (green), Z (blue)
		axisX = spawnAxis(rotX, RED, "x");
		axisY = spawnAxis(rotY, GREEN, "y");
		axisZ = spawnAxis(rotZ, BLUE, "z");

		hoveredAxis = Axis::None;
		activeAxisDrag = Axis::None;
		isDragging = false;

		// Initialize highlight (none hovered/active)
		SetAxisHighlight(Axis::None, Axis::None);
	}

	entt::entity GizmoSystem::RayCastUnderMouse() const
	{
		auto input = activeScene->GetInputManager();
		glm::vec2 mousePos = input->GetMousePosition(false);
		mousePos.y += 14; // window title bar adjustment
		Engine::Ray ray = activeScene->ScreenPointToRay(mousePos);

		entt::entity result = entt::null;
		float bestT = std::numeric_limits<float>::infinity();

		auto& reg = activeScene->GetRegistry();

		// Visit hits front-to-back; stop on the first valid gizmo axis that isn't the focused entity.
		activeScene->GetSceneBVH()->RayCastCallback(
			ray,
			[&](entt::entity e, float t, const AABB& /*aabb*/)
		{
			// Skip invalids and the object we're currently manipulating
			if (e == entt::null || e == focusedEntity || !reg.valid(e))
			{
				return true; // keep searching
			}

			// We only care about gizmo parts for hover (editor-only objects named "gizmo x/y/z")
			if (!reg.any_of<ObjectTag>(e))
			{
				return true;
			}

			const ObjectTag& tag = reg.get<ObjectTag>(e);
			const bool isEditorObj = (tag.tag == TagConstants::EDITOR_MODE_OBJECT);
			const bool isGizmoAxis = (tag.name == "gizmo x") || (tag.name == "gizmo y") || (tag.name == "gizmo z");

			if (!isEditorObj || !isGizmoAxis)
			{
				return true;
			}

			// Take nearest gizmo hit and early-out (callback returns false to stop)
			if (t < bestT)
			{
				bestT = t;
				result = e;
			}
			return false; // nearest acceptable hit found
		}, 0.0f, std::numeric_limits<float>::infinity());

		return result;
	}

	Axis GizmoSystem::AxisFromTagEntity(entt::entity e) const
	{
		if (e == entt::null)
		{
			return Axis::None;
		}

		entt::registry& reg = activeScene->GetRegistry();

		if (!reg.valid(e) || !reg.any_of<ObjectTag>(e))
		{
			return Axis::None;
		}

		const ObjectTag& tag = reg.get<ObjectTag>(e);

		// Tags are "gizmo x/y/z"
		if (tag.name == "gizmo x") return Axis::X;
		if (tag.name == "gizmo y") return Axis::Y;
		if (tag.name == "gizmo z") return Axis::Z;

		return Axis::None;
	}

	void GizmoSystem::BeginDrag(Axis axis, const glm::vec3& rayOrigin, const glm::vec3& rayDirN)
	{
		activeAxisDrag = axis;
		isDragging = true;

		entt::registry& reg = activeScene->GetRegistry();
		Transform& focusedTf = reg.get<Transform>(focusedEntity);
		const glm::vec3 gizmoOrigin = focusedTf.GetWorldPosition(reg);

		// Cache everything we need for the start of the drag to use in UpdateDrag
		dragAxisDir = glm::normalize(AxisDirWorld(axis));
		dragStartT = ParamOnAxisFromRay(gizmoOrigin, dragAxisDir, rayOrigin, rayDirN);
		dragStartObjPos = gizmoOrigin; 
		dragStartObjScale = focusedTf.GetWorldScale(reg);

		// Highlight active axis strongly; lock hover
		SetAxisHighlight(Axis::None, activeAxisDrag);
	}

	// Right now this is only does translation and scale
	void GizmoSystem::UpdateDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirN)
	{
		if (activeAxisDrag == Axis::None || focusedEntity == entt::null)
		{
			return;
		}

		entt::registry& reg = activeScene->GetRegistry();
		if (!reg.valid(focusedEntity) || !reg.any_of<Transform>(focusedEntity))
		{
			return;
		}

		// Axis is anchored at the object position when drag started
		const glm::vec3 axisOrigin = dragStartObjPos;

		const float tNow = ParamOnAxisFromRay(axisOrigin, dragAxisDir, rayOrigin, rayDirN);
		const float dt = tNow - dragStartT;

		const glm::vec3 delta = dt * dragAxisDir;

		// Apply to focused object (world-space)
		Transform& focusedTf = reg.get<Transform>(focusedEntity);

		if (activeGizmoType == GizmoType::Translate)
		{
			// we need to translate (dragStartObjPos + delta) into the focused transforms world space based on its parents, otherwise we teleport on dragging
			focusedTf.SetWorldPosition(reg, dragStartObjPos + delta);
		}
		else if (activeGizmoType == GizmoType::Scale)
		{
			// We should make this clamp to not be too small (like not under 0.1 on any axis)
			const int axis = AxisIndex(activeAxisDrag);
			if (axis >= 0)
			{
				glm::vec3 newScale = dragStartObjScale;

				const float sensitivity = 1.0f;
				float factor = 1.0f + dt * sensitivity;

				const float minAxisScale = 0.1f;
				const float minFactor = minAxisScale / std::max(0.0001f, dragStartObjScale[axis]);

				factor = std::max(factor, minFactor);

				newScale[axis] = dragStartObjScale[axis] * factor;

				newScale.x = std::max(newScale.x, minAxisScale);
				newScale.y = std::max(newScale.y, minAxisScale);
				newScale.z = std::max(newScale.z, minAxisScale);

				// we need to translate newScale into the focused transforms world space based on its parents, so we can grow correctly
				focusedTf.SetWorldScale(reg, newScale);
			}
		}

		// Keep gizmo root on the object while dragging 
		if (rootGizmoControl != entt::null && reg.valid(rootGizmoControl))
		{
			Transform& gizmoTf = reg.get<Transform>(rootGizmoControl);
			gizmoTf.GetPositionRef() = focusedTf.GetWorldPosition(reg);
		}
	}

	void GizmoSystem::EndDrag()
	{
		isDragging = false;
		activeAxisDrag = Axis::None;

		// Return to hover-only highlight
		SetAxisHighlight(hoveredAxis, Axis::None);
	}

	void GizmoSystem::SetAxisHighlight(Axis hovered, Axis active)
	{
		auto setColor = [&](entt::entity e, const glm::vec4& color)
		{
			if (e == entt::null)
			{
				return;
			}

			entt::registry& reg = activeScene->GetRegistry();

			if (!reg.valid(e) || !reg.any_of<MeshDecorator>(e))
			{
				return;
			}

			MeshDecorator& deco = reg.get<MeshDecorator>(e);
			deco.fillColor = color;
		};

		// Base colors
		const glm::vec4 RED(1, 0, 0, 1);
		const glm::vec4 GREEN(0, 1, 0, 1);
		const glm::vec4 BLUE(0, 0, 1, 1);

		// Hover/active accents
		const glm::vec4 RED_H(1, 0.4f, 0.4f, 1);
		const glm::vec4 GREEN_H(0.4f, 1, 0.4f, 1);
		const glm::vec4 BLUE_H(0.4f, 0.4f, 1, 1);

		const glm::vec4 RED_A(1, 0.7f, 0.7f, 1);
		const glm::vec4 GREEN_A(0.7f, 1, 0.7f, 1);
		const glm::vec4 BLUE_A(0.7f, 0.7f, 1, 1);

		// Dragging color (yellow)
		const glm::vec4 DRAG_COLOR(1.0f, 1.0f, 0.f, 1.0f);

		auto choose = [&](Axis a) -> glm::vec4
		{
			// If currently dragging this axis, make it yellow
			if (isDragging && activeAxisDrag == a)
			{
				return DRAG_COLOR;
			}

			if (active == a)
			{
				switch (a)
				{
					case Axis::X: return RED_A;
					case Axis::Y: return GREEN_A;
					case Axis::Z: return BLUE_A;
					default:      break;
				}
			}

			if (hovered == a)
			{
				switch (a)
				{
					case Axis::X: return RED_H;
					case Axis::Y: return GREEN_H;
					case Axis::Z: return BLUE_H;
					default:      break;
				}
			}

			switch (a)
			{
				case Axis::X: return RED;
				case Axis::Y: return GREEN;
				case Axis::Z: return BLUE;
				default:      return glm::vec4(1);
			}
		};

		setColor(axisX, choose(Axis::X));
		setColor(axisY, choose(Axis::Y));
		setColor(axisZ, choose(Axis::Z));
	}

}
