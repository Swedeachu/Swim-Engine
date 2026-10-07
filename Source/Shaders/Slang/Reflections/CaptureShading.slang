// Shared by the capture resolves (PlanarReflectionResolve.slang, ReflectionProbeResolve.slang):
// reflections of reflections inside a capture.
//
// A capture is an ordinary Forward+ render: reflective surfaces in it carry only the
// global environment's specular IBL (the screen-space composite never runs on it). Seen in a
// mirror or a chrome ball, another chrome ball or mirror then looked like a flat piece of
// sky - most visibly from its far side, which the main view never shows. Here such texels
// replace that specular IBL with their reflection probe (their object probe, else the area
// probe covering them; ReflectionProbes::Select's rules without the parallax march, which a
// capture's small texels do not need), from the probes as they were before this frame's
// captures. Mirrors facing each other converge over a few frames.

// GpuReflectionProbeRecord (ScreenSpaceComposite.slang's ReflectionProbeRecord).
struct CaptureProbeRecord
{
	float4 PositionRadius; // xyz: capture position; w: influence radius.
	float4 Params;		   // x: blend distance; y: slot; z: age (s); w: owner ObjectId + 1 (0: area probe).
};

// The texel's radiance with its specular IBL replaced by its probe's reflection. `world`: the
// surface point; `eye`: the capture position it was seen from.
float3 CaptureReflections(float3 radiance, int2 texel, float3 eye, float3 world, uint probeCount, uint probeMips, Texture2D<float4> normals,
	Texture2D<float4> reflectances, Texture2D<float4> speculars, Texture2D<float> objectIds, TextureCubeArray<float4> cubes,
	StructuredBuffer<CaptureProbeRecord> records, SamplerState probeSampler)
{
	if (probeCount == 0u)
	{
		return radiance;
	}
	const int3 at = int3(texel, 0);
	const float3 reflectance = reflectances.Load(at).rgb;
	if (!(dot(reflectance, float3(0.2126, 0.7152, 0.0722)) > 0.02))
	{
		return radiance;
	}
	const float4 encoded = normals.Load(at);
	if (!(dot(encoded.xyz, encoded.xyz) > 1.0e-6))
	{
		return radiance;
	}
	const float object = objectIds.Load(at);
	int chosen = -1;
	float coverage = 0.0;
	for (uint i = 0; i < probeCount; ++i)
	{
		const CaptureProbeRecord record = records[i];
		const float owner = record.Params.w;
		if (owner != 0.0)
		{
			if (owner == object)
			{
				chosen = int(i);
				coverage = 1.0;
				break;
			}
			continue;
		}
		const float d = length(world - record.PositionRadius.xyz);
		const float weight = saturate((record.PositionRadius.w - d) / max(record.Params.x, 1.0e-3));
		if (weight > coverage)
		{
			coverage = weight;
			chosen = int(i);
		}
	}
	if (chosen < 0)
	{
		return radiance;
	}
	const float3 r = reflect(normalize(world - eye), normalize(encoded.xyz));
	const float lod = saturate(encoded.w) * float(max(probeMips, 1u) - 1u);
	const float3 probe = max(cubes.SampleLevel(probeSampler, float4(r, records[chosen].Params.y), lod).rgb, float3(0.0, 0.0, 0.0));
	return max(radiance + coverage * (reflectance * probe - speculars.Load(at).rgb), float3(0.0, 0.0, 0.0));
}
