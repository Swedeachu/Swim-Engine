#include "Engine/Systems/Renderer/Resources/TextureArray.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

using namespace Swim;
using namespace Swim::Render;

SWIM_TEST("Render.TextureArray", "RejectsMoreLayersThanTheDeviceSupports")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	device.adapterInfo.Capabilities.MaxTextureArrayLayers = 4;
	TextureArrayDesc desc;
	desc.Width = 4;
	desc.Height = 4;
	desc.Layers = 5;
	desc.DebugName = "too deep";
	SWIM_CHECK_THROWS(TextureArray(device, desc), std::invalid_argument);
	SWIM_CHECK_EQUAL(device.TextureCreateCount, 0u);
	desc.Layers = 4;
	TextureArray fits(device, desc);
	SWIM_CHECK_EQUAL(device.TextureCreateCount, 1u);
}
