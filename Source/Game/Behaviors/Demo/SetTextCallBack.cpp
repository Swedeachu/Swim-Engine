#include "PCH.h"
#include "SetTextCallBack.h"
#include "Engine/Components/TextComponent.h"
#include "Engine/Systems/Scene/Scene.h"

namespace Game
{

	int SetTextCallback::Init()
	{
		tc = &scene->GetRegistry().get<Engine::TextComponent>(entity);

		if (chromaEnabled)
		{
			chroma = Game::ChromaHelper(Game::ChromaHelper::startHueFromSeed(1.0f));
		}

		return 0;
	}

	void SetTextCallback::Update(double dt)
	{
		if (!tc) return;

		// Let user code set/modify text (and anything on the component) each frame.
		if (callback) callback(*tc, entity, dt);

		// Optional chroma tinting
		if (chromaEnabled)
		{
			chroma.Update(dt);
			tc->fillColor = chroma.GetRGBA();
		}
	}

}
