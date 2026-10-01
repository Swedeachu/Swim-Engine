#pragma once

#include "Vertex.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Engine
{

	// Transitional CPU-only mesh payload used by the legacy renderer. Backend
	// buffer residency is owned separately by the renderer residency pool.
	struct Mesh
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		Mesh() = default;

		Mesh(std::vector<Vertex> v, std::vector<uint32_t> i)
			: vertices(std::move(v)), indices(std::move(i))
		{}
	};

}
