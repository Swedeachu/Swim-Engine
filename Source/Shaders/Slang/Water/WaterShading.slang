// Water surface shading for transparent feature draws (include after
// "ForwardPlus/ForwardFrame.slang"). The surface reads the opaque scene behind it
// (RenderFeatureDrawContext::SceneColor / SceneDepth, PrepareGeometry asks with SceneCopy)
// and writes its final colour opaquely (alpha 1):
//  - refraction: the scene seen through the surface, offset along the normal's slope and
//    by the water's thickness, never picking up what lies in front of the surface;
//  - Beer-Lambert absorption and single scattering along the path under the surface
//    (deep water and the horizon turn to the scattering colour);
//  - Fresnel from the index of refraction (Schlick, R0 = 0.02 at 1.33), with
//    screen-space reflection against the scene copy and the environment cube where the
//    reflected ray leaves the screen;
//  - GGX specular from every directional light, shadowed, its roughness widened by the
//    normal's screen-space variation so small waves never sparkle into single-pixel dots;
//  - caustics on the bed under shallow water and foam (shoreline by thickness, plus
//    any crest foam the caller computes).
// A surface seen from below (the camera under water) gets the transmitted term only.
#ifndef SWIM_WATER_SHADING_SLANG
#define SWIM_WATER_SHADING_SLANG
#include "ScreenSpace/DistanceFog.slang"
#include "Water/WaterCommon.slang"

struct WaterOptics
{
	float3 Absorption;		  // Per metre (rgb): clear sea water about (0.45, 0.06, 0.03).
	float3 Scattering;		  // Per metre (rgb): clear about (0.002, 0.004, 0.005); murky higher.
	float Ior;				  // 1.33.
	float Roughness;		  // Perceptual roughness of the calm surface (0.02 .. 0.1).
	float Reflection;		  // Scales reflection and specular (1 = physical; less under cover).
	float Refraction;		  // Screen-space bend (0 = none; 1 = default strength).
	float ReflectionSteps;	  // Screen-space reflection steps (0 = environment only; 16 typical).
	float CausticStrength;	  // 0 = none; 1 = default.
	float CausticScale;		  // Pattern frequency (per metre, about 0.5).
	float CausticDepth;		  // Caustics fade out below this depth (metres).
	float FoamDepth;		  // Shoreline foam where the water is thinner than this (metres; 0 = none).
	float3 FoamColor;		  // Albedo of the foam.
	float MaxDistance;		  // Path length used where nothing is behind the water (metres).
};

WaterOptics WaterDefaultOptics()
{
	WaterOptics o;
	o.Absorption = float3(0.45, 0.06, 0.03);
	o.Scattering = float3(0.002, 0.004, 0.005);
	o.Ior = 1.33;
	o.Roughness = 0.04;
	o.Reflection = 1.0;
	o.Refraction = 1.0;
	o.ReflectionSteps = 16.0;
	o.CausticStrength = 1.0;
	o.CausticScale = 0.5;
	o.CausticDepth = 6.0;
	o.FoamDepth = 0.35;
	o.FoamColor = float3(0.9, 0.92, 0.95);
	o.MaxDistance = 200.0;
	return o;
}

struct WaterSurface
{
	float3 WorldPosition;
	float3 Normal;	   // Shading normal (world, unit), waves and detail included.
	float3 Up;		   // The flat surface's normal (world, unit; usually +Y).
	float4 Position;   // SV_Position of the fragment (xy pixel, z reverse-Z depth).
	float Time;		   // Seconds, for caustics and foam drift.
	float Foam;		   // Crest foam from the caller (0..1).
};

struct WaterScene
{
	Texture2D<float4> Color;
	Texture2D<float> Depth;
	SamplerState Sampler; // Linear clamp.
	float2 Size;		  // Viewport in pixels.
};

float WaterFresnel(float cosTheta, float ior)
{
	const float r = (1.0 - ior) / (1.0 + ior);
	const float r0 = r * r;
	const float m = 1.0 - saturate(cosTheta);
	return r0 + (1.0 - r0) * m * m * m * m * m;
}

// The distance (metres) from the camera to what the scene depth at uv shows, along the ray
// through the surface fragment: with infinite reverse-Z, depth ~ 1 / w, so
// distance = distance to the surface x surfaceDepth / sceneDepth.
float WaterSceneDistance(float surfaceDistance, float surfaceDepth, float sceneDepth, float maxDistance)
{
	return sceneDepth > 0.0 ? surfaceDistance * surfaceDepth / sceneDepth : surfaceDistance + maxDistance;
}

float4 WaterProject(float3 world)
{
	return ForwardProject(Views[0].ViewProjection, float4(world, 1.0));
}

float2 WaterClipToUv(float4 clip)
{
	return clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
}

// Screen-space reflection: marches the reflected ray in world space with growing steps,
// projecting each point and comparing its depth with the scene copy, then refines the hit
// by bisection. Returns radiance and confidence (0 where the ray left the screen or hid).
float4 WaterTraceReflection(WaterScene scene, float3 origin, float3 direction, float surfaceDistance, uint steps)
{
	if (steps == 0u)
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	float stepLength = max(surfaceDistance * 0.04, 0.15);
	float travelled = stepLength;
	float previous = 0.0;
	[loop]
	for (uint i = 0u; i < steps; ++i)
	{
		const float3 p = origin + direction * travelled;
		const float4 clip = WaterProject(p);
		if (clip.w <= 0.0)
		{
			break;
		}
		const float2 uv = WaterClipToUv(clip);
		if (any(uv < 0.0) || any(uv > 1.0))
		{
			break;
		}
		const float rayDepth = clip.z / clip.w;
		const float sceneDepth = scene.Depth.SampleLevel(scene.Sampler, uv, 0.0);
		// Reverse-Z: the scene is in front of the ray point where its depth is larger.
		if (sceneDepth > rayDepth)
		{
			// Thickness test: a hit far behind the scene surface is the ray passing behind an object.
			const float rayW = clip.w;
			const float sceneW = rayW * rayDepth / max(sceneDepth, 1.0e-7);
			if (rayW - sceneW > stepLength * 2.0 + 0.5)
			{
				break;
			}
			float lo = previous;
			float hi = travelled;
			for (uint b = 0u; b < 5u; ++b)
			{
				const float mid = 0.5 * (lo + hi);
				const float4 c = WaterProject(origin + direction * mid);
				const float d = scene.Depth.SampleLevel(scene.Sampler, WaterClipToUv(c), 0.0);
				if (d > c.z / c.w)
				{
					hi = mid;
				}
				else
				{
					lo = mid;
				}
			}
			const float2 hitUv = WaterClipToUv(WaterProject(origin + direction * hi));
			const float2 edge = saturate(min(hitUv, 1.0 - hitUv) * 8.0);
			const float fade = edge.x * edge.y * saturate(1.0 - float(i) / float(steps));
			return float4(scene.Color.SampleLevel(scene.Sampler, hitUv, 0.0).rgb, fade);
		}
		previous = travelled;
		stepLength *= 1.35;
		travelled += stepLength;
	}
	return float4(0.0, 0.0, 0.0, 0.0);
}

float3 WaterEnvironment(float3 direction, float perceptualRoughness)
{
	const ForwardView view = Views[0];
	if ((view.Flags & ForwardViewFlagEnvironment) == 0)
	{
		return view.Ambient;
	}
	return EnvironmentPrefiltered.SampleLevel(EnvironmentSampler, EnvironmentRotateLookup(direction, view.EnvironmentRotation),
		EnvironmentPrefilterLodForRoughness(perceptualRoughness, view.PrefilteredMipCount)).rgb * view.EnvironmentIntensity;
}

float3 WaterSkyIrradiance(float3 normal)
{
	const ForwardView view = Views[0];
	if ((view.Flags & ForwardViewFlagEnvironment) == 0)
	{
		return view.Ambient;
	}
	return EnvironmentEvaluateIrradiance(EnvironmentIrradiance, EnvironmentRotateLookup(normal, view.EnvironmentRotation)) *
		view.EnvironmentIntensity + view.Ambient;
}

// GGX (isotropic) specular of one light: D * G * F / (4 N.V) * N.L.
float WaterGgx(float3 n, float3 v, float3 l, float alpha, float ior)
{
	const float3 h = normalize(v + l);
	const float nDotL = saturate(dot(n, l));
	const float nDotV = max(dot(n, v), 1.0e-4);
	const float nDotH = saturate(dot(n, h));
	const float a2 = alpha * alpha;
	const float d = a2 / (3.14159265 * pow(nDotH * nDotH * (a2 - 1.0) + 1.0, 2.0));
	const float k = alpha * 0.5;
	const float g = nDotL / (nDotL * (1.0 - k) + k) * nDotV / (nDotV * (1.0 - k) + k);
	return d * g * WaterFresnel(saturate(dot(v, h)), ior) / (4.0 * nDotV);
}

// Caustics on the bed, the pattern projected along the light as it falls through the surface.
float WaterBedCaustics(float3 bed, float3 toLight, float time, float scale)
{
	return WaterCaustics((bed.xz - toLight.xz / max(toLight.y, 0.2) * bed.y) * scale, time);
}

// The shaded water colour of one fragment (opaque: write it with alpha 1).
float3 WaterShade(WaterSurface s, WaterOptics o, WaterScene scene)
{
	const ForwardView view = Views[0];
	const float3 toCamera = view.CameraPosition - s.WorldPosition;
	const float surfaceDistance = length(toCamera);
	const float3 v = toCamera / max(surfaceDistance, 1.0e-4);
	const bool below = dot(v, s.Up) < 0.0;
	float3 n = normalize(s.Normal);
	if (below)
	{
		n = -n;
	}
	// Normal variation under the pixel widens the specular lobe (no sparkling dots).
	const float3 dn = abs(ddx(n)) + abs(ddy(n));
	const float variance = dot(dn, dn);
	const float roughness = clamp(sqrt(o.Roughness * o.Roughness + variance * 0.5), 0.02, 1.0);
	const float alpha = roughness * roughness;

	const float2 uv = s.Position.xy / scene.Size;
	const float surfaceDepth = s.Position.z;
	const float3 sigmaT = max(o.Absorption + o.Scattering, float3(1.0e-5, 1.0e-5, 1.0e-5));

	// Refraction: bend by the slope of the normal relative to the flat surface, more for
	// thicker water, and reject offsets that land on something in front of the surface.
	const float straightDepth = scene.Depth.SampleLevel(scene.Sampler, uv, 0.0);
	const float straightThickness = WaterSceneDistance(surfaceDistance, surfaceDepth, straightDepth, o.MaxDistance) - surfaceDistance;
	const float3 slope = n - s.Up * dot(n, s.Up);
	const float4 clipSlope = WaterProject(s.WorldPosition + slope * clamp(straightThickness, 0.0, 4.0) * 0.35 * o.Refraction);
	float2 refractedUv = clamp(WaterClipToUv(clipSlope), 0.0, 1.0);
	float sceneDepth = scene.Depth.SampleLevel(scene.Sampler, refractedUv, 0.0);
	if (sceneDepth > surfaceDepth)
	{
		refractedUv = uv;
		sceneDepth = straightDepth;
	}
	const float behind = WaterSceneDistance(surfaceDistance, surfaceDepth, sceneDepth, o.MaxDistance);
	const float thickness = max(behind - surfaceDistance, 0.0);
	float3 transmitted = scene.Color.SampleLevel(scene.Sampler, refractedUv, 0.0).rgb;

	// The bed under the water, lit through the surface: caustics and light lost on the way down.
	const GpuLightHeader header = LightHeader[0];
	const float3 rayDirection = -v;
	const float3 bed = s.WorldPosition + rayDirection * thickness;
	const float bedDepth = max(dot(s.WorldPosition - bed, s.Up), 0.0);
	float3 sunIrradiance = float3(0.0, 0.0, 0.0);
	float3 specular = float3(0.0, 0.0, 0.0);
	for (uint i = 0u; i < header.DirectionalCount; ++i)
	{
		const GpuLightRecord light = Lights[i];
		const GpuLightSample sample = GpuLightEvaluate(light, s.WorldPosition);
		const float shadow = ForwardLightShadow(view, light, s.WorldPosition, s.Up, sample.Direction);
		sunIrradiance += sample.Radiance * shadow * saturate(dot(s.Up, sample.Direction));
		if (!below)
		{
			specular += sample.Radiance * shadow * WaterGgx(n, v, sample.Direction, alpha, o.Ior);
		}
		if (i == 0u && o.CausticStrength > 0.0 && sceneDepth > 0.0 && !below)
		{
			const float fade = saturate(1.0 - bedDepth / max(o.CausticDepth, 1.0e-3)) * saturate(bedDepth * 4.0);
			const float caustic = WaterBedCaustics(bed, sample.Direction, s.Time, o.CausticScale);
			transmitted *= 1.0 + o.CausticStrength * fade * shadow * (caustic - 0.35) * saturate(sample.Direction.y * 3.0);
		}
	}

	// Beer-Lambert along the path under the surface (and the light's way down to the bed
	// for what is seen there), plus single scattering of sky and sun light.
	const float3 transmittance = exp(-sigmaT * thickness);
	const float3 albedo = o.Scattering / sigmaT;
	const float3 lightIn = WaterSkyIrradiance(s.Up) + sunIrradiance * 0.25;
	const float3 inscatter = albedo * lightIn * (1.0 - transmittance);
	transmitted = transmitted * transmittance * exp(-sigmaT * bedDepth * 0.5) + inscatter;

	if (below)
	{
		return transmitted;
	}

	// Reflection: the scene where the screen has it, the environment elsewhere.
	const float3 r = reflect(-v, n);
	const float3 reflectedDirection = dot(r, s.Up) < 0.02 ? normalize(r - s.Up * (dot(r, s.Up) - 0.02)) : r;
	float3 reflected = WaterEnvironment(reflectedDirection, roughness);
	const float4 traced = WaterTraceReflection(scene, s.WorldPosition + s.Up * 0.02, reflectedDirection, surfaceDistance, uint(o.ReflectionSteps));
	reflected = lerp(reflected, traced.rgb, traced.a);
	const float fresnel = WaterFresnel(saturate(dot(n, v)), o.Ior) * saturate(o.Reflection);
	float3 color = lerp(transmitted, reflected, fresnel) + specular * max(o.Reflection, 0.0);

	// Foam: a ragged band along the shore plus the caller's crest foam, lit diffusely.
	float foam = s.Foam;
	if (o.FoamDepth > 0.0 && sceneDepth > 0.0)
	{
		const float2 fp = s.WorldPosition.xz * 2.3 + float2(s.Time * 0.05, -s.Time * 0.04);
		const float breakup = 1.0 - WaterCausticLayer(fp, s.Time * 0.3) * 0.6 - WaterCausticLayer(fp * 2.7 + 3.1, s.Time * 0.2) * 0.4;
		const float shore = saturate(1.0 - thickness / o.FoamDepth);
		foam = max(foam, saturate(shore * 1.4 - (1.0 - breakup) * 0.8));
	}
	if (foam > 0.0)
	{
		const float3 foamLight = WaterSkyIrradiance(s.Up) + sunIrradiance / 3.14159265;
		color = lerp(color, o.FoamColor * foamLight, saturate(foam));
	}
	return color;
}

// WaterShade fogged at the surface's own distance with the curve the surrounding ground uses,
// for scenes that fog in their own shaders (the scene copy behind is already fogged). Set
// fog.ColorOpacity.rgb per pixel for a sky-coloured fog.
float3 WaterShade(WaterSurface s, WaterOptics o, WaterScene scene, DistanceFog fog)
{
	return DistanceFogApply(fog, WaterShade(s, o, scene), length(Views[0].CameraPosition - s.WorldPosition));
}

#endif
