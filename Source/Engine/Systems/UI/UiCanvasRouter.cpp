#include "Engine/Systems/UI/UiCanvasRouter.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::UI
{
	namespace
	{
		bool IsWorldMode(UiCanvasMode mode)
		{
			return mode != UiCanvasMode::Screen;
		}

		bool LaidOut(UiDocument& document)
		{
			document.EnsureLayout();
			return document.IsLayoutCurrent();
		}
	} // namespace

	UiCanvasHandle UiCanvasRouter::Add(const UiCanvasDesc& desc)
	{
		if (!desc.Document || static_cast<std::uint8_t>(desc.Mode) > static_cast<std::uint8_t>(UiCanvasMode::Billboard))
		{
			throw std::invalid_argument("A UI canvas needs a document and a known mode");
		}
		Canvas canvas;
		canvas.Handle = { nextHandle++ };
		canvas.Desc = desc;
		canvas.Sequence = nextSequence++;
		canvases.push_back(canvas);
		return canvas.Handle;
	}

	bool UiCanvasRouter::Remove(UiCanvasHandle handle)
	{
		auto* canvas = Find(handle);
		if (!canvas)
		{
			return false;
		}
		if (captured == handle)
		{
			canvas->Desc.Document->CancelPointer();
			captured = {};
		}
		if (hovered == handle)
		{
			canvas->Desc.Document->PointerLeave();
			hovered = {};
		}
		if (focused == handle)
		{
			focused = {};
		}
		std::erase_if(canvases,
			[&](const Canvas& c)
			{
				return c.Handle == handle;
			});
		return true;
	}

	bool UiCanvasRouter::Contains(UiCanvasHandle handle) const
	{
		return Find(handle) != nullptr;
	}

	UiDocument* UiCanvasRouter::GetDocument(UiCanvasHandle handle) const
	{
		const auto* canvas = Find(handle);
		return canvas ? canvas->Desc.Document : nullptr;
	}

	UiCanvasRouter::Canvas* UiCanvasRouter::Find(UiCanvasHandle handle)
	{
		const auto it = std::find_if(canvases.begin(), canvases.end(),
			[&](const Canvas& c)
			{
				return c.Handle == handle;
			});
		return it == canvases.end() ? nullptr : &*it;
	}

	const UiCanvasRouter::Canvas* UiCanvasRouter::Find(UiCanvasHandle handle) const
	{
		return const_cast<UiCanvasRouter*>(this)->Find(handle);
	}

	void UiCanvasRouter::SetInteractive(UiCanvasHandle handle, bool interactive)
	{
		auto* canvas = Find(handle);
		if (!canvas)
		{
			throw std::invalid_argument("Unknown UI canvas");
		}
		canvas->Desc.Interactive = interactive;
		if (!interactive)
		{
			if (captured == handle)
			{
				canvas->Desc.Document->CancelPointer();
				captured = {};
			}
			if (hovered == handle)
			{
				canvas->Desc.Document->PointerLeave();
				hovered = {};
			}
		}
	}

	void UiCanvasRouter::SetScreenPlacement(UiCanvasHandle handle, UiPoint offset, UiPoint size)
	{
		auto* canvas = Find(handle);
		if (!canvas || canvas->Desc.Mode != UiCanvasMode::Screen)
		{
			throw std::invalid_argument("SetScreenPlacement needs a screen canvas");
		}
		if (!std::isfinite(offset.X) || !std::isfinite(offset.Y) || !std::isfinite(size.X) || !std::isfinite(size.Y) || size.X < 0.0f ||
			size.Y < 0.0f)
		{
			throw std::invalid_argument("UI screen placement must be finite");
		}
		canvas->Offset = offset;
		canvas->Size = size;
	}

	void UiCanvasRouter::SetWorldPlacement(UiCanvasHandle handle, const UiMatrix3x4& canvasToWorld, UiPoint canvasSize, bool twoSided)
	{
		auto* canvas = Find(handle);
		if (!canvas || !IsWorldMode(canvas->Desc.Mode))
		{
			throw std::invalid_argument("SetWorldPlacement needs a world panel, billboard or render surface canvas");
		}
		const bool finite = std::all_of(canvasToWorld.begin(), canvasToWorld.end(),
			[](float v)
			{
				return std::isfinite(v);
			});
		if (!finite || !std::isfinite(canvasSize.X) || !std::isfinite(canvasSize.Y) || canvasSize.X < 0.0f || canvasSize.Y < 0.0f)
		{
			throw std::invalid_argument("UI world placement must be finite");
		}
		canvas->HasWorld = true;
		canvas->CanvasToWorld = canvasToWorld;
		canvas->CanvasSize = canvasSize;
		canvas->TwoSided = twoSided;
	}

	void UiCanvasRouter::SetCamera(const UiCameraView& view)
	{
		ScreenRay(view, { 0.0f, 0.0f }); // Validates.
		camera = view;
	}

	std::vector<UiCanvasRouter::Canvas*> UiCanvasRouter::ByPriority()
	{
		std::vector<Canvas*> result;
		for (auto& canvas : canvases)
		{
			result.push_back(&canvas);
		}
		std::stable_sort(result.begin(), result.end(),
			[](const Canvas* a, const Canvas* b)
			{
				const bool screenA = a->Desc.Mode == UiCanvasMode::Screen;
				const bool screenB = b->Desc.Mode == UiCanvasMode::Screen;
				if (screenA != screenB)
				{
					return screenA;
				}
				if (screenA && a->Desc.Order != b->Desc.Order)
				{
					return a->Desc.Order > b->Desc.Order;
				}
				return screenA ? a->Sequence > b->Sequence : a->Sequence < b->Sequence;
			});
		return result;
	}

	std::optional<UiPoint> UiCanvasRouter::MapCaptured(Canvas& canvas, const UiPointer& pointer) const
	{
		if (canvas.Desc.Mode == UiCanvasMode::Screen)
		{
			if (pointer.Screen)
			{
				return UiPoint{ pointer.Screen->X - canvas.Offset.X, pointer.Screen->Y - canvas.Offset.Y };
			}
			return std::nullopt;
		}
		for (const auto& hit : pointer.SurfaceHits)
		{
			if (hit.Canvas == canvas.Handle)
			{
				return hit.Point;
			}
		}
		if (canvas.HasWorld && pointer.Ray)
		{
			if (const auto hit = IntersectCanvasPlane(*pointer.Ray, canvas.CanvasToWorld))
			{
				return hit->Point;
			}
		}
		return std::nullopt; // Keeps the last point.
	}

	UiCanvasHandle UiCanvasRouter::PointerMove(const UiPointer& pointer)
	{
		if (captured)
		{
			if (auto* canvas = Find(captured))
			{
				if (const auto point = MapCaptured(*canvas, pointer))
				{
					canvas->LastPoint = *point;
					canvas->Desc.Document->PointerMove(*point);
				}
				return captured;
			}
			captured = {};
		}
		Canvas* target = nullptr;
		UiPoint targetPoint;
		// Screen canvases first, top-most first.
		for (auto* canvas : ByPriority())
		{
			if (canvas->Desc.Mode != UiCanvasMode::Screen || !canvas->Desc.Interactive || !pointer.Screen)
			{
				continue;
			}
			const UiPoint point{ pointer.Screen->X - canvas->Offset.X, pointer.Screen->Y - canvas->Offset.Y };
			const bool bounded = canvas->Size.X > 0.0f || canvas->Size.Y > 0.0f;
			const bool inside = !bounded || (point.X >= 0.0f && point.Y >= 0.0f && point.X < canvas->Size.X && point.Y < canvas->Size.Y);
			if (!inside)
			{
				continue;
			}
			auto& document = *canvas->Desc.Document;
			document.EnsureLayout();
			if (!document.IsLayoutCurrent())
			{
				continue;
			}
			if (canvas->Desc.BlocksPointer || document.HitTest(point))
			{
				target = canvas;
				targetPoint = point;
				break;
			}
		}
		if (!target)
		{
			// World canvases and application surface hits, nearest first.
			struct Candidate
			{
				Canvas* Target;
				UiPoint Point;
				float Distance;
			};

			std::vector<Candidate> candidates;
			for (auto& canvas : canvases)
			{
				if (canvas.Desc.Mode == UiCanvasMode::Screen || !canvas.Desc.Interactive || !canvas.HasWorld || !pointer.Ray)
				{
					continue;
				}
				if (const auto hit = IntersectCanvas(*pointer.Ray, canvas.CanvasToWorld, canvas.CanvasSize, canvas.TwoSided))
				{
					candidates.push_back({ &canvas, hit->Point, hit->Distance });
				}
			}
			for (const auto& hit : pointer.SurfaceHits)
			{
				auto* canvas = Find(hit.Canvas);
				if (canvas && canvas->Desc.Mode != UiCanvasMode::Screen && canvas->Desc.Interactive && std::isfinite(hit.Distance) &&
					std::isfinite(hit.Point.X) && std::isfinite(hit.Point.Y))
				{
					candidates.push_back({ canvas, hit.Point, hit.Distance });
				}
			}
			std::stable_sort(candidates.begin(), candidates.end(),
				[](const Candidate& a, const Candidate& b)
				{
					return a.Distance < b.Distance;
				});
			for (const auto& candidate : candidates)
			{
				auto& document = *candidate.Target->Desc.Document;
				document.EnsureLayout();
				if (!document.IsLayoutCurrent())
				{
					continue;
				}
				if (candidate.Target->Desc.BlocksPointer || document.HitTest(candidate.Point))
				{
					target = candidate.Target;
					targetPoint = candidate.Point;
					break;
				}
			}
		}
		const UiCanvasHandle next = target ? target->Handle : UiCanvasHandle{};
		if (hovered && hovered != next)
		{
			if (auto* previous = Find(hovered))
			{
				previous->Desc.Document->PointerLeave();
			}
		}
		hovered = next;
		if (target)
		{
			target->LastPoint = targetPoint;
			target->Desc.Document->PointerMove(targetPoint);
		}
		return hovered;
	}

	void UiCanvasRouter::SetFocused(UiCanvasHandle handle)
	{
		if (focused && focused != handle)
		{
			if (auto* previous = Find(focused))
			{
				previous->Desc.Document->Focus({});
			}
		}
		focused = handle;
	}

	void UiCanvasRouter::PointerDown(UiKeyModifiers modifiers)
	{
		auto* canvas = Find(hovered);
		if (!canvas)
		{
			ClearFocus(); // A press outside every canvas returns the keyboard to the game.
			return;
		}
		auto& document = *canvas->Desc.Document;
		document.PointerDown(canvas->LastPoint, modifiers);
		captured = canvas->Handle;
		if (document.GetFocus())
		{
			SetFocused(canvas->Handle);
		}
		else if (focused)
		{
			ClearFocus();
		}
	}

	void UiCanvasRouter::PointerUp()
	{
		if (auto* canvas = Find(captured))
		{
			canvas->Desc.Document->PointerUp(canvas->LastPoint);
		}
		captured = {};
	}

	void UiCanvasRouter::CancelPointer()
	{
		if (auto* canvas = Find(captured))
		{
			canvas->Desc.Document->CancelPointer();
		}
		captured = {};
		if (auto* canvas = Find(hovered))
		{
			canvas->Desc.Document->PointerLeave();
		}
		hovered = {};
	}

	bool UiCanvasRouter::Wheel(UiPoint delta)
	{
		auto* canvas = Find(captured ? captured : hovered);
		return canvas && canvas->Desc.Document->Wheel(canvas->LastPoint, delta);
	}

	bool UiCanvasRouter::FocusNext(bool backwards)
	{
		if (auto* canvas = Find(focused))
		{
			canvas->Desc.Document->FocusNext(backwards);
			if (canvas->Desc.Document->GetFocus())
			{
				return true;
			}
		}
		for (auto* canvas : ByPriority())
		{
			if (!canvas->Desc.Interactive || !LaidOut(*canvas->Desc.Document))
			{
				continue;
			}
			canvas->Desc.Document->FocusNext(backwards);
			if (canvas->Desc.Document->GetFocus())
			{
				SetFocused(canvas->Handle);
				return true;
			}
		}
		return false;
	}

	bool UiCanvasRouter::Navigate(UiNavDirection direction)
	{
		if (auto* canvas = Find(focused))
		{
			return canvas->Desc.Document->Navigate(direction);
		}
		for (auto* canvas : ByPriority())
		{
			if (canvas->Desc.Interactive && LaidOut(*canvas->Desc.Document) && canvas->Desc.Document->Navigate(direction))
			{
				SetFocused(canvas->Handle);
				return true;
			}
		}
		return false;
	}

	bool UiCanvasRouter::KeyDown(UiKey key, UiKeyModifiers modifiers)
	{
		if (auto* canvas = Find(focused))
		{
			const bool consumed = canvas->Desc.Document->KeyDown(key, modifiers);
			if (!canvas->Desc.Document->GetFocus())
			{
				focused = {}; // Escape (or Tab past the end of an empty document) released it.
			}
			return consumed;
		}
		if (key == UiKey::Tab)
		{
			return FocusNext(modifiers.Shift);
		}
		return false;
	}

	void UiCanvasRouter::TextInput(std::string_view utf8)
	{
		if (auto* canvas = Find(focused))
		{
			canvas->Desc.Document->TextInput(utf8);
		}
	}

	void UiCanvasRouter::SetComposition(std::string_view utf8, std::uint32_t cursor)
	{
		if (auto* canvas = Find(focused))
		{
			canvas->Desc.Document->SetComposition(utf8, cursor);
		}
	}

	void UiCanvasRouter::Focus(UiCanvasHandle handle)
	{
		if (!Find(handle))
		{
			throw std::invalid_argument("Unknown UI canvas");
		}
		SetFocused(handle);
	}

	void UiCanvasRouter::ClearFocus()
	{
		if (auto* canvas = Find(focused))
		{
			canvas->Desc.Document->Focus({});
		}
		focused = {};
	}

	bool UiCanvasRouter::WantsTextInput() const
	{
		const auto* canvas = Find(focused);
		return canvas && canvas->Desc.Document->WantsTextInput();
	}

	std::optional<UiRect> UiCanvasRouter::GetTextInputRect() const
	{
		const auto* canvas = Find(focused);
		if (!canvas || !canvas->Desc.Document->WantsTextInput() || !canvas->Desc.Document->IsLayoutCurrent())
		{
			return std::nullopt;
		}
		const auto rect = canvas->Desc.Document->GetTextInputRect();
		if (canvas->Desc.Mode == UiCanvasMode::Screen)
		{
			return UiRect{ rect.X + canvas->Offset.X, rect.Y + canvas->Offset.Y, rect.Width, rect.Height };
		}
		if (!canvas->HasWorld || !camera)
		{
			return std::nullopt;
		}
		return ProjectCanvasRect(ClipFromCanvas(canvas->CanvasToWorld, *camera), { camera->ViewportWidth, camera->ViewportHeight }, rect);
	}
} // namespace Swim::UI
