#include "Engine/Systems/Renderer/Features/Outline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace Engine
{

	namespace
	{

		using Float4 = std::array<float, 4>;

		struct FrameData
		{
			Float4 Forward, Right, Up, Camera;
			std::array<Float4, 4> ViewProjection;
			std::uint32_t Rect[4];
			std::uint32_t Size[4];
		};

		struct ObjectData
		{
			std::array<Float4, 3> ToLocal;
			Float4 Color, Rim, Params;
			std::uint32_t Range[4];
		};

		constexpr float MaxWidth = 8.0f;

		// The inverse of an affine 3x4 (row-major); identity when it is singular.
		std::array<Float4, 3> Invert(const std::array<float, 12>& m)
		{
			const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
			const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);

			if (std::abs(det) < 1.0e-12f)
			{
				return { Float4{ 1, 0, 0, 0 }, Float4{ 0, 1, 0, 0 }, Float4{ 0, 0, 1, 0 } };
			}

			const float s = 1.0f / det;
			const std::array<float, 9> r{ (e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s, (f * g - d * i) * s, (a * i - c * g) * s,
				(c * d - a * f) * s, (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s };
			const float tx = m[3], ty = m[7], tz = m[11];
			std::array<Float4, 3> out{};

			for (int row = 0; row < 3; ++row)
			{
				out[row] = { r[row * 3], r[row * 3 + 1], r[row * 3 + 2],
					-(r[row * 3] * tx + r[row * 3 + 1] * ty + r[row * 3 + 2] * tz) };
			}

			return out;
		}

		std::array<float, 3> Apply(const std::array<float, 12>& m, const std::array<float, 3>& p)
		{
			return { m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
				m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11] };
		}

	} // namespace

	std::uint32_t Outline::Set(const Object& object, std::uint32_t handle)
	{
		for (auto& entry : objects)
		{
			if (handle != 0 && entry.Handle == handle)
			{
				entry.Shape = object;
				return handle;
			}
		}

		objects.push_back({ nextHandle, object });
		return nextHandle++;
	}

	void Outline::SetTransform(std::uint32_t handle, const std::array<float, 12>& transform)
	{
		for (auto& entry : objects)
		{
			if (entry.Handle == handle)
			{
				entry.Shape.Transform = transform;
			}
		}
	}

	void Outline::Remove(std::uint32_t handle)
	{
		std::erase_if(objects, [handle](const Entry& entry) { return entry.Handle == handle; });
	}

	void Outline::Clear()
	{
		objects.clear();
	}

	void Outline::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();

		if (objects.empty() || view.Width == 0 || view.Height == 0)
		{
			return;
		}

		std::vector<ObjectData> data;
		std::vector<Float4> triangles;
		std::vector<Float4> boxes;
		float minX = std::numeric_limits<float>::max(), minY = minX, maxX = -minX, maxY = -minX;
		float radius = 0.0f;
		const auto& vp = view.ViewProjection;

		for (const auto& entry : objects)
		{
			const auto& shape = entry.Shape;

			if (shape.Triangles.size() < 3 && shape.Boxes.empty())
			{
				continue;
			}

			// The screen rectangle of the object's local bounds; a corner behind the camera
			// makes it the whole screen.
			std::array<float, 3> lo{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
			std::array<float, 3> hi{ -lo[0], -lo[1], -lo[2] };
			const auto grow = [&](const std::array<float, 3>& p)
			{
				for (int c = 0; c < 3; ++c)
				{
					lo[c] = std::min(lo[c], p[c]);
					hi[c] = std::max(hi[c], p[c]);
				}
			};
			for (const auto& p : shape.Triangles)
			{
				grow(p);
			}
			for (const auto& box : shape.Boxes)
			{
				grow(box.Min);
				grow(box.Max);
			}

			bool behind = false;
			for (int corner = 0; corner < 8; ++corner)
			{
				const auto world =
					Apply(shape.Transform, { (corner & 1) ? hi[0] : lo[0], (corner & 2) ? hi[1] : lo[1], (corner & 4) ? hi[2] : lo[2] });
				const float x = vp[0] * world[0] + vp[1] * world[1] + vp[2] * world[2] + vp[3];
				const float y = vp[4] * world[0] + vp[5] * world[1] + vp[6] * world[2] + vp[7];
				const float w = vp[12] * world[0] + vp[13] * world[1] + vp[14] * world[2] + vp[15];

				if (w <= 1.0e-4f)
				{
					behind = true;
					break;
				}

				const float px = (x / w * 0.5f + 0.5f) * float(view.Width);
				const float py = (0.5f - y / w * 0.5f) * float(view.Height);
				minX = std::min(minX, px);
				maxX = std::max(maxX, px);
				minY = std::min(minY, py);
				maxY = std::max(maxY, py);
			}

			if (behind)
			{
				minX = minY = 0.0f;
				maxX = float(view.Width);
				maxY = float(view.Height);
			}

			const float width = std::clamp(shape.Width, 0.0f, MaxWidth);
			radius = std::max(radius, width);
			ObjectData object{};
			object.ToLocal = Invert(shape.Transform);
			object.Color = shape.Color;
			object.Rim = shape.Rim;
			object.Params = { width, shape.RimPower, std::clamp(shape.HiddenOpacity, 0.0f, 1.0f),
				shape.Creases ? std::cos(shape.CreaseAngle * 3.14159265f / 180.0f) : -2.0f };
			object.Range[0] = static_cast<std::uint32_t>(triangles.size() / 3);
			object.Range[1] = static_cast<std::uint32_t>(shape.Triangles.size() / 3);
			object.Range[2] = static_cast<std::uint32_t>(boxes.size() / 2);
			object.Range[3] = static_cast<std::uint32_t>(shape.Boxes.size());

			for (std::size_t i = 0; i < std::size_t(object.Range[1]) * 3; ++i)
			{
				triangles.push_back({ shape.Triangles[i][0], shape.Triangles[i][1], shape.Triangles[i][2], 0.0f });
			}

			for (const auto& box : shape.Boxes)
			{
				boxes.push_back({ box.Min[0], box.Min[1], box.Min[2], 0.0f });
				boxes.push_back({ box.Max[0], box.Max[1], box.Max[2], 0.0f });
			}

			data.push_back(object);
		}

		const auto x0 = static_cast<std::int64_t>(std::floor(std::max(minX, 0.0f)));
		const auto y0 = static_cast<std::int64_t>(std::floor(std::max(minY, 0.0f)));
		const auto x1 = static_cast<std::int64_t>(std::ceil(std::min(maxX, float(view.Width))));
		const auto y1 = static_cast<std::int64_t>(std::ceil(std::min(maxY, float(view.Height))));

		if (data.empty() || x1 <= x0 || y1 <= y0)
		{
			return;
		}

		if (triangles.empty())
		{
			triangles.push_back({ 0, 0, 0, 0 });
		}

		if (boxes.empty())
		{
			boxes.push_back({ 0, 0, 0, 0 });
		}

		FrameData frame{};
		frame.Forward = { view.Forward[0], view.Forward[1], view.Forward[2], view.TanHalfFovX };
		frame.Right = { view.Right[0], view.Right[1], view.Right[2], view.TanHalfFovY };
		frame.Up = { view.Up[0], view.Up[1], view.Up[2], 0.0f };
		frame.Camera = { view.Position[0], view.Position[1], view.Position[2], 0.0f };

		for (int row = 0; row < 4; ++row)
		{
			frame.ViewProjection[row] = { vp[row * 4], vp[row * 4 + 1], vp[row * 4 + 2], vp[row * 4 + 3] };
		}

		frame.Rect[0] = static_cast<std::uint32_t>(x0);
		frame.Rect[1] = static_cast<std::uint32_t>(y0);
		frame.Rect[2] = static_cast<std::uint32_t>(x1 - x0);
		frame.Rect[3] = static_cast<std::uint32_t>(y1 - y0);
		frame.Size[0] = view.Width;
		frame.Size[1] = view.Height;
		frame.Size[2] = static_cast<std::uint32_t>(std::ceil(radius)) + 1u;
		frame.Size[3] = static_cast<std::uint32_t>(data.size());

		auto& graph = context.Graph();
		const auto usage = Swim::Rhi::BufferUsage::Storage;
		const auto frameBuffer = graph.CreateUpload(std::as_bytes(std::span(&frame, 1)), "Outline frame", usage, 16);
		const auto objectBuffer = graph.CreateUpload(std::as_bytes(std::span(data)), "Outline objects", usage, 16);
		const auto triangleBuffer = graph.CreateUpload(std::as_bytes(std::span(triangles)), "Outline triangles", usage, 16);
		const auto boxBuffer = graph.CreateUpload(std::as_bytes(std::span(boxes)), "Outline boxes", usage, 16);
		const auto mask = context.CreateTexture(Swim::Rhi::Format::RGBA16Float, frame.Rect[2], frame.Rect[3], "Outline mask");
		context.Compute("OutlineMask")
			.Buffer("Frame", frameBuffer)
			.Buffer("Objects", objectBuffer)
			.Buffer("Triangles", triangleBuffer)
			.Buffer("Boxes", boxBuffer)
			.Texture("Depth", context.Depth())
			.Storage("Mask", mask)
			.Dispatch(frame.Rect[2], frame.Rect[3]);
		const auto output = context.CreateColorTarget("Outline");
		context.Compute("OutlineComposite")
			.Buffer("Frame", frameBuffer)
			.Buffer("Objects", objectBuffer)
			.Texture("Color", context.Color())
			.Texture("Mask", mask)
			.Storage("Output", output)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

} // namespace Engine
