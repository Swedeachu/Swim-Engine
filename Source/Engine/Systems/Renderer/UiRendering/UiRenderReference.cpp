#include "Engine/Systems/Renderer/UiRendering/UiRenderReference.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render
{
	void ValidateUiCompositionSettings(const UiCompositionSettings& settings)
	{
		const bool valid = static_cast<std::uint32_t>(settings.Encoding) <= static_cast<std::uint32_t>(UiOutputEncoding::ScRgb) &&
			std::isfinite(settings.PaperWhiteNits) && settings.PaperWhiteNits > 0.0f && settings.PaperWhiteNits <= 10000.0f &&
			std::isfinite(settings.LinearScale) && settings.LinearScale >= 0.0f && settings.LinearScale <= 65504.0f;
		if (!valid)
		{
			throw std::invalid_argument("Invalid UI composition settings");
		}
	}
} // namespace Swim::Render

namespace Swim::Render::Ui
{
	namespace
	{
		float Saturate(float value)
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		Float4 Scale(const Float4& color, float factor)
		{
			return { color[0] * factor, color[1] * factor, color[2] * factor, color[3] * factor };
		}
	} // namespace

	std::vector<GpuUiQuad> BuildQuads(std::span<const UI::UiPaintQuad> paint, const QuadBuildDesc& desc, QuadBuildStats* stats)
	{
		if (!std::isfinite(desc.DpiScale) || desc.DpiScale <= 0.0f || !std::isfinite(desc.OffsetX) || !std::isfinite(desc.OffsetY))
		{
			throw std::invalid_argument("UI quad build needs a positive DPI scale and a finite offset");
		}
		QuadBuildStats local;
		std::vector<GpuUiQuad> quads;
		quads.reserve(paint.size());
		const float s = desc.DpiScale;
		for (const auto& source : paint)
		{
			GpuUiQuad quad;
			const auto& b = source.Bounds;
			const auto& c = source.Clip;
			quad.Rect[0] = b.X * s + desc.OffsetX;
			quad.Rect[1] = b.Y * s + desc.OffsetY;
			quad.Rect[2] = (b.X + b.Width) * s + desc.OffsetX;
			quad.Rect[3] = (b.Y + b.Height) * s + desc.OffsetY;
			quad.Clip[0] = c.X * s + desc.OffsetX;
			quad.Clip[1] = c.Y * s + desc.OffsetY;
			quad.Clip[2] = (c.X + c.Width) * s + desc.OffsetX;
			quad.Clip[3] = (c.Y + c.Height) * s + desc.OffsetY;
			const bool empty = std::max(quad.Rect[0], quad.Clip[0]) >= std::min(quad.Rect[2], quad.Clip[2]) ||
				std::max(quad.Rect[1], quad.Clip[1]) >= std::min(quad.Rect[3], quad.Clip[3]);
			if (empty)
			{
				++local.Culled;
				continue;
			}
			quad.Uv[0] = source.Uv.X;
			quad.Uv[1] = source.Uv.Y;
			quad.Uv[2] = source.Uv.X + source.Uv.Width;
			quad.Uv[3] = source.Uv.Y + source.Uv.Height;
			quad.Color[0] = source.Color.R;
			quad.Color[1] = source.Color.G;
			quad.Color[2] = source.Color.B;
			quad.Color[3] = source.Color.A;
			switch (source.Kind)
			{
			case UI::UiPaintKind::Glyph:
			{
				if (source.AtlasPage >= desc.AtlasTextures.size() || desc.AtlasPageSize == 0 || source.Uv.Width <= 0.0f)
				{
					++local.Culled;
					continue;
				}
				quad.Kind = UiQuadGlyph;
				quad.Texture = desc.AtlasTextures[source.AtlasPage];
				quad.Sampler = desc.AtlasSampler;
				const float texels = source.Uv.Width * float(desc.AtlasPageSize);
				quad.PixelRange = std::max(1.0f, source.DistanceRange * (b.Width * s) / texels);
				quad.UnitRange = source.DistanceRange / float(desc.AtlasPageSize);
				++local.Glyphs;
				break;
			}
			case UI::UiPaintKind::Image:
				quad.Kind = UiQuadImage;
				quad.Texture = source.Texture;
				quad.Sampler = source.Sampler;
				++local.Images;
				break;
			default:
				quad.Kind = UiQuadSolid;
				quad.Radius = source.CornerRadius * s;
				quad.Border = source.BorderWidth * s;
				quad.BorderColor[0] = source.BorderColor.R;
				quad.BorderColor[1] = source.BorderColor.G;
				quad.BorderColor[2] = source.BorderColor.B;
				quad.BorderColor[3] = source.BorderColor.A;
				++local.Solids;
				break;
			}
			quads.push_back(quad);
		}
		if (stats)
		{
			*stats = local;
		}
		return quads;
	}

	GpuUiDrawConstants BuildDrawConstants(std::uint32_t width, std::uint32_t height, const UiCompositionSettings& settings, float opacity)
	{
		const float w = float(std::max(width, 1u));
		const float h = float(std::max(height, 1u));
		// Clip space is +Y up (the Vulkan backend flips the viewport); pixels are +Y down.
		const std::array<float, 16> screen{ 2.0f / w, 0, 0, -1.0f, 0, -2.0f / h, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1 };
		auto constants = BuildCanvasDrawConstants(width, height, settings, screen, opacity);
		constants.Flags = 0;
		return constants;
	}

	GpuUiDrawConstants BuildCanvasDrawConstants(std::uint32_t width, std::uint32_t height, const UiCompositionSettings& settings,
		const std::array<float, 16>& clipFromCanvas, float opacity)
	{
		ValidateUiCompositionSettings(settings);
		if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f ||
			!std::all_of(clipFromCanvas.begin(), clipFromCanvas.end(),
				[](float v)
				{
					return std::isfinite(v);
				}))
		{
			throw std::invalid_argument("UI draw constants need a finite canvas matrix and an opacity in [0, 1]");
		}
		GpuUiDrawConstants constants;
		std::copy(clipFromCanvas.begin(), clipFromCanvas.end(), constants.ClipFromCanvas);
		constants.Flags = UiDrawWorld;
		constants.Opacity = opacity;
		constants.TargetSize[0] = float(width);
		constants.TargetSize[1] = float(height);
		constants.Encoding = static_cast<std::uint32_t>(settings.Encoding);
		switch (settings.Encoding)
		{
		case UiOutputEncoding::Linear:
			constants.WhiteScale = settings.LinearScale;
			break;
		case UiOutputEncoding::ScRgb:
			constants.WhiteScale = settings.PaperWhiteNits / 80.0f;
			break;
		case UiOutputEncoding::Hdr10:
			constants.WhiteScale = settings.PaperWhiteNits;
			break;
		default:
			constants.WhiteScale = 1.0f;
			break;
		}
		return constants;
	}

	float SrgbOetf(float linear)
	{
		return linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
	}

	float PqOetf(float nits)
	{
		constexpr float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
		const float y = std::pow(Saturate(nits / 10000.0f), m1);
		return std::pow((c1 + c2 * y) / (1.0f + c3 * y), m2);
	}

	std::array<float, 3> Rec709ToRec2020(const std::array<float, 3>& c)
	{
		return { 0.6274040f * c[0] + 0.3292820f * c[1] + 0.0433136f * c[2], 0.0690970f * c[0] + 0.9195400f * c[1] + 0.0113612f * c[2],
			0.0163916f * c[0] + 0.0880132f * c[1] + 0.8955950f * c[2] };
	}

	float Median(float r, float g, float b)
	{
		return std::max(std::min(r, g), std::min(std::max(r, g), b));
	}

	float RoundedBoxDistance(float px, float py, float centerX, float centerY, float halfX, float halfY, float radius)
	{
		const float qx = std::abs(px - centerX) - (halfX - radius);
		const float qy = std::abs(py - centerY) - (halfY - radius);
		const float ox = std::max(qx, 0.0f);
		const float oy = std::max(qy, 0.0f);
		return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
	}

	Float4 ShadeQuad(const GpuUiQuad& q, float px, float py, const TextureSampler& sample, const Footprint* footprint)
	{
		const float x0 = std::max(q.Rect[0], q.Clip[0]);
		const float y0 = std::max(q.Rect[1], q.Clip[1]);
		const float x1 = std::min(q.Rect[2], q.Clip[2]);
		const float y1 = std::min(q.Rect[3], q.Clip[3]);
		if (!(px >= x0 && px < x1 && py >= y0 && py < y1))
		{
			return {};
		}
		const Float4 color{ q.Color[0], q.Color[1], q.Color[2], q.Color[3] };
		const float width = std::max(q.Rect[2] - q.Rect[0], 1e-6f);
		const float height = std::max(q.Rect[3] - q.Rect[1], 1e-6f);
		const float u = q.Uv[0] + (px - q.Rect[0]) / width * (q.Uv[2] - q.Uv[0]);
		const float v = q.Uv[1] + (py - q.Rect[1]) / height * (q.Uv[3] - q.Uv[1]);
		if (q.Kind == UiQuadGlyph)
		{
			const auto msd = sample(q.Texture, q.Sampler, u, v);
			const float range = footprint
				? std::max(0.5f * (q.UnitRange / std::max(footprint->U, 1e-9f) + q.UnitRange / std::max(footprint->V, 1e-9f)), 1.0f)
				: q.PixelRange;
			const float coverage = Saturate(range * (Median(msd[0], msd[1], msd[2]) - 0.5f) + 0.5f);
			return Scale(color, coverage);
		}
		if (q.Kind == UiQuadImage)
		{
			const auto texel = sample(q.Texture, q.Sampler, u, v);
			return { texel[0] * color[0], texel[1] * color[1], texel[2] * color[2], texel[3] * color[3] };
		}
		if (q.Radius <= 0.0f && q.Border <= 0.0f)
		{
			return color;
		}
		const float cx = (q.Rect[0] + q.Rect[2]) * 0.5f;
		const float cy = (q.Rect[1] + q.Rect[3]) * 0.5f;
		const float hx = width * 0.5f;
		const float hy = height * 0.5f;
		const float radius = std::min(q.Radius, std::min(hx, hy));
		const float ramp = footprint ? std::max(0.5f * (footprint->CanvasX + footprint->CanvasY), 1e-6f) : 1.0f;
		const float outer = Saturate(0.5f - RoundedBoxDistance(px, py, cx, cy, hx, hy, radius) / ramp);
		if (q.Border <= 0.0f)
		{
			return Scale(color, outer);
		}
		const float ix = std::max(hx - q.Border, 0.0f);
		const float iy = std::max(hy - q.Border, 0.0f);
		const float fill = ix > 0.0f && iy > 0.0f
			? std::min(outer, Saturate(0.5f - RoundedBoxDistance(px, py, cx, cy, ix, iy, std::max(radius - q.Border, 0.0f)) / ramp))
			: 0.0f;
		const Float4 border{ q.BorderColor[0], q.BorderColor[1], q.BorderColor[2], q.BorderColor[3] };
		Float4 result{};
		for (int c = 0; c < 4; ++c)
		{
			result[c] = border[c] * (outer - fill) + color[c] * fill;
		}
		return result;
	}

	Float4 Encode(const Float4& color, const GpuUiDrawConstants& constants)
	{
		const Float4 p = Scale(color, constants.Opacity);
		const auto encoding = static_cast<UiOutputEncoding>(constants.Encoding);
		if (encoding == UiOutputEncoding::Linear || encoding == UiOutputEncoding::ScRgb)
		{
			return { p[0] * constants.WhiteScale, p[1] * constants.WhiteScale, p[2] * constants.WhiteScale, p[3] };
		}
		if (p[3] <= 0.0f)
		{
			return {};
		}
		const std::array<float, 3> straight{ p[0] / p[3], p[1] / p[3], p[2] / p[3] };
		std::array<float, 3> encoded{};
		if (encoding == UiOutputEncoding::Srgb)
		{
			for (int c = 0; c < 3; ++c)
			{
				encoded[c] = SrgbOetf(Saturate(straight[c]));
			}
		}
		else
		{
			const auto wide = Rec709ToRec2020({ std::max(straight[0], 0.0f), std::max(straight[1], 0.0f), std::max(straight[2], 0.0f) });
			for (int c = 0; c < 3; ++c)
			{
				encoded[c] = PqOetf(wide[c] * constants.WhiteScale);
			}
		}
		return { encoded[0] * p[3], encoded[1] * p[3], encoded[2] * p[3], p[3] };
	}

	Float4 Blend(const Float4& destination, const Float4& source)
	{
		const float keep = 1.0f - source[3];
		return { source[0] + destination[0] * keep, source[1] + destination[1] * keep, source[2] + destination[2] * keep,
			source[3] + destination[3] * keep };
	}

	void Rasterize(Canvas& canvas, std::span<const GpuUiQuad> quads, const GpuUiDrawConstants& constants, const TextureSampler& sample)
	{
		for (const auto& quad : quads)
		{
			const float x0 = std::max(quad.Rect[0], quad.Clip[0]);
			const float y0 = std::max(quad.Rect[1], quad.Clip[1]);
			const float x1 = std::min(quad.Rect[2], quad.Clip[2]);
			const float y1 = std::min(quad.Rect[3], quad.Clip[3]);
			if (x0 >= x1 || y0 >= y1)
			{
				continue;
			}
			// Pixel centres x + 0.5 in [x0, x1).
			const int first = std::max(0, int(std::ceil(x0 - 0.5f)));
			const int last = std::min(int(canvas.Width), int(std::ceil(x1 - 0.5f)));
			const int top = std::max(0, int(std::ceil(y0 - 0.5f)));
			const int bottom = std::min(int(canvas.Height), int(std::ceil(y1 - 0.5f)));
			for (int y = top; y < bottom; ++y)
			{
				for (int x = first; x < last; ++x)
				{
					const auto shaded = ShadeQuad(quad, float(x) + 0.5f, float(y) + 0.5f, sample);
					auto& texel = canvas.Texels[std::size_t(y) * canvas.Width + std::size_t(x)];
					texel = Blend(texel, Encode(shaded, constants));
				}
			}
		}
	}

	std::optional<CanvasSample> CanvasAt(const GpuUiDrawConstants& constants, float px, float py)
	{
		const auto& m = constants.ClipFromCanvas;
		const float width = std::max(constants.TargetSize[0], 1.0f);
		const float height = std::max(constants.TargetSize[1], 1.0f);
		const auto point = [&](float x, float y) -> std::optional<std::array<float, 2>>
		{
			const float nx = 2.0f * x / width - 1.0f;
			const float ny = 1.0f - 2.0f * y / height;
			// (row0 - nx row3) . (cx, cy, 0, 1) = 0, (row1 - ny row3) . (cx, cy, 0, 1) = 0.
			const float a0 = m[0] - nx * m[12], b0 = m[1] - nx * m[13], c0 = m[3] - nx * m[15];
			const float a1 = m[4] - ny * m[12], b1 = m[5] - ny * m[13], c1 = m[7] - ny * m[15];
			const float det = a0 * b1 - a1 * b0;
			if (!(std::abs(det) > 1e-20f))
			{
				return std::nullopt;
			}
			const float cx = (-c0 * b1 + c1 * b0) / det;
			const float cy = (-a0 * c1 + a1 * c0) / det;
			const float w = m[12] * cx + m[13] * cy + m[15];
			if (!(w > 0.0f) || !std::isfinite(cx) || !std::isfinite(cy))
			{
				return std::nullopt;
			}
			return std::array<float, 2>{ cx, cy };
		};
		const auto centre = point(px, py);
		const auto left = point(px - 0.5f, py);
		const auto right = point(px + 0.5f, py);
		const auto up = point(px, py - 0.5f);
		const auto down = point(px, py + 0.5f);
		if (!centre || !left || !right || !up || !down)
		{
			return std::nullopt;
		}
		CanvasSample sample;
		sample.X = (*centre)[0];
		sample.Y = (*centre)[1];
		sample.FootprintX = std::abs((*right)[0] - (*left)[0]) + std::abs((*down)[0] - (*up)[0]);
		sample.FootprintY = std::abs((*right)[1] - (*left)[1]) + std::abs((*down)[1] - (*up)[1]);
		return sample;
	}

	void RasterizeProjected(
		Canvas& canvas, std::span<const GpuUiQuad> quads, const GpuUiDrawConstants& constants, const TextureSampler& sample)
	{
		// Canvas point and footprint per target pixel, shared by every quad.
		std::vector<std::optional<CanvasSample>> samples(std::size_t(canvas.Width) * canvas.Height);
		for (std::uint32_t y = 0; y < canvas.Height; ++y)
		{
			for (std::uint32_t x = 0; x < canvas.Width; ++x)
			{
				samples[std::size_t(y) * canvas.Width + x] = CanvasAt(constants, float(x) + 0.5f, float(y) + 0.5f);
			}
		}
		for (const auto& quad : quads)
		{
			const float x0 = std::max(quad.Rect[0], quad.Clip[0]);
			const float y0 = std::max(quad.Rect[1], quad.Clip[1]);
			const float x1 = std::min(quad.Rect[2], quad.Clip[2]);
			const float y1 = std::min(quad.Rect[3], quad.Clip[3]);
			if (x0 >= x1 || y0 >= y1)
			{
				continue;
			}
			const float du = (quad.Uv[2] - quad.Uv[0]) / std::max(quad.Rect[2] - quad.Rect[0], 1e-6f);
			const float dv = (quad.Uv[3] - quad.Uv[1]) / std::max(quad.Rect[3] - quad.Rect[1], 1e-6f);
			for (std::size_t i = 0; i < samples.size(); ++i)
			{
				const auto& s = samples[i];
				if (!s || !(s->X >= x0 && s->X < x1 && s->Y >= y0 && s->Y < y1))
				{
					continue;
				}
				Footprint footprint;
				footprint.CanvasX = s->FootprintX;
				footprint.CanvasY = s->FootprintY;
				footprint.U = std::abs(du) * s->FootprintX;
				footprint.V = std::abs(dv) * s->FootprintY;
				const auto shaded = ShadeQuad(quad, s->X, s->Y, sample, &footprint);
				canvas.Texels[i] = Blend(canvas.Texels[i], Encode(shaded, constants));
			}
		}
	}

	Float4 SampleBilinear(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, float u, float v)
	{
		const float fx = u * float(width) - 0.5f;
		const float fy = v * float(height) - 0.5f;
		const int x0 = int(std::floor(fx));
		const int y0 = int(std::floor(fy));
		const float tx = fx - float(x0);
		const float ty = fy - float(y0);
		const auto texel = [&](int x, int y)
		{
			x = std::clamp(x, 0, int(width) - 1);
			y = std::clamp(y, 0, int(height) - 1);
			const std::size_t index = (std::size_t(y) * width + std::size_t(x)) * 4;
			return Float4{ rgba[index] / 255.0f, rgba[index + 1] / 255.0f, rgba[index + 2] / 255.0f, rgba[index + 3] / 255.0f };
		};
		const auto a = texel(x0, y0);
		const auto b = texel(x0 + 1, y0);
		const auto c = texel(x0, y0 + 1);
		const auto d = texel(x0 + 1, y0 + 1);
		Float4 result{};
		for (int k = 0; k < 4; ++k)
		{
			result[k] = (a[k] * (1.0f - tx) + b[k] * tx) * (1.0f - ty) + (c[k] * (1.0f - tx) + d[k] * tx) * ty;
		}
		return result;
	}
} // namespace Swim::Render::Ui
