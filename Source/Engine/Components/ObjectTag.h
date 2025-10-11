#pragma once
#include <string>

namespace Engine
{

	// Convenience macro for declaring static constexpr int tags
#define TAG(name, value) static constexpr int name = value;

	// Public facing contants for consistency 
	namespace TagConstants
	{
		// --- Core world objects ---
		TAG(WORLD, 0)
			TAG(UI, 1)
			TAG(WATER, 2)
			TAG(LIGHT, 3)
			TAG(SKY, 4)
			TAG(CAMERA, 5)
			TAG(PLAYER, 6)
			TAG(NPC, 7)
			TAG(ENEMY, 8)
			TAG(ITEM, 9)
			TAG(PROJECTILE, 10)
			TAG(TRIGGER, 11)
			TAG(PHYSICS_OBJECT, 12)
			TAG(AUDIO_SOURCE, 13)

			// --- Scene / environment ---
			TAG(ENVIRONMENT, 20)
			TAG(BACKGROUND, 21)
			TAG(FOREGROUND, 22)
			TAG(EFFECT, 23)

			// --- Internal / special ---
			TAG(EDITOR_MODE_OBJECT, -1)
			TAG(SYSTEM_OBJECT, -2)
			TAG(DEBUG_OBJECT, -3)
			TAG(TEMPORARY_OBJECT, -4)

			// --- UI subcategories ---
			TAG(UI_CANVAS, 30)
			TAG(UI_BUTTON, 31)
			TAG(UI_TEXT, 32)
			TAG(UI_IMAGE, 33)
			TAG(UI_PANEL, 34)

			// --- Reserved for user space ---
			TAG(USER_TAG_BEGIN, 100)
	}

#undef TAG

	struct ObjectTag
	{

		int tag = 0;
		std::string name;

		ObjectTag() = default;
		ObjectTag(int t, const std::string& n = "")
			: tag(t), name(n)
		{}

	};

}