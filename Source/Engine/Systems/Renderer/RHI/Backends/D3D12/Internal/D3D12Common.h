#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Swim::RhiD3D12
{

	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	struct D3D12DeviceState;

	// Throws std::runtime_error naming the call when the result is a failure. Device removal is
	// recorded in the device's diagnostics first (when a state is given) and raised as
	// Rhi::DeviceLostError.
	void Check(HRESULT result, std::string_view what, const D3D12DeviceState* state = nullptr);

	std::wstring Widen(std::string_view text);
	std::string Narrow(std::wstring_view text);

	// Debug names show in the debug layer's messages and in GPU tools.
	void SetDebugName(ID3D12Object* object, std::string_view name);

	DXGI_FORMAT ToDxgiFormat(Rhi::Format format);

	// The typeless family of a format, so depth textures can also be sampled and sRGB/linear
	// views can share one texture.
	DXGI_FORMAT ToTypelessFormat(Rhi::Format format);

	// The format a shader resource view of a depth/stencil texture reads (depth plane).
	DXGI_FORMAT ToDepthReadFormat(Rhi::Format format);

	D3D12_COMPARISON_FUNC ToComparison(Rhi::CompareOp op);

	// Bytes per texel, or per 4x4 block for block-compressed formats; zero for unsupported ones.
	std::uint32_t FormatBlockBytes(Rhi::Format format);
	bool IsBlockCompressed(Rhi::Format format);

	inline std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment)
	{
		return (value + alignment - 1) / alignment * alignment;
	}

	inline std::uint64_t AlignUp64(std::uint64_t value, std::uint64_t alignment)
	{
		return (value + alignment - 1) / alignment * alignment;
	}

} // namespace Swim::RhiD3D12
