#include "Engine/Systems/Replay/ReplaySequence.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace Swim::Replay
{

	using Tooling::Json;

	const char* ToString(TransitionKind kind)
	{
		switch (kind)
		{
		case TransitionKind::Crossfade:
			return "crossfade";
		case TransitionKind::Fade:
			return "fade";
		default:
			return "cut";
		}
	}

	bool ParseTransitionKind(std::string_view text, TransitionKind& kind)
	{
		for (const TransitionKind k : { TransitionKind::Cut, TransitionKind::Crossfade, TransitionKind::Fade })
		{
			if (text == ToString(k))
			{
				kind = k;
				return true;
			}
		}

		return false;
	}

	namespace
	{

		double Finite(double value, double fallback)
		{
			return std::isfinite(value) ? value : fallback;
		}

		// The fade half-length of an item's Enter transition (limited to half of each side).
		double FadeHalf(const SequenceItem& item, std::span<const SequenceSpan> spans, std::size_t index)
		{
			if (item.Enter.Kind != TransitionKind::Fade || !(item.Enter.Duration > 0.0))
			{
				return 0.0;
			}

			double half = item.Enter.Duration * 0.5;
			half = std::min(half, spans[index].End - spans[index].Start);

			if (index > 0)
			{
				half = std::min(half, spans[index - 1].End - spans[index - 1].Start);
			}

			return std::max(half, 0.0);
		}

	} // namespace

	std::vector<SequenceSpan> ReplaySequence::Layout(std::span<const double> itemLengths) const
	{
		std::vector<SequenceSpan> spans;
		spans.reserve(Items.size());

		for (std::size_t i = 0; i < Items.size(); ++i)
		{
			const SequenceItem& item = Items[i];
			const double length = i < itemLengths.size() ? std::max(Finite(itemLengths[i], 0.0), 0.0) : 0.0;
			const double in = std::clamp(Finite(item.In, 0.0), 0.0, length);
			const double out = Finite(item.Out, -1.0) > in ? std::min(item.Out, length) : length;
			const double duration = std::max(out - in, 0.0);
			double start = spans.empty() ? 0.0 : spans.back().End;

			if (!spans.empty() && item.Enter.Kind == TransitionKind::Crossfade && item.Enter.Duration > 0.0)
			{
				const double previous = spans.back().End - spans.back().Start;
				start -= std::min({ item.Enter.Duration, previous * 0.5, duration * 0.5 });
			}

			spans.push_back({ start, start + duration, in });
		}

		return spans;
	}

	SequenceSample ReplaySequence::Sample(const std::vector<SequenceItem>& items, std::span<const SequenceSpan> spans, double time)
	{
		SequenceSample s;
		const std::size_t n = std::min(items.size(), spans.size());

		if (n == 0)
		{
			return s;
		}

		time = Finite(time, 0.0);
		std::size_t i = 0;

		while (i + 1 < n && spans[i + 1].Start <= time)
		{
			++i;
		}

		const auto itemTime = [&](std::size_t k)
		{
			return spans[k].In + std::clamp(time - spans[k].Start, 0.0, spans[k].End - spans[k].Start);
		};

		if (i > 0 && items[i].Enter.Kind == TransitionKind::Crossfade && time < spans[i - 1].End)
		{
			const double overlap = spans[i - 1].End - spans[i].Start;
			s.Primary = static_cast<int>(i - 1);
			s.Secondary = static_cast<int>(i);
			s.PrimaryTime = itemTime(i - 1);
			s.SecondaryTime = itemTime(i);
			s.Weight = overlap > 0.0 ? static_cast<float>(std::clamp((time - spans[i].Start) / overlap, 0.0, 1.0)) : 1.0f;
			return s;
		}

		s.Primary = static_cast<int>(i);
		s.PrimaryTime = itemTime(i);

		// Fading in from this item's Enter colour.
		if (const double half = FadeHalf(items[i], spans, i); half > 0.0 && time < spans[i].Start + half)
		{
			s.FadeColor = items[i].Enter.Color;
			s.FadeAmount = static_cast<float>(std::clamp(1.0 - (time - spans[i].Start) / half, 0.0, 1.0));
		}

		// Fading out toward the next item's.
		if (i + 1 < n)
		{
			if (const double half = FadeHalf(items[i + 1], spans, i + 1); half > 0.0 && time >= spans[i].End - half)
			{
				s.FadeColor = items[i + 1].Enter.Color;
				s.FadeAmount = static_cast<float>(std::clamp((time - (spans[i].End - half)) / half, 0.0, 1.0));
			}
		}

		return s;
	}

	Json ReplaySequence::ToJson() const
	{
		Json items = Json::MakeArray();

		for (const SequenceItem& item : Items)
		{
			Json j = Json::MakeObject();
			j["name"] = item.Name;
			j["recording"] = item.Recording;

			if (!item.Project.empty())
			{
				j["project"] = item.Project;
			}

			j["in"] = item.In;

			if (item.Out > item.In)
			{
				j["out"] = item.Out;
			}

			Json t = Json::MakeObject();
			t["kind"] = ToString(item.Enter.Kind);
			t["duration"] = item.Enter.Duration;
			t["color"] = Replay::ToJson(item.Enter.Color);
			j["transition"] = std::move(t);
			items.Push(std::move(j));
		}

		Json root = Json::MakeObject();
		root["version"] = 1;
		root["items"] = std::move(items);
		return root;
	}

	ReplaySequence ReplaySequence::FromJson(const Json& json)
	{
		if (!json.IsObject() || !json["items"].IsArray())
		{
			throw std::runtime_error("a sequence is an object with an 'items' array");
		}

		ReplaySequence sequence;

		for (const Json& j : json["items"].GetArray())
		{
			SequenceItem item;
			item.Name = j["name"].AsString();
			item.Recording = j["recording"].AsString();

			if (item.Recording.empty())
			{
				throw std::runtime_error("a sequence item has no recording");
			}

			item.Project = j["project"].AsString();
			item.In = std::max(Finite(j["in"].AsNumber(0.0), 0.0), 0.0);
			item.Out = Finite(j["out"].AsNumber(-1.0), -1.0);
			const Json& t = j["transition"];

			if (t.IsObject())
			{
				if (!ParseTransitionKind(t["kind"].AsString("cut"), item.Enter.Kind))
				{
					throw std::runtime_error("transition kind must be cut, crossfade or fade");
				}

				item.Enter.Duration = std::max(Finite(t["duration"].AsNumber(0.0), 0.0), 0.0);
				item.Enter.Color = Vec3FromJson(t["color"]);
			}

			sequence.Items.push_back(std::move(item));
		}

		return sequence;
	}

	bool ReplaySequence::Save(const std::filesystem::path& path) const
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << ToJson().Dump(2) << '\n';
		return static_cast<bool>(out);
	}

	bool ReplaySequence::Load(const std::filesystem::path& path, std::string* error)
	{
		std::ifstream in(path, std::ios::binary);

		if (!in)
		{
			if (error)
			{
				*error = "cannot open " + path.string();
			}

			return false;
		}

		std::stringstream text;
		text << in.rdbuf();

		try
		{
			*this = FromJson(Json::Parse(text.str()));
			return true;
		}
		catch (const std::exception& e)
		{
			if (error)
			{
				*error = e.what();
			}

			return false;
		}
	}

	// ---- SequencePlayer -------------------------------------------------------------------

	void SequencePlayer::Changed()
	{
		// Players follow their recording when items move; changed items reopen.
		std::vector<Slot> old = std::move(slots);
		slots.clear();
		slots.resize(sequence.Items.size());

		for (std::size_t i = 0; i < sequence.Items.size(); ++i)
		{
			const SequenceItem& item = sequence.Items[i];
			const auto found = std::find_if(old.begin(), old.end(),
				[&](const Slot& s)
				{
					return s.Tried && s.Recording == item.Recording && s.Project == item.Project;
				});

			if (found != old.end())
			{
				slots[i] = std::move(*found);
				found->Tried = false;
			}
		}

		layoutDirty = true;
		view = {};
	}

	ReplayPlayer* SequencePlayer::GetPlayer(std::size_t index)
	{
		if (index >= sequence.Items.size())
		{
			return nullptr;
		}

		if (slots.size() != sequence.Items.size())
		{
			Changed();
		}

		Slot& slot = slots[index];

		if (!slot.Tried)
		{
			const SequenceItem& item = sequence.Items[index];
			slot.Tried = true;
			slot.Recording = item.Recording;
			slot.Project = item.Project;
			slot.Error.clear();
			auto player = std::make_unique<ReplayPlayer>();
			bool ok = false;

			if (opener)
			{
				ok = opener(*player, item, slot.Error);
			}
			else
			{
				const auto resolve = [&](const std::string& path)
				{
					const std::filesystem::path p(path);
					return p.is_relative() && !root.empty() ? root / p : p;
				};
				ok = player->Open(resolve(item.Recording), &slot.Error) &&
					(item.Project.empty() || player->GetProject().Load(resolve(item.Project), &slot.Error));
			}

			if (ok)
			{
				player->Seek(std::max(item.In, 0.0));
				slot.Player = std::move(player);
			}
			else if (slot.Error.empty())
			{
				slot.Error = "cannot open " + item.Recording;
			}
		}

		return slot.Player.get();
	}

	const std::string& SequencePlayer::GetError(std::size_t index) const
	{
		static const std::string none;
		return index < slots.size() ? slots[index].Error : none;
	}

	const std::vector<SequenceSpan>& SequencePlayer::GetSpans()
	{
		if (layoutDirty || spans.size() != sequence.Items.size())
		{
			std::vector<double> lengths;

			for (std::size_t i = 0; i < sequence.Items.size(); ++i)
			{
				const ReplayPlayer* player = GetPlayer(i);
				lengths.push_back(player ? player->GetLength() : 0.0);
			}

			spans = sequence.Layout(lengths);
			layoutDirty = false;
		}

		return spans;
	}

	double SequencePlayer::GetLength()
	{
		double length = 0.0;

		for (const SequenceSpan& span : GetSpans())
		{
			length = std::max(length, span.End);
		}

		return length;
	}

	void SequencePlayer::Seek(double newTime)
	{
		time = std::clamp(Finite(newTime, 0.0), 0.0, GetLength());
		Update(0.0);
	}

	void SequencePlayer::Place(int index, double itemTime, double dt)
	{
		ReplayPlayer* player = index >= 0 ? GetPlayer(static_cast<std::size_t>(index)) : nullptr;

		if (!player)
		{
			return;
		}

		const double gap = itemTime - player->GetTime();

		// Small forward steps play through (events fire); anything else seeks.
		if (playing && dt > 0.0 && gap > 0.0 && gap <= 2.0 * dt + 0.05)
		{
			player->SetSpeed(1.0);
			player->Play();
			player->Update(gap);
			player->Pause();
		}

		if (std::abs(itemTime - player->GetTime()) > 1.0e-6)
		{
			player->Seek(itemTime);
		}
	}

	const SequenceView& SequencePlayer::Update(double dt)
	{
		const double length = GetLength();
		dt = Finite(dt, 0.0);

		if (playing && dt > 0.0)
		{
			time += dt;

			if (time >= length)
			{
				time = length;
				playing = false;
			}
		}

		const SequenceSample s = ReplaySequence::Sample(sequence.Items, GetSpans(), time);
		Place(s.Primary, s.PrimaryTime, dt);
		Place(s.Secondary, s.SecondaryTime, dt);
		view.Time = time;
		view.PrimaryIndex = s.Primary;
		view.SecondaryIndex = s.Secondary;
		view.Primary = s.Primary >= 0 ? GetPlayer(static_cast<std::size_t>(s.Primary)) : nullptr;
		view.Secondary = s.Secondary >= 0 ? GetPlayer(static_cast<std::size_t>(s.Secondary)) : nullptr;
		view.Weight = s.Weight;
		view.FadeColor = s.FadeColor;
		view.FadeAmount = s.FadeAmount;
		return view;
	}

} // namespace Swim::Replay
