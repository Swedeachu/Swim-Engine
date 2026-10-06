#include "Engine/Systems/UI/Widgets/UiModal.h"

#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/Widgets/UiButton.h"

namespace Swim::UI
{

	UiModal CreateModal(UiDocument& document, std::string title)
	{
		UiModal modal;
		UiStyle scrim;
		scrim.Visible = false;
		scrim.Absolute = true;
		scrim.HitTest = true; // Presses on the page below land here.
		scrim.Width = UiLength::Percent(1.0f);
		scrim.Height = UiLength::Percent(1.0f);
		scrim.Flow = UiFlow::Column;
		scrim.Justify = UiJustify::Center;
		scrim.AlignItems = UiAlign::Center;
		modal.Root = Internal::CreateStyled(document, document.GetRoot(), scrim);
		document.SetThemeClass(modal.Root, UiThemeClass::ModalScrim, UiThemeApply::Paint);
		UiStyle dialog;
		dialog.Flow = UiFlow::Column;
		dialog.AlignItems = UiAlign::Stretch;
		modal.Dialog = Internal::CreateStyled(document, modal.Root, dialog);
		document.SetThemeClass(modal.Dialog, UiThemeClass::Dialog);

		if (!title.empty())
		{
			modal.Title = document.Create(modal.Dialog);
			document.SetText(
				modal.Title, Internal::ThemeFonts(document), std::move(title), Internal::ThemeTextSize(document, UiThemeClass::DialogTitle));
			document.SetThemeClass(modal.Title, UiThemeClass::DialogTitle);
		}

		UiStyle content;
		content.Flow = UiFlow::Column;
		content.Gap = Internal::ThemeOf(document).Metrics.Spacing;
		modal.Content = Internal::CreateStyled(document, modal.Dialog, content);
		UiStyle buttons;
		buttons.Flow = UiFlow::Row;
		buttons.Justify = UiJustify::End;
		buttons.Gap = Internal::ThemeOf(document).Metrics.Spacing;
		modal.Buttons = Internal::CreateStyled(document, modal.Dialog, buttons);
		return modal;
	}

	UiNodeId AddModalButton(UiDocument& document, const UiModal& modal, std::string label)
	{
		return CreateButton(document, modal.Buttons, std::move(label));
	}

	void OpenModal(UiDocument& document, const UiModal& modal)
	{
		UiPopupDesc desc;
		desc.Side = UiPopupSide::Center;
		desc.Modal = true;
		desc.LightDismiss = false;
		document.OpenPopup(modal.Root, desc);
	}

} // namespace Swim::UI
