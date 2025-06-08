#pragma once

#include "Library/glm/vec4.hpp"

namespace Engine
{

	// Optional enum if we later want per-corner rounding
	enum class Corner
	{
		TopLeft,
		TopRight,
		BottomRight,
		BottomLeft
	};

	struct DecoratorUI
	{
		glm::vec4 fillColor = glm::vec4(1.0f);       // Default: solid white
		glm::vec4 strokeColor = glm::vec4(0.0f);     // Default: no stroke
		float     strokeWidth = 0.0f;                // 0 = no stroke
		float     cornerRadius = 0.0f;               // uniform rounded corners
		glm::vec2 padding = glm::vec2(0.0f);         // padding (for future layout)

		// Optional: Future support for per-corner radius
		// glm::vec4 cornerRadii = glm::vec4(0.0f); // TL, TR, BR, BL

		// Flags for rendering hints (optional, future use)
		bool roundCorners = false;                  // use cornerRadius
		bool enableStroke = false;
		bool enableFill = true;

		// Debug/utility functions
		void SetColors(glm::vec4 fill, glm::vec4 stroke = glm::vec4(0.0f))
		{
			fillColor = fill;
			strokeColor = stroke;
			enableFill = fill.a > 0.0f;
			enableStroke = stroke.a > 0.0f && strokeWidth > 0.0f;
		}

		void SetCornerRadius(float radius)
		{
			cornerRadius = radius;
			roundCorners = (radius > 0.0f);
		}

	};

}
