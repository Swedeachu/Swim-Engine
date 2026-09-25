#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"
#include "Engine/Systems/Text/Utf8.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::UI
{
	namespace
	{
		// Committed text accepted by a field: valid UTF-8 without other C0 controls; a
		// single-line field turns line breaks into nothing, a multiline one keeps LF.
		std::string FilterInput(std::string_view input, bool multiline)
		{
			const auto text = Text::SanitizeUtf8(input);
			std::string result;
			result.reserve(text.size());
			for (std::size_t i = 0; i < text.size(); ++i)
			{
				const char c = text[i];
				if (c == '\r')
				{
					if (multiline && (i + 1 >= text.size() || text[i + 1] != '\n'))
					{
						result.push_back('\n'); // Lone CR becomes LF.
					}
					continue;
				}
				if (c == '\n')
				{
					if (multiline)
					{
						result.push_back('\n');
					}
					continue;
				}
				if (static_cast<unsigned char>(c) < 0x20 && c != '\t')
				{
					continue;
				}
				result.push_back(c);
			}
			return result;
		}

		// The longest prefix of text, at most `limit` bytes, ending on a grapheme boundary.
		std::size_t GraphemePrefix(const std::string& text, std::size_t limit)
		{
			if (text.size() <= limit)
			{
				return text.size();
			}
			const auto boundaries = Text::FindTextBoundaries(text);
			std::size_t cut = limit;
			while (cut > 0 && !boundaries.Grapheme[cut])
			{
				--cut;
			}
			return cut;
		}
	} // namespace

	UiTextSelection UiDocument::Impl::ClampSelection(Node& node, UiTextSelection selection)
	{
		const auto size = static_cast<std::uint32_t>(node.TextContents.size());
		selection.Anchor = std::min(selection.Anchor, size);
		selection.Caret = std::min(selection.Caret, size);
		if (!node.Fonts || node.TextContents.empty())
		{
			return { 0, 0 };
		}
		if (IsComposing(node))
		{
			return selection;
		}
		const auto& layout = EditLayout(node);
		const auto snap = [&](std::uint32_t offset)
		{
			while (offset > 0 && !layout.IsCaretStop(offset))
			{
				--offset;
			}
			return offset;
		};
		return { snap(selection.Anchor), snap(selection.Caret) };
	}

	void UiDocument::Impl::EndComposition()
	{
		if (!Composition.empty())
		{
			Composition.clear();
			if (Focused)
			{
				MarkLayoutDirty(Focused);
			}
		}
	}

	std::uint32_t UiDocument::Impl::TextHit(Node& node, UiPoint logical)
	{
		if (!node.Fonts)
		{
			return 0;
		}
		const auto& layout = EditLayout(node);
		const UiRect content = Internal::ContentBox(node.Bounds, node.Style.Padding);
		return layout.HitTest(logical.X - (content.X - node.Scroll.X), logical.Y - (content.Y - node.Scroll.Y));
	}

	void UiDocument::Impl::SetCaret(Node& node, std::uint32_t caret, bool extend)
	{
		const auto previous = node.Selection;
		node.Selection.Caret = caret;
		if (!extend)
		{
			node.Selection.Anchor = caret;
		}
		node.Selection = ClampSelection(node, node.Selection);
		if (node.Selection.Anchor != previous.Anchor || node.Selection.Caret != previous.Caret)
		{
			node.RevealCaret = true;
			node.PaintDirty = true;
			Dirty = true; // Revealing the caret may scroll; arrangement only.
		}
	}

	void UiDocument::Impl::ReplaceSelection(Node& node, std::string_view input)
	{
		const std::string text = FilterInput(input, node.EditOptions.Multiline);
		const auto begin = std::min(node.Selection.Anchor, node.Selection.Caret);
		const auto end = std::max(node.Selection.Anchor, node.Selection.Caret);
		const std::size_t kept = node.TextContents.size() - (end - begin);
		const std::size_t room = node.EditOptions.MaxBytes > kept ? node.EditOptions.MaxBytes - kept : 0;
		const std::size_t inserted = GraphemePrefix(text, room);
		if (begin == end && inserted == 0)
		{
			return;
		}
		node.TextContents.replace(begin, end - begin, text.substr(0, inserted));
		node.Selection = { static_cast<std::uint32_t>(begin + inserted), static_cast<std::uint32_t>(begin + inserted) };
		node.PreferredCaretX = std::numeric_limits<float>::quiet_NaN();
		node.RevealCaret = true;
		MarkLayoutDirty(node.Id);
		node.Selection = ClampSelection(node, node.Selection);
		Events.push_back({ UiEventKind::TextChanged, node.Id });
	}

	bool UiDocument::Impl::EditKey(Node& node, UiKey key, UiKeyModifiers modifiers)
	{
		if (!Composition.empty())
		{
			// The IME owns the keyboard while composing; Escape abandons the preedit.
			if (key == UiKey::Escape)
			{
				EndComposition();
			}
			return true;
		}
		const auto& layout = EditLayout(node);
		const auto size = static_cast<std::uint32_t>(node.TextContents.size());
		const auto begin = std::min(node.Selection.Anchor, node.Selection.Caret);
		const auto end = std::max(node.Selection.Anchor, node.Selection.Caret);
		const bool selection = begin != end;
		const auto caret = node.Selection.Caret;
		const bool vertical = key == UiKey::Up || key == UiKey::Down;
		if (!vertical)
		{
			node.PreferredCaretX = std::numeric_limits<float>::quiet_NaN();
		}
		switch (key)
		{
		case UiKey::Left:
		case UiKey::Right:
		{
			const bool forward = key == UiKey::Right;
			if (selection && !modifiers.Shift)
			{
				SetCaret(node, forward ? end : begin, false);
				return true;
			}
			const auto target = modifiers.Control ? (forward ? layout.NextWord(caret) : layout.PreviousWord(caret))
												  : (forward ? layout.NextCaretStop(caret) : layout.PreviousCaretStop(caret));
			SetCaret(node, target, modifiers.Shift);
			return true;
		}
		case UiKey::Up:
		case UiKey::Down:
		{
			if (!node.EditOptions.Multiline)
			{
				SetCaret(node, key == UiKey::Up ? 0 : size, modifiers.Shift);
				return true;
			}
			if (std::isnan(node.PreferredCaretX))
			{
				node.PreferredCaretX = layout.GetCaret(caret).X;
			}
			const float x = node.PreferredCaretX;
			SetCaret(node, key == UiKey::Up ? layout.LineAbove(caret, x) : layout.LineBelow(caret, x), modifiers.Shift);
			node.PreferredCaretX = x;
			return true;
		}
		case UiKey::Home:
			SetCaret(node, modifiers.Control ? 0 : layout.LineStart(caret), modifiers.Shift);
			return true;
		case UiKey::End:
			SetCaret(node, modifiers.Control ? size : layout.LineEnd(caret), modifiers.Shift);
			return true;
		case UiKey::Backspace:
		case UiKey::Delete:
		{
			if (!selection)
			{
				const bool forward = key == UiKey::Delete;
				const auto other = modifiers.Control ? (forward ? layout.NextWord(caret) : layout.PreviousWord(caret))
													 : (forward ? layout.NextCaretStop(caret) : layout.PreviousCaretStop(caret));
				if (other == caret)
				{
					return true;
				}
				node.Selection = { caret, other };
			}
			ReplaceSelection(node, {}); // Deletes the range.
			return true;
		}
		case UiKey::Enter:
			if (node.EditOptions.Multiline)
			{
				ReplaceSelection(node, "\n");
			}
			else
			{
				Events.push_back({ UiEventKind::Submit, node.Id });
			}
			return true;
		case UiKey::Escape:
			if (selection)
			{
				SetCaret(node, caret, false);
			}
			return true;
		case UiKey::A:
			if (!modifiers.Control)
			{
				return false;
			}
			node.Selection = { 0, 0 };
			SetCaret(node, size, true);
			return true;
		case UiKey::C:
		case UiKey::X:
			if (!modifiers.Control)
			{
				return false;
			}
			if (selection && Clipboard.Write)
			{
				Clipboard.Write(std::string_view(node.TextContents).substr(begin, end - begin));
				if (key == UiKey::X)
				{
					ReplaceSelection(node, {});
				}
			}
			return true;
		case UiKey::V:
			if (!modifiers.Control)
			{
				return false;
			}
			if (Clipboard.Read)
			{
				ReplaceSelection(node, Clipboard.Read());
			}
			return true;
		default:
			return false; // Space arrives as text input.
		}
	}

	void UiDocument::SetEditable(UiNodeId id, bool editable, const UiTextEditOptions& options)
	{
		if (options.MaxBytes == 0 || options.MaxBytes > 1024u * 1024u)
		{
			throw std::invalid_argument("UI text field byte limit must be 1 .. 1 MiB");
		}
		auto& node = impl->Get(id);
		node.Editable = editable;
		node.EditOptions = options;
		if (!editable && node.Id == impl->Focused)
		{
			impl->EndComposition();
		}
		impl->MarkLayoutDirty(id);
		impl->ClearUnavailable();
	}

	bool UiDocument::IsEditable(UiNodeId id) const
	{
		return impl->Get(id).Editable;
	}

	UiTextSelection UiDocument::GetSelection(UiNodeId id) const
	{
		return impl->Get(id).Selection;
	}

	void UiDocument::SetSelection(UiNodeId id, UiTextSelection selection)
	{
		auto& node = impl->Get(id);
		node.Selection = impl->ClampSelection(node, selection);
		node.RevealCaret = true;
		node.PaintDirty = true;
		impl->Dirty = true;
	}
} // namespace Swim::UI
