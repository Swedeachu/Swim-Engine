#include "Engine/Systems/UI/Widgets/UiColorPicker.h"

#include "Engine/Math/ColorMath.h"
#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/UiControlBehavior.h"
#include "Engine/Systems/UI/Widgets/UiBasicWidgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace Swim::UI
{

	namespace
	{

		// sRGB-encoded HSV to linear RGB.
		std::array<float, 3> HsvToLinear(float h, float s, float v)
		{
			const float sector = std::fmod(std::max(h, 0.0f), 1.0f) * 6.0f;
			const float c = v * s;
			const float x = c * (1.0f - std::abs(std::fmod(sector, 2.0f) - 1.0f));
			const float m = v - c;
			std::array<float, 3> rgb{};

			switch (static_cast<int>(sector) % 6)
			{
			case 0:
				rgb = { c, x, 0.0f };
				break;
			case 1:
				rgb = { x, c, 0.0f };
				break;
			case 2:
				rgb = { 0.0f, c, x };
				break;
			case 3:
				rgb = { 0.0f, x, c };
				break;
			case 4:
				rgb = { x, 0.0f, c };
				break;
			default:
				rgb = { c, 0.0f, x };
				break;
			}

			for (auto& channel : rgb)
			{
				channel = Engine::SrgbToLinear(std::clamp(channel + m, 0.0f, 1.0f));
			}

			return rgb;
		}

		// Linear RGB to sRGB-encoded HSV (h in [0, 1)).
		std::array<float, 3> LinearToHsv(const UiColor& color, float keepHue)
		{
			const float r = Engine::LinearToSrgb(std::clamp(color.R, 0.0f, 1.0f));
			const float g = Engine::LinearToSrgb(std::clamp(color.G, 0.0f, 1.0f));
			const float b = Engine::LinearToSrgb(std::clamp(color.B, 0.0f, 1.0f));
			const float high = std::max({ r, g, b });
			const float range = high - std::min({ r, g, b });
			float h = keepHue;

			if (range > 1.0e-5f)
			{
				h = high == r ? std::fmod((g - b) / range + 6.0f, 6.0f) : high == g ? (b - r) / range + 2.0f : (r - g) / range + 4.0f;
				h /= 6.0f;
			}

			return { h, high > 0.0f ? range / high : 0.0f, high };
		}

		// A press-and-drag area reporting the pointer as fractions of its bounds.
		class PickArea final : public UiControlBehavior
		{

		  public:
			std::function<void(float x, float y, bool done)> OnPick;

			UiPointerResponse OnPointerDown(UiControlContext& context, const UiPointerInput& pointer) override
			{
				Report(context, pointer, false);
				return UiPointerResponse::Capture;
			}

			void OnPointerDrag(UiControlContext& context, const UiPointerInput& pointer) override { Report(context, pointer, false); }

			void OnPointerUp(UiControlContext&, bool, bool captured) override
			{
				if (captured && OnPick)
				{
					OnPick(lastX, lastY, true);
				}
			}

			void OnPointerCancel(UiControlContext& context, bool captured) override { OnPointerUp(context, false, captured); }

		  private:
			void Report(UiControlContext& context, const UiPointerInput& pointer, bool done)
			{
				const UiRect box = context.GetBounds(context.GetNode());
				lastX = box.Width > 0.0f ? std::clamp((pointer.Position.X - box.X) / box.Width, 0.0f, 1.0f) : 0.0f;
				lastY = box.Height > 0.0f ? std::clamp((pointer.Position.Y - box.Y) / box.Height, 0.0f, 1.0f) : 0.0f;

				if (OnPick)
				{
					OnPick(lastX, lastY, done);
				}
			}

			float lastX = 0.0f;
			float lastY = 0.0f;
		};

		struct PickerState
		{
			UiDocument* Document = nullptr;
			float H = 0.0f, S = 0.0f, V = 1.0f, A = 1.0f;
			bool Alpha = true;
			UiNodeId Square, SquareMarker, HueMarker, AlphaFill, AlphaMarker, Preview, Hex;
			UiColorCallback Change, Commit;

			UiColor Color() const
			{
				const auto rgb = HsvToLinear(H, S, V);
				return { rgb[0], rgb[1], rgb[2], Alpha ? A : 1.0f };
			}

			void SetStyle(UiNodeId node, const std::function<void(UiStyle&)>& edit) const
			{
				auto style = Document->GetStyle(node);
				edit(style);
				Document->SetStyle(node, style);
			}

			void Refresh(bool writeHex) const
			{
				const auto pure = HsvToLinear(H, 1.0f, 1.0f);
				const UiColor color = Color();
				SetStyle(Square,
					[&](UiStyle& s)
					{
						s.BackgroundEnd = { pure[0], pure[1], pure[2], 1.0f };
					});
				SetStyle(SquareMarker,
					[&](UiStyle& s)
					{
						s.AnchorMin = s.AnchorMax = { S, 1.0f - V };
					});
				SetStyle(HueMarker,
					[&](UiStyle& s)
					{
						s.AnchorMin = s.AnchorMax = { H, 0.5f };
					});

				if (Alpha)
				{
					SetStyle(AlphaFill,
						[&](UiStyle& s)
						{
							s.Background = { color.R, color.G, color.B, 0.0f };
							s.BackgroundEnd = { color.R, color.G, color.B, 1.0f };
						});
					SetStyle(AlphaMarker,
						[&](UiStyle& s)
						{
							s.AnchorMin = s.AnchorMax = { A, 0.5f };
						});
				}

				SetStyle(Preview,
					[&](UiStyle& s)
					{
						s.Background = color;
					});

				if (writeHex && Document->GetFocus() != Hex)
				{
					const auto byte = [](float linear)
					{
						return static_cast<unsigned>(
							std::lround(std::clamp(Engine::LinearToSrgb(std::clamp(linear, 0.0f, 1.0f)), 0.0f, 1.0f) * 255.0f));
					};
					char text[16];

					if (Alpha)
					{
						std::snprintf(text, sizeof(text), "#%02X%02X%02X%02X", byte(color.R), byte(color.G), byte(color.B),
							static_cast<unsigned>(std::lround(std::clamp(color.A, 0.0f, 1.0f) * 255.0f)));
					}
					else
					{
						std::snprintf(text, sizeof(text), "#%02X%02X%02X", byte(color.R), byte(color.G), byte(color.B));
					}

					SetLabelText(
						*Document, Hex, text, Internal::ThemeFonts(*Document), Internal::ThemeTextSize(*Document, UiThemeClass::TextField));
				}
			}

			void Changed(bool commit) const
			{
				Refresh(true);
				const UiColor color = Color();

				if (Change)
				{
					Change(color);
				}

				if (commit && Commit)
				{
					Commit(color);
				}
			}

			bool ParseHex(std::string text)
			{
				std::erase_if(text,
					[](char c)
					{
						return c == ' ' || c == '#';
					});

				if (text.size() != 6 && !(Alpha && text.size() == 8))
				{
					return false;
				}

				std::array<float, 4> channels{ 0.0f, 0.0f, 0.0f, 1.0f };

				for (std::size_t i = 0; i < text.size() / 2; ++i)
				{
					unsigned value = 0;

					if (std::sscanf(text.substr(i * 2, 2).c_str(), "%2x", &value) != 1)
					{
						return false;
					}

					channels[i] = float(value) / 255.0f;
				}

				const UiColor linear{ Engine::SrgbToLinear(channels[0]), Engine::SrgbToLinear(channels[1]),
					Engine::SrgbToLinear(channels[2]), channels[3] };
				const auto hsv = LinearToHsv(linear, H);
				H = hsv[0];
				S = hsv[1];
				V = hsv[2];
				A = channels[3];
				return true;
			}
		};

		UiNodeId Box(UiDocument& document, UiNodeId parent, const UiStyle& style)
		{
			return Internal::CreateStyled(document, parent, style);
		}

		UiStyle Stretched()
		{
			UiStyle s;
			s.Absolute = true;
			s.AnchorMin = { 0.0f, 0.0f };
			s.AnchorMax = { 1.0f, 1.0f };
			return s;
		}

		UiStyle Marker(float width, float height)
		{
			UiStyle s;
			s.Absolute = true;
			s.Pivot = { 0.5f, 0.5f };
			s.Width = UiLength::Pixels(width);
			s.Height = UiLength::Pixels(height);
			s.CornerRadius = std::min(width, height) * 0.5f;
			s.BorderWidth = 2.0f;
			s.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f };
			return s;
		}

		UiStyle Strip(float width)
		{
			UiStyle s;
			s.Width = UiLength::Pixels(width);
			s.Height = UiLength::Pixels(14.0f);
			s.Flow = UiFlow::Row;
			s.AlignItems = UiAlign::Stretch;
			s.HitTest = true;
			s.CornerRadius = 3.0f;
			return s;
		}

	} // namespace

	UiNodeId CreateColorPicker(
		UiDocument& document, UiNodeId parent, const UiColorPickerDesc& desc, UiColorCallback onChange, UiColorCallback onCommit)
	{
		auto state = std::make_shared<PickerState>();
		state->Document = &document;
		state->Alpha = desc.Alpha;
		state->Change = std::move(onChange);
		state->Commit = std::move(onCommit);
		const auto hsv = LinearToHsv(desc.Color, 0.0f);
		state->H = hsv[0];
		state->S = hsv[1];
		state->V = hsv[2];
		state->A = std::clamp(desc.Color.A, 0.0f, 1.0f);
		const float size = std::clamp(std::isfinite(desc.Size) ? desc.Size : 168.0f, 48.0f, 1024.0f);

		UiStyle column;
		column.Flow = UiFlow::Column;
		column.Gap = 8.0f;
		const auto root = Box(document, parent, column);

		// Saturation (right) and value (up): white to the hue, then darkened toward the bottom.
		UiStyle square;
		square.Width = UiLength::Pixels(size);
		square.Height = UiLength::Pixels(size);
		square.HitTest = true;
		square.Background = { 1.0f, 1.0f, 1.0f, 1.0f };
		square.Gradient = UiGradient::Horizontal;
		state->Square = Box(document, root, square);
		auto shade = Stretched();
		shade.Background = { 0.0f, 0.0f, 0.0f, 0.0f };
		shade.Gradient = UiGradient::Vertical;
		shade.BackgroundEnd = { 0.0f, 0.0f, 0.0f, 1.0f };
		Box(document, state->Square, shade);
		state->SquareMarker = Box(document, state->Square, Marker(12.0f, 12.0f));

		// Hue: six segments between the primaries and secondaries.
		const auto hue = Box(document, root, Strip(size));

		for (int i = 0; i < 6; ++i)
		{
			UiStyle segment;
			segment.Grow = 1.0f;
			const auto from = HsvToLinear(float(i) / 6.0f, 1.0f, 1.0f);
			const auto to = HsvToLinear(float(i + 1) / 6.0f, 1.0f, 1.0f);
			segment.Background = { from[0], from[1], from[2], 1.0f };
			segment.BackgroundEnd = { to[0], to[1], to[2], 1.0f };
			segment.Gradient = UiGradient::Horizontal;
			Box(document, hue, segment);
		}

		state->HueMarker = Box(document, hue, Marker(6.0f, 18.0f));

		if (desc.Alpha)
		{
			auto strip = Strip(size);
			strip.Background = { 0.18f, 0.18f, 0.18f, 1.0f };
			const auto alpha = Box(document, root, strip);
			auto fill = Stretched();
			fill.Gradient = UiGradient::Horizontal;
			fill.CornerRadius = 3.0f;
			state->AlphaFill = Box(document, alpha, fill);
			state->AlphaMarker = Box(document, alpha, Marker(6.0f, 18.0f));
			auto& area = document.AttachControl<PickArea>(alpha);
			area.OnPick = [state](float x, float, bool done)
			{
				state->A = x;
				state->Changed(done);
			};
		}

		UiStyle row;
		row.Flow = UiFlow::Row;
		row.Gap = 8.0f;
		row.AlignItems = UiAlign::Center;
		const auto line = Box(document, root, row);
		UiStyle preview;
		preview.Width = UiLength::Pixels(28.0f);
		preview.Height = UiLength::Pixels(28.0f);
		preview.CornerRadius = 4.0f;
		preview.BorderWidth = 1.0f;
		preview.BorderColor = { 1.0f, 1.0f, 1.0f, 0.35f };
		state->Preview = Box(document, line, preview);
		UiTextEditOptions options;
		options.MaxBytes = 12;
		state->Hex = CreateTextField(document, line, options);
		auto hexStyle = document.GetStyle(state->Hex);
		hexStyle.Width = UiLength::Pixels(std::max(size - 36.0f, 60.0f));
		document.SetStyle(state->Hex, hexStyle);

		auto& squareArea = document.AttachControl<PickArea>(state->Square);
		squareArea.OnPick = [state](float x, float y, bool done)
		{
			state->S = x;
			state->V = 1.0f - y;
			state->Changed(done);
		};
		auto& hueArea = document.AttachControl<PickArea>(hue);
		hueArea.OnPick = [state](float x, float, bool done)
		{
			state->H = std::min(x, 0.9999f);
			state->Changed(done);
		};

		const auto typed = [state](const UiEvent&)
		{
			if (state->ParseHex(state->Document->GetText(state->Hex)))
			{
				state->Changed(true);
			}
			else
			{
				state->Refresh(true);
			}
		};
		document.On(state->Hex, UiEventKind::Submit, typed);
		document.On(state->Hex, UiEventKind::Blur, typed);
		state->Refresh(true);
		return root;
	}

	UiNodeId OpenColorPicker(
		UiDocument& document, UiNodeId anchor, const UiColorPickerDesc& desc, UiColorCallback onChange, UiColorCallback onCommit)
	{
		UiStyle popupStyle;
		popupStyle.Visible = false; // Shown and placed by OpenPopup.
		popupStyle.Absolute = true;
		popupStyle.HitTest = true;
		const float padding = Internal::ThemeOf(document).Metrics.PopupPadding;
		popupStyle.Padding = { padding, padding, padding, padding };
		const auto popup = Internal::CreateStyled(document, document.GetRoot(), popupStyle);
		document.SetThemeClass(popup, UiThemeClass::Popup);
		auto last = std::make_shared<UiColor>(desc.Color);
		auto commit = std::make_shared<UiColorCallback>(std::move(onCommit));
		CreateColorPicker(
			document, popup, desc,
			[last, onChange = std::move(onChange)](const UiColor& color)
			{
				*last = color;

				if (onChange)
				{
					onChange(color);
				}
			},
			[last, commit](const UiColor& color)
			{
				*last = color;

				if (*commit)
				{
					(*commit)(color);
				}
			});
		document.On(popup, UiEventKind::PopupClosed,
			[&document, popup, last, commit](const UiEvent&)
			{
				if (*commit)
				{
					(*commit)(*last);
				}

				document.Remove(popup);
			});
		UiPopupDesc placement;
		placement.Anchor = anchor;
		placement.Side = UiPopupSide::Below;
		placement.Offset = { 0.0f, 4.0f };
		placement.FocusFirst = false;
		document.OpenPopup(popup, placement);
		return popup;
	}

} // namespace Swim::UI
