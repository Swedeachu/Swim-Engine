#pragma once

#include "Engine/Systems/Entity/Behavior.h"
#include "Engine/Components/TextComponent.h"
#include "Engine/SwimEngine.h"
#include "Game/Behaviors/Util/ChromaHelper.h"
#include <functional>
#include <memory>

namespace Game
{

	// A behavior that lets you provide a lambda/functor to set text each frame.
	// Signature: void(TextComponent& tc, entt::entity e, double dt)
	class SetTextCallback : public Engine::Behavior
	{

	public:

		using Engine::Behavior::Behavior;

		explicit SetTextCallback(Engine::Scene* scene,
			entt::entity owner,
			bool chroma = false)
			: Engine::Behavior(scene, owner), chromaEnabled(chroma) {}

		// Provide a lambda/functor with captures if you like:
		// e.g., set SetTextCallback::Callback cb = [&](Engine::TextComponent& tc, entt::entity e, double dt){...};
		using Callback = std::function<void(Engine::TextComponent&, entt::entity, double)>;

		void SetCallback(Callback cb) { callback = std::move(cb); }

		// Lifecycle
		int Awake() override { return 0; }
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int) override {}
		int Exit() override { return 0; }

	private:

		bool chromaEnabled = false;

		std::shared_ptr<Engine::SwimEngine> engine;
		Engine::TextComponent* tc = nullptr;
		Callback callback;

		Game::ChromaHelper chroma;

	};

}
