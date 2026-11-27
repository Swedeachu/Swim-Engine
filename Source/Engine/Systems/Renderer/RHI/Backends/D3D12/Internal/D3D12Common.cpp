#include "Engine/Systems/Renderer/RHI/Backends/D3D12/Internal/D3D12Common.h"

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/Internal/D3D12DeviceState.h"

#include <cstdio>
#include <stdexcept>

namespace Swim::RhiD3D12
{

	void Check(HRESULT result, std::string_view what, const D3D12DeviceState* state)
	{
		if (SUCCEEDED(result))
		{
			return;
		}

		const bool removed = result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET || result == DXGI_ERROR_DEVICE_HUNG;

		if (removed && state)
		{
			state->RecordLoss(what, result);
			throw Rhi::DeviceLostError();
		}

		char code[16]{};
		std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
		throw std::runtime_error(std::string(what) + " failed (" + code + ")");
	}

	std::wstring Widen(std::string_view text)
	{
		if (text.empty())
		{
			return {};
		}

		const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring wide(static_cast<std::size_t>(length), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
		return wide;
	}

	std::string Narrow(std::wstring_view text)
	{
		if (text.empty())
		{
			return {};
		}

		const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		std::string narrow(static_cast<std::size_t>(length), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr, nullptr);
		return narrow;
	}

	void SetDebugName(ID3D12Object* object, std::string_view name)
	{
		if (object && !name.empty())
		{
			object->SetName(Widen(name).c_str());
		}
	}

	DXGI_FORMAT ToDxgiFormat(Rhi::Format format)
	{
		using F = Rhi::Format;

		switch (format)
		{
		case F::R8Unorm:
			return DXGI_FORMAT_R8_UNORM;
		case F::R8Snorm:
			return DXGI_FORMAT_R8_SNORM;
		case F::R8Uint:
			return DXGI_FORMAT_R8_UINT;
		case F::R8Sint:
			return DXGI_FORMAT_R8_SINT;
		case F::R16Unorm:
			return DXGI_FORMAT_R16_UNORM;
		case F::R16Snorm:
			return DXGI_FORMAT_R16_SNORM;
		case F::R16Uint:
			return DXGI_FORMAT_R16_UINT;
		case F::R16Sint:
			return DXGI_FORMAT_R16_SINT;
		case F::R16Float:
			return DXGI_FORMAT_R16_FLOAT;
		case F::R32Uint:
			return DXGI_FORMAT_R32_UINT;
		case F::R32Sint:
			return DXGI_FORMAT_R32_SINT;
		case F::R32Float:
			return DXGI_FORMAT_R32_FLOAT;
		case F::RG8Unorm:
			return DXGI_FORMAT_R8G8_UNORM;
		case F::RG8Snorm:
			return DXGI_FORMAT_R8G8_SNORM;
		case F::RG8Uint:
			return DXGI_FORMAT_R8G8_UINT;
		case F::RG8Sint:
			return DXGI_FORMAT_R8G8_SINT;
		case F::RG16Unorm:
			return DXGI_FORMAT_R16G16_UNORM;
		case F::RG16Snorm:
			return DXGI_FORMAT_R16G16_SNORM;
		case F::RG16Uint:
			return DXGI_FORMAT_R16G16_UINT;
		case F::RG16Sint:
			return DXGI_FORMAT_R16G16_SINT;
		case F::RG16Float:
			return DXGI_FORMAT_R16G16_FLOAT;
		case F::RG32Uint:
			return DXGI_FORMAT_R32G32_UINT;
		case F::RG32Sint:
			return DXGI_FORMAT_R32G32_SINT;
		case F::RG32Float:
			return DXGI_FORMAT_R32G32_FLOAT;
		case F::RGB32Uint:
			return DXGI_FORMAT_R32G32B32_UINT;
		case F::RGB32Sint:
			return DXGI_FORMAT_R32G32B32_SINT;
		case F::RGB32Float:
			return DXGI_FORMAT_R32G32B32_FLOAT;
		case F::RGBA8Unorm:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case F::RGBA8UnormSrgb:
			return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case F::RGBA8Snorm:
			return DXGI_FORMAT_R8G8B8A8_SNORM;
		case F::RGBA8Uint:
			return DXGI_FORMAT_R8G8B8A8_UINT;
		case F::RGBA8Sint:
			return DXGI_FORMAT_R8G8B8A8_SINT;
		case F::BGRA8Unorm:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case F::BGRA8UnormSrgb:
			return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case F::RGBA16Unorm:
			return DXGI_FORMAT_R16G16B16A16_UNORM;
		case F::RGBA16Snorm:
			return DXGI_FORMAT_R16G16B16A16_SNORM;
		case F::RGBA16Uint:
			return DXGI_FORMAT_R16G16B16A16_UINT;
		case F::RGBA16Sint:
			return DXGI_FORMAT_R16G16B16A16_SINT;
		case F::RGBA16Float:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case F::RGBA32Uint:
			return DXGI_FORMAT_R32G32B32A32_UINT;
		case F::RGBA32Sint:
			return DXGI_FORMAT_R32G32B32A32_SINT;
		case F::RGBA32Float:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case F::RGB10A2Unorm:
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		case F::RGB10A2Uint:
			return DXGI_FORMAT_R10G10B10A2_UINT;
		case F::R11G11B10Float:
			return DXGI_FORMAT_R11G11B10_FLOAT;
		case F::RGB9E5Float:
			return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
		case F::D16Unorm:
			return DXGI_FORMAT_D16_UNORM;
		case F::D24UnormS8Uint:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case F::D32Float:
			return DXGI_FORMAT_D32_FLOAT;
		case F::D32FloatS8Uint:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		case F::BC1RGBAUnorm:
			return DXGI_FORMAT_BC1_UNORM;
		case F::BC1RGBAUnormSrgb:
			return DXGI_FORMAT_BC1_UNORM_SRGB;
		case F::BC3Unorm:
			return DXGI_FORMAT_BC3_UNORM;
		case F::BC3UnormSrgb:
			return DXGI_FORMAT_BC3_UNORM_SRGB;
		case F::BC4Unorm:
			return DXGI_FORMAT_BC4_UNORM;
		case F::BC4Snorm:
			return DXGI_FORMAT_BC4_SNORM;
		case F::BC5Unorm:
			return DXGI_FORMAT_BC5_UNORM;
		case F::BC5Snorm:
			return DXGI_FORMAT_BC5_SNORM;
		case F::BC6HUfloat:
			return DXGI_FORMAT_BC6H_UF16;
		case F::BC6HSfloat:
			return DXGI_FORMAT_BC6H_SF16;
		case F::BC7Unorm:
			return DXGI_FORMAT_BC7_UNORM;
		case F::BC7UnormSrgb:
			return DXGI_FORMAT_BC7_UNORM_SRGB;
		// No ETC2/ASTC in D3D12; BGR10A2 has no DXGI equivalent.
		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	DXGI_FORMAT ToTypelessFormat(Rhi::Format format)
	{
		using F = Rhi::Format;

		switch (format)
		{
		case F::RGBA8Unorm:
		case F::RGBA8UnormSrgb:
			return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case F::BGRA8Unorm:
		case F::BGRA8UnormSrgb:
			return DXGI_FORMAT_B8G8R8A8_TYPELESS;
		case F::D16Unorm:
			return DXGI_FORMAT_R16_TYPELESS;
		case F::D24UnormS8Uint:
			return DXGI_FORMAT_R24G8_TYPELESS;
		case F::D32Float:
			return DXGI_FORMAT_R32_TYPELESS;
		case F::D32FloatS8Uint:
			return DXGI_FORMAT_R32G8X24_TYPELESS;
		case F::BC1RGBAUnorm:
		case F::BC1RGBAUnormSrgb:
			return DXGI_FORMAT_BC1_TYPELESS;
		case F::BC3Unorm:
		case F::BC3UnormSrgb:
			return DXGI_FORMAT_BC3_TYPELESS;
		case F::BC7Unorm:
		case F::BC7UnormSrgb:
			return DXGI_FORMAT_BC7_TYPELESS;
		default:
			return ToDxgiFormat(format);
		}
	}

	DXGI_FORMAT ToDepthReadFormat(Rhi::Format format)
	{
		switch (format)
		{
		case Rhi::Format::D16Unorm:
			return DXGI_FORMAT_R16_UNORM;
		case Rhi::Format::D24UnormS8Uint:
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case Rhi::Format::D32Float:
			return DXGI_FORMAT_R32_FLOAT;
		case Rhi::Format::D32FloatS8Uint:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		default:
			return ToDxgiFormat(format);
		}
	}

	bool IsBlockCompressed(Rhi::Format format)
	{
		return format >= Rhi::Format::BC1RGBAUnorm && format <= Rhi::Format::BC7UnormSrgb;
	}

	std::uint32_t FormatBlockBytes(Rhi::Format format)
	{
		using F = Rhi::Format;

		switch (format)
		{
		case F::R8Unorm:
		case F::R8Snorm:
		case F::R8Uint:
		case F::R8Sint:
			return 1;
		case F::R16Unorm:
		case F::R16Snorm:
		case F::R16Uint:
		case F::R16Sint:
		case F::R16Float:
		case F::RG8Unorm:
		case F::RG8Snorm:
		case F::RG8Uint:
		case F::RG8Sint:
		case F::D16Unorm:
			return 2;
		case F::RGB32Uint:
		case F::RGB32Sint:
		case F::RGB32Float:
			return 12;
		case F::RG32Uint:
		case F::RG32Sint:
		case F::RG32Float:
		case F::RGBA16Unorm:
		case F::RGBA16Snorm:
		case F::RGBA16Uint:
		case F::RGBA16Sint:
		case F::RGBA16Float:
		case F::D32FloatS8Uint:
		case F::BC1RGBAUnorm:
		case F::BC1RGBAUnormSrgb:
		case F::BC4Unorm:
		case F::BC4Snorm:
			return 8;
		case F::RGBA32Uint:
		case F::RGBA32Sint:
		case F::RGBA32Float:
		case F::BC3Unorm:
		case F::BC3UnormSrgb:
		case F::BC5Unorm:
		case F::BC5Snorm:
		case F::BC6HUfloat:
		case F::BC6HSfloat:
		case F::BC7Unorm:
		case F::BC7UnormSrgb:
			return 16;
		case F::Undefined:
			return 0;
		default:
			return ToDxgiFormat(format) == DXGI_FORMAT_UNKNOWN ? 0 : 4;
		}
	}

} // namespace Swim::RhiD3D12
