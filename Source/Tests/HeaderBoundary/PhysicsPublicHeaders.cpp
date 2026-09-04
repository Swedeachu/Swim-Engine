#include "Engine/Systems/Physics/IPhysicsBackend.h"
#include "Engine/Systems/Physics/PhysicsHandles.h"
#include "Engine/Systems/Physics/PhysicsSystem.h"
#include "Engine/Systems/Physics/PhysicsTypes.h"
#include "Engine/Systems/Physics/PhysicsWorld.h"
#include "Engine/Systems/Physics/RigidBody.h"

namespace
{
	Engine::BodyHandle Body;
	Engine::ShapeHandle Shape;
	Engine::PhysicsMaterialHandle Material;
	Engine::ConstraintHandle Constraint;
	Engine::CharacterHandle Character;
	Engine::PhysicsWorldDesc WorldDesc;
	Engine::BodyDesc BodyDesc;
	Engine::ShapeDesc ShapeDesc;
	Engine::PhysicsMaterialDesc MaterialDesc;
	Engine::RaycastHit RaycastHit;
	Engine::SweepHit SweepHit;
	Engine::OverlapHit OverlapHit;
	Engine::CollisionEvent CollisionEvent;
	Engine::TriggerEvent TriggerEvent;
}
