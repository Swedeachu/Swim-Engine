#include "Engine/Systems/UI/Widgets/UiBasicWidgets.h"

#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"

namespace Swim::UI
{

	UiNodeId CreatePanel(UiDocument& document, UiNodeId parent, UiFlow flow)
	{
		UiStyle style;
		style.Flow = flow;
		const auto node = Internal::CreateStyled(document, parent, style);
		document.SetThemeClass(node, UiThemeClass::Panel);
		return node;
	}

	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::string text)
	{
		return Internal::ThemedLabel(document, parent, std::move(text));
	}

	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, const UiTextEditOptions& options)
	{
		UiStyle style;
		style.Clip = true;
		style.TextWrap = options.Multiline ? Text::TextWrap::Word : Text::TextWrap::None;
		const auto node = Internal::CreateStyled(document, parent, style);
		document.SetText(node, Internal::ThemeFonts(document), {}, Internal::ThemeTextSize(document, UiThemeClass::TextField));
		document.SetEditable(node, true, options);
		document.SetThemeClass(node, UiThemeClass::TextField);
		return node;
	}

	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string text,
		float size, const UiStyle& style)
	{
		const auto node = Internal::CreateStyled(document, parent, style);
		document.SetText(node, std::move(fonts), std::move(text), size);
		return node;
	}

	UiNodeId CreateImage(UiDocument& document, UiNodeId parent, const UiImage& image, const UiStyle& style)
	{
		const auto node = Internal::CreateStyled(document, parent, style);
		document.SetImage(node, image);
		return node;
	}

	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, float size,
		const UiTextEditOptions& options, const UiStyle& style)
	{
		auto field = style;
		field.Clip = true;

		if (!options.Multiline)
		{
			field.TextWrap = Text::TextWrap::None;
		}

		const auto node = Internal::CreateStyled(document, parent, field);
		document.SetText(node, std::move(fonts), {}, size);
		document.SetEditable(node, true, options);
		return node;
	}

	void SetLabelText(UiDocument& document, UiNodeId node, const std::string& text)
	{
		const auto& theme = document.GetTheme();

		if (!theme)
		{
			return;
		}

		SetLabelText(document, node, text, theme->Fonts, theme->Class(UiThemeClass::Label).TextSize);
	}

	void SetLabelText(UiDocument& document, UiNodeId node, const std::string& text,
		const std::shared_ptr<const Swim::Text::FontCollection>& fonts, float size)
	{
		if (!document.Contains(node) || !fonts || document.GetText(node) == text)
		{
			return;
		}

		document.SetText(node, fonts, text, size);
	}

	UiNodeId CreateStyledNode(UiDocument& document, UiNodeId parent, const UiStyle& style, UiThemeClass paintClass)
	{
		const auto node = document.Create(parent);

		if (paintClass != UiThemeClass::None)
		{
			document.SetThemeClass(node, paintClass, UiThemeApply::Paint);
		}

		document.SetStyle(node, style);
		return node;
	}

	UiNodeId CreateHeading(UiDocument& document, UiNodeId parent, const std::string& text)
	{
		const auto label = CreateLabel(document, parent, text);
		auto style = document.GetStyle(label);
		style.TextColor = UiSrgbHex(0x6fadff); // Authored in sRGB.
		style.Margin.Top = 6.0f;
		document.SetStyle(label, style);
		return label;
	}

	UiNodeId CreateRow(UiDocument& document, UiNodeId parent, float gap)
	{
		UiStyle style;
		style.Flow = UiFlow::Row;
		style.Gap = gap;
		style.AlignItems = UiAlign::Center;
		return CreateStyledNode(document, parent, style);
	}

} // namespace Swim::UI
