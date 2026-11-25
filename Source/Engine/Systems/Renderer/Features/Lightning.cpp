#include "Engine/Systems/Renderer/Features/Lightning.h"

#include "Engine/Math/VectorMath.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <span>

#include <glm/glm.hpp>

namespace Engine
{

	namespace
	{

		glm::vec3 Direction(const glm::vec3& v)
		{
			return NormalizeOr(v, { 0.0f, -1.0f, 0.0f }, 1.0e-6f);
		}

		// Two unit vectors perpendicular to d and to each other.
		std::pair<glm::vec3, glm::vec3> Basis(const glm::vec3& d)
		{
			const glm::vec3 reference = std::abs(d.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			const glm::vec3 e1 = Direction(glm::cross(d, reference));
			return { e1, glm::cross(d, e1) };
		}

		struct Generator
		{
			std::mt19937 Random;
			float Jaggedness = 0.16f;

			float Uniform(float low, float high) { return std::uniform_real_distribution<float>(low, high)(Random); }

			// A sideways offset in the plane (e1, e2), each component in [-extent, extent].
			// The e2 draw comes first: the order the original argument evaluation used, so
			// seeded strikes keep their shape.
			glm::vec3 Offset(const glm::vec3& e1, const glm::vec3& e2, float extent)
			{
				const float b = Uniform(-extent, extent);
				const float a = Uniform(-extent, extent);
				return e1 * a + e2 * b;
			}

			// Midpoint displacement: each pass splits every segment and pushes its middle
			// sideways by a share of the segment length, so the channel is jagged at every scale.
			std::vector<glm::vec3> Channel(const glm::vec3& start, const glm::vec3& end, std::uint32_t passes)
			{
				std::vector<glm::vec3> points{ start, end };

				for (std::uint32_t pass = 0; pass < passes; ++pass)
				{
					std::vector<glm::vec3> next;
					next.reserve(points.size() * 2);

					for (std::size_t i = 0; i + 1 < points.size(); ++i)
					{
						const glm::vec3 d = points[i + 1] - points[i];
						const auto [e1, e2] = Basis(Direction(d));
						const glm::vec3 mid = (points[i] + points[i + 1]) * 0.5f + Offset(e1, e2, glm::length(d) * Jaggedness);
						next.push_back(points[i]);
						next.push_back(mid);
					}

					next.push_back(points.back());
					points = std::move(next);
				}

				return points;
			}
		};

	} // namespace

	void Lightning::Strike(const StrikeDesc& desc)
	{
		Bolt bolt;
		bolt.Desc = desc;
		Generator generator{ std::mt19937(desc.Seed), std::clamp(desc.Jaggedness, 0.0f, 0.5f) };
		float stroke = 0.0f;

		for (std::uint32_t i = 0; i < std::clamp(desc.ReturnStrokes, 1u, 6u); ++i)
		{
			bolt.Strokes.push_back(stroke);
			stroke += generator.Uniform(0.05f, 0.14f);
		}

		const auto addChannel = [&](auto& self, const glm::vec3& start, const glm::vec3& end, float width, std::uint32_t level,
									std::uint32_t passes, float branching) -> void
		{
			const auto points = generator.Channel(start, end, passes);

			for (std::size_t i = 0; i + 1 < points.size(); ++i)
			{
				bolt.Segments.push_back({ ToArray(points[i]), ToArray(points[i + 1]), width, level });
			}

			if (level >= 2 || branching <= 0.0f || passes < 3)
			{
				return;
			}

			for (std::size_t i = 1; i + 1 < points.size(); ++i)
			{
				if (generator.Uniform(0.0f, 1.0f) > branching * 0.1f)
				{
					continue;
				}

				const glm::vec3 along = Direction(points[i + 1] - points[i]);
				const auto [e1, e2] = Basis(along);
				glm::vec3 direction = along + generator.Offset(e1, e2, 0.9f);
				direction.y -= 0.25f;
				const float length = glm::distance(end, points[i]) * generator.Uniform(0.12f, 0.4f);
				self(self, points[i], points[i] + Direction(direction) * length, width * 0.45f, level + 1, passes - 2, branching * 0.6f);
			}
		};

		addChannel(
			addChannel, ToGlm(desc.Start), ToGlm(desc.End), std::max(desc.Width, 0.01f), 0, 7, std::clamp(desc.Branching, 0.0f, 1.0f));
		bolts.push_back(std::move(bolt));
	}

	float Lightning::Envelope(const Bolt& bolt, std::uint32_t level)
	{
		const float t = bolt.Age;
		const float duration = std::max(bolt.Desc.Duration, 0.05f);

		if (t >= duration)
		{
			return 0.0f;
		}

		float value = 0.12f * std::exp(-t / 0.3f);

		for (std::size_t i = 0; i < bolt.Strokes.size(); ++i)
		{
			if (t >= bolt.Strokes[i])
			{
				value += (i == 0 ? 1.0f : 0.75f) * std::exp(-(t - bolt.Strokes[i]) / 0.045f);
			}
		}

		// Branches belong to the leader: they show in the first stroke and fade fast.
		if (level > 0)
		{
			value *= 0.6f * std::exp(-t / 0.12f);
		}

		return value * std::clamp((duration - t) / 0.15f, 0.0f, 1.0f) * bolt.Desc.Intensity;
	}

	std::uint32_t Lightning::GetGeometryPasses() const
	{
		return RenderGeometryPassBit(RenderGeometryPass::Transparent);
	}

	RenderGeometryResources Lightning::PrepareGeometry(RenderFeatureContext& context)
	{
		upload.clear();
		flash = 0.0f;

		for (const auto& bolt : bolts)
		{
			flash += Envelope(bolt, 0);
			const float main = Envelope(bolt, 0);
			const float branch = Envelope(bolt, 1);

			for (const auto& segment : bolt.Segments)
			{
				const float intensity = (segment.Level == 0 ? main : branch / float(segment.Level));

				if (intensity < 1.0e-3f)
				{
					continue;
				}

				upload.push_back({ segment.A[0], segment.A[1], segment.A[2], segment.Width });
				upload.push_back({ segment.B[0], segment.B[1], segment.B[2], intensity });
			}
		}

		const float dt = context.View().DeltaTime;

		for (auto& bolt : bolts)
		{
			bolt.Age += dt;
		}

		std::erase_if(bolts,
			[](const Bolt& bolt)
			{
				return bolt.Age >= std::max(bolt.Desc.Duration, 0.05f);
			});
		segmentCount = static_cast<std::uint32_t>(upload.size() / 2);

		if (segmentCount == 0)
		{
			return {};
		}

		segments = context.Graph().CreateUpload(std::as_bytes(std::span(upload)), "Lightning segments", Swim::Rhi::BufferUsage::Storage, 16);
		return { { segments }, {}, {} };
	}

	void Lightning::DrawGeometry(RenderFeatureDrawContext& context)
	{
		if (segmentCount == 0 || context.Pass() != RenderGeometryPass::Transparent)
		{
			return;
		}

		struct Constants
		{
			std::array<float, 4> Color;
			std::array<float, 4> Glow;
		} constants{};

		const float intensity = std::max(Settings.Intensity, 0.0f);
		constants.Color = { Settings.Color[0] * intensity, Settings.Color[1] * intensity, Settings.Color[2] * intensity,
			std::max(Settings.GlowWidth, 1.0f) };
		constants.Glow = { std::max(Settings.GlowIntensity, 0.0f), std::max(Settings.CoreSharpness, 0.1f), 0.0f, 0.0f };
		context.Program("Lightning").Buffer("Segments", segments).Constants(constants).Draw(segmentCount * 6);
	}

} // namespace Engine
