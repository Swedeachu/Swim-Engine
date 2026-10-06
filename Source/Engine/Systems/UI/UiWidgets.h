#pragma once

// The widget layer over UiDocument nodes (critical-path item 79). Every widget lives in its
// own unit under Widgets/: the builder that makes its node tree and, for interactive ones,
// its control behaviour (a UiControlBehavior registered by name in UiControlRegistry, the
// same way gameplay registers its own controls). Widgets are ordinary nodes styled by the
// document's theme; every part stays reachable (UiDocument::GetControl(node).Parts).
//
//   auto volume = UiWidget(document, CreateSlider(document, panel, { .Max = 1.0f }));
//   volume.Bind(settings.Volume);                 // two-way: no manual syncing
//   UiWidget(document, CreateButton(document, panel, "Play")).OnClick([&] { StartGame(); });
//   auto knob = CreateWidget(document, panel, "Game.Knob"); // any registered type, by name

#include "Engine/Systems/UI/UiControlBehavior.h"
#include "Engine/Systems/UI/UiControlRegistry.h"
#include "Engine/Systems/UI/UiDocument.h"
#include "Engine/Systems/UI/UiTheme.h"
#include "Engine/Systems/UI/Widgets/UiBasicWidgets.h"
#include "Engine/Systems/UI/Widgets/UiButton.h"
#include "Engine/Systems/UI/Widgets/UiCheckbox.h"
#include "Engine/Systems/UI/Widgets/UiDropdown.h"
#include "Engine/Systems/UI/Widgets/UiListView.h"
#include "Engine/Systems/UI/Widgets/UiMenu.h"
#include "Engine/Systems/UI/Widgets/UiModal.h"
#include "Engine/Systems/UI/Widgets/UiRadioGroup.h"
#include "Engine/Systems/UI/Widgets/UiScrollBar.h"
#include "Engine/Systems/UI/Widgets/UiSelection.h"
#include "Engine/Systems/UI/Widgets/UiSlider.h"
#include "Engine/Systems/UI/Widgets/UiToggle.h"
#include "Engine/Systems/UI/Widgets/UiTooltip.h"

#include <functional>
#include <string>
#include <string_view>

namespace Swim::UI
{

	// A light, copyable reference to a widget for readable callbacks, bindings and common
	// property changes. It does not own the node; keep it only while the document lives.
	class UiWidget
	{

	  public:

		UiWidget() = default;

		UiWidget(UiDocument& document, UiNodeId node) : document(&document), node(node) {}

		UiNodeId GetNode() const { return node; }

		operator UiNodeId() const { return node; }

		explicit operator bool() const { return document && node && document->Contains(node); }

		UiDocument& GetDocument() const { return *document; }

		UiWidget& OnClick(std::function<void()> handler)
		{
			document->OnClick(node, std::move(handler));
			return *this;
		}

		UiWidget& OnValue(std::function<void(float)> handler)
		{
			document->OnValue(node, std::move(handler));
			return *this;
		}

		UiWidget& OnChecked(std::function<void(bool)> handler)
		{
			document->OnChecked(node, std::move(handler));
			return *this;
		}

		UiWidget& OnText(std::function<void(const std::string&)> handler)
		{
			document->OnText(node, std::move(handler));
			return *this;
		}

		// Two-way bindings (UiDocument::BindValue): edits write the variable; changes made
		// elsewhere show up on the next Update.
		UiWidget& Bind(float& value)
		{
			document->BindValue(node, value);
			return *this;
		}

		UiWidget& Bind(bool& value)
		{
			document->BindChecked(node, value);
			return *this;
		}

		UiWidget& Bind(std::function<float()> get, std::function<void(float)> set)
		{
			document->BindValue(node, std::move(get), std::move(set));
			return *this;
		}

		UiWidget& SetValue(float value)
		{
			document->SetValue(node, value);
			return *this;
		}

		float GetValue() const { return document->GetValue(node); }

		UiWidget& SetStyle(const UiStyle& style)
		{
			document->SetStyle(node, style);
			return *this;
		}

		UiWidget& SetText(const std::string& text)
		{
			SetLabelText(*document, node, text);
			return *this;
		}

		UiWidget& SetEnabled(bool enabled)
		{
			auto style = document->GetStyle(node);
			style.Enabled = enabled;
			document->SetStyle(node, style);
			return *this;
		}

		UiWidget& SetVisible(bool visible)
		{
			auto style = document->GetStyle(node);
			style.Visible = visible;
			document->SetStyle(node, style);
			return *this;
		}

		UiWidget& SetTooltip(std::string text, float delaySeconds = 0.5f)
		{
			CreateTooltip(*document, node, std::move(text), delaySeconds);
			return *this;
		}

	  private:

		UiDocument* document = nullptr;
		UiNodeId node;

	};

	// Creates a widget of a registered control type by name (data-driven UI): the type's
	// default widget when it registered one, else a plain node with the control attached.
	// Throws std::invalid_argument for unknown types.
	UiWidget CreateWidget(UiDocument& document, UiNodeId parent, std::string_view type);

} // namespace Swim::UI
