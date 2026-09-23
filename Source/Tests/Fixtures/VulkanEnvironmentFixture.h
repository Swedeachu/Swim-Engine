#pragma once
// Shared setup of the environment native smokes (items 61-62): loads the five
// environment programs, builds an EnvironmentBuilder with a linear clamp sampler,
// and reads RGBA16Float cube maps and textures back into CPU images.
#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentReference.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Tests/Framework/Test.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <vector>

namespace Swim::Testing::EnvironmentSmoke
{
	inline std::vector<std::byte> ReadSpirv(const char* path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SWIM_REQUIRE(file && !bytes.empty());
		return bytes;
	}

	inline ShaderCompiler::ShaderRhiInterfaceResult ReflectProgram(const char* path)
	{
		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(path);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		return converted;
	}

	inline float HalfToFloat(std::uint16_t half)
	{
		const std::uint32_t sign = (half >> 15) & 1u;
		const std::uint32_t exponent = (half >> 10) & 0x1fu;
		const std::uint32_t mantissa = half & 0x3ffu;
		float value = 0.0f;
		if (exponent == 0)
		{
			value = std::ldexp(float(mantissa), -24);
		}
		else if (exponent == 31)
		{
			value = mantissa ? NAN : INFINITY;
		}
		else
		{
			value = std::ldexp(float(mantissa | 0x400u), int(exponent) - 25);
		}
		return sign ? -value : value;
	}

	// One compiled compute program and the objects it owns.
	struct ComputeProgram
	{
		std::unique_ptr<Rhi::ShaderProgram> Program;
		std::unique_ptr<Rhi::PipelineLayout> Layout;
		std::unique_ptr<Rhi::ComputePipeline> Pipeline;
		std::uint32_t Space = 0;

		Render::EnvironmentProgram Get() const { return { Pipeline.get(), Layout.get(), Space }; }
	};

	inline ComputeProgram MakeCompute(Rhi::Device& device, const char* spirvPath, const char* reflectionPath, const char* label)
	{
		const auto reflected = ReflectProgram(reflectionPath);
		const auto bytes = ReadSpirv(spirvPath);
		const auto& programInterface = reflected.Interface;
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytes };
		ComputeProgram program;
		program.Program = device.CreateShaderProgram({ { &stage, 1 },
			{ programInterface.DescriptorSchemas, programInterface.PushConstants, programInterface.ComputeThreadGroupSize }, label });
		SWIM_REQUIRE(program.Program);
		program.Layout = device.CreatePipelineLayout({ program.Program.get(), label });
		SWIM_REQUIRE(program.Layout);
		program.Pipeline = device.CreateComputePipeline({ program.Program.get(), program.Layout.get(), {}, label });
		SWIM_REQUIRE(program.Pipeline);
		program.Space = programInterface.DescriptorSchemas.at(0).Space;
		return program;
	}

	// The five environment programs, a linear clamp sampler and the builder over them.
	struct EnvironmentPrograms
	{
		explicit EnvironmentPrograms(Rhi::Device& device)
			: Sky(MakeCompute(device, SWIM_ENVIRONMENT_SKY_SPIRV_PATH, SWIM_ENVIRONMENT_SKY_REFLECTION_PATH, "Environment sky")),
			  Downsample(MakeCompute(
				  device, SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH, SWIM_ENVIRONMENT_DOWNSAMPLE_REFLECTION_PATH, "Environment downsample")),
			  Prefilter(MakeCompute(
				  device, SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH, SWIM_ENVIRONMENT_PREFILTER_REFLECTION_PATH, "Environment prefilter")),
			  Irradiance(MakeCompute(
				  device, SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH, SWIM_ENVIRONMENT_IRRADIANCE_REFLECTION_PATH, "Environment irradiance")),
			  BrdfLut(
				  MakeCompute(device, SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH, SWIM_ENVIRONMENT_BRDF_LUT_REFLECTION_PATH, "Environment LUT"))
		{
			Rhi::SamplerDesc samplerDesc{};
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = Rhi::SamplerAddressMode::ClampToEdge;
			LinearClamp = device.CreateSampler(samplerDesc);
			SWIM_REQUIRE(LinearClamp);
			Render::EnvironmentBuilderDesc desc;
			desc.Sky = Sky.Get();
			desc.Downsample = Downsample.Get();
			desc.Prefilter = Prefilter.Get();
			desc.Irradiance = Irradiance.Get();
			desc.BrdfLut = BrdfLut.Get();
			desc.Sampler = LinearClamp.get();
			desc.DebugName = "Smoke environment";
			Builder = std::make_unique<Render::EnvironmentBuilder>(desc);
		}

		ComputeProgram Sky;
		ComputeProgram Downsample;
		ComputeProgram Prefilter;
		ComputeProgram Irradiance;
		ComputeProgram BrdfLut;
		std::unique_ptr<Rhi::Sampler> LinearClamp;
		std::unique_ptr<Render::EnvironmentBuilder> Builder;
	};

	// Readbacks of every mip and face of an RGBA16Float cube.
	struct CubeReadback
	{
		std::uint32_t Size = 0;
		std::uint32_t MipCount = 0;
		std::vector<Render::GraphReadback> Faces; // Per mip, then per face.
	};

	inline CubeReadback AddCubeReadback(Render::RenderGraph& graph, Render::GraphTexture cube, std::uint32_t size, std::uint32_t mipCount)
	{
		CubeReadback readback{ size, mipCount, {} };
		for (std::uint32_t mip = 0; mip < mipCount; ++mip)
		{
			const std::uint32_t mipSize = std::max(size >> mip, 1u);
			for (std::uint32_t face = 0; face < 6; ++face)
			{
				readback.Faces.push_back(
					Render::AddTextureReadback(graph, "Cube readback", cube, { 0, { mip, face }, {}, { mipSize, mipSize, 1 } }));
			}
		}
		return readback;
	}

	inline std::vector<Render::Environment::Float4> ReadHalfTexels(
		Render::RenderGraphExecutor& executor, const Render::GraphReadback& readback, std::size_t texelCount)
	{
		std::vector<std::uint16_t> halves(texelCount * 4);
		SWIM_REQUIRE(executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(halves))) == Rhi::ReadbackStatus::Ready);
		std::vector<Render::Environment::Float4> texels(texelCount);
		for (std::size_t i = 0; i < texelCount; ++i)
		{
			for (int c = 0; c < 4; ++c)
			{
				texels[i][c] = HalfToFloat(halves[i * 4 + c]);
			}
		}
		return texels;
	}

	inline Render::Environment::CubeImage ReadCube(Render::RenderGraphExecutor& executor, const CubeReadback& readback)
	{
		Render::Environment::CubeImage cube(readback.Size, readback.MipCount);
		for (std::uint32_t mip = 0; mip < readback.MipCount; ++mip)
		{
			const std::uint32_t mipSize = cube.GetMipSize(mip);
			for (std::uint32_t face = 0; face < 6; ++face)
			{
				const auto texels = ReadHalfTexels(executor, readback.Faces[mip * 6 + face], std::size_t(mipSize) * mipSize);
				std::copy(texels.begin(), texels.end(), cube.Face(mip, face).begin());
			}
		}
		return cube;
	}

	inline Render::Environment::Image2D ReadImage(
		Render::RenderGraphExecutor& executor, const Render::GraphReadback& readback, std::uint32_t width, std::uint32_t height)
	{
		return { width, height, ReadHalfTexels(executor, readback, std::size_t(width) * height) };
	}

	inline Render::Environment::IrradianceSh ReadIrradiance(Render::RenderGraphExecutor& executor, const Render::GraphReadback& readback)
	{
		std::array<float, 36> values{};
		SWIM_REQUIRE(executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(values))) == Rhi::ReadbackStatus::Ready);
		Render::Environment::IrradianceSh sh;
		for (std::uint32_t i = 0; i < 9; ++i)
		{
			sh.Coefficients[i] = { values[i * 4], values[i * 4 + 1], values[i * 4 + 2] };
		}
		return sh;
	}

	// Relative error with an absolute floor, per channel.
	inline float RelativeError(float actual, float expected, float floor)
	{
		return std::abs(actual - expected) / (std::abs(expected) + floor);
	}
} // namespace Swim::Testing::EnvironmentSmoke
