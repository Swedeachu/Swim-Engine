#include "Engine/Systems/Scene/SceneCatalog.h"
#include "Engine/Systems/Scene/SceneId.h"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>

int main()
{
	Engine::SceneCatalog catalog;

	catalog.Register("Sandbox", [](const std::string&)
	{
		return std::shared_ptr<Engine::Scene>{};
	});
	catalog.Register("LightingTest", [](const std::string&)
	{
		return std::shared_ptr<Engine::Scene>{};
	});

	assert(catalog.Contains("Sandbox"));
	assert(catalog.Contains("LightingTest"));
	assert(!catalog.Contains("Missing"));
	assert(catalog.GetDescriptors().size() == 2);
	assert(catalog.GetDescriptors()[0].Name == "Sandbox");
	assert(catalog.GetDescriptors()[1].Name == "LightingTest");

	bool duplicateRejected = false;
	try
	{
		catalog.Register("Sandbox", [](const std::string&)
		{
			return std::shared_ptr<Engine::Scene>{};
		});
	}
	catch (const std::runtime_error&)
	{
		duplicateRejected = true;
	}
	assert(duplicateRejected);

	bool emptyNameRejected = false;
	try
	{
		catalog.Register("", [](const std::string&)
		{
			return std::shared_ptr<Engine::Scene>{};
		});
	}
	catch (const std::invalid_argument&)
	{
		emptyNameRejected = true;
	}
	assert(emptyNameRejected);

	Engine::SceneId invalidId;
	assert(!invalidId.IsValid());
	assert(!static_cast<bool>(invalidId));

	return 0;
}
