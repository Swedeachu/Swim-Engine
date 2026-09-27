#include "Tools/AssetCompiler/TextureBlockEncoder.h"

#include <encoder/basisu_enc.h>
#include <encoder/basisu_gpu_texture.h>
#include <encoder/basisu_uastc_enc.h>
#include <transcoder/basisu_transcoder.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace Swim::AssetCompiler
{
	namespace
	{
		void InitializeBasis()
		{
			static std::once_flag initialized;
			std::call_once(initialized,
				[]
				{
					basisu::basisu_encoder_init();
					basist::basisu_transcoder_init();
				});
		}
	} // namespace

	std::vector<std::byte> EncodeRgba8ToBc7(std::span<const std::byte> rgba, std::uint32_t width, std::uint32_t height)
	{
		if (!width || !height || rgba.size() != std::size_t(width) * height * 4u)
		{
			throw std::invalid_argument("BC7 encode needs a tightly packed RGBA8 image");
		}
		InitializeBasis();
		const std::uint32_t columns = (width + 3) / 4;
		const std::uint32_t rows = (height + 3) / 4;
		std::vector<std::byte> blocks(static_cast<std::size_t>(GetBc7Bytes(width, height)));

		std::atomic<std::uint32_t> nextRow{ 0 };
		std::atomic<bool> failed{ false };
		const auto work = [&]
		{
			for (std::uint32_t row = nextRow++; row < rows; row = nextRow++)
			{
				for (std::uint32_t column = 0; column < columns; ++column)
				{
					std::uint8_t pixels[16 * 4];
					for (std::uint32_t y = 0; y < 4; ++y)
					{
						const std::uint32_t sy = std::min(row * 4 + y, height - 1);
						for (std::uint32_t x = 0; x < 4; ++x)
						{
							const std::uint32_t sx = std::min(column * 4 + x, width - 1);
							std::memcpy(pixels + (y * 4 + x) * 4, rgba.data() + (std::size_t(sy) * width + sx) * 4, 4);
						}
					}
					basist::uastc_block uastc;
					basisu::encode_uastc(pixels, uastc, basisu::cPackUASTCLevelFaster);
					if (!basist::transcode_uastc_to_bc7(uastc, blocks.data() + (std::size_t(row) * columns + column) * 16))
					{
						failed = true;
					}
				}
			}
		};
		const std::uint32_t threads = std::clamp<std::uint32_t>(std::thread::hardware_concurrency(), 1u, std::min(rows, 32u));
		std::vector<std::thread> pool;
		for (std::uint32_t i = 1; i < threads; ++i)
		{
			pool.emplace_back(work);
		}
		work();
		for (auto& thread : pool)
		{
			thread.join();
		}
		if (failed)
		{
			throw std::runtime_error("UASTC -> BC7 transcoding failed");
		}
		return blocks;
	}

	std::vector<std::byte> DecodeBc7ToRgba8(std::span<const std::byte> blocks, std::uint32_t width, std::uint32_t height)
	{
		if (blocks.size() != GetBc7Bytes(width, height))
		{
			throw std::invalid_argument("BC7 decode needs whole blocks for the extent");
		}
		InitializeBasis();
		const std::uint32_t columns = (width + 3) / 4;
		std::vector<std::byte> rgba(std::size_t(width) * height * 4u);
		for (std::uint32_t by = 0; by < (height + 3) / 4; ++by)
		{
			for (std::uint32_t bx = 0; bx < columns; ++bx)
			{
				basisu::color_rgba texels[16];
				basisu::unpack_bc7(blocks.data() + (std::size_t(by) * columns + bx) * 16, texels);
				for (std::uint32_t i = 0; i < 16; ++i)
				{
					const std::uint32_t x = bx * 4 + (i & 3);
					const std::uint32_t y = by * 4 + (i >> 2);
					if (x < width && y < height)
					{
						std::memcpy(rgba.data() + (std::size_t(y) * width + x) * 4, &texels[i], 4);
					}
				}
			}
		}
		return rgba;
	}
} // namespace Swim::AssetCompiler
