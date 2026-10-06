#include "Engine/Systems/UI/Widgets/UiListView.h"

#include "Engine/Systems/UI/Internal/UiBuiltInControls.h"
#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/UiControlRegistry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::UI
{

	namespace
	{

		// Rows a page key moves when neither the item extent nor the viewport is known.
		constexpr std::int32_t DefaultPageRows = 10;

	} // namespace

	bool UiListViewControl::OnKey(UiControlContext& context, UiKey key, UiKeyModifiers)
	{
		const auto& c = context.Control();

		if (c.ReadOnly)
		{
			return false;
		}

		const bool horizontal = IsHorizontalList(context);
		const auto count = static_cast<std::int32_t>(context.GetOptionCount());
		const auto value = GetSelected(context);
		std::int32_t page = DefaultPageRows;

		if (c.ScrollTarget && context.Contains(c.ScrollTarget))
		{
			const UiRect inner = context.GetContentBox(c.ScrollTarget);
			float extent = c.ItemExtent;

			if (extent <= 0.0f)
			{
				if (const auto option = context.FindOption(std::max(0, value)); option && context.IsLaidOut(option))
				{
					const auto bounds = context.GetBounds(option);
					extent = horizontal ? bounds.Width : bounds.Height;
				}
			}

			if (extent > 0.0f)
			{
				page = std::max(1, static_cast<std::int32_t>((horizontal ? inner.Width : inner.Height) / extent));
			}
		}

		std::int32_t next = value;
		const auto previousKey = horizontal ? UiKey::Left : UiKey::Up;
		const auto nextKey = horizontal ? UiKey::Right : UiKey::Down;

		if (key == previousKey)
		{
			next = value < 0 ? 0 : value - 1;
		}
		else if (key == nextKey)
		{
			next = value < 0 ? 0 : value + 1;
		}
		else if (key == UiKey::PageUp)
		{
			next = value < 0 ? 0 : value - page;
		}
		else if (key == UiKey::PageDown)
		{
			next = value < 0 ? 0 : value + page;
		}
		else if (key == UiKey::Home)
		{
			next = 0;
		}
		else if (key == UiKey::End)
		{
			next = count - 1;
		}
		else if (key == UiKey::Enter)
		{
			if (value >= 0)
			{
				context.Emit(UiEventKind::Submit, c.Value);
			}

			return true;
		}
		else
		{
			return false; // The cross axis navigates.
		}

		if (count > 0)
		{
			Select(context, std::clamp(next, 0, count - 1), true);
		}

		return true;
	}

	UiListView CreateListView(
		UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, const std::vector<std::string>& items, std::int32_t selected)
	{
		const auto area = CreateScrollArea(document, parent, rootStyle, true, false, UiScrollBarVisibility::Auto, false);
		auto viewport = document.GetStyle(area.Viewport);
		viewport.AlignItems = UiAlign::Stretch;
		document.SetStyle(area.Viewport, viewport);
		UiControl control;
		control.Kind = UiControlKind::ListView;
		control.Orientation = UiOrientation::Vertical;
		control.Value = -1.0f;
		control.ScrollTarget = area.Viewport;
		document.SetControl(area.Root, control);
		document.SetThemeClass(area.Root, UiThemeClass::ListView, UiThemeApply::Paint); // The caller's size stays.
		UiListView list{ area.Root, area.Viewport, area.Vertical };

		for (const auto& item : items)
		{
			AddListItem(document, list, item);
		}

		document.SetValue(list.Root, float(selected));
		return list;
	}

	UiNodeId AddListItem(UiDocument& document, const UiListView& list, std::string text)
	{
		return CreateTextOption(document, list.Viewport, list.Root, std::move(text));
	}

	UiVirtualList::UiVirtualList(UiDocument& documentInput, UiNodeId parent, UiVirtualListDesc descInput)
		: document(documentInput), desc(std::move(descInput))
	{
		if (desc.ItemHeight == 0.0f)
		{
			desc.ItemHeight = document.GetTheme()->Metrics.ControlHeight;
		}

		if (!std::isfinite(desc.ItemHeight) || desc.ItemHeight <= 0.0f ||
			double(desc.ItemCount) * double(desc.ItemHeight) > double(1000000.0f) || desc.Overscan > 1024)
		{
			throw std::invalid_argument("A virtual list needs a positive item height, at most 1,000,000 units long in total");
		}

		list = CreateListView(document, parent, desc.Style);
		UiStyle contentStyle;
		contentStyle.AlignSelf = UiAlign::Stretch;
		contentStyle.Height = UiLength::Pixels(float(desc.ItemCount) * desc.ItemHeight);
		content = Internal::CreateStyled(document, list.Viewport, contentStyle);
		auto control = document.GetControl(list.Root);
		control.ItemCount = desc.ItemCount;
		control.ItemExtent = desc.ItemHeight;
		control.Value = desc.ItemCount > 0 ? float(std::clamp<std::int64_t>(desc.Selected, -1, std::int64_t(desc.ItemCount) - 1)) : -1.0f;
		document.SetControl(list.Root, control);
	}

	void UiVirtualList::SetItemCount(std::uint32_t count)
	{
		if (double(count) * double(desc.ItemHeight) > double(1000000.0f))
		{
			throw std::invalid_argument("A virtual list is at most 1,000,000 units long");
		}

		desc.ItemCount = count;
		auto contentStyle = document.GetStyle(content);
		contentStyle.Height = UiLength::Pixels(float(count) * desc.ItemHeight);
		document.SetStyle(content, contentStyle);
		auto control = document.GetControl(list.Root);
		control.ItemCount = count;
		control.Value = count > 0 ? std::min(control.Value, float(count) - 1.0f) : -1.0f;
		document.SetControl(list.Root, control);
		Refresh();
	}

	void UiVirtualList::Refresh()
	{
		for (auto& row : rows)
		{
			row.Index = -1;
		}
	}

	bool UiVirtualList::Update()
	{
		if (document.IsLayoutCurrent())
		{
			viewport = document.GetBounds(list.Viewport).Height;
		}

		const float extent = desc.ItemHeight;
		const float total = float(desc.ItemCount) * extent;
		const float scroll = std::clamp(document.GetScroll(list.Viewport).Y, 0.0f, std::max(0.0f, total - viewport));
		const auto count = std::int64_t(desc.ItemCount);
		const auto first = std::max<std::int64_t>(0, std::int64_t(std::floor(scroll / extent)) - desc.Overscan);
		const auto last = std::min<std::int64_t>(count, std::int64_t(std::ceil((scroll + viewport) / extent)) + desc.Overscan);
		bool changed = false;
		freeRows.clear();
		shownRows.assign(std::size_t(std::max<std::int64_t>(0, last - first)), false);

		for (std::size_t i = 0; i < rows.size(); ++i)
		{
			const auto index = rows[i].Index;

			if (index >= first && index < last && !shownRows[std::size_t(index - first)])
			{
				shownRows[std::size_t(index - first)] = true;
			}
			else
			{
				freeRows.push_back(i);
			}
		}

		for (auto index = first; index < last; ++index)
		{
			if (shownRows[std::size_t(index - first)])
			{
				continue;
			}

			std::size_t slot = 0;

			if (!freeRows.empty())
			{
				slot = freeRows.back();
				freeRows.pop_back();
			}
			else
			{
				UiStyle rowStyle;
				rowStyle.Absolute = true;
				rowStyle.AnchorMin = { 0.0f, 0.0f };
				rowStyle.AnchorMax = { 1.0f, 0.0f };
				rowStyle.Padding = document.GetTheme()->Metrics.MenuItemPadding;
				rowStyle.TextWrap = Text::TextWrap::None;
				rows.push_back({ Internal::CreateStyled(document, content, rowStyle), -1 });
				document.SetThemeClass(rows.back().Node, UiThemeClass::MenuItem, UiThemeApply::Paint | UiThemeApply::Text);
				slot = rows.size() - 1;
			}

			auto& row = rows[slot];
			auto style = document.GetStyle(row.Node);
			style.Visible = true;
			style.Height = UiLength::Pixels(extent);
			style.Offset = { 0.0f, float(index) * extent };
			document.SetStyle(row.Node, style);
			document.SetPartRole(row.Node, list.Root, UiPartRole::Option, float(index));
			row.Index = index;

			if (desc.Bind)
			{
				desc.Bind(document, row.Node, std::uint32_t(index));
			}

			++binds;
			changed = true;
		}

		for (const auto slot : freeRows)
		{
			auto& row = rows[slot];

			if (document.GetStyle(row.Node).Visible || row.Index >= 0)
			{
				auto style = document.GetStyle(row.Node);
				style.Visible = false;
				document.SetStyle(row.Node, style);
				document.SetPartRole(row.Node, {}, UiPartRole::None);
				row.Index = -1;
				changed = true;
			}
		}

		return changed;
	}

	std::uint32_t UiVirtualList::GetBoundRowCount() const
	{
		return std::uint32_t(std::count_if(rows.begin(), rows.end(),
			[](const Row& row)
			{
				return row.Index >= 0;
			}));
	}

	UiNodeId UiVirtualList::FindRow(std::uint32_t index) const
	{
		for (const auto& row : rows)
		{
			if (row.Index == std::int64_t(index))
			{
				return row.Node;
			}
		}

		return {};
	}

	void Internal::RegisterListView(UiControlRegistry& registry)
	{
		registry.Register<UiListViewControl>("ListView",
			[](UiDocument& document, UiNodeId parent)
			{
				UiStyle style;
				style.Height = UiLength::Pixels(160.0f);
				return CreateListView(document, parent, style).Root;
			});
	}

} // namespace Swim::UI
