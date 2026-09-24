// Exposure (item 73): Post::UpdateExposure in one thread, so the reduction order equals
// the CPU definition. Writes the persistent ExposureState. Bindings follow PostExposureBindings.
#include "PostProcess/PostProcessRecords.slang"

struct ExposureConstants
{
	uint Mode; // 0 manual, 1 automatic.
	float ManualEv100;
	float Compensation;
	float MinEv100;
	float MaxEv100;
	float LowPercentile;
	float HighPercentile;
	float SpeedUp;
	float SpeedDown;
	float DeltaTime;
	float MinLog2Luminance;
	float Log2Range;
	uint Reset;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<ExposureConstants> Constants;
[[vk::binding(0, 0)]] StructuredBuffer<uint> Histogram;
[[vk::binding(1, 0)]] RWStructuredBuffer<ExposureState> State;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
	const ExposureState previous = State[0];
	const bool snap = Constants.Reset != 0 || previous.Valid == 0;
	ExposureState state;
	if (Constants.Mode == 0)
	{
		state.AverageLog2Luminance = snap ? 0.0 : previous.AverageLog2Luminance;
		state.Ev100 = clamp(Constants.ManualEv100 - Constants.Compensation, Constants.MinEv100, Constants.MaxEv100);
	}
	else
	{
		float total = 0.0;
		for (uint b = 1; b < PostHistogramBins; ++b)
		{
			total += float(Histogram[b]);
		}
		const float low = total * Constants.LowPercentile;
		const float high = total * Constants.HighPercentile;
		float cumulative = 0.0;
		float weight = 0.0;
		float sum = 0.0;
		for (uint b = 1; b < PostHistogramBins; ++b)
		{
			const float count = float(Histogram[b]);
			const float w = max(min(cumulative + count, high) - max(cumulative, low), 0.0);
			weight += w;
			sum += w * PostBinLog2Luminance(b, Constants.MinLog2Luminance, Constants.Log2Range);
			cumulative += count;
		}
		float average = sum / weight;
		if (!(weight > 0.0))
		{
			average = snap ? log2(0.18) : previous.AverageLog2Luminance;
		}
		state.AverageLog2Luminance = average;
		const float target = clamp(average + 3.0 - Constants.Compensation, Constants.MinEv100, Constants.MaxEv100);
		if (snap)
		{
			state.Ev100 = target;
		}
		else
		{
			const float speed = target > previous.Ev100 ? Constants.SpeedUp : Constants.SpeedDown;
			state.Ev100 = previous.Ev100 + (target - previous.Ev100) * (1.0 - exp(-Constants.DeltaTime * speed));
		}
	}
	state.Exposure = PostExposureFromEv100(state.Ev100);
	state.Valid = 1;
	State[0] = state;
}
