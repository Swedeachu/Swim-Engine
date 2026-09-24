// GTAO (item 76): ScreenSpace::GtaoTexel for every pixel. Bindings follow ScreenSpaceAoBindings.
#include "ScreenSpace/ScreenSpaceRecords.slang"

[[vk::binding(0, 0)]] Texture2D<float> Depth;
[[vk::binding(1, 0)]] Texture2D<float4> Normal;
[[vk::binding(2, 0)]] StructuredBuffer<ScreenSpaceParams> Params;
[[vk::binding(3, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> Output;

float Gtao(ScreenSpaceView view, int x, int y)
{
	bool valid;
	const float3 p = ScreenSpaceLoadViewPosition(view, Depth, x, y, valid);
	if (!valid || !(-p.z > 0.0))
	{
		return 1.0;
	}
	// ScreenSpace::ViewNormal.
	const float3 world = Normal.Load(int3(x, y, 0)).xyz;
	float3 n = float3(0.0, 0.0, 0.0);
	for (uint row = 0; row < 3; ++row)
	{
		n[row] = dot(Params[0].ViewRows[row].xyz, world);
	}
	const float normalLength = length(n);
	if (!(normalLength > 1.0e-6))
	{
		return 1.0;
	}
	const float3 normalV = n * (1.0 / normalLength);
	const float3 v = p * (-1.0 / length(p));
	const float radius = Params[0].AoRadius;
	const float radiusPixels = min(radius * Params[0].AoRadiusToPixels / -p.z, Params[0].AoMaxRadiusPixels);
	if (radiusPixels < 1.0)
	{
		return 1.0;
	}
	const uint frame = Params[0].NoiseFrame;
	const float noise = ScreenSpaceNoise(float(x), float(y), frame);
	const float stepNoise = ScreenSpaceNoise(float(x) + 17.0, float(y) + 29.0, frame);
	const float falloffRange = radius * Params[0].AoFalloff;
	const uint slices = Params[0].AoSliceCount;
	const uint steps = Params[0].AoStepCount;
	float visibility = 0.0;
	for (uint slice = 0; slice < slices; ++slice)
	{
		const float phi = (float(slice) + noise) * ScreenSpacePi / float(slices);
		const float dx = cos(phi);
		const float dy = sin(phi);
		const float3 direction = float3(dx, -dy, 0.0); // Screen y points down, view y up.
		const float3 ortho = direction - v * dot(direction, v);
		const float3 axisRaw = cross(direction, v);
		const float3 axis = axisRaw * (1.0 / length(axisRaw));
		const float3 projected = normalV - axis * dot(normalV, axis);
		const float projectedLength = length(projected);
		if (!(projectedLength > 1.0e-6))
		{
			continue;
		}
		const float cosN = clamp(dot(projected, v) / projectedLength, -1.0, 1.0);
		const float angleN = (dot(projected, ortho) < 0.0 ? -1.0 : 1.0) * acos(cosN);
		const float lowPositive = cos(angleN + 0.5 * ScreenSpacePi);
		const float lowNegative = cos(angleN - 0.5 * ScreenSpacePi);
		float horizonPositive = lowPositive;
		float horizonNegative = lowNegative;
		for (uint stepIndex = 0; stepIndex < steps; ++stepIndex)
		{
			const float t = (float(stepIndex) + stepNoise) / float(steps) * radiusPixels;
			for (uint side = 0; side < 2; ++side)
			{
				const float offsetSign = side == 0 ? 1.0 : -1.0;
				const float sx = floor(float(x) + 0.5 + offsetSign * dx * t);
				const float sy = floor(float(y) + 0.5 + offsetSign * dy * t);
				if (sx < 0.0 || sy < 0.0 || sx >= float(view.Width) || sy >= float(view.Height))
				{
					continue;
				}
				const int ix = int(sx);
				const int iy = int(sy);
				if (ix == x && iy == y)
				{
					continue;
				}
				bool sampleValid;
				const float3 samplePosition = ScreenSpaceLoadViewPosition(view, Depth, ix, iy, sampleValid);
				if (!sampleValid)
				{
					continue;
				}
				const float3 delta = samplePosition - p;
				const float sampleDistance = length(delta);
				if (!(sampleDistance > 1.0e-6))
				{
					continue;
				}
				const float cosSample = dot(delta, v) / sampleDistance;
				const float weight = clamp((radius - sampleDistance) / falloffRange, 0.0, 1.0);
				if (side == 0)
				{
					horizonPositive = max(horizonPositive, lowPositive + (cosSample - lowPositive) * weight);
				}
				else
				{
					horizonNegative = max(horizonNegative, lowNegative + (cosSample - lowNegative) * weight);
				}
			}
		}
		float h1 = acos(clamp(horizonPositive, -1.0, 1.0));
		float h0 = -acos(clamp(horizonNegative, -1.0, 1.0));
		h1 = angleN + min(h1 - angleN, 0.5 * ScreenSpacePi);
		h0 = angleN + max(h0 - angleN, -0.5 * ScreenSpacePi);
		const float sinN = sin(angleN);
		const float arc1 = (cosN + 2.0 * h1 * sinN - cos(2.0 * h1 - angleN)) * 0.25;
		const float arc0 = (cosN + 2.0 * h0 * sinN - cos(2.0 * h0 - angleN)) * 0.25;
		visibility += projectedLength * (arc0 + arc1);
	}
	return max(visibility / float(slices), 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ScreenSpaceView view = ScreenSpaceLoadView(Params);
	if (id.x >= view.Width || id.y >= view.Height)
	{
		return;
	}
	Output[id.xy] = Gtao(view, int(id.x), int(id.y));
}
