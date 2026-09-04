#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"

#include <cassert>
#include <cstdint>

int main()
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

	assert(primaryRevision != 0);
	assert(secondaryRevision != 0);
	assert(primaryRevision != secondaryRevision);
	assert(primaryView.DidCameraMoveThisFrame());
	assert(secondaryView.DidCameraMoveThisFrame());

	primaryView.Update(identity, identity);
	assert(primaryView.GetRevision() == primaryRevision);
	assert(!primaryView.DidCameraMoveThisFrame());
	assert(secondaryView.GetRevision() == secondaryRevision);
	assert(secondaryView.DidCameraMoveThisFrame());

	glm::mat4 movedPrimaryCamera{ 1.0f };
	movedPrimaryCamera[3][2] = -3.0f;
	primaryView.Update(movedPrimaryCamera, identity);

	assert(primaryView.GetRevision() != primaryRevision);
	assert(primaryView.GetRevision() != secondaryRevision);
	assert(primaryView.DidCameraMoveThisFrame());
	assert(secondaryView.GetRevision() == secondaryRevision);
	assert(secondaryView.DidCameraMoveThisFrame());

	secondaryView.Update(secondaryCamera, identity);
	assert(secondaryView.GetRevision() == secondaryRevision);
	assert(!secondaryView.DidCameraMoveThisFrame());

	return 0;
}
