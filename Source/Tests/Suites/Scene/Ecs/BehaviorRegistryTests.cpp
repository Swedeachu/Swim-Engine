#include "Engine/Systems/Entity/BehaviorRegistry.h"
#include "Tests/Framework/Test.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{

	Engine::BehaviorRegistry::Factory MakeNullBehaviorFactory()
	{
		return [](Engine::Scene*, entt::entity)
		{
			return std::unique_ptr<Engine::Behavior>{};
		};
	}

}

SWIM_TEST("Scene.BehaviorRegistry", "RegistrationPreservesDeclarationOrder")
{
	Engine::BehaviorRegistry registry;
	registry.Register("Spin", MakeNullBehaviorFactory());
	registry.Register("Movement", MakeNullBehaviorFactory());

	SWIM_CHECK(registry.Contains("Spin"));
	SWIM_CHECK(registry.Contains("Movement"));
	SWIM_CHECK(!registry.Contains("Missing"));
	SWIM_REQUIRE(registry.GetDescriptors().size() == 2);
	SWIM_CHECK_EQUAL(registry.GetDescriptors()[0].Name, std::string("Spin"));
	SWIM_CHECK_EQUAL(registry.GetDescriptors()[1].Name, std::string("Movement"));
}

SWIM_TEST("Scene.BehaviorRegistry", "InvalidRegistrationsAreRejectedByType")
{
	Engine::BehaviorRegistry registry;
	registry.Register("Spin", MakeNullBehaviorFactory());

	SWIM_CHECK_THROWS(registry.Register("Spin", MakeNullBehaviorFactory()), std::runtime_error);
	SWIM_CHECK_THROWS(registry.Register("", MakeNullBehaviorFactory()), std::invalid_argument);
	SWIM_CHECK_THROWS(registry.Register("Broken", Engine::BehaviorRegistry::Factory{}), std::invalid_argument);
}
