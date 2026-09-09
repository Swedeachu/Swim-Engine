struct DispatchSettings
{
	uint Count;
	uint Seed;
	uint Reserved0;
	uint Reserved1;
};

[[vk::push_constant]] ConstantBuffer<DispatchSettings> Dispatch;
[[vk::binding(2, 0)]] Texture2D<uint> UnsignedImages[2];
[[vk::binding(3, 0)]] Texture2D<float4> NormalizedImage;
[[vk::binding(0, 1)]] RWStructuredBuffer<uint4> Output;

[shader("compute")]
[numthreads(8, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID,
	[[vk::binding(5, 1)]] uniform Texture2D<int> SignedImage)
{
	if (id.x >= Dispatch.Count)
	{
		return;
	}
	// Integer texel loads have no sampler/filtering and retain all 32 bits.
	// The selected mip/layer belongs to the view; Load's mip is relative to it.
	int3 location = int3(id.x, 0, 0);
	Output[id.x] = uint4(UnsignedImages[0].Load(location) + Dispatch.Seed,
		UnsignedImages[1].Load(location), asuint(SignedImage.Load(location)),
		uint(NormalizedImage.Load(location).x * 255.0 + 0.5));
}
