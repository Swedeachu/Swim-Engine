#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <cmath>
#include <stdexcept>

// Visual states, state rules, paint transitions and document themes (critical-path
// item 79). A node's displayed paint (UiResolvedVisual) is its style, then its theme
// class's rules, then its own rules, for its current UiState, eased over
// UiStyle::TransitionSeconds when the state changes.
namespace Swim::UI
{
	namespace
	{
		bool SameColor(const UiColor& a, const UiColor& b)
		{
			return a.R == b.R && a.G == b.G && a.B == b.B && a.A == b.A;
		}

		bool SameImage(const UiImage& a, const UiImage& b)
		{
			const auto rect = [](const UiRect& x, const UiRect& y)
			{
				return x.X == y.X && x.Y == y.Y && x.Width == y.Width && x.Height == y.Height;
			};
			const auto edges = [](const UiEdges& x, const UiEdges& y)
			{
				return x.Left == y.Left && x.Top == y.Top && x.Right == y.Right && x.Bottom == y.Bottom;
			};
			return a.Texture == b.Texture && a.Sampler == b.Sampler && a.Size.X == b.Size.X && a.Size.Y == b.Size.Y && rect(a.Uv, b.Uv) &&
				edges(a.Slice, b.Slice) && edges(a.SliceUv, b.SliceUv) && SameColor(a.Tint, b.Tint) && a.Fit == b.Fit;
		}

		bool SameVisual(const UiResolvedVisual& a, const UiResolvedVisual& b)
		{
			return SameColor(a.Background, b.Background) && SameColor(a.BorderColor, b.BorderColor) &&
				SameColor(a.TextColor, b.TextColor) && SameColor(a.ImageTint, b.ImageTint) && a.BorderWidth == b.BorderWidth &&
				a.CornerRadius == b.CornerRadius && a.Opacity == b.Opacity && a.HasImage == b.HasImage &&
				(!a.HasImage || SameImage(a.Image, b.Image));
		}

		float Mix(float a, float b, float t)
		{
			return a + (b - a) * t;
		}

		UiColor Mix(const UiColor& a, const UiColor& b, float t)
		{
			return { Mix(a.R, b.R, t), Mix(a.G, b.G, t), Mix(a.B, b.B, t), Mix(a.A, b.A, t) };
		}

		void Apply(UiResolvedVisual& visual, const UiVisual& rule)
		{
			if (rule.Background)
			{
				visual.Background = *rule.Background;
			}
			if (rule.BorderColor)
			{
				visual.BorderColor = *rule.BorderColor;
			}
			if (rule.TextColor)
			{
				visual.TextColor = *rule.TextColor;
			}
			if (rule.BorderWidth)
			{
				visual.BorderWidth = *rule.BorderWidth;
			}
			if (rule.CornerRadius)
			{
				visual.CornerRadius = *rule.CornerRadius;
			}
			if (rule.Opacity)
			{
				visual.Opacity = *rule.Opacity;
			}
			if (rule.Image)
			{
				visual.HasImage = true;
				visual.Image = *rule.Image;
				visual.ImageTint = rule.Image->Tint;
			}
			if (rule.ImageTint)
			{
				visual.ImageTint = *rule.ImageTint;
			}
		}

		bool Matches(UiState state, const UiStateRule& rule)
		{
			return HasState(state, rule.When) && (state & rule.Unless) == UiState::None;
		}
	} // namespace

	UiNodeId UiDocument::Impl::StateOwner(UiNodeId id) const
	{
		for (auto current = id; current; current = Get(current).Parent)
		{
			if (IsInteractive(Get(current)))
			{
				return current;
			}
		}
		return {};
	}

	UiState UiDocument::Impl::ComputeState(UiNodeId id) const
	{
		UiState state = UiState::None;
		if (const auto owner = StateOwner(id))
		{
			if (owner == Hover)
			{
				state = state | UiState::Hovered;
			}
			if (owner == Pressed)
			{
				state = state | UiState::Pressed;
			}
			if (owner == Focused)
			{
				state = state | UiState::Focused;
			}
			if (owner == Dragging)
			{
				state = state | UiState::Dragging;
			}
			const auto& control = Get(owner).Control;
			if (control.Kind == UiControlKind::Option)
			{
				// Checked while selected; Focused while its owner is focused and it is the
				// selection (or, in an open dropdown, the highlight keys move).
				const auto& option = Get(owner);
				if (option.PartOf && Nodes.contains(option.PartOf.Value) && IsSelectionOwner(Get(option.PartOf)))
				{
					const auto& group = Get(option.PartOf);
					const auto index = static_cast<std::int32_t>(std::lround(option.PartValue));
					const auto selected =
						std::isfinite(group.Control.Value) ? static_cast<std::int32_t>(std::lround(group.Control.Value)) : -1;
					if (index == selected)
					{
						state = state | UiState::Checked;
					}
					const bool open = IsDropdownOpen(group);
					if ((open && group.Highlight == index) || (!open && group.Id == Focused && index == selected))
					{
						state = state | UiState::Focused;
					}
					if (group.Control.ReadOnly)
					{
						state = state | UiState::ReadOnly;
					}
					if (!Available(group.Id))
					{
						state = state | UiState::Disabled;
					}
				}
			}
			else if (control.Kind != UiControlKind::None)
			{
				if (control.Check == UiCheckState::Checked)
				{
					state = state | UiState::Checked;
				}
				else if (control.Check == UiCheckState::Mixed)
				{
					state = state | UiState::Mixed;
				}
				if (control.ReadOnly)
				{
					state = state | UiState::ReadOnly;
				}
			}
		}
		if (!Available(id))
		{
			state = state | UiState::Disabled;
		}
		return state;
	}

	UiResolvedVisual UiDocument::Impl::ResolveTarget(const Node& node, UiState state) const
	{
		const auto& s = node.Style;
		UiResolvedVisual visual;
		visual.Background = s.Background;
		visual.BorderColor = s.BorderColor;
		visual.TextColor = s.TextColor;
		visual.BorderWidth = s.BorderWidth;
		visual.CornerRadius = s.CornerRadius;
		visual.Opacity = s.Opacity;
		visual.HasImage = node.HasImage;
		if (node.HasImage)
		{
			visual.Image = node.Image;
			visual.ImageTint = node.Image.Tint;
		}
		if (node.ThemeClass != UiThemeClass::None && HasApply(node.ThemeApply, UiThemeApply::Paint))
		{
			for (const auto& rule : Classes[static_cast<std::size_t>(node.ThemeClass)].Rules)
			{
				if (Matches(state, rule))
				{
					Apply(visual, rule.Visual);
				}
			}
		}
		for (const auto& rule : node.Rules)
		{
			if (Matches(state, rule))
			{
				Apply(visual, rule.Visual);
			}
		}
		return visual;
	}

	void UiDocument::Impl::ResolveVisuals()
	{
		for (auto& [key, node] : Nodes)
		{
			const auto state = ComputeState(node.Id);
			const bool stateChanged = state != node.LastState;
			if (!stateChanged && !node.VisualDirty && node.HasVisual)
			{
				continue;
			}
			node.LastState = state;
			node.VisualDirty = false;
			const auto target = ResolveTarget(node, state);
			if (!node.HasVisual)
			{
				node.Visual = target;
				node.HasVisual = true;
				node.PaintDirty = true;
				continue;
			}
			const auto& goal = node.Transitioning ? node.TransitionTo : node.Visual;
			if (SameVisual(goal, target))
			{
				continue;
			}
			// State changes ease; style, rule and theme changes snap.
			if (stateChanged && node.Style.TransitionSeconds > 0.0f)
			{
				node.TransitionFrom = node.Visual;
				node.TransitionFrom.HasImage = target.HasImage; // Images switch at once.
				node.TransitionFrom.Image = target.Image;
				node.TransitionTo = target;
				node.TransitionElapsed = 0.0f;
				node.TransitionDuration = node.Style.TransitionSeconds;
				node.Transitioning = true;
				node.Visual.HasImage = target.HasImage;
				node.Visual.Image = target.Image;
			}
			else
			{
				node.Visual = target;
				node.Transitioning = false;
			}
			node.PaintDirty = true;
		}
	}

	bool UiDocument::Impl::AdvanceTransitions(float seconds)
	{
		bool animating = false;
		for (auto& [key, node] : Nodes)
		{
			if (!node.Transitioning)
			{
				continue;
			}
			node.TransitionElapsed += seconds;
			const float t = node.TransitionDuration > 0.0f ? std::min(1.0f, node.TransitionElapsed / node.TransitionDuration) : 1.0f;
			const float e = t * t * (3.0f - 2.0f * t); // Smoothstep.
			const auto& a = node.TransitionFrom;
			const auto& b = node.TransitionTo;
			node.Visual.Background = Mix(a.Background, b.Background, e);
			node.Visual.BorderColor = Mix(a.BorderColor, b.BorderColor, e);
			node.Visual.TextColor = Mix(a.TextColor, b.TextColor, e);
			node.Visual.ImageTint = Mix(a.ImageTint, b.ImageTint, e);
			node.Visual.BorderWidth = Mix(a.BorderWidth, b.BorderWidth, e);
			node.Visual.CornerRadius = Mix(a.CornerRadius, b.CornerRadius, e);
			node.Visual.Opacity = Mix(a.Opacity, b.Opacity, e);
			node.PaintDirty = true;
			if (t >= 1.0f)
			{
				node.Visual = b;
				node.Transitioning = false;
			}
			else
			{
				animating = true;
			}
		}
		return animating;
	}

	void UiDocument::Impl::MarkSubtreeVisualDirty(UiNodeId id)
	{
		if (!id || !Nodes.contains(id.Value))
		{
			return;
		}
		auto& node = Get(id);
		node.VisualDirty = true;
		node.PaintDirty = true;
		for (const auto child : node.Children)
		{
			MarkSubtreeVisualDirty(child);
		}
	}

	void UiDocument::Impl::ApplyTheme(Node& node)
	{
		if (node.ThemeClass == UiThemeClass::None)
		{
			return;
		}
		const auto& themed = Classes[static_cast<std::size_t>(node.ThemeClass)];
		const auto& from = themed.Style;
		UiStyle style = node.Style;
		// Class styles describe horizontal controls; vertical ones swap axes.
		const Node* control = IsControl(node) ? &node : (node.PartOf && Nodes.contains(node.PartOf.Value) ? &Get(node.PartOf) : nullptr);
		const bool vertical =
			control && control->Control.Orientation == UiOrientation::Vertical && node.Role != UiPartRole::Label; // Text stays horizontal.
		if (HasApply(node.ThemeApply, UiThemeApply::Layout))
		{
			style.Width = vertical ? from.Height : from.Width;
			style.Height = vertical ? from.Width : from.Height;
			style.MinSize = vertical ? UiPoint{ from.MinSize.Y, from.MinSize.X } : from.MinSize;
			style.MaxSize = vertical ? UiPoint{ from.MaxSize.Y, from.MaxSize.X } : from.MaxSize;
			style.Padding =
				vertical ? UiEdges{ from.Padding.Top, from.Padding.Left, from.Padding.Bottom, from.Padding.Right } : from.Padding;
			style.Gap = from.Gap;
		}
		if (HasApply(node.ThemeApply, UiThemeApply::Paint))
		{
			style.Background = from.Background;
			style.BorderColor = from.BorderColor;
			style.BorderWidth = from.BorderWidth;
			style.CornerRadius = from.CornerRadius;
			style.TextColor = from.TextColor;
			style.SelectionColor = from.SelectionColor;
			style.CaretColor = from.CaretColor;
			style.Opacity = from.Opacity;
			style.TransitionSeconds = from.TransitionSeconds;
		}
		const bool paintOnly = Internal::OnlyPaintChanged(node.Style, style);
		node.Style = style;
		node.VisualDirty = true;
		node.PaintDirty = true;
		if (!paintOnly)
		{
			MarkLayoutDirty(node.Id);
		}
		if (HasApply(node.ThemeApply, UiThemeApply::Text) && node.Fonts && Theme->Fonts &&
			(node.Fonts != Theme->Fonts || node.FontSize != themed.TextSize))
		{
			node.Fonts = Theme->Fonts;
			node.FontSize = themed.TextSize;
			node.TextLayout.reset();
			node.MeasureLayout.reset();
			node.Selection = ClampSelection(node, node.Selection);
			MarkLayoutDirty(node.Id);
		}
	}

	void UiDocument::SetStateRules(UiNodeId id, std::vector<UiStateRule> rules)
	{
		for (const auto& rule : rules)
		{
			Internal::ValidateVisual(rule.Visual);
		}
		auto& node = impl->Get(id);
		node.Rules = std::move(rules);
		node.VisualDirty = true;
	}

	const std::vector<UiStateRule>& UiDocument::GetStateRules(UiNodeId id) const
	{
		return impl->Get(id).Rules;
	}

	UiState UiDocument::GetState(UiNodeId id) const
	{
		impl->Get(id); // Throws for unknown nodes.
		return impl->ComputeState(id);
	}

	const UiResolvedVisual& UiDocument::GetVisual(UiNodeId id) const
	{
		return impl->Get(id).Visual;
	}

	void UiDocument::SetTheme(std::shared_ptr<const UiTheme> theme)
	{
		if (!theme)
		{
			throw std::invalid_argument("A UI document needs a theme");
		}
		auto classes = theme->Build(); // Validates before anything changes.
		impl->Theme = std::move(theme);
		impl->Classes = std::move(classes);
		for (auto& [key, node] : impl->Nodes)
		{
			if (node.ThemeClass != UiThemeClass::None)
			{
				impl->ApplyTheme(node);
			}
		}
		impl->ClearUnavailable();
	}

	const std::shared_ptr<const UiTheme>& UiDocument::GetTheme() const
	{
		return impl->Theme;
	}

	void UiDocument::SetThemeClass(UiNodeId id, UiThemeClass themeClass, UiThemeApply apply)
	{
		if (static_cast<std::uint8_t>(themeClass) >= static_cast<std::uint8_t>(UiThemeClass::Count) ||
			static_cast<std::uint8_t>(apply) > static_cast<std::uint8_t>(UiThemeApply::All))
		{
			throw std::invalid_argument("Invalid UI theme class");
		}
		auto& node = impl->Get(id);
		node.ThemeClass = themeClass;
		node.ThemeApply = themeClass == UiThemeClass::None ? UiThemeApply::None : apply;
		node.VisualDirty = true;
		node.PaintDirty = true;
		impl->ApplyTheme(node);
		impl->ClearUnavailable();
	}

	UiThemeClass UiDocument::GetThemeClass(UiNodeId id) const
	{
		return impl->Get(id).ThemeClass;
	}

	bool UiDocument::Update(float seconds)
	{
		if (!std::isfinite(seconds) || seconds < 0.0f || seconds > 3600.0f)
		{
			throw std::invalid_argument("UI update needs 0 .. 3600 seconds");
		}
		impl->ResolveVisuals();
		const bool transitions = impl->AdvanceTransitions(seconds);
		const bool controls = impl->AnimateControls(seconds);
		impl->UpdateTooltips(seconds);
		const bool tooltip = impl->TooltipTarget && !impl->TooltipShown && !impl->TooltipSuppressed;
		return transitions || controls || tooltip;
	}

	bool UiDocument::IsAnimating() const
	{
		if (impl->TooltipTarget && !impl->TooltipShown && !impl->TooltipSuppressed)
		{
			return true; // A tooltip delay is running.
		}
		for (const auto& [key, node] : impl->Nodes)
		{
			if (node.Transitioning)
			{
				return true;
			}
			const auto& c = node.Control;
			if (c.Kind == UiControlKind::Toggle && node.Knob != (c.Check == UiCheckState::Checked ? 1.0f : 0.0f) &&
				!(impl->Dragging == node.Id && impl->DragMoved))
			{
				return true;
			}
			if (c.Kind == UiControlKind::ScrollBar && c.Visibility == UiScrollBarVisibility::Overlay && !node.ControlHidden &&
				node.ControlOpacity > 0.0f && (node.ControlOpacity < 1.0f || node.ScrollActivity <= c.FadeDelaySeconds + c.FadeSeconds))
			{
				return true;
			}
		}
		return false;
	}

	std::uint64_t UiDocument::GetPaintRevision() const
	{
		return impl->PaintRevision;
	}
} // namespace Swim::UI
