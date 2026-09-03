#include "Engine/Assets/ContentHash.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Swim::Assets
{

	namespace
	{

		constexpr std::array<std::uint32_t, 64> RoundConstants =
		{
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
			0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
			0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
			0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
			0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
			0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
			0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
			0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
			0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
		};

		constexpr std::array<std::uint32_t, 8> InitialState =
		{
			0x6a09e667u,
			0xbb67ae85u,
			0x3c6ef372u,
			0xa54ff53au,
			0x510e527fu,
			0x9b05688cu,
			0x1f83d9abu,
			0x5be0cd19u
		};

		std::uint32_t LoadBigEndian32(const std::byte* bytes)
		{
			return
				(static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) << 24) |
				(static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 16) |
				(static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 8) |
				static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3]));
		}

		std::uint8_t HexValue(char character)
		{
			if (character >= '0' && character <= '9')
			{
				return static_cast<std::uint8_t>(character - '0');
			}
			if (character >= 'a' && character <= 'f')
			{
				return static_cast<std::uint8_t>(character - 'a' + 10);
			}
			if (character >= 'A' && character <= 'F')
			{
				return static_cast<std::uint8_t>(character - 'A' + 10);
			}
			throw std::invalid_argument("ContentHash contains a non-hexadecimal character.");
		}

	}

	bool ContentHash::IsZero() const
	{
		return std::all_of(Bytes.begin(), Bytes.end(), [](std::uint8_t value)
		{
			return value == 0;
		});
	}

	std::string ContentHash::ToHex() const
	{
		static constexpr char Digits[] = "0123456789abcdef";
		std::string result;
		result.resize(Bytes.size() * 2);

		for (std::size_t index = 0; index < Bytes.size(); ++index)
		{
			result[index * 2] = Digits[Bytes[index] >> 4];
			result[index * 2 + 1] = Digits[Bytes[index] & 0x0f];
		}
		return result;
	}

	ContentHash ContentHash::FromHex(std::string_view text)
	{
		if (text.size() != 64)
		{
			throw std::invalid_argument("ContentHash hex text must contain exactly 64 characters.");
		}

		ContentHash result{};
		for (std::size_t index = 0; index < result.Bytes.size(); ++index)
		{
			result.Bytes[index] = static_cast<std::uint8_t>(
				(HexValue(text[index * 2]) << 4) |
				HexValue(text[index * 2 + 1])
			);
		}
		return result;
	}

	ContentHash ComputeContentHash(std::span<const std::byte> bytes)
	{
		if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8ull)
		{
			throw std::length_error("ContentHash input is too large for SHA-256 length encoding.");
		}

		std::vector<std::byte> padded(bytes.begin(), bytes.end());
		padded.push_back(std::byte{ 0x80 });

		while ((padded.size() % 64) != 56)
		{
			padded.push_back(std::byte{ 0 });
		}

		const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8ull;
		for (int shift = 56; shift >= 0; shift -= 8)
		{
			padded.push_back(std::byte{ static_cast<std::uint8_t>((bitLength >> shift) & 0xffu) });
		}

		std::array<std::uint32_t, 8> state = InitialState;
		std::array<std::uint32_t, 64> words{};

		for (std::size_t blockOffset = 0; blockOffset < padded.size(); blockOffset += 64)
		{
			for (std::size_t index = 0; index < 16; ++index)
			{
				words[index] = LoadBigEndian32(padded.data() + blockOffset + index * 4);
			}
			for (std::size_t index = 16; index < words.size(); ++index)
			{
				const std::uint32_t s0 = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^ (words[index - 15] >> 3);
				const std::uint32_t s1 = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10);
				words[index] = words[index - 16] + s0 + words[index - 7] + s1;
			}

			std::uint32_t a = state[0];
			std::uint32_t b = state[1];
			std::uint32_t c = state[2];
			std::uint32_t d = state[3];
			std::uint32_t e = state[4];
			std::uint32_t f = state[5];
			std::uint32_t g = state[6];
			std::uint32_t h = state[7];

			for (std::size_t index = 0; index < words.size(); ++index)
			{
				const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
				const std::uint32_t choose = (e & f) ^ ((~e) & g);
				const std::uint32_t temp1 = h + sum1 + choose + RoundConstants[index] + words[index];
				const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
				const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
				const std::uint32_t temp2 = sum0 + majority;

				h = g;
				g = f;
				f = e;
				e = d + temp1;
				d = c;
				c = b;
				b = a;
				a = temp1 + temp2;
			}

			state[0] += a;
			state[1] += b;
			state[2] += c;
			state[3] += d;
			state[4] += e;
			state[5] += f;
			state[6] += g;
			state[7] += h;
		}

		ContentHash result{};
		for (std::size_t word = 0; word < state.size(); ++word)
		{
			result.Bytes[word * 4] = static_cast<std::uint8_t>((state[word] >> 24) & 0xffu);
			result.Bytes[word * 4 + 1] = static_cast<std::uint8_t>((state[word] >> 16) & 0xffu);
			result.Bytes[word * 4 + 2] = static_cast<std::uint8_t>((state[word] >> 8) & 0xffu);
			result.Bytes[word * 4 + 3] = static_cast<std::uint8_t>(state[word] & 0xffu);
		}
		return result;
	}

	ContentHash ComputeContentHash(std::string_view text)
	{
		return ComputeContentHash(std::as_bytes(std::span(text.data(), text.size())));
	}

}
