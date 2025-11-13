#pragma once

#include "Library/glm/vec2.hpp"
#include "Library/glm/vec4.hpp"

namespace Engine
{

	/*
	 Important notes about this component:

	 1. When fill color is set to {-1, -1, -1, -1), the shader will render using the mesh material color at that fragment instead. 
			This is how UI gradients from mesh vertex color sampling can be done. This will not happen if enable fill is set to false.
			Setting Fill color to have an alpha of 0 will make enable fill get set to false.
			CRUCIAL: to get mesh material sampling for gradients to work, do not manually set fill color to all -1,
			instead use SetUseMeshMaterialColor(bool) to keep things in sync due to this component caching what fill was.

	2. If use material texture is enabled and the material has a texture, the shader will use the texture for drawing instead of fill color.
		 This will happen as long as enableFill is true.

	3. These properties will effect text rendering the exact same.
	*/
	struct MeshDecorator
	{
		glm::vec4 fillColor = glm::vec4(1.0f);    // Default: solid white
		glm::vec4 strokeColor = glm::vec4(0.0f);  // Default: no stroke
		glm::vec2 strokeWidth = glm::vec2(0.0f);  // in pixels, width/height
		glm::vec2 cornerRadius = glm::vec2(0.0f); // in pixels, X/Y radius
		glm::vec2 padding = glm::vec2(0.0f);			// optional layout padding (pretty much unused in the shader currently)

		bool roundCorners = false;
		bool enableStroke = false;
		bool enableFill = true;
		bool useMaterialTexture = false; // if enabled, will use the material texture instead of fill color

		// This works like a force layer:
		// If 0, uses the regular depth buffer, but at like something at 10 renders below 9 etc
		// The prime example of this is gizmos that we want drawing above things but still in 3D world space, but avoiding Z fighting on each other,
		// or things we need to draw through walls such as billboard indicators in the world.
		int renderOnTop = 0; // 0 = normal depth, x >= 1 = force in front

		// Convenient initialization
		MeshDecorator(
			glm::vec4 fill = glm::vec4(1.0f),
			glm::vec4 stroke = glm::vec4(0.0f),
			glm::vec2 strokeW = glm::vec2(0.0f),
			glm::vec2 cornerR = glm::vec2(0.0f),
			glm::vec2 pad = glm::vec2(0.0f),
			bool rounded = false,
			bool strokeEnabled = false,
			bool fillEnabled = true,
			bool useTexture = false,
			int renderOnTop = 0
		)
			: fillColor(fill),
			strokeColor(stroke),
			strokeWidth(strokeW),
			cornerRadius(cornerR),
			padding(pad),
			roundCorners(rounded),
			enableStroke(strokeEnabled),
			enableFill(fillEnabled),
			useMaterialTexture(useTexture),
			cachedFill(fill),
			renderOnTop(renderOnTop)
		{}

		void SetColors(glm::vec4 fill, glm::vec4 stroke = glm::vec4(0.0f))
		{
			fillColor = fill;
			strokeColor = stroke;
			cachedFill = fill;
		}

		void SetCornerRadius(glm::vec2 radius)
		{
			cornerRadius = radius;
		}

		void SetUseMeshMaterialColor(bool value)
		{
			if (value)
			{
				cachedFill = fillColor;
				fillColor = glm::vec4(-1.0f);
			}
			else
			{
				fillColor = cachedFill;
			}
		}

	private:

		glm::vec4 cachedFill = glm::vec4(1.0f);

	};

}
