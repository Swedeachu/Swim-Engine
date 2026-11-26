#include "Engine/Systems/Tooling/ImageEncoding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace Swim::Tooling
{

	namespace
	{

		class BitWriter
		{

		  public:
			std::vector<std::uint8_t> Bytes;

			void Bits(std::uint32_t value, std::uint32_t count)
			{
				buffer |= static_cast<std::uint64_t>(value) << used;
				used += count;

				while (used >= 8)
				{
					Bytes.push_back(static_cast<std::uint8_t>(buffer));
					buffer >>= 8;
					used -= 8;
				}
			}

			// Huffman codes go most significant bit first.
			void Code(std::uint32_t code, std::uint32_t length)
			{
				std::uint32_t reversed = 0;

				for (std::uint32_t i = 0; i < length; ++i)
				{
					reversed |= ((code >> i) & 1u) << (length - 1 - i);
				}

				Bits(reversed, length);
			}

			void Flush()
			{
				if (used > 0)
				{
					Bytes.push_back(static_cast<std::uint8_t>(buffer));
				}

				buffer = 0;
				used = 0;
			}

		  private:
			std::uint64_t buffer = 0;
			std::uint32_t used = 0;
		};

		void Literal(BitWriter& out, std::uint32_t symbol)
		{
			if (symbol < 144)
			{
				out.Code(0x30 + symbol, 8);
			}
			else if (symbol < 256)
			{
				out.Code(0x190 + symbol - 144, 9);
			}
			else if (symbol < 280)
			{
				out.Code(symbol - 256, 7);
			}
			else
			{
				out.Code(0xC0 + symbol - 280, 8);
			}
		}

		constexpr std::array<std::uint16_t, 29> LengthBase{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
			99, 115, 131, 163, 195, 227, 258 };
		constexpr std::array<std::uint8_t, 29> LengthExtra{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5,
			5, 0 };
		constexpr std::array<std::uint16_t, 30> DistanceBase{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
			1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
		constexpr std::array<std::uint8_t, 30> DistanceExtra{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11,
			11, 12, 12, 13, 13 };

		void Match(BitWriter& out, std::uint32_t length, std::uint32_t distance)
		{
			std::size_t l = LengthBase.size() - 1;

			while (LengthBase[l] > length)
			{
				--l;
			}

			Literal(out, 257 + static_cast<std::uint32_t>(l));
			out.Bits(length - LengthBase[l], LengthExtra[l]);
			std::size_t d = DistanceBase.size() - 1;

			while (DistanceBase[d] > distance)
			{
				--d;
			}

			out.Code(static_cast<std::uint32_t>(d), 5);
			out.Bits(distance - DistanceBase[d], DistanceExtra[d]);
		}

		// One fixed-Huffman block with hash-chain LZ77 matches.
		std::vector<std::uint8_t> Deflate(std::span<const std::uint8_t> data)
		{
			constexpr std::uint32_t Window = 32768;
			constexpr std::uint32_t HashBits = 15;
			constexpr std::uint32_t MaxChain = 48;
			constexpr std::uint32_t MinMatch = 3;
			constexpr std::uint32_t MaxMatch = 258;
			BitWriter out;
			out.Bits(1, 1); // Final block.
			out.Bits(1, 2); // Fixed Huffman.
			std::vector<std::int32_t> head(std::size_t(1) << HashBits, -1);
			std::vector<std::int32_t> previous(Window, -1);
			const std::size_t n = data.size();
			const auto hash = [&](std::size_t i)
			{
				const std::uint32_t v = data[i] | (data[i + 1] << 8) | (data[i + 2] << 16);
				return (v * 2654435761u) >> (32 - HashBits);
			};
			const auto insert = [&](std::size_t i)
			{
				if (i + MinMatch <= n)
				{
					const std::uint32_t h = hash(i);
					previous[i % Window] = head[h];
					head[h] = static_cast<std::int32_t>(i);
				}
			};

			std::size_t i = 0;

			while (i < n)
			{
				std::uint32_t bestLength = 0;
				std::uint32_t bestDistance = 0;

				if (i + MinMatch <= n)
				{
					std::int32_t candidate = head[hash(i)];
					const std::uint32_t limit = static_cast<std::uint32_t>(std::min<std::size_t>(MaxMatch, n - i));

					for (std::uint32_t chain = 0; candidate >= 0 && chain < MaxChain; ++chain)
					{
						const std::size_t c = static_cast<std::size_t>(candidate);

						if (i - c > Window - 1)
						{
							break;
						}

						std::uint32_t length = 0;

						while (length < limit && data[c + length] == data[i + length])
						{
							++length;
						}

						if (length > bestLength)
						{
							bestLength = length;
							bestDistance = static_cast<std::uint32_t>(i - c);

							if (length == limit)
							{
								break;
							}
						}

						const std::int32_t next = previous[c % Window];

						if (next >= candidate)
						{
							break;
						}

						candidate = next;
					}
				}

				if (bestLength >= MinMatch)
				{
					Match(out, bestLength, bestDistance);

					for (std::uint32_t k = 0; k < bestLength; ++k)
					{
						insert(i + k);
					}

					i += bestLength;
				}
				else
				{
					Literal(out, data[i]);
					insert(i);
					++i;
				}
			}

			Literal(out, 256);
			out.Flush();
			return std::move(out.Bytes);
		}

		std::uint32_t Crc32(std::span<const std::uint8_t> bytes, std::uint32_t crc = 0)
		{
			static const auto table = []
			{
				std::array<std::uint32_t, 256> t{};

				for (std::uint32_t n = 0; n < 256; ++n)
				{
					std::uint32_t c = n;

					for (int k = 0; k < 8; ++k)
					{
						c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					}

					t[n] = c;
				}

				return t;
			}();

			crc = ~crc;

			for (const std::uint8_t b : bytes)
			{
				crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
			}

			return ~crc;
		}

		std::uint32_t Adler32(std::span<const std::uint8_t> bytes)
		{
			std::uint32_t a = 1;
			std::uint32_t b = 0;

			for (const std::uint8_t v : bytes)
			{
				a = (a + v) % 65521u;
				b = (b + a) % 65521u;
			}

			return (b << 16) | a;
		}

		void PutU32(std::vector<std::uint8_t>& out, std::uint32_t v)
		{
			out.push_back(static_cast<std::uint8_t>(v >> 24));
			out.push_back(static_cast<std::uint8_t>(v >> 16));
			out.push_back(static_cast<std::uint8_t>(v >> 8));
			out.push_back(static_cast<std::uint8_t>(v));
		}

		void Chunk(std::vector<std::uint8_t>& out, const char* type, std::span<const std::uint8_t> data)
		{
			PutU32(out, static_cast<std::uint32_t>(data.size()));
			const std::size_t start = out.size();
			out.insert(out.end(), type, type + 4);
			out.insert(out.end(), data.begin(), data.end());
			PutU32(out, Crc32(std::span<const std::uint8_t>(out).subspan(start)));
		}

		std::uint8_t Paeth(int a, int b, int c)
		{
			const int p = a + b - c;
			const int pa = std::abs(p - a);
			const int pb = std::abs(p - b);
			const int pc = std::abs(p - c);
			return static_cast<std::uint8_t>(pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
		}

	} // namespace

	std::vector<std::uint8_t> EncodePng(const Image& image)
	{
		if (!image.IsValid())
		{
			return {};
		}

		// Each row takes the filter with the smallest sum of absolute (signed) residuals.
		const std::size_t stride = std::size_t(image.Width) * 4;
		std::vector<std::uint8_t> filtered;
		filtered.reserve((stride + 1) * image.Height);
		std::array<std::vector<std::uint8_t>, 5> candidates;

		for (auto& c : candidates)
		{
			c.resize(stride);
		}

		const std::vector<std::uint8_t> zero(stride, 0);

		for (std::uint32_t y = 0; y < image.Height; ++y)
		{
			const std::uint8_t* row = image.Rgba.data() + y * stride;
			const std::uint8_t* up = y ? row - stride : zero.data();
			std::size_t best = 0;
			std::uint64_t bestScore = UINT64_MAX;

			for (std::size_t f = 0; f < candidates.size(); ++f)
			{
				std::uint64_t score = 0;

				for (std::size_t x = 0; x < stride; ++x)
				{
					const int a = x >= 4 ? row[x - 4] : 0;
					const int b = up[x];
					const int c = x >= 4 ? up[x - 4] : 0;
					int predicted = 0;

					switch (f)
					{
					case 1:
						predicted = a;
						break;
					case 2:
						predicted = b;
						break;
					case 3:
						predicted = (a + b) / 2;
						break;
					case 4:
						predicted = Paeth(a, b, c);
						break;
					default:
						break;
					}

					const auto v = static_cast<std::uint8_t>(row[x] - predicted);
					candidates[f][x] = v;
					score += static_cast<std::uint64_t>(std::abs(static_cast<int>(static_cast<std::int8_t>(v))));
				}

				if (score < bestScore)
				{
					bestScore = score;
					best = f;
				}
			}

			filtered.push_back(static_cast<std::uint8_t>(best));
			filtered.insert(filtered.end(), candidates[best].begin(), candidates[best].end());
		}

		std::vector<std::uint8_t> zlib{ 0x78, 0x01 };
		const std::vector<std::uint8_t> deflated = Deflate(filtered);
		zlib.insert(zlib.end(), deflated.begin(), deflated.end());
		PutU32(zlib, Adler32(filtered));

		std::vector<std::uint8_t> png{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		std::vector<std::uint8_t> header;
		PutU32(header, image.Width);
		PutU32(header, image.Height);
		header.insert(header.end(), { 8, 6, 0, 0, 0 }); // 8-bit RGBA, deflate, adaptive filters, no interlace.
		Chunk(png, "IHDR", header);
		Chunk(png, "IDAT", zlib);
		Chunk(png, "IEND", {});
		return png;
	}

	bool WritePng(const std::filesystem::path& path, const Image& image)
	{
		const std::vector<std::uint8_t> png = EncodePng(image);

		if (png.empty())
		{
			return false;
		}

		if (path.has_parent_path())
		{
			std::error_code ignored;
			std::filesystem::create_directories(path.parent_path(), ignored);
		}

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
		return static_cast<bool>(file);
	}

	std::string EncodeBase64(std::span<const std::uint8_t> bytes)
	{
		static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve((bytes.size() + 2) / 3 * 4);

		for (std::size_t i = 0; i < bytes.size(); i += 3)
		{
			const std::uint32_t remaining = static_cast<std::uint32_t>(std::min<std::size_t>(3, bytes.size() - i));
			std::uint32_t v = std::uint32_t(bytes[i]) << 16;
			v |= remaining > 1 ? std::uint32_t(bytes[i + 1]) << 8 : 0u;
			v |= remaining > 2 ? std::uint32_t(bytes[i + 2]) : 0u;
			out.push_back(Alphabet[(v >> 18) & 63]);
			out.push_back(Alphabet[(v >> 12) & 63]);
			out.push_back(remaining > 1 ? Alphabet[(v >> 6) & 63] : '=');
			out.push_back(remaining > 2 ? Alphabet[v & 63] : '=');
		}

		return out;
	}

	Json ImageContent(std::span<const std::uint8_t> png)
	{
		Json j = Json::MakeObject();
		j["type"] = "image";
		j["data"] = EncodeBase64(png);
		j["mimeType"] = "image/png";
		return j;
	}

	Image TileImages(std::span<const Image> images, std::uint32_t cellWidth, std::uint32_t cellHeight, std::uint32_t columns,
		std::uint32_t gap, std::uint32_t background)
	{
		Image sheet;

		if (images.empty() || cellWidth == 0 || cellHeight == 0)
		{
			return sheet;
		}

		const auto count = static_cast<std::uint32_t>(images.size());

		if (columns == 0)
		{
			columns = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(count))));
		}

		columns = std::clamp(columns, 1u, count);
		const std::uint32_t rows = (count + columns - 1) / columns;
		sheet.Width = columns * cellWidth + (columns + 1) * gap;
		sheet.Height = rows * cellHeight + (rows + 1) * gap;
		sheet.Rgba.resize(std::size_t(sheet.Width) * sheet.Height * 4);
		const std::array<std::uint8_t, 4> fill{ static_cast<std::uint8_t>(background), static_cast<std::uint8_t>(background >> 8),
			static_cast<std::uint8_t>(background >> 16), static_cast<std::uint8_t>(background >> 24) };

		for (std::size_t p = 0; p < sheet.Rgba.size(); p += 4)
		{
			std::copy(fill.begin(), fill.end(), sheet.Rgba.begin() + static_cast<std::ptrdiff_t>(p));
		}

		for (std::uint32_t index = 0; index < count; ++index)
		{
			const Image& image = images[index];

			if (!image.IsValid())
			{
				continue;
			}

			const double scale = std::min(double(cellWidth) / image.Width, double(cellHeight) / image.Height);
			const auto w = std::max(1u, static_cast<std::uint32_t>(image.Width * scale));
			const auto h = std::max(1u, static_cast<std::uint32_t>(image.Height * scale));
			const std::uint32_t x0 = gap + (index % columns) * (cellWidth + gap) + (cellWidth - std::min(w, cellWidth)) / 2;
			const std::uint32_t y0 = gap + (index / columns) * (cellHeight + gap) + (cellHeight - std::min(h, cellHeight)) / 2;

			for (std::uint32_t y = 0; y < h && y < cellHeight; ++y)
			{
				const double sy = std::clamp((y + 0.5) / scale - 0.5, 0.0, double(image.Height - 1));
				const auto iy = static_cast<std::uint32_t>(sy);
				const std::uint32_t iy1 = std::min(iy + 1, image.Height - 1);
				const double fy = sy - iy;

				for (std::uint32_t x = 0; x < w && x < cellWidth; ++x)
				{
					const double sx = std::clamp((x + 0.5) / scale - 0.5, 0.0, double(image.Width - 1));
					const auto ix = static_cast<std::uint32_t>(sx);
					const std::uint32_t ix1 = std::min(ix + 1, image.Width - 1);
					const double fx = sx - ix;
					std::uint8_t* out = sheet.Rgba.data() + (std::size_t(y0 + y) * sheet.Width + x0 + x) * 4;

					for (int c = 0; c < 4; ++c)
					{
						const auto at = [&](std::uint32_t px, std::uint32_t py)
						{
							return double(image.Rgba[(std::size_t(py) * image.Width + px) * 4 + c]);
						};
						const double top = at(ix, iy) + (at(ix1, iy) - at(ix, iy)) * fx;
						const double bottom = at(ix, iy1) + (at(ix1, iy1) - at(ix, iy1)) * fx;
						out[c] = static_cast<std::uint8_t>(std::lround(top + (bottom - top) * fy));
					}
				}
			}
		}

		return sheet;
	}

} // namespace Swim::Tooling
