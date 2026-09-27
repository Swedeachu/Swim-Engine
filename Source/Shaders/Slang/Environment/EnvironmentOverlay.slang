// Environment overlay (item 61 extension): folds a feature's contribution into one face
// of the source cube's mip 0 before the mips and the prefilter are built, as
// cube = cube * overlay.a + overlay.rgb (premultiplied, alpha = transmittance). The
// overlay is a Size x 6 * Size atlas with face f in rows f * Size..; volumetric clouds
// write one (VolumetricCloudsEnvironment) so reflections and ambient light see them.
// Mirrors Environment::ApplyOverlay; bindings follow EnvironmentOverlayBindings.
struct OverlayConstants
{
	uint Size; // Face size.
	uint Face;
	uint Reserved0;
	uint Reserved1;
};

[[vk::push_constant]] ConstantBuffer<OverlayConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Overlay;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Destination;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size || id.y >= Constants.Size)
	{
		return;
	}
	const float4 overlay = Overlay.Load(int3(id.x, Constants.Face * Constants.Size + id.y, 0));
	const float4 sky = Destination[id.xy];
	Destination[id.xy] = float4(sky.rgb * saturate(overlay.a) + overlay.rgb, sky.a);
}
