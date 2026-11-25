// Outlines of highlighted objects (Engine::Outline), two programs from one source:
//  - OutlineMask (default): over the objects' screen rectangle, each pixel's view ray is
//    intersected with every object's triangles and boxes (in the object's local space). The
//    nearest hit writes the object (index + 1), whether it is in front of the scene's depth,
//    and its world normal (octahedral) to a rectangle-sized mask.
//  - OutlineComposite (OUTLINE_COMPOSITE=1): over the whole frame (display-referred), draws
//    an anti-aliased outer outline around each object's silhouette in the mask, crease lines
//    where an object's normal turns sharply, and the fresnel rim glow on its visible surface.
#include "Features/FeatureCommon.slang"

#ifndef OUTLINE_COMPOSITE
#define OUTLINE_COMPOSITE 0
#endif

struct OutlineFrame
{
	float4 Forward; // w: tan(half horizontal fov).
	float4 Right;	// w: tan(half vertical fov).
	float4 Up;
	float4 Camera; // xyz: world position.
	float4 ViewProjection[4]; // Rows, unjittered.
	uint4 Rect;	   // xy: first pixel; zw: size (the mask's).
	uint4 Size;	   // xy: frame size; z: search radius (pixels); w: object count.
};

struct OutlineObject
{
	float4 ToLocal[3]; // World to local, rows of a 3x4.
	float4 Color;	   // Display-referred rgb, a: opacity.
	float4 Rim;		   // rgb, a: strength.
	float4 Params;	   // x: width (pixels); y: rim power; z: hidden opacity; w: cosine of the crease angle (< -1: no creases).
	uint4 Range;	   // x: first triangle; y: triangle count; z: first box; w: box count.
};

[[vk::binding(0, 0)]] StructuredBuffer<OutlineFrame> Frame;
[[vk::binding(1, 0)]] StructuredBuffer<OutlineObject> Objects;

float2 OutlineOctEncode(float3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	float2 e = n.xy;
	if (n.z < 0.0)
	{
		e = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	}
	return e;
}

float3 OutlineOctDecode(float2 e)
{
	float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0)
	{
		n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	}
	return normalize(n);
}

#if !OUTLINE_COMPOSITE
[[vk::binding(2, 0)]] StructuredBuffer<float4> Triangles; // Three corners per triangle (xyz).
[[vk::binding(3, 0)]] StructuredBuffer<float4> Boxes;	  // Min, max per box (xyz).
[[vk::binding(4, 0)]] Texture2D<float> Depth;
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Mask;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const OutlineFrame frame = Frame[0];
	if (id.x >= frame.Rect.z || id.y >= frame.Rect.w)
	{
		return;
	}
	const uint2 pixel = frame.Rect.xy + id.xy;
	const float2 uv = (float2(pixel) + 0.5) / float2(frame.Size.xy);
	const float3 ray = FeatureViewRay(uv, frame.Forward, frame.Right, frame.Up);
	float nearest = 1.0e30;
	uint hit = 0;
	float3 normal = float3(0.0, 1.0, 0.0);
	for (uint o = 0; o < frame.Size.w; ++o)
	{
		const OutlineObject object = Objects[o];
		// The ray in local space: affine, so the ray parameter (world distance) is unchanged.
		const float3 origin = float3(dot(object.ToLocal[0], float4(frame.Camera.xyz, 1.0)), dot(object.ToLocal[1], float4(frame.Camera.xyz, 1.0)),
			dot(object.ToLocal[2], float4(frame.Camera.xyz, 1.0)));
		const float3 direction = float3(dot(object.ToLocal[0].xyz, ray), dot(object.ToLocal[1].xyz, ray), dot(object.ToLocal[2].xyz, ray));
		float best = nearest;
		float3 local = float3(0.0, 0.0, 0.0);
		for (uint t = 0; t < object.Range.y; ++t)
		{
			// Moller-Trumbore, both faces.
			const uint base = (object.Range.x + t) * 3u;
			const float3 a = Triangles[base].xyz;
			const float3 e1 = Triangles[base + 1u].xyz - a;
			const float3 e2 = Triangles[base + 2u].xyz - a;
			const float3 p = cross(direction, e2);
			const float det = dot(e1, p);
			if (abs(det) < 1.0e-12)
			{
				continue;
			}
			const float inv = 1.0 / det;
			const float3 s = origin - a;
			const float u = dot(s, p) * inv;
			const float3 q = cross(s, e1);
			const float v = dot(direction, q) * inv;
			const float d = dot(e2, q) * inv;
			if (u >= 0.0 && v >= 0.0 && u + v <= 1.0 && d > 0.0 && d < best)
			{
				best = d;
				local = cross(e1, e2);
			}
		}
		for (uint b = 0; b < object.Range.w; ++b)
		{
			const float3 lo = Boxes[(object.Range.z + b) * 2u].xyz;
			const float3 hi = Boxes[(object.Range.z + b) * 2u + 1u].xyz;
			const float3 inv = 1.0 / (abs(direction) < 1.0e-12 ? float3(1.0e-12, 1.0e-12, 1.0e-12) * sign(direction + 1.0e-30) : direction);
			const float3 t0 = (lo - origin) * inv;
			const float3 t1 = (hi - origin) * inv;
			const float3 near3 = min(t0, t1);
			const float3 far3 = max(t0, t1);
			const float enter = max(max(near3.x, near3.y), near3.z);
			const float exit = min(min(far3.x, far3.y), far3.z);
			// From inside a box the ray hits its far face.
			const float d = enter > 0.0 ? enter : exit;
			if (exit >= max(enter, 0.0) && d > 0.0 && d < best)
			{
				best = d;
				const float3 faces = enter > 0.0 ? near3 : far3;
				const float f = enter > 0.0 ? enter : exit;
				local = faces.x == f ? float3(1, 0, 0) : faces.y == f ? float3(0, 1, 0) : float3(0, 0, 1);
			}
		}
		if (best < nearest)
		{
			nearest = best;
			hit = o + 1u;
			// Normals go to world space with the transpose of the world-to-local matrix.
			const float3 n = object.ToLocal[0].xyz * local.x + object.ToLocal[1].xyz * local.y + object.ToLocal[2].xyz * local.z;
			normal = normalize(n) * (dot(n, ray) > 0.0 ? -1.0 : 1.0);
		}
	}
	if (hit == 0u)
	{
		Mask[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
		return;
	}
	// In front of the scene (reverse-Z: larger is nearer), with a little slack so the
	// object's own surface in the scene does not hide it.
	const float4 world = float4(frame.Camera.xyz + ray * nearest, 1.0);
	const float clipZ = dot(frame.ViewProjection[2], world);
	const float clipW = dot(frame.ViewProjection[3], world);
	const float depth = clipZ / max(clipW, 1.0e-6);
	const float scene = Depth.Load(int3(pixel, 0));
	const bool visible = depth * 1.004 >= scene;
	Mask[id.xy] = float4(float(hit), visible ? 1.0 : 0.0, OutlineOctEncode(normal));
}
#else
[[vk::binding(2, 0)]] Texture2D<float4> Color;
[[vk::binding(3, 0)]] Texture2D<float4> Mask;
[[vk::binding(4, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Output;

float4 OutlineMaskAt(OutlineFrame frame, int2 pixel)
{
	const int2 local = pixel - int2(frame.Rect.xy);
	if (any(local < 0) || any(local >= int2(frame.Rect.zw)))
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	return Mask.Load(int3(local, 0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const OutlineFrame frame = Frame[0];
	if (id.x >= frame.Size.x || id.y >= frame.Size.y)
	{
		return;
	}
	float4 color = Color.Load(int3(id.xy, 0));
	const int radius = int(frame.Size.z);
	const int2 pixel = int2(id.xy);
	if (any(pixel < int2(frame.Rect.xy) - radius) || any(pixel >= int2(frame.Rect.xy + frame.Rect.zw) + radius))
	{
		Output[id.xy] = color;
		return;
	}
	const float4 own = OutlineMaskAt(frame, pixel);
	const uint ownObject = uint(own.x + 0.5);
	float alpha = 0.0;
	float3 line = float3(0.0, 0.0, 0.0);
	if (ownObject != 0u)
	{
		const OutlineObject object = Objects[ownObject - 1u];
		const float3 normal = OutlineOctDecode(own.zw);
		if (own.y > 0.5 && object.Rim.a > 0.0)
		{
			const float2 uv = (float2(pixel) + 0.5) / float2(frame.Size.xy);
			const float3 ray = FeatureViewRay(uv, frame.Forward, frame.Right, frame.Up);
			const float fresnel = pow(1.0 - saturate(abs(dot(normal, ray))), max(object.Params.y, 0.1));
			color.rgb += object.Rim.rgb * object.Rim.a * fresnel;
		}
		if (object.Params.w >= -1.0)
		{
			// Creases: a neighbour of the same object facing another way, both sides of the
			// fold together making a line of about the outline width.
			const int reach = max(1, int(object.Params.x * 0.5 + 0.5));
			for (int axis = 0; axis < 4; ++axis)
			{
				const int2 offset = axis == 0 ? int2(reach, 0) : axis == 1 ? int2(-reach, 0) : axis == 2 ? int2(0, reach) : int2(0, -reach);
				const float4 other = OutlineMaskAt(frame, pixel + offset);
				if (uint(other.x + 0.5) == ownObject && dot(OutlineOctDecode(other.zw), normal) < object.Params.w)
				{
					const float a = object.Color.a * (own.y > 0.5 ? 1.0 : object.Params.z);
					if (a > alpha)
					{
						alpha = a;
						line = object.Color.rgb;
					}
				}
			}
		}
	}
	// The outer outline: the nearest pixel of another object's silhouette within its width.
	for (int y = -radius; y <= radius; ++y)
	{
		for (int x = -radius; x <= radius; ++x)
		{
			const float4 other = OutlineMaskAt(frame, pixel + int2(x, y));
			const uint index = uint(other.x + 0.5);
			if (index == 0u || index == ownObject)
			{
				continue;
			}
			const OutlineObject object = Objects[index - 1u];
			const float coverage = saturate(object.Params.x + 1.0 - length(float2(x, y)));
			const float a = coverage * object.Color.a * (other.y > 0.5 ? 1.0 : object.Params.z);
			if (a > alpha)
			{
				alpha = a;
				line = object.Color.rgb;
			}
		}
	}
	color.rgb = lerp(color.rgb, line, alpha);
	Output[id.xy] = color;
}
#endif
