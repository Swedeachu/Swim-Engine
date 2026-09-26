#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"
#include "Tests/Framework/Test.h"

#include <cstdint>

SWIM_TEST("Scene.Frustum", "RevisionsAreIndependentPerView")
{
	Engine::Frustum primaryView{};
	Engine::Frustum secondaryView{};

	const glm::mat4 identity{ 1.0f };
	glm::mat4 secondaryCamera{ 1.0f };
	secondaryCamera[3][0] = 5.0f;

	primaryView.Update(identity, identity);
	secondaryView.Update(secondaryCamera, identity);

	const std::uint64_t primaryRevision = primaryView.GetRevision();
	const std::uint64_t secondaryRevision = secondaryView.GetRevision();

	SWIM_CHECK(primaryRevision != 0);
	SWIM_CHECK(secondaryRevision != 0);
	SWIM_CHECK(primaryRevision != secondaryRevision);
	SWIM_CHECK(primaryView.DidCameraMoveThisFrame());
	SWIM_CHECK(secondaryView.DidCameraMoveThisFrame());
}

SWIM_TEST("Scene.Frustum", "UnchangedViewsDoNotAdvanceTheirRevision")
{
	Engine::Frustum primaryView{};
	Engine::Frustum secondaryView{};

	const glm::mat4 identity{ 1.0f };
	glm::mat4 secondaryCamera{ 1.0f };
	secondaryCamera[3][0] = 5.0f;

	primaryView.Update(identity, identity);
	secondaryView.Update(secondaryCamera, identity);
	const std::uint64_t primaryRevision = primaryView.GetRevision();
	const std::uint64_t secondaryRevision = secondaryView.GetRevision();

	// Updating one view must never disturb the other view's motion state.
	primaryView.Update(identity, identity);
	SWIM_CHECK_EQUAL(primaryView.GetRevision(), primaryRevision);
	SWIM_CHECK(!primaryView.DidCameraMoveThisFrame());
	SWIM_CHECK_EQUAL(secondaryView.GetRevision(), secondaryRevision);
	SWIM_CHECK(secondaryView.DidCameraMoveThisFrame());

	glm::mat4 movedPrimaryCamera{ 1.0f };
	movedPrimaryCamera[3][2] = -3.0f;
	primaryView.Update(movedPrimaryCamera, identity);

	SWIM_CHECK(primaryView.GetRevision() != primaryRevision);
	SWIM_CHECK(primaryView.GetRevision() != secondaryRevision);
	SWIM_CHECK(primaryView.DidCameraMoveThisFrame());
	SWIM_CHECK_EQUAL(secondaryView.GetRevision(), secondaryRevision);

	secondaryView.Update(secondaryCamera, identity);
	SWIM_CHECK_EQUAL(secondaryView.GetRevision(), secondaryRevision);
	SWIM_CHECK(!secondaryView.DidCameraMoveThisFrame());
}
