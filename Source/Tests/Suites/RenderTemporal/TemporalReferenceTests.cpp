#include "Engine/Systems/Renderer/Temporal/TemporalReference.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

using namespace Swim;
using namespace Swim::Render;
namespace Ta = Swim::Render::Temporal;

namespace
{
	bool Near(float a, float b, float tolerance = 1.0e-6f)
	{
		return std::abs(a - b) <= tolerance;
	}

	// A frame of a 1D-in-x scene: color(x) at every row, constant depth and velocity.
	struct Frame
	{
		Ta::ColorImage Color;
		Ta::DepthImage Depth;
		Ta::VelocityImage Velocity;

		Frame(std::uint32_t width, std::uint32_t height) : Color(width, height), Depth(width, height), Velocity(width, height)
		{
			std::fill(Depth.Texels.begin(), Depth.Texels.end(), 0.1f);
		}
	};

	float Mean(const std::vector<float>& values)
	{
		float sum = 0.0f;
		for (const float v : values)
		{
			sum += v;
		}
		return sum / float(values.size());
	}

	float Deviation(const std::vector<float>& values)
	{
		const float mean = Mean(values);
		float sum = 0.0f;
		for (const float v : values)
		{
			sum += (v - mean) * (v - mean);
		}
		return std::sqrt(sum / float(values.size()));
	}
} // namespace

SWIM_TEST("Render.Temporal.Reference", "HaltonJitterCoversThePixelAndRepeats")
{
	SWIM_CHECK(Ta::Halton(1, 2) == 0.5f && Ta::Halton(2, 2) == 0.25f && Ta::Halton(3, 2) == 0.75f && Ta::Halton(4, 2) == 0.125f);
	SWIM_CHECK(Near(Ta::Halton(1, 3), 1.0f / 3.0f) && Near(Ta::Halton(2, 3), 2.0f / 3.0f) && Near(Ta::Halton(3, 3), 1.0f / 9.0f));
	SWIM_CHECK_THROWS(Ta::Halton(0, 2), std::invalid_argument);
	SWIM_CHECK_THROWS(Ta::Halton(1, 1), std::invalid_argument);

	// Frame 0 is Halton index 1; the sequence repeats every `phases` frames.
	const auto first = Ta::JitterPixels(0, 8);
	SWIM_CHECK(first[0] == 0.0f && Near(first[1], 1.0f / 3.0f - 0.5f));
	float sumX = 0.0f, sumY = 0.0f;
	for (std::uint64_t frame = 0; frame < 8; ++frame)
	{
		const auto jitter = Ta::JitterPixels(frame, 8);
		SWIM_CHECK(jitter[0] > -0.5f && jitter[0] < 0.5f && jitter[1] > -0.5f && jitter[1] < 0.5f);
		SWIM_CHECK(jitter == Ta::JitterPixels(frame + 8, 8));
		sumX += jitter[0];
		sumY += jitter[1];
	}
	SWIM_CHECK(std::abs(sumX / 8.0f) < 0.07f && std::abs(sumY / 8.0f) < 0.07f); // Centred on the pixel.
	// Eight phases cover each quarter of the pixel.
	int quadrants[4]{};
	for (std::uint64_t frame = 0; frame < 8; ++frame)
	{
		const auto jitter = Ta::JitterPixels(frame, 8);
		++quadrants[(jitter[0] >= 0.0f ? 1 : 0) + (jitter[1] >= 0.0f ? 2 : 0)];
	}
	SWIM_CHECK(quadrants[0] > 0 && quadrants[1] > 0 && quadrants[2] > 0 && quadrants[3] > 0);

	SWIM_CHECK((Ta::JitterPixels(5, 0) == Ta::Float2{ 0.0f, 0.0f }));
	SWIM_CHECK((Ta::JitterPixels(3, 1) == Ta::JitterPixels(0, 1)));
	// NDC: two units per viewport, y up.
	const auto pixels = Ta::JitterPixels(2, 8);
	const auto ndc = Ta::JitterNdc(2, 8, 200, 100);
	SWIM_CHECK(Near(ndc[0], pixels[0] * 2.0f / 200.0f) && Near(ndc[1], -pixels[1] * 2.0f / 100.0f));
	SWIM_CHECK_THROWS(Ta::JitterNdc(0, 8, 0, 100), std::invalid_argument);
}

SWIM_TEST("Render.Temporal.Reference", "ColorSpaceClippingAndSampling")
{
	const Ta::Float3 rgb{ 0.8f, 0.3f, 0.1f };
	const auto back = Ta::YCoCgToRgb(Ta::RgbToYCoCg(rgb));
	SWIM_CHECK(Near(back[0], rgb[0]) && Near(back[1], rgb[1]) && Near(back[2], rgb[2]));
	SWIM_CHECK(Near(Ta::RgbToYCoCg({ 1, 1, 1 })[0], 1.0f) && Ta::RgbToYCoCg({ 1, 1, 1 })[1] == 0.0f);
	SWIM_CHECK(Near(Ta::Luminance({ 1, 1, 1 }), 1.0f));
	const auto clean = Ta::Sanitize({ NAN, -2.0f, INFINITY, 5.0f });
	SWIM_CHECK(clean[0] == 0.0f && clean[1] == 0.0f && clean[2] == 0.0f);

	// Inside: unchanged. Outside: onto the surface, along the line to the centre.
	const Ta::Float3 lo{ 0, 0, 0 }, hi{ 1, 2, 4 };
	SWIM_CHECK((Ta::ClipToBox({ 0.5f, 1.5f, 3.0f }, lo, hi) == Ta::Float3{ 0.5f, 1.5f, 3.0f }));
	const auto clipped = Ta::ClipToBox({ 3.0f, 1.0f, 2.0f }, lo, hi); // Centre (0.5, 1, 2): x offset 2.5 over extent 0.5.
	SWIM_CHECK(Near(clipped[0], 1.0f) && Near(clipped[1], 1.0f) && Near(clipped[2], 2.0f));
	const auto diagonal = Ta::ClipToBox({ -0.5f, 0.0f, 0.0f }, lo, hi); // Offsets (-1, -1, -2): x wins with 2 extents.
	SWIM_CHECK(Near(diagonal[0], 0.0f) && Near(diagonal[1], 0.5f) && Near(diagonal[2], 1.0f));
	const auto flat = Ta::ClipToBox({ 9.0f, 9.0f, 9.0f }, { 1, 1, 1 }, { 1, 1, 1 });
	SWIM_CHECK(Near(flat[0], 1.0f, 1.0e-5f) && Near(flat[2], 1.0f, 1.0e-5f)); // A flat box clips to its point.

	Ta::ColorImage image(4, 2);
	for (std::uint32_t y = 0; y < 2; ++y)
	{
		for (std::uint32_t x = 0; x < 4; ++x)
		{
			image.At(x, y) = { float(x), float(y) * 10.0f, 1.0f, 1.0f };
		}
	}
	const auto centre = Ta::SampleBilinear(image, { 2.5f / 4.0f, 0.5f / 2.0f });
	SWIM_CHECK(centre[0] == 2.0f && centre[1] == 0.0f);
	const auto between = Ta::SampleBilinear(image, { 2.0f / 4.0f, 1.0f / 2.0f });
	SWIM_CHECK(Near(between[0], 1.5f) && Near(between[1], 5.0f) && Near(between[2], 1.0f));
	const auto corner = Ta::SampleBilinear(image, { 0.0f, 0.0f }); // Clamp to edge.
	SWIM_CHECK(corner[0] == 0.0f && corner[1] == 0.0f);
	const auto farCorner = Ta::SampleBilinear(image, { 1.0f, 1.0f });
	SWIM_CHECK(farCorner[0] == 3.0f && farCorner[1] == 10.0f);
}

SWIM_TEST("Render.Temporal.Reference", "NeighborhoodDilatesVelocityToTheNearestTexel")
{
	Frame frame(5, 5);
	for (auto& texel : frame.Color.Texels)
	{
		texel = { 0.2f, 0.2f, 0.2f, 1.0f };
	}
	frame.Color.At(2, 2) = { 1.0f, 0.0f, 0.0f, 1.0f };
	frame.Depth.At(3, 1) = 0.8f; // Nearest in (2, 2)'s neighborhood.
	frame.Velocity.At(3, 1) = { 0.25f, -0.125f };
	frame.Depth.At(1, 3) = 0.8f; // Equally near, later in scan order: loses the tie.
	frame.Velocity.At(1, 3) = { -1.0f, -1.0f };
	const auto n = Ta::Gather(frame.Color, frame.Depth, frame.Velocity, 2, 2, 1.0f);
	SWIM_CHECK((n.Velocity == Ta::Float2{ 0.25f, -0.125f }));
	SWIM_CHECK((n.Center == Ta::Float3{ 1.0f, 0.0f, 0.0f }));
	// The variance box lies within min/max and contains the mean.
	const auto grey = Ta::RgbToYCoCg({ 0.2f, 0.2f, 0.2f });
	const auto red = Ta::RgbToYCoCg({ 1.0f, 0.0f, 0.0f });
	for (int c = 0; c < 3; ++c)
	{
		const float mean = (8.0f * grey[c] + red[c]) / 9.0f;
		SWIM_CHECK(n.Minimum[c] >= std::min(grey[c], red[c]) - 1.0e-6f && n.Maximum[c] <= std::max(grey[c], red[c]) + 1.0e-6f);
		SWIM_CHECK(n.Minimum[c] <= mean + 1.0e-6f && n.Maximum[c] >= mean - 1.0e-6f);
	}
	// Clamped at the corner: the texel counts itself several times.
	const auto corner = Ta::Gather(frame.Color, frame.Depth, frame.Velocity, 0, 0, 1.0f);
	SWIM_CHECK(Near(corner.Minimum[0], grey[0]) && Near(corner.Maximum[0], grey[0]));
}

SWIM_TEST("Render.Temporal.Reference", "StaticEdgeConvergesToItsCoverage")
{
	// A vertical edge at x = 10.3 px: white left, black right. Each jittered frame the
	// pixel at centre c sees the scene at c - jitter, so pixel 10 is white 30% of the time.
	constexpr std::uint32_t width = 24, height = 4;
	const auto render = [&](std::uint64_t index, std::uint32_t phases)
	{
		Frame frame(width, height);
		const auto jitter = Ta::JitterPixels(index, phases);
		for (std::uint32_t y = 0; y < height; ++y)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const float v = float(x) + 0.5f - jitter[0] < 10.3f ? 1.0f : 0.0f;
				frame.Color.At(x, y) = { v, v, v, 1.0f };
			}
		}
		return frame;
	};
	TemporalSettings settings;
	const auto run = [&](std::uint32_t phases, std::vector<float>& raw, std::vector<float>& resolved)
	{
		std::optional<Ta::ColorImage> history;
		for (std::uint64_t index = 0; index < 96; ++index)
		{
			const auto frame = render(index, phases);
			auto output = Ta::Resolve(frame.Color, frame.Depth, frame.Velocity, history ? &*history : nullptr, settings);
			if (index >= 64)
			{
				raw.push_back(frame.Color.At(10, 1)[0]);
				resolved.push_back(output.At(10, 1)[0]);
				SWIM_CHECK(output.At(5, 1)[0] == 1.0f && output.At(15, 1)[0] == 0.0f); // Flat regions are exact.
			}
			history = std::move(output);
		}
	};
	std::vector<float> raw, resolved;
	settings.JitterPhases = 8;
	run(8, raw, resolved);
	// Luminance weighting 1 / (1 + L) favours the dark sample: below the 0.3 coverage, but anti-aliased.
	SWIM_CHECK(Mean(resolved) > 0.1f && Mean(resolved) < 0.35f);
	SWIM_CHECK(Deviation(raw) > 0.4f);		 // The input flickers between black and white...
	SWIM_CHECK(Deviation(resolved) < 0.08f); // ...the output is stable.

	std::vector<float> rawStill, resolvedStill;
	settings.JitterPhases = 0;
	run(0, rawStill, resolvedStill);
	// Without jitter the edge pixel's centre (10.5) is always black: aliased.
	SWIM_CHECK(std::all_of(resolvedStill.begin(), resolvedStill.end(),
		[](float v)
		{
			return v == 0.0f;
		}));
}

SWIM_TEST("Render.Temporal.Reference", "MotionVectorsAndClippingPreventGhosting")
{
	// A bright 6-pixel square moves 3 pixels right per frame over a dark background.
	constexpr std::uint32_t width = 64, height = 16;
	const auto render = [&](int left, float speed)
	{
		Frame frame(width, height);
		for (std::uint32_t y = 0; y < height; ++y)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const bool inside = int(x) >= left && int(x) < left + 6 && y >= 5 && y < 11;
				frame.Color.At(x, y) = inside ? Ta::Float4{ 4.0f, 3.0f, 1.0f, 1.0f } : Ta::Float4{ 0.05f, 0.05f, 0.08f, 1.0f };
				frame.Depth.At(x, y) = inside ? 0.6f : 0.1f;
				frame.Velocity.At(x, y) = inside ? Ta::Float2{ speed / float(width), 0.0f } : Ta::Float2{ 0.0f, 0.0f };
			}
		}
		return frame;
	};
	const auto run = [&](const TemporalSettings& settings, bool motionVectors, float& trail, float& inside)
	{
		std::optional<Ta::ColorImage> history;
		int left = 4;
		for (int frameIndex = 0; frameIndex < 12; ++frameIndex, left += 3)
		{
			const auto frame = render(left, motionVectors ? 3.0f : 0.0f);
			auto output = Ta::Resolve(frame.Color, frame.Depth, frame.Velocity, history ? &*history : nullptr, settings);
			// Covered one frame ago, background now, next to the square: its box spans both colors.
			trail = output.At(std::uint32_t(left - 1), 8)[0];
			inside = output.At(std::uint32_t(left + 2), 8)[0];
			history = std::move(output);
		}
	};
	TemporalSettings settings;
	float trail = 0.0f, inside = 0.0f;
	run(settings, true, trail, inside);
	SWIM_CHECK(trail < 0.06f); // No ghost: the dilated velocity reprojects to where the background was.
	SWIM_CHECK(inside > 3.9f); // The square keeps its color through reprojection.

	// Without motion vectors and with a loose box, the square's old color survives there.
	TemporalSettings loose;
	loose.ClipGamma = 8.0f;
	float looseTrail = 0.0f, looseInside = 0.0f;
	run(loose, false, looseTrail, looseInside);
	SWIM_CHECK(looseTrail > 1.0f);
}

SWIM_TEST("Render.Temporal.Reference", "ResetsOffscreenReprojectionAndValidation")
{
	Frame frame(8, 8);
	for (std::uint32_t i = 0; i < 64; ++i)
	{
		const float v = float(i) / 64.0f;
		frame.Color.Texels[i] = { v, 1.0f - v, 0.5f, 0.3f };
	}
	frame.Color.At(3, 3) = { NAN, 2.0f, -1.0f, 1.0f };
	Ta::ColorImage history(8, 8);
	std::fill(history.Texels.begin(), history.Texels.end(), Ta::Float4{ 9.0f, 9.0f, 9.0f, 1.0f });
	TemporalSettings settings;

	// No history: the sanitized current frame with alpha 1.
	const auto fresh = Ta::Resolve(frame.Color, frame.Depth, frame.Velocity, nullptr, settings);
	SWIM_CHECK((fresh.At(3, 3) == Ta::Float4{ 0.0f, 2.0f, 0.0f, 1.0f }));
	SWIM_CHECK((fresh.At(1, 0) == Ta::Float4{ 1.0f / 64.0f, 63.0f / 64.0f, 0.5f, 1.0f }));

	// Off-screen reprojection: the current frame again.
	std::fill(frame.Velocity.Texels.begin(), frame.Velocity.Texels.end(), Ta::Float2{ 2.0f, 0.0f });
	const auto offscreen = Ta::Resolve(frame.Color, frame.Depth, frame.Velocity, &history, settings);
	SWIM_CHECK(offscreen.Texels == fresh.Texels);

	// Feedback 1 ignores history; otherwise the (clipped) history is blended in.
	std::fill(frame.Velocity.Texels.begin(), frame.Velocity.Texels.end(), Ta::Float2{ 0.0f, 0.0f });
	settings.Feedback = 1.0f;
	const auto current = Ta::Resolve(frame.Color, frame.Depth, frame.Velocity, &history, settings);
	for (std::size_t i = 0; i < current.Texels.size(); ++i)
	{
		for (int c = 0; c < 4; ++c)
		{
			SWIM_CHECK(Near(current.Texels[i][c], fresh.Texels[i][c], 1.0e-6f));
		}
	}
	settings.Feedback = 0.1f;
	const auto blended = Ta::Resolve(frame.Color, frame.Depth, frame.Velocity, &history, settings);
	SWIM_CHECK(blended.At(5, 5)[0] > fresh.At(5, 5)[0]); // Pulled toward the (clipped) bright history...
	SWIM_CHECK(blended.At(5, 5)[0] < 1.0f);				 // ...but never past the neighborhood.

	TemporalSettings bad;
	bad.Feedback = 0.0f;
	SWIM_CHECK_THROWS(ValidateTemporalSettings(bad), std::invalid_argument);
	bad.Feedback = 1.5f;
	SWIM_CHECK_THROWS(ValidateTemporalSettings(bad), std::invalid_argument);
	bad = {};
	bad.ClipGamma = NAN;
	SWIM_CHECK_THROWS(ValidateTemporalSettings(bad), std::invalid_argument);
	bad.ClipGamma = 0.1f;
	SWIM_CHECK_THROWS(ValidateTemporalSettings(bad), std::invalid_argument);
	bad = {};
	bad.JitterPhases = MaxJitterPhases + 1;
	SWIM_CHECK_THROWS(ValidateTemporalSettings(bad), std::invalid_argument);
	ValidateTemporalSettings(TemporalSettings{});

	const Ta::DepthImage smallDepth(4, 8);
	SWIM_CHECK_THROWS(Ta::Resolve(frame.Color, smallDepth, frame.Velocity, nullptr, settings), std::invalid_argument);
	const Ta::ColorImage smallHistory(8, 4);
	SWIM_CHECK_THROWS(Ta::Resolve(frame.Color, frame.Depth, frame.Velocity, &smallHistory, settings), std::invalid_argument);
}
