// Clustered Forward+ records and helpers (items 66-67). Mirrors
// Engine/Systems/Renderer/ForwardPlus/ForwardPlusRecords.h and ForwardPlusReference.cpp
// line for line; ShaderCompiler.ForwardPlusLayout compares the reflected layouts.
#ifndef SWIM_FORWARD_PLUS_RECORDS_SLANG
#define SWIM_FORWARD_PLUS_RECORDS_SLANG
#include "Features/Glint.slang"
#include "Materials/StandardMaterialParameters.slang"

static const uint ForwardViewFlagEnvironment = 1u << 0;
static const uint ForwardViewFlagShadows = 1u << 1;
static const uint ForwardViewFlagBrdfLut = 1u << 2;
static const uint ForwardViewFlagAlphaToCoverage = 1u << 3;
static const uint ForwardDebugNone = 0;
static const uint ForwardDebugClusterHeatmap = 1;

struct ForwardView
{
	float4 ViewProjection[4]; // Row-major rows.
	float3 CameraPosition;
	float EnvironmentIntensity;
	float3 CameraForward;
	float EnvironmentRotation;
	float3 Ambient;
	float MinimumLight; // Full-bright floor (RenderSettings::MinimumLight).
	uint MaterialCount;
	uint PrefilteredMipCount;
	uint Flags;
	uint DebugMode;
	float4 PreviousViewProjection[4]; // Previous frame, unjittered (motion vectors, item 75).
	// xy: the jitter, an NDC offset added to clip.xy / clip.w. zw: RenderObjectFlags::Overlay
	// objects' clip.xy scale (tan(camera fov / 2) / tan(overlay fov / 2)) and near plane.
	float4 JitterOverlay;
	GlintParams Glint; // Materials with GlintIntensity > 0 (RenderSettings::Glint).
};

struct ForwardSortEntry
{
	float Depth;
	uint InstanceRow;
	uint SubmeshRow;
	uint Slot;
};

// Stride of StandardVertex in floats: Position 3, Normal 3, Tangent 4, TexCoord0 2.
static const uint ForwardVertexFloats = 12;

float4 ForwardProject(float4 rows[4], float4 p)
{
	return float4(dot(rows[0], p), dot(rows[1], p), dot(rows[2], p), dot(rows[3], p));
}

// A first-person overlay object's clip position from its scene clip position: the overlay's
// own field of view (clip.xy scaled) and near plane, its depth (near / w, reverse-Z)
// squeezed into [Overlay.y, 1] so it lies in front of everything in the scene.
// The depth (reverse-Z) the overlay's range starts at: the scene is farther than this
// everywhere except within 1% of its near plane.
static const float ForwardOverlayDepth = 0.99;

// overlay: ForwardView::JitterOverlay.zw.
float4 ForwardOverlayClip(float2 overlay, float4 clip)
{
	const float w = max(clip.w, 1.0e-6);
	const float depth = ForwardOverlayDepth + (1.0 - ForwardOverlayDepth) * saturate(overlay.y / w);
	return float4(clip.xy * overlay.x, depth * w, w);
}

float ForwardDeterminant(float4 rows[3])
{
	const float3 c0 = float3(rows[0].x, rows[1].x, rows[2].x);
	const float3 c1 = float3(rows[0].y, rows[1].y, rows[2].y);
	const float3 c2 = float3(rows[0].z, rows[1].z, rows[2].z);
	return dot(c0, cross(c1, c2));
}

float3 ForwardTransformDirection(float4 rows[3], float3 d)
{
	return float3(dot(rows[0].xyz, d), dot(rows[1].xyz, d), dot(rows[2].xyz, d));
}

// Cofactor (det x inverse-transpose) times n, sign-corrected so mirrored transforms
// keep the normal on its side (ForwardPlus::TransformNormal).
float3 ForwardTransformNormal(float4 rows[3], float3 n)
{
	const float3 c0 = float3(rows[0].x, rows[1].x, rows[2].x);
	const float3 c1 = float3(rows[0].y, rows[1].y, rows[2].y);
	const float3 c2 = float3(rows[0].z, rows[1].z, rows[2].z);
	const float3 result = cross(c1, c2) * n.x + cross(c2, c0) * n.y + cross(c0, c1) * n.z;
	return ForwardDeterminant(rows) < 0.0 ? -result : result;
}

// Single-sided materials skip back faces (ForwardPlus::CullsFace).
bool ForwardCullsFace(StandardMaterialParameters material, bool frontFacing)
{
	return !frontFacing && (material.Flags & StandardMaterialFlagDoubleSided) == 0;
}

// ForwardPlus::SortsBefore: back to front, then instance row, then submesh row.
bool ForwardSortsBefore(ForwardSortEntry a, ForwardSortEntry b)
{
	if (a.Depth != b.Depth)
	{
		return a.Depth > b.Depth;
	}
	if (a.InstanceRow != b.InstanceRow)
	{
		return a.InstanceRow < b.InstanceRow;
	}
	return a.SubmeshRow < b.SubmeshRow;
}

#endif
