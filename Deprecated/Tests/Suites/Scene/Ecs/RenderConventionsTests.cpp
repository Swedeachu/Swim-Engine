#include "Engine/Systems/Renderer/Core/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"
#include "Engine/Systems/Renderer/Core/RenderConventions.h"
#include "Tests/Framework/Test.h"

namespace
{

	Engine::Camera MakeCanonicalCamera()
	{
		Engine::Camera camera;
		camera.SetAspect(16.0f / 9.0f);
		camera.SetFOV(60.0f);
		camera.SetClipPlanes(0.1f, 1000.0f);
		return camera;
	}

}

// The canonical conventions are compile-time facts, so a mismatch must fail the
// build rather than a run.
static_assert(Engine::CanonicalWorldHandedness == Engine::WorldHandedness::RightHanded);
static_assert(Engine::CanonicalClipSpaceDepthRange == Engine::ClipSpaceDepthRange::ZeroToOne);
static_assert(Engine::CanonicalClipSpaceYAxis == Engine::ClipSpaceYAxis::Up);
static_assert(Engine::CanonicalUiCoordinateOrigin == Engine::UiCoordinateOrigin::BottomLeft);

SWIM_TEST("Scene.RenderConventions", "ProjectionUsesZeroToOneReversedDepthRange")
{
	Engine::Camera camera = MakeCanonicalCamera();

	const glm::mat4& projection = camera.GetProjectionMatrix();
	const glm::vec4 nearClip = projection * glm::vec4(0.0f, 0.0f, -0.1f, 1.0f);
	const glm::vec4 farClip = projection * glm::vec4(0.0f, 0.0f, -1000.0f, 1.0f);

	SWIM_CHECK_NEAR(nearClip.z / nearClip.w, 0.0f, 0.0001f);
	SWIM_CHECK_NEAR(farClip.z / farClip.w, 1.0f, 0.0001f);
}

SWIM_TEST("Scene.RenderConventions", "ViewSpaceLooksDownNegativeZ")
{
	Engine::Camera camera = MakeCanonicalCamera();

	const glm::mat4& view = camera.GetViewMatrix();
	const glm::vec4 forwardPoint = view * glm::vec4(0.0f, 0.0f, -5.0f, 1.0f);
	SWIM_CHECK(forwardPoint.z < 0.0f);
}

SWIM_TEST("Scene.RenderConventions", "ScreenProjectionMapsBottomLeftOrigin")
{
	const glm::mat4 screenProjection = Engine::BuildCanonicalScreenProjection(1920.0f, 1080.0f);
	const glm::vec4 bottomLeft = screenProjection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	const glm::vec4 topRight = screenProjection * glm::vec4(1920.0f, 1080.0f, 1.0f, 1.0f);

	SWIM_CHECK_NEAR(bottomLeft.x / bottomLeft.w, -1.0f, 0.0001f);
	SWIM_CHECK_NEAR(bottomLeft.y / bottomLeft.w, -1.0f, 0.0001f);
	SWIM_CHECK_NEAR(bottomLeft.z / bottomLeft.w, 0.0f, 0.0001f);
	SWIM_CHECK_NEAR(topRight.x / topRight.w, 1.0f, 0.0001f);
	SWIM_CHECK_NEAR(topRight.y / topRight.w, 1.0f, 0.0001f);
	SWIM_CHECK_NEAR(topRight.z / topRight.w, 1.0f, 0.0001f);
}

SWIM_TEST("Scene.RenderConventions", "FrustumCullingAgreesWithTheCameraConvention")
{
	Engine::Camera camera = MakeCanonicalCamera();

	Engine::Frustum frustum;
	frustum.Update(camera.GetViewMatrix(), camera.GetProjectionMatrix());

	Engine::AABB visibleBox;
	visibleBox.min = glm::vec3(-0.5f, -0.5f, -5.5f);
	visibleBox.max = glm::vec3(0.5f, 0.5f, -4.5f);
	SWIM_CHECK(frustum.IsAABBVisible(visibleBox));

	Engine::AABB beforeNearPlane;
	beforeNearPlane.min = glm::vec3(-0.01f, -0.01f, -0.05f);
	beforeNearPlane.max = glm::vec3(0.01f, 0.01f, -0.01f);
	SWIM_CHECK(!frustum.IsAABBVisible(beforeNearPlane));

	Engine::AABB behindCamera;
	behindCamera.min = glm::vec3(-0.5f, -0.5f, 4.5f);
	behindCamera.max = glm::vec3(0.5f, 0.5f, 5.5f);
	SWIM_CHECK(!frustum.IsAABBVisible(behindCamera));
}
