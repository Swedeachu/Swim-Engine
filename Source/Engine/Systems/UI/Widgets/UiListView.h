#pragma once

#include "Engine/Systems/UI/Widgets/UiSelection.h"
#include "Engine/Systems/UI/Widgets/UiScrollBar.h"

#include <functional>
#include <string>
#include <vector>

namespace Swim::UI
{

	// A scrolling list: a click selects; Up/Down (Left/Right when horizontal), PageUp/PageDown
	// by the rows that fit, Home/End select and scroll ScrollTarget to reveal; Enter emits
	// Submit with the index.
	class UiListViewControl final : public UiSelectionControl
	{

	  public:

		bool UsesScrollTarget() const override { return true; }

		bool OnKey(UiControlContext& context, UiKey key, UiKeyModifiers modifiers) override;

	  protected:

		bool IsHorizontalList(const UiControlContext& context) const override
		{
			return context.Control().Orientation == UiOrientation::Horizontal;
		}

	};

	struct UiListView
	{
		UiNodeId Root;	   // The ListView control (focusable, themed ListView).
		UiNodeId Viewport; // Clipped: the option rows (and the ScrollTarget).
		UiNodeId ScrollBar;
	};

	struct UiListViewDesc
	{
		UiStyle Style;
		std::vector<std::string> Items;
		std::int32_t Selected = -1;
	};

	// A scrolling list of single-line text rows (themed MenuItem options).
	UiListView CreateListView(UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, const std::vector<std::string>& items = {},
		std::int32_t selected = -1);

	inline UiListView CreateListView(UiDocument& document, UiNodeId parent, const UiListViewDesc& desc)
	{
		return CreateListView(document, parent, desc.Style, desc.Items, desc.Selected);
	}

	UiNodeId AddListItem(UiDocument& document, const UiListView& list, std::string text);

	struct UiVirtualListDesc
	{
		std::uint32_t ItemCount = 0;
		float ItemHeight = 0.0f; // 0: the theme's ControlHeight. ItemCount x ItemHeight <= 1,000,000.
		// Fills a row for an index (text, images, child nodes); rows are reused.
		std::function<void(UiDocument&, UiNodeId row, std::uint32_t index)> Bind;
		UiStyle Style;				// The list's root (typically a size).
		std::uint32_t Overscan = 2; // Extra rows bound above and below the viewport.
		std::int32_t Selected = -1;
	};

	// A ListView whose rows exist only while visible: a pool of MenuItem rows placed at
	// index x ItemHeight inside a content node ItemCount x ItemHeight long. Selection, keys
	// and scrolling behave as for CreateListView (UiControl::ItemCount/ItemExtent).
	class UiVirtualList
	{

	  public:

		UiVirtualList(UiDocument& document, UiNodeId parent, UiVirtualListDesc desc);

		// Binds the rows the viewport shows (with the scroll offset and viewport of the last
		// Layout). Call after Layout; true when rows changed (Layout again before Paint).
		bool Update();

		void SetItemCount(std::uint32_t count); // Clamps the selection; rebinds every row.

		void Refresh(); // Rebinds every row (the items changed).

		UiNodeId GetRoot() const { return list.Root; }

		UiNodeId GetViewport() const { return list.Viewport; }

		std::uint32_t GetItemCount() const { return desc.ItemCount; }

		std::uint32_t GetBoundRowCount() const; // Rows showing an item.

		UiNodeId FindRow(std::uint32_t index) const; // The row showing index (empty when not bound).

		std::uint64_t GetBindCount() const { return binds; }

	  private:

		UiDocument& document;
		UiVirtualListDesc desc;
		UiListView list;
		UiNodeId content;

		struct Row
		{
			UiNodeId Node;
			std::int64_t Index = -1;
		};

		std::vector<Row> rows;
		// Per-Update scratch (reused: no allocation per frame once warm).
		std::vector<std::size_t> freeRows;
		std::vector<bool> shownRows;
		std::uint64_t binds = 0;
		float viewport = 0.0f;

	};

} // namespace Swim::UI
