#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Engine
{

	constexpr float TWO_PI = 2.0f * M_PI;
	constexpr float HALF_PI = 0.5f * M_PI;
	constexpr float QUARTER_PI = 0.25f * M_PI;
	constexpr float EIGHTH_PI = 0.125f * M_PI;
	constexpr float SIXTEENTH_PI = 0.0625f * M_PI;

	constexpr float DEG_TO_RAD = M_PI / 180.0f;
	constexpr float RAD_TO_DEG = 180.0f / M_PI;

	constexpr int NUM_STEPS_PI = 36;
	constexpr int TWO_NUM_STEPS_PI = 2 * NUM_STEPS_PI;
	constexpr float ONE_STEP = M_PI / NUM_STEPS_PI;

	constexpr float EPSILON = 0.00001f;

	typedef std::pair<std::vector<Engine::Vertex>, std::vector<uint32_t>> VertexesIndexesPair;

	VertexesIndexesPair MakeCube();

	VertexesIndexesPair MakeRandomColoredCube();

	VertexesIndexesPair MakeSphere
	(
		int latitudeSegments,
		int longitudeSegments,
		glm::vec3 colorTop,
		glm::vec3 colorMid,
		glm::vec3 colorBottom
	);

	VertexesIndexesPair GenerateCircleMesh
	(
		float radius = 0.5f,
		uint32_t segmentCount = 64,
		const glm::vec3& color = { 1.0f, 1.0f, 1.0f }
	);

	// Cylinder with top and bottom caps
	VertexesIndexesPair MakeCylinder
	(
		float radius = 0.5f,
		float height = 1.0f,
		uint32_t segmentCount = 64,
		const glm::vec3& color = { 1.0f, 1.0f, 1.0f }
	);

	// Cone with base cap
	VertexesIndexesPair MakeCone
	(
		float radius = 0.5f,
		float height = 1.0f,
		uint32_t segmentCount = 64,
		const glm::vec3& color = { 1.0f, 1.0f, 1.0f }
	);

	// Torus (ring / donut)
	// outerRadius = main ring radius
	// thickness = tube radius
	VertexesIndexesPair MakeTorus
	(
		float outerRadius = 0.35f,
		float thickness = 0.15f,
		uint32_t majorSegments = 48,
		uint32_t minorSegments = 24,
		const glm::vec3& color = { 1.0f, 1.0f, 1.0f }
	);

}
