#include "Engine/Systems/Renderer/PostProcess/PostProcessReference.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;
namespace Pp = Swim::Render::Post;

namespace
{
	Pp::Image Uniform(std::uint32_t width, std::uint32_t height, const Pp::Float4& value)
	{
		Pp::Image image(width, height);
		for (auto& texel : image.Texels)
		{
			texel = value;
		}
		return image;
	}

	bool Near(float a, float b, float tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}
} // namespace

SWIM_TEST("Render.PostProcess.Reference", "RoundToHalfIsBinary16RoundToNearestEven")
{
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(1.0f), 1.0f);
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(0.1f), 0.0999755859375f);
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(1.0f / 3.0f), 0.333251953125f);
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(-2.5f), -2.5f);
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(65504.0f), 65504.0f);
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(65519.0f), 65504.0f);
	SWIM_CHECK(std::isinf(Pp::RoundToHalf(65520.0f)));
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(1.0f + 1.0f / 2048.0f), 1.0f);					 // Tie: to even.
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(1.0f + 3.0f / 2048.0f), 1.0f + 1.0f / 512.0f);	 // Tie: to even (up).
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(std::ldexp(1.0f, -24)), std::ldexp(1.0f, -24)); // Smallest subnormal.
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(std::ldexp(1.0f, -26)), 0.0f);
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(std::ldexp(3.0f, -26)), std::ldexp(1.0f, -24));
	SWIM_CHECK_EQUAL(Pp::RoundToHalf(std::ldexp(1.0f, -14)), std::ldexp(1.0f, -14)); // Smallest normal.
	SWIM_CHECK(std::isnan(Pp::RoundToHalf(std::numeric_limits<float>::quiet_NaN())));
	std::mt19937 random(73);
	std::uniform_real_distribution<float> unit(-100.0f, 100.0f);
	for (int i = 0; i < 1000; ++i)
	{
		const float v = unit(random);
		const float h = Pp::RoundToHalf(v);
		SWIM_CHECK(std::abs(h - v) <= std::abs(v) * (1.0f / 2048.0f) + 1.0e-7f);
		SWIM_CHECK_EQUAL(Pp::RoundToHalf(h), h); // Idempotent.
	}
}

SWIM_TEST("Render.PostProcess.Reference", "ToneMappersAreMonotonicBoundedAndMatchTheirDefinitions")
{
	for (const auto op : { ToneMapper::Clamp, ToneMapper::Reinhard, ToneMapper::Aces, ToneMapper::PbrNeutral })
	{
		float previous = -1.0f;
		for (float x = 0.0f; x <= 64.0f; x = x < 1.0f ? x + 0.01f : x * 1.05f)
		{
			const auto y = Pp::ToneMap(op, { x, x, x }, 4.0f);
			SWIM_CHECK(y[0] >= previous - 1.0e-6f && y[0] <= 1.0f && y[0] >= 0.0f);
			SWIM_CHECK(Near(y[0], y[1], 1.0e-3f) && Near(y[1], y[2], 1.0e-3f)); // Grey stays grey (ACES fit rows sum to 1 +- 1e-5).
			previous = y[0];
		}
		const auto zero = Pp::ToneMap(op, { 0, 0, 0 }, 4.0f);
		SWIM_CHECK(zero[0] <= 1.0e-4f);
		const auto negative = Pp::ToneMap(op, { -1, -2, -3 }, 4.0f);
		SWIM_CHECK(negative[0] >= 0.0f && negative[1] >= 0.0f && negative[2] >= 0.0f);
	}
	// Reinhard: the white point maps to 1.
	SWIM_CHECK(Near(Pp::ToneMapReinhard({ 4, 4, 4 }, 4.0f)[0], 1.0f, 1.0e-6f));
	SWIM_CHECK(Pp::ToneMapReinhard({ 3.9f, 0, 0 }, 4.0f)[0] < 1.0f);
	SWIM_CHECK(Near(Pp::ToneMapReinhard({ 1, 1, 1 }, 1.0f)[0], 1.0f, 1.0e-6f));
	// ACES (Hill): 18 % grey -> 0.1056.
	SWIM_CHECK(Near(Pp::ToneMapAces({ 0.18f, 0.18f, 0.18f })[0], 0.10559f, 2.0e-4f));
	SWIM_CHECK(Pp::ToneMapAces({ 100, 100, 100 })[0] > 0.99f);
	// PBR Neutral: x - 0.04 on its linear section, 6.25 x^2 in the toe, compressed above 0.76.
	SWIM_CHECK(Near(Pp::ToneMapPbrNeutral({ 0.5f, 0.3f, 0.2f })[0], 0.46f, 1.0e-6f));
	SWIM_CHECK(Near(Pp::ToneMapPbrNeutral({ 0.5f, 0.3f, 0.2f })[2], 0.16f, 1.0e-6f));
	SWIM_CHECK(Near(Pp::ToneMapPbrNeutral({ 0.04f, 0.04f, 0.04f })[0], 6.25f * 0.04f * 0.04f, 1.0e-7f));
	SWIM_CHECK(Pp::ToneMapPbrNeutral({ 1000, 1000, 1000 })[0] > 0.99f);
	// Hue: a saturated bright color keeps its channel order and moves toward white.
	const auto bright = Pp::ToneMapPbrNeutral({ 8.0f, 4.0f, 1.0f });
	SWIM_CHECK(bright[0] > bright[1] && bright[1] > bright[2]);
	SWIM_CHECK(bright[2] / bright[0] > 1.0f / 8.0f);
}

SWIM_TEST("Render.PostProcess.Reference", "OutputTransferFunctionsMatchTheirStandards")
{
	SWIM_CHECK(Near(Pp::SrgbOetf(0.5f), 0.7353569f, 1.0e-6f));
	SWIM_CHECK(Near(Pp::SrgbOetf(0.0031308f), 0.04045f, 1.0e-5f));
	SWIM_CHECK_EQUAL(Pp::SrgbOetf(0.0f), 0.0f);
	SWIM_CHECK(Near(Pp::SrgbOetf(1.0f), 1.0f, 1.0e-6f));
	for (float v = 0.0f; v <= 1.0f; v += 0.01f)
	{
		SWIM_CHECK(Near(Pp::SrgbEotf(Pp::SrgbOetf(v)), v, 1.0e-5f));
	}
	// ST 2084: 10,000 nits -> 1, 100 nits -> 0.5081, black -> c1^m2.
	SWIM_CHECK(Near(Pp::PqOetf(10000.0f), 1.0f, 1.0e-6f));
	SWIM_CHECK(Near(Pp::PqOetf(100.0f), 0.508078f, 2.0e-5f));
	SWIM_CHECK(Near(Pp::PqOetf(1000.0f), 0.751827f, 2.0e-5f));
	SWIM_CHECK(Pp::PqOetf(0.0f) < 1.0e-6f);
	for (const float nits : { 0.1f, 1.0f, 80.0f, 203.0f, 1000.0f, 4000.0f })
	{
		SWIM_CHECK(Near(Pp::PqEotf(Pp::PqOetf(nits)), nits, nits * 1.0e-3f));
	}
	// BT.709 -> BT.2020 keeps white and luminance-neutral grey.
	const auto white = Pp::Rec709ToRec2020({ 1, 1, 1 });
	SWIM_CHECK(Near(white[0], 1.0f, 1.0e-5f) && Near(white[1], 1.0f, 1.0e-5f) && Near(white[2], 1.0f, 1.0e-5f));
	const auto red = Pp::Rec709ToRec2020({ 1, 0, 0 });
	SWIM_CHECK(red[0] < 1.0f && red[1] > 0.0f); // 709 red lies inside the 2020 gamut.
	// Interleaved gradient noise stays in [0, 1) and averages to 1/2.
	double sum = 0.0;
	for (std::uint32_t y = 0; y < 64; ++y)
	{
		for (std::uint32_t x = 0; x < 64; ++x)
		{
			const float n = Pp::InterleavedGradientNoise(x, y);
			SWIM_CHECK(n >= 0.0f && n < 1.0f);
			sum += n;
		}
	}
	SWIM_CHECK(std::abs(sum / 4096.0 - 0.5) < 0.02);
}

SWIM_TEST("Render.PostProcess.Reference", "GradingDefaultsAreTheIdentityAndEachStepBehaves")
{
	PostProcessSettings settings;
	auto params = Pp::BuildPostParams(settings, 0);
	SWIM_CHECK_EQUAL(params.GradingEnabled, 0u);
	const Pp::Float3 color{ 0.3f, 1.7f, 0.02f };
	const auto same = Pp::Grade(params, color);
	SWIM_CHECK(same == color);

	// White balance: identity at 0, warmer raises red over blue, tint moves green.
	const auto identity = Pp::WhiteBalanceMatrix(0.0f, 0.0f);
	SWIM_CHECK((identity == Pp::Matrix3{ 1, 0, 0, 0, 1, 0, 0, 0, 1 }));
	const auto nearIdentity = Pp::WhiteBalanceMatrix(0.001f, 0.0f);
	for (int i = 0; i < 9; ++i)
	{
		SWIM_CHECK(Near(nearIdentity[i], identity[i], 2.0e-3f)); // The fitted matrices invert each other closely.
	}
	settings.Grading.Temperature = 50.0f;
	params = Pp::BuildPostParams(settings, 0);
	SWIM_CHECK_EQUAL(params.GradingEnabled, 1u);
	auto warm = Pp::Grade(params, { 0.5f, 0.5f, 0.5f });
	SWIM_CHECK(warm[0] > warm[2]);
	settings.Grading.Temperature = -50.0f;
	params = Pp::BuildPostParams(settings, 0);
	auto cool = Pp::Grade(params, { 0.5f, 0.5f, 0.5f });
	SWIM_CHECK(cool[2] > cool[0]);
	settings.Grading.Temperature = 0.0f;
	settings.Grading.Tint = 60.0f;
	params = Pp::BuildPostParams(settings, 0);
	auto magenta = Pp::Grade(params, { 0.5f, 0.5f, 0.5f });
	SWIM_CHECK(magenta[1] < magenta[0] && magenta[1] < magenta[2]);

	// Contrast pivots on 18 % grey.
	settings.Grading = {};
	settings.Grading.Contrast = 2.0f;
	params = Pp::BuildPostParams(settings, 0);
	SWIM_CHECK(Near(Pp::Grade(params, { 0.18f, 0.18f, 0.18f })[0], 0.18f, 1.0e-6f));
	SWIM_CHECK(Pp::Grade(params, { 0.36f, 0.36f, 0.36f })[0] > 0.36f);
	SWIM_CHECK(Pp::Grade(params, { 0.09f, 0.09f, 0.09f })[0] < 0.09f);
	// CDL: out = (in * slope + offset) ^ power.
	settings.Grading = {};
	settings.Grading.Slope = { 2.0f, 1.0f, 0.5f };
	settings.Grading.Offset = { 0.1f, 0.0f, -1.0f };
	settings.Grading.Power = { 2.0f, 1.0f, 1.0f };
	params = Pp::BuildPostParams(settings, 0);
	const auto cdl = Pp::Grade(params, { 0.2f, 0.4f, 0.6f });
	SWIM_CHECK(Near(cdl[0], 0.25f, 1.0e-5f) && Near(cdl[1], 0.4f, 1.0e-5f) && cdl[2] == 0.0f);
	// Saturation 0: every channel equals the luminance.
	settings.Grading = {};
	settings.Grading.Saturation = 0.0f;
	params = Pp::BuildPostParams(settings, 0);
	const auto grey = Pp::Grade(params, { 0.8f, 0.2f, 0.1f });
	const float luma = Pp::Luminance({ 0.8f, 0.2f, 0.1f });
	SWIM_CHECK(Near(grey[0], luma, 1.0e-6f) && Near(grey[1], luma, 1.0e-6f) && Near(grey[2], luma, 1.0e-6f));
	settings.Grading.Saturation = 2.0f;
	params = Pp::BuildPostParams(settings, 0);
	const auto vivid = Pp::Grade(params, { 0.8f, 0.2f, 0.1f });
	SWIM_CHECK(vivid[0] > 0.8f && Near(Pp::Luminance(vivid), luma, 2.0e-2f));
}

SWIM_TEST("Render.PostProcess.Reference", "HistogramBinsCoverTheLogRangeWithABlackBin")
{
	ExposureSettings e;
	const float inverse = 1.0f / (e.MaxLog2Luminance - e.MinLog2Luminance);
	SWIM_CHECK_EQUAL(Pp::HistogramBin(0.0f, e.MinLog2Luminance, inverse), 0u);
	SWIM_CHECK_EQUAL(Pp::HistogramBin(-1.0f, e.MinLog2Luminance, inverse), 0u);
	SWIM_CHECK_EQUAL(Pp::HistogramBin(std::numeric_limits<float>::quiet_NaN(), e.MinLog2Luminance, inverse), 0u);
	SWIM_CHECK_EQUAL(Pp::HistogramBin(std::ldexp(1.0f, -13), e.MinLog2Luminance, inverse), 0u);
	SWIM_CHECK_EQUAL(Pp::HistogramBin(std::ldexp(1.0f, -12), e.MinLog2Luminance, inverse), 1u);
	SWIM_CHECK_EQUAL(Pp::HistogramBin(std::ldexp(1.0f, 12), e.MinLog2Luminance, inverse), 255u);
	SWIM_CHECK_EQUAL(Pp::HistogramBin(1.0e30f, e.MinLog2Luminance, inverse), 255u);
	SWIM_CHECK_EQUAL(Pp::HistogramBin(std::numeric_limits<float>::infinity(), e.MinLog2Luminance, inverse), 255u);
	std::uint32_t previous = 0;
	for (float l = std::ldexp(1.0f, -12); l < 4096.0f; l *= 1.01f)
	{
		const auto bin = Pp::HistogramBin(l, e.MinLog2Luminance, inverse);
		SWIM_CHECK(bin >= previous && bin >= 1u);
		// The bin's center is within half a bin of the sample.
		SWIM_CHECK(std::abs(Pp::BinLog2Luminance(bin, e.MinLog2Luminance, 24.0f) - std::log2(l)) <= 24.0f / 254.0f * 0.5f + 1.0e-4f);
		previous = bin;
	}
	Pp::Image image(16, 8);
	for (std::size_t i = 0; i < image.Texels.size(); ++i)
	{
		const float v = float(i) / 16.0f;
		image.Texels[i] = { v, v, v, 1.0f };
	}
	const auto bins = Pp::BuildHistogram(image, e);
	std::uint32_t total = 0;
	for (const auto n : bins)
	{
		total += n;
	}
	SWIM_CHECK_EQUAL(total, 128u);
	SWIM_CHECK_EQUAL(bins[0], 1u); // The black pixel.
}

SWIM_TEST("Render.PostProcess.Reference", "AverageLuminanceHonorsPercentilesAndIgnoresBlack")
{
	ExposureSettings e;
	e.LowPercentile = 0.0f;
	e.HighPercentile = 1.0f;
	// A uniform image averages to its luminance (within half a bin).
	for (const float l : { 0.01f, 0.18f, 1.0f, 50.0f })
	{
		const auto bins = Pp::BuildHistogram(Uniform(8, 8, { l, l, l, 1 }), e);
		float average = 0.0f;
		SWIM_REQUIRE(Pp::AverageLog2Luminance(bins, e, average));
		SWIM_CHECK(std::abs(average - std::log2(l)) <= 24.0f / 508.0f + 1.0e-4f);
	}
	// 90 % at 0.1 and 10 % at 1000: a 50-90 % window drops the bright outliers.
	Pp::Histogram bins{};
	const float inverse = 1.0f / 24.0f;
	bins[Pp::HistogramBin(0.1f, -12.0f, inverse)] = 90;
	bins[Pp::HistogramBin(1000.0f, -12.0f, inverse)] = 10;
	bins[0] = 1000; // Black is ignored.
	auto window = e;
	window.LowPercentile = 0.5f;
	window.HighPercentile = 0.9f;
	float average = 0.0f;
	SWIM_REQUIRE(Pp::AverageLog2Luminance(bins, window, average));
	SWIM_CHECK(std::abs(average - std::log2(0.1f)) < 0.1f);
	// Keeping everything, the outliers pull the average up.
	SWIM_REQUIRE(Pp::AverageLog2Luminance(bins, e, average));
	SWIM_CHECK(average > std::log2(0.1f) + 1.0f);
	// A low percentile of 0.95 keeps only the brightest 5 %.
	auto bright = e;
	bright.LowPercentile = 0.95f;
	SWIM_REQUIRE(Pp::AverageLog2Luminance(bins, bright, average));
	SWIM_CHECK(std::abs(average - std::log2(1000.0f)) < 0.1f);
	// Nothing but black: no measurement.
	Pp::Histogram black{};
	black[0] = 64;
	SWIM_CHECK(!Pp::AverageLog2Luminance(black, e, average));
}

SWIM_TEST("Render.PostProcess.Reference", "ExposureSnapsAdaptsAndClamps")
{
	SWIM_CHECK(Near(Pp::ExposureFromEv100(0.0f), 1.0f / 1.2f, 1.0e-7f));
	SWIM_CHECK(Near(Pp::ExposureFromEv100(1.0f), 1.0f / 2.4f, 1.0e-7f));
	SWIM_CHECK_EQUAL(Pp::Ev100FromAverageLog2(0.0f), 3.0f); // 1 cd/m^2 -> EV 3.

	ExposureSettings e;
	const float inverse = 1.0f / 24.0f;
	Pp::Histogram dim{}, brightScene{};
	dim[Pp::HistogramBin(0.05f, -12.0f, inverse)] = 100;
	brightScene[Pp::HistogramBin(500.0f, -12.0f, inverse)] = 100;
	float dimAverage = 0.0f, brightAverage = 0.0f;
	Pp::AverageLog2Luminance(dim, e, dimAverage);
	Pp::AverageLog2Luminance(brightScene, e, brightAverage);

	// The first frame (invalid history) snaps to the target.
	const auto first = Pp::UpdateExposure(dim, e, {}, 0.016f, false);
	SWIM_CHECK_EQUAL(first.Valid, 1u);
	SWIM_CHECK_EQUAL(first.Ev100, dimAverage + 3.0f);
	SWIM_CHECK_EQUAL(first.Exposure, Pp::ExposureFromEv100(first.Ev100));
	// Then it adapts toward the bright target with SpeedUp: fraction 1 - exp(-dt * speed).
	const auto next = Pp::UpdateExposure(brightScene, e, first, 0.1f, false);
	const float target = brightAverage + 3.0f;
	const float expected = first.Ev100 + (target - first.Ev100) * (1.0f - std::exp(-0.1f * e.SpeedUp));
	SWIM_CHECK(Near(next.Ev100, expected, 1.0e-5f));
	// ...and back down more slowly.
	const auto down = Pp::UpdateExposure(dim, e, Pp::UpdateExposure(brightScene, e, {}, 0.0f, true), 0.1f, false);
	const float downExpected = target + (first.Ev100 - target) * (1.0f - std::exp(-0.1f * e.SpeedDown));
	SWIM_CHECK(Near(down.Ev100, downExpected, 1.0e-5f));
	SWIM_CHECK(std::abs(next.Ev100 - first.Ev100) > std::abs(down.Ev100 - target));
	// Many frames converge; reset snaps immediately.
	auto state = first;
	for (int i = 0; i < 300; ++i)
	{
		state = Pp::UpdateExposure(brightScene, e, state, 1.0f / 60.0f, false);
	}
	SWIM_CHECK(Near(state.Ev100, target, 1.0e-3f));
	SWIM_CHECK_EQUAL(Pp::UpdateExposure(dim, e, state, 1.0f / 60.0f, true).Ev100, first.Ev100);
	// Zero delta time holds the previous value.
	SWIM_CHECK_EQUAL(Pp::UpdateExposure(dim, e, state, 0.0f, false).Ev100, state.Ev100);

	// Compensation and clamps.
	auto compensated = e;
	compensated.Compensation = 1.0f;
	SWIM_CHECK(Near(Pp::UpdateExposure(dim, compensated, {}, 0.0f, true).Exposure, 2.0f * first.Exposure, 1.0e-6f));
	auto clamped = e;
	clamped.MaxEv100 = 2.0f;
	SWIM_CHECK_EQUAL(Pp::UpdateExposure(brightScene, clamped, {}, 0.0f, true).Ev100, 2.0f);
	// No measurement: 18 % grey on a snap, the previous average afterwards.
	Pp::Histogram empty{};
	SWIM_CHECK(Near(Pp::UpdateExposure(empty, e, {}, 0.0f, true).AverageLog2Luminance, std::log2(0.18f), 1.0e-6f));
	SWIM_CHECK_EQUAL(Pp::UpdateExposure(empty, e, state, 0.1f, false).AverageLog2Luminance, state.AverageLog2Luminance);
	// Manual mode ignores the histogram and never adapts.
	auto manual = e;
	manual.Mode = ExposureMode::Manual;
	manual.ManualEv100 = 9.0f;
	manual.Compensation = 1.0f;
	const auto m = Pp::UpdateExposure(brightScene, manual, state, 0.1f, false);
	SWIM_CHECK_EQUAL(m.Ev100, 8.0f);
	SWIM_CHECK_EQUAL(m.Exposure, Pp::ExposureFromEv100(8.0f));
}

SWIM_TEST("Render.PostProcess.Reference", "BloomFiltersPreserveConstantsAndThresholdSmoothly")
{
	SWIM_CHECK_EQUAL(Pp::BloomLevelCount(64, 32, 8), 5u);
	SWIM_CHECK_EQUAL(Pp::BloomLevelCount(64, 32, 3), 3u);
	SWIM_CHECK_EQUAL(Pp::BloomLevelCount(1, 32, 8), 0u);
	SWIM_CHECK_EQUAL(Pp::BloomLevelCount(1920, 1080, 6), 6u);

	// Constant in, constant out (the weights sum to 1), for any level size.
	const auto flat = Uniform(13, 9, { 2.0f, 1.0f, 0.5f, 1.0f });
	const auto down = Pp::BloomDownsample(flat, 6, 4, false, 1.0f, 0.0f, 0.5f);
	for (const auto& t : down.Texels)
	{
		SWIM_CHECK(t[0] == 2.0f && t[1] == 1.0f && t[2] == 0.5f && t[3] == 1.0f);
	}
	// First level: exposure, Karis (constant stays constant), threshold 0 keeps bright texels.
	const auto first = Pp::BloomDownsample(flat, 6, 4, true, 0.5f, 0.0f, 0.5f);
	for (const auto& t : first.Texels)
	{
		SWIM_CHECK(Near(t[0], 1.0f, 1.0e-3f) && Near(t[1], 0.5f, 1.0e-3f) && Near(t[2], 0.25f, 1.0e-3f));
	}
	const auto up = Pp::BloomUpsample(down, Uniform(12, 8, { 1.0f, 1.0f, 1.0f, 1.0f }));
	for (const auto& t : up.Texels)
	{
		SWIM_CHECK(Near(t[0], 3.0f, 2.0e-3f) && Near(t[1], 2.0f, 2.0e-3f) && Near(t[2], 1.5f, 2.0e-3f));
	}

	// Threshold: zero well below, continuous through the knee, ~(b - t) / b far above.
	SWIM_CHECK(Pp::BloomThreshold({ 0.4f, 0.4f, 0.4f }, 1.0f, 0.5f)[0] == 0.0f);
	float previous = 0.0f;
	for (float b = 0.0f; b < 4.0f; b += 0.01f)
	{
		const float v = Pp::BloomThreshold({ b, b * 0.5f, 0.0f }, 1.0f, 0.5f)[0];
		SWIM_CHECK(v >= previous - 1.0e-6f && v <= b + 1.0e-6f);
		SWIM_CHECK(v - previous < 0.02f); // No jumps.
		previous = v;
	}
	SWIM_CHECK(Near(Pp::BloomThreshold({ 10, 10, 10 }, 1.0f, 0.5f)[0], 9.0f, 1.0e-3f));

	// Karis averaging tames a single firefly compared with the plain filter.
	Pp::Image spike = Uniform(16, 16, { 0.1f, 0.1f, 0.1f, 1.0f });
	spike.At(6, 6) = { 1000.0f, 1000.0f, 1000.0f, 1.0f }; // Only in one outer group of level texel (4, 4).
	const auto plain = Pp::BloomDownsample(spike, 8, 8, false, 1.0f, 0.0f, 0.5f);
	const auto karis = Pp::BloomDownsample(spike, 8, 8, true, 1.0f, 0.0f, 0.5f);
	SWIM_CHECK(karis.At(4, 4)[0] < plain.At(4, 4)[0] * 0.5f);
	// The spread is symmetric about the diagonal through the spike.
	SWIM_CHECK(Near(plain.At(2, 3)[0], plain.At(3, 2)[0], 1.0e-3f));
}

SWIM_TEST("Render.PostProcess.Reference", "FullFramesComposeEveryStageForEachEncoding")
{
	Pp::Image image(32, 16);
	for (std::uint32_t y = 0; y < 16; ++y)
	{
		for (std::uint32_t x = 0; x < 32; ++x)
		{
			const float v = Pp::RoundToHalf(0.001f * float(x) * float(y + 1));
			image.At(x, y) = { v, Pp::RoundToHalf(v * 0.5f), Pp::RoundToHalf(0.1f), 1.0f };
		}
	}
	image.At(20, 8) = { 200.0f, 150.0f, 100.0f, 1.0f };
	PostProcessSettings settings;
	const auto sdr = Pp::RunPostProcess(image, settings, {}, 1.0f / 60.0f, true);
	SWIM_CHECK_EQUAL(sdr.Down.size(), std::size_t(4)); // 16 -> 8, 4, 2, 1.
	SWIM_CHECK_EQUAL(sdr.Up.size(), std::size_t(4));
	SWIM_CHECK(sdr.Up.back().Texels == sdr.Down.back().Texels);
	SWIM_CHECK_EQUAL(sdr.Params.BloomEnabled, 1u);
	SWIM_CHECK(Near(sdr.Params.PowerBloom[3], settings.Bloom.Intensity / 4.0f, 1.0e-9f));
	std::uint32_t pixels = 0;
	for (const auto n : sdr.Bins)
	{
		pixels += n;
	}
	SWIM_CHECK_EQUAL(pixels, 32u * 16u);
	for (const auto& t : sdr.Output.Texels)
	{
		for (const float v : t)
		{
			SWIM_CHECK(v >= 0.0f && v <= 1.0f && v * 255.0f == std::round(v * 255.0f));
		}
	}
	// The bright pixel blooms into its neighbors.
	settings.Bloom.Enabled = false;
	const auto noBloom = Pp::RunPostProcess(image, settings, {}, 1.0f / 60.0f, true);
	SWIM_CHECK(noBloom.Down.empty() && noBloom.Params.BloomEnabled == 0u);
	SWIM_CHECK(sdr.Output.At(22, 8)[1] > noBloom.Output.At(22, 8)[1]);
	// Dither changes each channel by at most one step and averages out.
	settings.Output.Dither = false;
	const auto flat = Pp::RunPostProcess(image, settings, {}, 1.0f / 60.0f, true);
	double drift = 0.0;
	for (std::size_t i = 0; i < flat.Output.Texels.size(); ++i)
	{
		for (int c = 0; c < 3; ++c)
		{
			const float d = noBloom.Output.Texels[i][c] - flat.Output.Texels[i][c];
			SWIM_CHECK(std::abs(d) <= 1.0f / 255.0f + 1.0e-6f);
			drift += d;
		}
	}
	SWIM_CHECK(std::abs(drift) / double(flat.Output.Texels.size() * 3) < 0.5 / 255.0);

	// HDR: with Clamp tone mapping and exposure 1, scene value 1 is paper white.
	Pp::Image grey = Uniform(8, 8, { 1.2f, 1.2f, 1.2f, 1.0f });
	PostProcessSettings hdr;
	hdr.Exposure.Mode = ExposureMode::Manual;
	hdr.Exposure.ManualEv100 = 0.0f; // Exposure 1 / 1.2.
	hdr.Bloom.Enabled = false;
	hdr.ToneMap.Operator = ToneMapper::Clamp;
	hdr.Output.Encoding = OutputEncoding::Hdr10;
	const auto pq = Pp::RunPostProcess(grey, hdr, {}, 0.0f, true);
	SWIM_CHECK(pq.Bins == Pp::Histogram{}); // Manual exposure builds no histogram.
	SWIM_CHECK(Near(pq.Output.At(3, 3)[0], Pp::PqOetf(200.0f), 1.0e-3f));
	SWIM_CHECK(Near(pq.Output.At(3, 3)[3], 1.0f, 0.0f));
	hdr.Output.Encoding = OutputEncoding::ScRgb;
	const auto sc = Pp::RunPostProcess(grey, hdr, {}, 0.0f, true);
	SWIM_CHECK(Near(sc.Output.At(3, 3)[0], 2.5f, 2.0e-3f));
	// Values beyond the peak clip at the peak (Clamp) in PQ.
	hdr.Output.Encoding = OutputEncoding::Hdr10;
	const auto blown = Pp::RunPostProcess(Uniform(4, 4, { 100.0f, 100.0f, 100.0f, 1.0f }), hdr, {}, 0.0f, true);
	SWIM_CHECK(Near(blown.Output.At(0, 0)[0], Pp::PqOetf(1000.0f), 1.0e-3f));
}

SWIM_TEST("Render.PostProcess.Reference", "SettingsValidationRejectsOutOfRangeValues")
{
	const auto invalid = [](auto edit)
	{
		PostProcessSettings settings;
		edit(settings);
		ValidatePostProcessSettings(settings);
	};
	PostProcessSettings valid;
	ValidatePostProcessSettings(valid);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Exposure.MinEv100 = 20.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Exposure.MaxLog2Luminance = -20.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Exposure.LowPercentile = 0.99f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Exposure.SpeedUp = 0.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Exposure.ManualEv100 = NAN;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Bloom.MipCount = 0;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Bloom.MipCount = MaxBloomMips + 1;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Bloom.Knee = 0.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Grading.Contrast = 0.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Grading.Temperature = 150.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Grading.Power[1] = 0.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.ToneMap.WhitePoint = 0.5f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.ToneMap.Operator = static_cast<ToneMapper>(9);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Output.PeakNits = 100.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(invalid(
						  [](PostProcessSettings& s)
						  {
							  s.Output.Encoding = static_cast<OutputEncoding>(3);
						  }),
		std::invalid_argument);
}
