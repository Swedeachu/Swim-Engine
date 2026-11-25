#include "Engine/Systems/UI/UiDocument.h"
#include "Tests/Framework/Test.h"

#include <algorithm>

using namespace Swim::UI;

namespace
{

	struct Slots
	{
		UiDocument Ui;
		UiNodeId A;
		UiNodeId B;

		Slots()
		{
			auto root = Ui.GetStyle(Ui.GetRoot());
			root.Flow = UiFlow::Row;
			Ui.SetStyle(Ui.GetRoot(), root);
			UiStyle slot;
			slot.Width = UiLength::Pixels(20);
			slot.Height = UiLength::Pixels(20);
			slot.HitTest = true;
			A = Ui.Create(Ui.GetRoot());
			B = Ui.Create(Ui.GetRoot());
			Ui.SetStyle(A, slot);
			Ui.SetStyle(B, slot);
			Ui.Layout({ 100, 100 });
		}
	};

	std::vector<UiEvent> OfKind(const std::vector<UiEvent>& events, UiEventKind kind)
	{
		std::vector<UiEvent> result;
		std::copy_if(events.begin(), events.end(), std::back_inserter(result),
			[&](const UiEvent& event)
			{
				return event.Kind == kind;
			});
		return result;
	}

} // namespace

SWIM_TEST("UI.Pointer", "RightAndMiddleButtonsReachTheNodeUnderThePointer")
{
	Slots s;
	s.Ui.ButtonDown({ 30, 5 }, UiPointerButton::Right, { true, false, false });
	s.Ui.ButtonUp({ 30, 5 }, UiPointerButton::Right);
	s.Ui.ButtonDown({ 5, 5 }, UiPointerButton::Middle);
	const auto events = s.Ui.DrainEvents();
	const auto downs = OfKind(events, UiEventKind::ButtonDown);
	SWIM_REQUIRE_EQUAL(downs.size(), 2u);
	SWIM_CHECK(downs[0].Node == s.B && downs[0].Button == UiPointerButton::Right && downs[0].Modifiers.Shift);
	SWIM_CHECK_NEAR(downs[0].Position.X, 30.0f, 1e-5f);
	SWIM_CHECK(downs[1].Node == s.A && downs[1].Button == UiPointerButton::Middle);
	SWIM_CHECK_EQUAL(OfKind(events, UiEventKind::ButtonUp).size(), 1u);
	SWIM_CHECK(OfKind(events, UiEventKind::Click).empty()); // Only the primary button clicks.
	SWIM_CHECK(!s.Ui.GetFocus());
	// The primary button reports ButtonDown/Up too, and handlers work for the new kinds.
	int handled = 0;
	s.Ui.On(s.A, UiEventKind::ButtonDown,
		[&](const UiEvent& event)
		{
			handled += event.Button == UiPointerButton::Left ? 1 : 10;
		});
	s.Ui.PointerDown({ 5, 5 });
	s.Ui.PointerUp({ 5, 5 });
	s.Ui.ButtonDown({ 5, 5 }, UiPointerButton::Right);
	s.Ui.DispatchCallbacks();
	SWIM_CHECK_EQUAL(handled, 11);
}

SWIM_TEST("UI.Pointer", "DoubleClicksNeedTheSameNodeTimeAndPlace")
{
	Slots s;
	const auto click = [&](UiPoint point)
	{
		s.Ui.PointerDown(point);
		s.Ui.PointerUp(point);
	};
	click({ 5, 5 });
	s.Ui.Update(0.1f);
	click({ 6, 5 });
	s.Ui.Update(0.1f);
	click({ 6, 6 });
	auto events = s.Ui.DrainEvents();
	auto clicks = OfKind(events, UiEventKind::Click);
	SWIM_REQUIRE_EQUAL(clicks.size(), 3u);
	SWIM_CHECK_EQUAL(clicks[0].Count, 1u);
	SWIM_CHECK_EQUAL(clicks[1].Count, 2u);
	SWIM_CHECK_EQUAL(clicks[2].Count, 3u);
	SWIM_CHECK_EQUAL(OfKind(events, UiEventKind::DoubleClick).size(), 1u);
	s.Ui.Update(1.0f); // Too slow.
	click({ 6, 6 });
	s.Ui.Update(0.1f);
	click({ 25, 5 }); // Another node.
	s.Ui.Update(0.1f);
	click({ 35, 5 }); // Too far (10 units).
	events = s.Ui.DrainEvents();
	SWIM_CHECK(OfKind(events, UiEventKind::DoubleClick).empty());
	clicks = OfKind(events, UiEventKind::Click);
	SWIM_REQUIRE_EQUAL(clicks.size(), 3u);
	SWIM_CHECK(clicks[0].Count == 1u && clicks[1].Count == 1u && clicks[2].Count == 1u);
	s.Ui.SetDoubleClick(0.5f, 12.0f);
	s.Ui.Update(0.1f);
	click({ 25, 5 });
	SWIM_CHECK_EQUAL(OfKind(s.Ui.DrainEvents(), UiEventKind::DoubleClick).size(), 1u);
}

SWIM_TEST("UI.Pointer", "PressDragReleaseDropsOnTheNodeUnderThePointer")
{
	Slots s;
	s.Ui.On(s.A, UiEventKind::Press,
		[&](const UiEvent&)
		{
			s.Ui.BeginDrag(s.A, { 42, true });
		});
	s.Ui.PointerDown({ 5, 5 });
	s.Ui.DispatchCallbacks();
	SWIM_CHECK(s.Ui.IsDragging());
	SWIM_CHECK(s.Ui.GetDragSource() == s.A);
	s.Ui.PointerMove({ 15, 5 });
	s.Ui.PointerMove({ 30, 8 });
	SWIM_CHECK_NEAR(s.Ui.GetPointerPosition().X, 30.0f, 1e-5f);
	s.Ui.PointerUp({ 30, 8 });
	SWIM_CHECK(!s.Ui.IsDragging());
	const auto events = s.Ui.DrainEvents();
	SWIM_CHECK_EQUAL(OfKind(events, UiEventKind::DragStart).size(), 1u);
	const auto over = OfKind(events, UiEventKind::DragOver);
	SWIM_REQUIRE_EQUAL(over.size(), 3u); // Every move while dragging (PointerUp moves too).
	SWIM_CHECK(over[0].Node == s.A && over[1].Node == s.B && over[1].Source == s.A && over[1].Payload == 42u);
	const auto drops = OfKind(events, UiEventKind::Drop);
	SWIM_REQUIRE_EQUAL(drops.size(), 1u);
	SWIM_CHECK(drops[0].Node == s.B && drops[0].Source == s.A && drops[0].Payload == 42u);
	const auto ends = OfKind(events, UiEventKind::DragEnd);
	SWIM_REQUIRE_EQUAL(ends.size(), 1u);
	SWIM_CHECK(ends[0].Node == s.A);
	SWIM_CHECK_NEAR(ends[0].Value, 1.0f, 1e-6f);
	SWIM_CHECK(OfKind(events, UiEventKind::Click).empty());
}

SWIM_TEST("UI.Pointer", "ClickToPickUpDragsDropOnTheNextPress")
{
	Slots s;
	s.Ui.PointerDown({ 5, 5 });
	s.Ui.PointerUp({ 5, 5 });
	s.Ui.BeginDrag(s.A, { 7, false });
	s.Ui.DrainEvents();
	s.Ui.PointerMove({ 25, 5 });
	SWIM_CHECK(s.Ui.IsDragging());
	s.Ui.ButtonDown({ 25, 5 }, UiPointerButton::Right); // Any button places.
	SWIM_CHECK(!s.Ui.IsDragging());
	auto events = s.Ui.DrainEvents();
	auto drops = OfKind(events, UiEventKind::Drop);
	SWIM_REQUIRE_EQUAL(drops.size(), 1u);
	SWIM_CHECK(drops[0].Node == s.B && drops[0].Button == UiPointerButton::Right);
	// The primary press that places is not a press or click of its own.
	s.Ui.BeginDrag(s.B);
	s.Ui.CancelDrag();
	events = s.Ui.DrainEvents();
	SWIM_CHECK(OfKind(events, UiEventKind::Drop).empty());
	SWIM_REQUIRE_EQUAL(OfKind(events, UiEventKind::DragEnd).size(), 1u);
	SWIM_CHECK_NEAR(OfKind(events, UiEventKind::DragEnd)[0].Value, 0.0f, 1e-6f);
	s.Ui.BeginDrag(s.B, { 0, false });
	s.Ui.PointerDown({ 5, 5 });
	s.Ui.PointerUp({ 5, 5 });
	events = s.Ui.DrainEvents();
	SWIM_CHECK_EQUAL(OfKind(events, UiEventKind::Drop).size(), 1u);
	SWIM_CHECK(OfKind(events, UiEventKind::Press).empty());
	SWIM_CHECK(OfKind(events, UiEventKind::Click).empty());
	// A removed source ends the drag silently.
	s.Ui.BeginDrag(s.B, { 0, false });
	s.Ui.Remove(s.B);
	SWIM_CHECK(!s.Ui.IsDragging());
}

SWIM_TEST("UI.Pointer", "KeyEventsGoToTheFocusedNodeOrTheRoot")
{
	Slots s;
	auto style = s.Ui.GetStyle(s.B);
	style.Focusable = true;
	s.Ui.SetStyle(s.B, style);
	SWIM_CHECK(!s.Ui.KeyEvent(65, true));
	s.Ui.Focus(s.B);
	SWIM_CHECK(s.Ui.KeyEvent(66, true, { false, true, false }));
	SWIM_CHECK(s.Ui.KeyEvent(66, false));
	const auto events = s.Ui.DrainEvents();
	const auto downs = OfKind(events, UiEventKind::KeyDown);
	SWIM_REQUIRE_EQUAL(downs.size(), 2u);
	SWIM_CHECK(downs[0].Node == s.Ui.GetRoot() && downs[0].Key == 65u);
	SWIM_CHECK(downs[1].Node == s.B && downs[1].Key == 66u && downs[1].Modifiers.Control);
	SWIM_CHECK_EQUAL(OfKind(events, UiEventKind::KeyUp).size(), 1u);
}
