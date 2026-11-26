#pragma once

#include "Engine/Systems/Replay/ReplayPlayer.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Replay
{

	enum class TransitionKind : std::uint8_t
	{
		Cut,	   // Instant.
		Crossfade, // The items overlap by Duration and blend.
		Fade,	   // Out to Color over the first half of Duration, then in from it (no overlap).
	};

	const char* ToString(TransitionKind kind);

	bool ParseTransitionKind(std::string_view text, TransitionKind& kind);

	struct Transition
	{
		TransitionKind Kind = TransitionKind::Cut;
		double Duration = 0.0;
		Vec3 Color{ 0.0f, 0.0f, 0.0f };
	};

	// One recording in a sequence, played from In to Out of its own timeline (its
	// project's edits applied). Enter is the transition from the previous item.
	struct SequenceItem
	{
		std::string Name;
		std::string Recording;
		std::string Project; // Optional project file for the recording.
		double In = 0.0;
		double Out = -1.0; // Below In: the end of the item's timeline.
		Transition Enter;
	};

	// Where an item sits on the sequence timeline.
	struct SequenceSpan
	{
		double Start = 0.0; // Sequence seconds.
		double End = 0.0;
		double In = 0.0; // Item timeline seconds at Start.
	};

	// What plays at one sequence time: Primary fully, Secondary blended over it by
	// Weight (crossfades), and FadeAmount of FadeColor over both (fades).
	struct SequenceSample
	{
		int Primary = -1;
		int Secondary = -1;
		double PrimaryTime = 0.0; // Item timeline seconds.
		double SecondaryTime = 0.0;
		float Weight = 0.0f;
		Vec3 FadeColor{ 0.0f, 0.0f, 0.0f };
		float FadeAmount = 0.0f;
	};

	// Several recordings played back to back with transitions, kept as readable JSON.
	class ReplaySequence
	{

	  public:
		std::vector<SequenceItem> Items;

		// Spans from item lengths (the timeline length of each item's recording and
		// project; Out clamps to it). A crossfade overlaps the previous item and is
		// limited to half of either item.
		std::vector<SequenceSpan> Layout(std::span<const double> itemLengths) const;

		static SequenceSample Sample(const std::vector<SequenceItem>& items, std::span<const SequenceSpan> spans, double time);

		Tooling::Json ToJson() const;

		static ReplaySequence FromJson(const Tooling::Json& json); // Throws std::runtime_error.

		bool Save(const std::filesystem::path& path) const;

		bool Load(const std::filesystem::path& path, std::string* error = nullptr);
	};

	// The players of the items playing now (Secondary only during a crossfade).
	struct SequenceView
	{
		double Time = 0.0;
		ReplayPlayer* Primary = nullptr;
		ReplayPlayer* Secondary = nullptr;
		int PrimaryIndex = -1;
		int SecondaryIndex = -1;
		float Weight = 0.0f; // Secondary over Primary.
		Vec3 FadeColor{ 0.0f, 0.0f, 0.0f };
		float FadeAmount = 0.0f;
	};

	// Plays a sequence: opens each item's player when first needed and keeps the playing
	// items at their item times (stepped forward while playing so events fire).
	class SequencePlayer
	{

	  public:
		ReplaySequence& GetSequence() { return sequence; }

		const ReplaySequence& GetSequence() const { return sequence; }

		// Root that relative item paths resolve against.
		void SetRoot(std::filesystem::path root) { this->root = std::move(root); }

		// Opens an item's recording into a player (default: Open, then the project).
		using Opener = std::function<bool(ReplayPlayer& player, const SequenceItem& item, std::string& error)>;

		void SetOpener(Opener opener) { this->opener = std::move(opener); }

		// Call after editing the items (players of changed items reopen).
		void Changed();

		void Play() { playing = true; }

		void Pause() { playing = false; }

		bool IsPlaying() const { return playing; }

		void Seek(double time);

		double GetTime() const { return time; }

		double GetLength();

		const std::vector<SequenceSpan>& GetSpans();

		// Advances while playing and positions the players.
		const SequenceView& Update(double dt);

		const SequenceView& GetView() const { return view; }

		// The item's player, opening it if needed (nullptr when it cannot be opened; the
		// error is kept in GetError).
		ReplayPlayer* GetPlayer(std::size_t index);

		const std::string& GetError(std::size_t index) const;

	  private:
		struct Slot
		{
			std::string Recording;
			std::string Project;
			std::unique_ptr<ReplayPlayer> Player;
			std::string Error;
			bool Tried = false;
		};

		void Place(int index, double itemTime, double dt);

		ReplaySequence sequence;
		std::filesystem::path root;
		Opener opener;
		std::vector<Slot> slots;
		std::vector<SequenceSpan> spans;
		bool layoutDirty = true;
		double time = 0.0;
		bool playing = false;
		SequenceView view;
	};

} // namespace Swim::Replay
