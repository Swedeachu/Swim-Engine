// PBR image-regression gallery (critical-path item 62): analytic sphere impostors
// in an orthographic view, shaded by StandardPbrShadeResolved with one directional
// light and image-based lighting (EnvironmentLookup). Each instance is a
// screen-space quad around one sphere; the fragment derives the exact sphere normal
// from its pixel center, so the CPU reference (the native gallery smoke) computes
// the identical inputs for every pixel. Linear HDR output.
#include "Environment/EnvironmentLighting.slang"

struct GalleryView
{
	float4 LightDirection; // xyz: toward the light.
	float4 LightRadiance;
	float Intensity; // Environment intensity.
	float Rotation;	 // Environment rotation around +Y (radians).
	uint PrefilteredMipCount;
	uint Width;
	uint Height;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

struct GallerySphere
{
	float4 CenterRadius;	  // Pixel-space center (x right, y down) and radius in pixels.
	float4 BaseColorMetallic; // Linear base color, metallic.
	float4 Roughness;		  // x: perceptual roughness; y: occlusion.
};

[[vk::binding(0, 0)]] StructuredBuffer<GallerySphere> Spheres;
[[vk::binding(1, 0)]] StructuredBuffer<GalleryView> Views;
[[vk::binding(2, 0)]] StructuredBuffer<float4> Irradiance;
[[vk::binding(3, 0)]] TextureCube<float4> Prefiltered;
[[vk::binding(4, 0)]] Texture2D<float4> BrdfLut;
[[vk::binding(5, 0)]] SamplerState LinearClamp;

struct VertexOutput
{
	float4 Position : SV_Position;
	nointerpolation uint Sphere : SPHERE;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
	const float2 corners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1) };
	const GalleryView view = Views[0];
	const GallerySphere sphere = Spheres[instance];
	// One pixel of margin so every covered pixel center is rasterized.
	const float2 pixel = sphere.CenterRadius.xy + corners[vertexId] * (sphere.CenterRadius.z + 1.0);
	VertexOutput output;
	// +Y-up NDC; the viewport flip maps NDC y = +1 to row 0.
	output.Position = float4(pixel.x / float(view.Width) * 2.0 - 1.0, 1.0 - pixel.y / float(view.Height) * 2.0, 0.5, 1.0);
	output.Sphere = instance;
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	const GalleryView view = Views[0];
	const GallerySphere sphere = Spheres[input.Sphere];
	const float2 offset = float2(input.Position.x - sphere.CenterRadius.x, sphere.CenterRadius.y - input.Position.y) / sphere.CenterRadius.z;
	const float r2 = dot(offset, offset);
	if (r2 >= 1.0)
	{
		discard;
	}
	StandardPbrSurface surface;
	surface.Normal = normalize(float3(offset, sqrt(1.0 - r2)));
	surface.BaseColor = sphere.BaseColorMetallic.rgb;
	surface.Metallic = sphere.BaseColorMetallic.a;
	surface.PerceptualRoughness = sphere.Roughness.x;
	surface.Occlusion = sphere.Roughness.y;
	surface.Emissive = float3(0.0, 0.0, 0.0);
	surface.Alpha = 1.0;
	surface.Discard = false;
	StandardPbrLighting lighting;
	lighting.View = float3(0.0, 0.0, 1.0);
	lighting.LightDirection = view.LightDirection.xyz;
	lighting.LightRadiance = view.LightRadiance.rgb;
	lighting.Ambient = float3(0.0, 0.0, 0.0);
	const StandardPbrEnvironment environment = EnvironmentLookup(Irradiance, Prefiltered, view.PrefilteredMipCount, BrdfLut, LinearClamp,
		view.Intensity, view.Rotation, surface, lighting.View);
	return StandardPbrShadeResolved(surface, lighting, true, environment);
}
