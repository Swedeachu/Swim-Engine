#pragma once
// Shared helpers for the GPU particle tests (item 77): a camera, emitter presets and
// a CPU rasterizer of the particle draw pass (Particles::BillboardCorner +
// ShadeFragment + the two blend modes), used by the CPU tests and the native smoke.
#include "Engine/Systems/Renderer/Particles/ParticleReference.h"
#include "Tests/Fixtures/ClusterFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numeric>
#include <vector>

namespace Swim::Testing::ParticleScene
{
	namespace P = Render::Particles;
	using Float2 = std::array<float, 2>;
	using Float3 = std::array<float, 3>;
	using Float4 = std::array<float, 4>;

	inline Render::ParticleView View(float aspect, const Float3& eye = { 0.0f, 2.0f, 8.0f }, const Float3& target = { 0.0f, 1.5f, 0.0f })
	{
		Render::ParticleView view;
		view.View = ClusterScene::LookAt(eye, target, { 0, 1, 0 });
		view.Projection = Render::PerspectiveReverseZRowMajor(1.0f, aspect, 0.1f);
		return view;
	}

	inline std::array<float, 12> Translation(float x, float y, float z)
	{
		return { 1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z };
	}

	// A world-space fountain bouncing on the ground.
	inline Render::ParticleEmitterDesc Fountain()
	{
		Render::ParticleEmitterDesc desc;
		desc.Capacity = 256;
		desc.Rate = 120.0f;
		desc.ConeAngle = 0.35f;
		desc.SpeedMin = 3.0f;
		desc.SpeedMax = 4.0f;
		desc.LifetimeMin = 0.8f;
		desc.LifetimeMax = 1.4f;
		desc.SizeMin = 0.15f;
		desc.SizeMax = 0.3f;
		desc.Collision = true;
		desc.GroundHeight = 0.0f;
		desc.Restitution = 0.4f;
		desc.Friction = 0.2f;
		desc.Drag = 0.2f;
		desc.SizeOverLife = { 3, { 0.0f, 0.3f, 1.0f, 1.0f }, { 0.5f, 1.0f, 0.2f, 0.2f } };
		desc.ColorOverLife = { 2, { 0.0f, 1.0f, 1.0f, 1.0f }, { { { 1.0f, 0.8f, 0.3f, 1.0f }, { 0.8f, 0.1f, 0.05f, 0.0f }, {}, {} } } };
		desc.Seed = 11;
		return desc;
	}

	// A local-space, alpha-blended smoke puff that moves with its emitter.
	inline Render::ParticleEmitterDesc Smoke()
	{
		Render::ParticleEmitterDesc desc;
		desc.Capacity = 128;
		desc.Space = Render::ParticleSpace::Local;
		desc.Blend = Render::ParticleBlendMode::AlphaBlend;
		desc.Rate = 60.0f;
		desc.Shape = Render::ParticleShape::Sphere;
		desc.ShapeExtent = { 0.3f, 0.0f, 0.0f };
		desc.ConeAngle = 0.8f;
		desc.SpeedMin = 0.3f;
		desc.SpeedMax = 0.8f;
		desc.LifetimeMin = 1.0f;
		desc.LifetimeMax = 1.6f;
		desc.SizeMin = 0.4f;
		desc.SizeMax = 0.7f;
		desc.RotationMin = -3.0f;
		desc.RotationMax = 3.0f;
		desc.AngularVelocityMin = -1.0f;
		desc.AngularVelocityMax = 1.0f;
		desc.Gravity = { 0.0f, 0.5f, 0.0f };
		desc.ColorOverLife = { 3, { 0.0f, 0.2f, 1.0f, 1.0f },
			{ { { 0.3f, 0.3f, 0.35f, 0.0f }, { 0.5f, 0.5f, 0.55f, 0.7f }, { 0.6f, 0.6f, 0.6f, 0.0f }, {} } } };
		desc.Seed = 23;
		return desc;
	}

	// Looping bursts of box-spawned sparks with a flipbook (texture indices set by the caller).
	inline Render::ParticleEmitterDesc Sparks()
	{
		Render::ParticleEmitterDesc desc;
		desc.Capacity = 200;
		desc.Rate = 0.0f;
		desc.Duration = 0.5f;
		desc.Looping = true;
		desc.Bursts = { { 0.0f, 40 }, { 0.25f, 20 } };
		desc.Shape = Render::ParticleShape::Box;
		desc.ShapeExtent = { 0.4f, 0.1f, 0.4f };
		desc.ConeAngle = 1.2f;
		desc.SpeedMin = 1.0f;
		desc.SpeedMax = 2.5f;
		desc.LifetimeMin = 0.4f;
		desc.LifetimeMax = 0.9f;
		desc.SizeMin = 0.2f;
		desc.SizeMax = 0.35f;
		desc.FlipbookColumns = 4;
		desc.FlipbookRows = 2;
		desc.FlipbookFrames = 7;
		desc.Seed = 37;
		return desc;
	}

	struct Image
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<Float4> Texels;
		std::vector<std::uint8_t> Ambiguous; // A billboard edge passes within the tolerance of the pixel centre.

		Float4& At(std::uint32_t x, std::uint32_t y) { return Texels[std::size_t(y) * Width + x]; }
	};

	// One emitter's particles in draw order, as the GPU would draw them.
	struct DrawBatch
	{
		const Render::GpuParticleEmitter* Emitter = nullptr;
		Render::ParticleBlendMode Blend = Render::ParticleBlendMode::Additive;
		std::vector<Render::GpuParticle> Particles;
	};

	using Sampler = std::function<Float4(const Render::GpuParticleEmitter&, const Float2& uv)>;

	// Rasterizes the batches in order over `image` (pre-filled with the clear color):
	// pixel centres inside a billboard's two triangles pass the reverse-Z depth test
	// against sceneDepth (GreaterEqual) and blend premultiplied (additive keeps alpha).
	inline void Rasterize(Image& image, const std::vector<float>& sceneDepth, const Render::GpuParticleFrame& frame,
		const std::vector<DrawBatch>& batches, const Sampler& sample, float edgeTolerance = 0.02f)
	{
		image.Ambiguous.assign(image.Texels.size(), 0);
		const float width = float(image.Width);
		const float height = float(image.Height);
		for (const auto& batch : batches)
		{
			for (const auto& particle : batch.Particles)
			{
				std::array<Float3, 4> screen{}; // x, y in pixels, depth
				std::array<Float2, 4> local{};
				std::array<Float2, 4> uv{};
				for (std::uint32_t c = 0; c < 4; ++c)
				{
					const auto vertex = P::BillboardCorner(frame, *batch.Emitter, particle, c);
					std::array<float, 4> clip{};
					for (int r = 0; r < 4; ++r)
					{
						clip[r] = frame.ViewProjection[r * 4] * vertex.Position[0] + frame.ViewProjection[r * 4 + 1] * vertex.Position[1] +
							frame.ViewProjection[r * 4 + 2] * vertex.Position[2] + frame.ViewProjection[r * 4 + 3];
					}
					screen[c] = { (clip[0] / clip[3] + 1.0f) * 0.5f * width, (1.0f - clip[1] / clip[3]) * 0.5f * height,
						clip[2] / clip[3] };
					local[c] = { (c & 1u) != 0 ? 1.0f : 0.0f, (c & 2u) != 0 ? 0.0f : 1.0f };
					uv[c] = vertex.Uv;
					if (!(clip[3] > 0.0f))
					{
						screen[c][2] = -1.0f; // Behind the camera: never drawn.
					}
				}
				if (screen[0][2] < 0.0f || screen[1][2] < 0.0f || screen[2][2] < 0.0f || screen[3][2] < 0.0f)
				{
					continue;
				}
				float minX = width, minY = height, maxX = 0.0f, maxY = 0.0f;
				for (const auto& s : screen)
				{
					minX = std::min(minX, s[0]);
					maxX = std::max(maxX, s[0]);
					minY = std::min(minY, s[1]);
					maxY = std::max(maxY, s[1]);
				}
				const int x0 = std::max(0, int(std::floor(minX)) - 1);
				const int x1 = std::min(int(image.Width) - 1, int(std::ceil(maxX)) + 1);
				const int y0 = std::max(0, int(std::floor(minY)) - 1);
				const int y1 = std::min(int(image.Height) - 1, int(std::ceil(maxY)) + 1);
				const std::array<std::array<int, 3>, 2> triangles{ { { 0, 1, 2 }, { 2, 1, 3 } } };
				for (int y = y0; y <= y1; ++y)
				{
					for (int x = x0; x <= x1; ++x)
					{
						const float px = float(x) + 0.5f;
						const float py = float(y) + 0.5f;
						bool covered = false;
						bool ambiguous = false;
						std::array<float, 3> weights{};
						std::array<int, 3> chosen{};
						for (std::size_t ti = 0; ti < triangles.size(); ++ti)
						{
							const auto& t = triangles[ti];
							const auto& a = screen[t[0]];
							const auto& b = screen[t[1]];
							const auto& c = screen[t[2]];
							const float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
							if (std::abs(area) < 1.0e-12f)
							{
								continue;
							}
							const float w0 = ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area;
							const float w1 = ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area;
							const float w2 = 1.0f - w0 - w1;
							// Distance to the nearest edge in pixels (edge function / edge length).
							const auto edgeDistance = [&](const Float3& p, const Float3& q, float w)
							{
								const float length = std::hypot(q[0] - p[0], q[1] - p[1]);
								return std::abs(w * area) / std::max(length, 1.0e-6f);
							};
							// Outer edges only: the shared diagonal (1-2) is interior to the quad.
							const float nearest = ti == 0 ? std::min(edgeDistance(c, a, w1), edgeDistance(a, b, w2))
														  : std::min(edgeDistance(b, c, w0), edgeDistance(c, a, w1));
							if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
							{
								covered = true;
								weights = { w0, w1, w2 };
								chosen = t;
							}
							ambiguous = ambiguous || nearest < edgeTolerance;
						}
						const std::size_t index = std::size_t(y) * image.Width + std::size_t(x);
						if (!covered)
						{
							image.Ambiguous[index] = image.Ambiguous[index] || ambiguous;
							continue;
						}
						float depth = 0.0f;
						Float2 l{ 0, 0 }, u{ 0, 0 };
						for (int k = 0; k < 3; ++k)
						{
							depth += weights[k] * screen[chosen[k]][2];
							for (int d = 0; d < 2; ++d)
							{
								l[d] += weights[k] * local[chosen[k]][d];
								u[d] += weights[k] * uv[chosen[k]][d];
							}
						}
						if (!(depth >= sceneDepth[index]))
						{
							continue;
						}
						image.Ambiguous[index] = image.Ambiguous[index] || ambiguous || std::abs(depth - sceneDepth[index]) < 1.0e-5f;
						const auto texel =
							(batch.Emitter->Flags & Render::ParticleFlagTextured) != 0 ? sample(*batch.Emitter, u) : Float4{ 1, 1, 1, 1 };
						const auto source = P::ShadeFragment(*batch.Emitter, particle, l, texel);
						auto& target = image.Texels[index];
						if (batch.Blend == Render::ParticleBlendMode::Additive)
						{
							for (int c = 0; c < 3; ++c)
							{
								target[c] += source[c];
							}
						}
						else
						{
							for (int c = 0; c < 4; ++c)
							{
								target[c] = source[c] + target[c] * (1.0f - source[3]);
							}
						}
					}
				}
			}
		}
	}

	// Sorts particles into the alpha-blend draw order.
	inline void SortBackToFront(
		std::vector<Render::GpuParticle>& particles, const Render::GpuParticleFrame& frame, const Render::GpuParticleEmitter& emitter)
	{
		std::stable_sort(particles.begin(), particles.end(),
			[&](const Render::GpuParticle& a, const Render::GpuParticle& b)
			{
				return P::DrawsBefore(P::ViewDepth(frame, emitter, a), a.Id, P::ViewDepth(frame, emitter, b), b.Id);
			});
	}
} // namespace Swim::Testing::ParticleScene
