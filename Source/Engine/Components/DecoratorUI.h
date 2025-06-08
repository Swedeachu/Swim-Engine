#pragma once

#include "Library/glm/vec2.hpp"
#include "Library/glm/vec4.hpp"

namespace Engine
{

	// Optional enum for future per-corner rounding (not yet used)
	enum class Corner
	{
		TopLeft,
		TopRight,
		BottomRight,
		BottomLeft
	};

	struct DecoratorUI
	{
		glm::vec4 fillColor = glm::vec4(1.0f);   // Default: solid white
		glm::vec4 strokeColor = glm::vec4(0.0f);   // Default: no stroke
		glm::vec2 strokeWidth = glm::vec2(0.0f);   // in pixels, width/height
		glm::vec2 cornerRadius = glm::vec2(0.0f);   // in pixels, X/Y radius
		glm::vec2 padding = glm::vec2(0.0f);   // optional layout padding

		bool roundCorners = false;
		bool enableStroke = false;
		bool enableFill = true;

		// Convenient initialization
		DecoratorUI(
			glm::vec4 fill = glm::vec4(1.0f),
			glm::vec4 stroke = glm::vec4(0.0f),
			glm::vec2 strokeW = glm::vec2(0.0f),
			glm::vec2 cornerR = glm::vec2(0.0f),
			glm::vec2 pad = glm::vec2(0.0f),
			bool rounded = false,
			bool strokeEnabled = false,
			bool fillEnabled = true
		)
			: fillColor(fill),
			strokeColor(stroke),
			strokeWidth(strokeW),
			cornerRadius(cornerR),
			padding(pad),
			roundCorners(rounded),
			enableStroke(strokeEnabled),
			enableFill(fillEnabled)
		{}

		void SetColors(glm::vec4 fill, glm::vec4 stroke = glm::vec4(0.0f))
		{
			fillColor = fill;
			strokeColor = stroke;
			enableFill = fill.a > 0.0f;
			enableStroke = stroke.a > 0.0f && (strokeWidth.x > 0.0f || strokeWidth.y > 0.0f);
		}

		void SetCornerRadius(glm::vec2 radius)
		{
			cornerRadius = radius;
			roundCorners = (radius.x > 0.0f || radius.y > 0.0f);
		}

	};

}
