#include "Engine/Systems/Scene/SceneCatalog.h"
#include "Engine/Systems/Scene/SceneId.h"
#include "Tests/Framework/Test.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{

	Engine::SceneCatalog::Factory MakeNullSceneFactory()
	{
		return [](const std::string&)
		{
			return std::shared_ptr<Engine::Scene>{};
		};
	}

}

SWIM_TEST("Scene.SceneCatalog", "RegistrationPreservesDeclarationOrder")
{
	Engine::SceneCatalog catalog;
	catalog.Register("Sandbox", MakeNullSceneFactory());
	catalog.Register("LightingTest", MakeNullSceneFactory());

	SWIM_CHECK(catalog.Contains("Sandbox"));
	SWIM_CHECK(catalog.Contains("LightingTest"));
	SWIM_CHECK(!catalog.Contains("Missing"));
	SWIM_REQUIRE(catalog.GetDescriptors().size() == 2);
	SWIM_CHECK_EQUAL(catalog.GetDescriptors()[0].Name, std::string("Sandbox"));
	SWIM_CHECK_EQUAL(catalog.GetDescriptors()[1].Name, std::string("LightingTest"));
}

SWIM_TEST("Scene.SceneCatalog", "InvalidRegistrationsAreRejectedByType")
{
	Engine::SceneCatalog catalog;
	catalog.Register("Sandbox", MakeNullSceneFactory());

	SWIM_CHECK_THROWS(catalog.Register("Sandbox", MakeNullSceneFactory()), std::runtime_error);
	SWIM_CHECK_THROWS(catalog.Register("", MakeNullSceneFactory()), std::invalid_argument);
}

SWIM_TEST("Scene.SceneId", "DefaultConstructedIdentityIsInvalid")
{
	Engine::SceneId invalidId;
	SWIM_CHECK(!invalidId.IsValid());
	SWIM_CHECK(!static_cast<bool>(invalidId));
}
