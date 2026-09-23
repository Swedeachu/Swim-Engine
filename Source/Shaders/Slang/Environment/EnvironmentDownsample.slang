// Cube mip generation (item 61): each destination texel is the 2x2 box average of
// the same face one mip up. Mirrors Environment::CubeImage::GenerateMips; bindings
// follow EnvironmentDownsampleBindings.
struct DownsampleConstants
{
	uint Size; // Destination face size.
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<DownsampleConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Source;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Destination;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size || id.y >= Constants.Size)
	{
		return;
	}
	const int2 base = int2(id.xy) * 2;
	float4 sum = float4(0.0, 0.0, 0.0, 0.0);
	for (int dy = 0; dy < 2; ++dy)
	{
		for (int dx = 0; dx < 2; ++dx)
		{
			sum += Source.Load(int3(base.x + dx, base.y + dy, 0));
		}
	}
	Destination[id.xy] = sum * 0.25;
}
