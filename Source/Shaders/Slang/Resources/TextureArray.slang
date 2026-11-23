#pragma once

// Sampling for runtime texture arrays (Swim::Render::TextureArray). A layer whose new image
// is still arriving has only its smaller mips; minLod (TextureArray::GetMinLod, or the
// ImportMinLod buffer indexed by layer) is the finest one that is there. The gradients are
// scaled up so the hardware never picks a finer mip, without the MinLod image operand.

float TextureArrayLod(Texture2DArray texture, float2 dx, float2 dy)
{
	uint width;
	uint height;
	uint elements;
	uint levels;
	texture.GetDimensions(0, width, height, elements, levels);
	const float2 size = float2(width, height);
	const float rho = max(length(dx * size), length(dy * size));
	return log2(max(rho, 1e-8));
}

// Fragment stages only (screen-space derivatives).
float4 TextureArraySample(Texture2DArray texture, SamplerState state, float2 uv, float layer, float minLod)
{
	const float2 dx = ddx(uv);
	const float2 dy = ddy(uv);
	const float scale = exp2(max(minLod - TextureArrayLod(texture, dx, dy), 0.0));
	return texture.SampleGrad(state, float3(uv, layer), dx * scale, dy * scale);
}

float4 TextureArraySampleLevel(Texture2DArray texture, SamplerState state, float2 uv, float layer, float lod, float minLod)
{
	return texture.SampleLevel(state, float3(uv, layer), max(lod, minLod));
}
