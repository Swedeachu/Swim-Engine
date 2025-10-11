#pragma once

#include "Engine/Components/TextComponent.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Renderer/Vulkan/Buffers/VulkanGpuInstanceData.h"

namespace Engine
{

	struct GlyphQuad
	{
		glm::vec4 plane; // l,b,r,t in EM space
		glm::vec4 uv;    // u0,v0,u1,v1
	};

	namespace TextLayout
	{

		template<typename Emit>
		inline void BuildLineQuads
		(
			const std::u32string& line,
			const FontInfo& fi,
			float xStartEm,
			float yBaseEm,
			Emit&& emit
		)
		{
			float penX = xStartEm;
			float penY = yBaseEm;

			for (size_t i = 0; i < line.size(); ++i)
			{
				auto itG = fi.glyphs.find(line[i]);
				if (itG == fi.glyphs.end()) continue;
				const Glyph& g = itG->second;

				const float l = penX + g.plane.left;
				const float b = penY + g.plane.bottom;
				const float r = penX + g.plane.right;
				const float t = penY + g.plane.top;

				emit(GlyphQuad{ {l,b,r,t}, {g.uv.left, g.uv.bottom, g.uv.right, g.uv.top} });

				penX += g.advance;
				if (i + 1 < line.size()) penX += fi.GetKerning(line[i], line[i + 1]);
			}
		}

		template<typename Emit>
		inline void ForEachGlyphQuad
		(
			TextComponent& tc,
			const FontInfo& fi,
			Emit&& emit
		)
		{
			const std::vector<std::u32string>& lines = tc.GetLines();
			const std::vector<float>& widths = tc.GetLineWidths();

			for (size_t i = 0; i < lines.size(); ++i)
			{
				float x0 = 0.0f;
				switch (tc.GetAlignment())
				{
					case TextAllignemt::Left:    x0 = 0.0f;              break;
					case TextAllignemt::Center:  x0 = -0.5f * widths[i]; break;
					case TextAllignemt::Right:   x0 = -widths[i];        break;
					case TextAllignemt::Justified:
					default:                     x0 = 0.0f;              break;
				}
				const float y0 = -static_cast<float>(i) * fi.lineHeight;

				BuildLineQuads(lines[i], fi, x0, y0, [&](const GlyphQuad& q)
				{
					emit(static_cast<uint32_t>(i), q);
				});
			}
		}

	} // namespace TextLayout (fully internal)

	inline MsdfTextGpuInstanceData BuildMsdfStateWorld
	(
		const entt::registry& registry,
		const Transform& tf,
		const TextComponent& tc,
		const FontInfo& fi,
		uint32_t atlasIndex
	)
	{
		MsdfTextGpuInstanceData s{};
		s.modelTR = Transform::MakeWorldTR(tf, registry);
		s.pxToModel = glm::vec2(1.0f, 1.0f);
		s.emScalePx = (tf.GetScale().y > 0.0f) ? tf.GetScale().y : 0.1f;
		s.msdfPixelRange = fi.distanceRange;
		s.fillColor = tc.fillColor;
		s.strokeColor = tc.strokeColor;
		s.strokeWidthPx = tc.strokeWidth;
		s.atlasTexIndex = atlasIndex;
		s.space = static_cast<int>(TransformSpace::World);
		return s;
	}

	inline MsdfTextGpuInstanceData BuildMsdfStateScreen
	(
		const entt::registry& registry,
		const Transform& tf,
		const TextComponent& tc,
		const FontInfo& fi,
		unsigned int windowWidth,
		unsigned int windowHeight,
		unsigned int virtualCanvasWidth,
		unsigned int virtualCanvasHeight,
		uint32_t atlasIndex
	)
	{
		const glm::vec2 screenScale(
			static_cast<float>(windowWidth) / virtualCanvasWidth,
			static_cast<float>(windowHeight) / virtualCanvasHeight
		);

		MsdfTextGpuInstanceData s{};
		s.modelTR = Transform::MakeWorldTR(tf, registry);
		s.pxToModel = 1.0f / screenScale;
		s.emScalePx = std::max(1.0f, tf.GetScale().y * screenScale.y);
		s.msdfPixelRange = fi.distanceRange;
		s.fillColor = tc.fillColor;
		s.strokeColor = tc.strokeColor;
		s.strokeWidthPx = tc.strokeWidth;
		s.atlasTexIndex = atlasIndex;
		s.space = static_cast<int>(TransformSpace::Screen);
		return s;
	}

	template<typename Sink>
	inline void EmitMsdf
	(
		TextComponent& tc,
		const FontInfo& fi,
		const MsdfTextGpuInstanceData& state,
		Sink&& sink
	)
	{
		TextLayout::ForEachGlyphQuad(tc, fi, [&](uint32_t lineIdx, const GlyphQuad& q)
		{
			sink(lineIdx, q, state);
		});
	}

} // namespace Engine
