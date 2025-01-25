#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

	class InputManager : public Machine
	{

	public:

		int Awake() override;

		int Init() override;

		void Update(double dt) override;

		void FixedUpdate(unsigned int tickThisSecond) {}

		int Exit() override { return 0; }

		// SwimEngine calls this everytime their is an input message that is most likely an input
		void InputMessage(UINT uMsg, WPARAM wParam);

		bool IsKeyDown(unsigned char key) const;
		bool IsKeyTriggered(unsigned char key) const;
		bool IsKeyReleased(unsigned char key) const;

		int GetMouseScrollDelta() const { return mouseWheelDelta; }
		const glm::vec2& GetMousePosition() const { return mousePos; }
		const glm::vec2& GetMousePositionDelta() const { return mouseDelta; }

	private:

		void KeySetState(unsigned char key, bool isDown);
		void SetMouseScrollDelta(int delta) { mouseWheelDelta = delta; }

		// the window handle of the window getting inputs from
		HWND windowHandle{ NULL };

		// amount of valid keys to accept
		unsigned int keyCount{ 0 };

		// 256 size array of all keys with a pair of booleans, 
		// first boolean holds the state of the key for the current frame
		// second boolean holds the state of the key for the previous frame
		// array[char key -> <bool currentState, bool previousState>]
		// this is needed for getting previous frame key states for key triggering and releasing
		// we have to use a deferred key state array as well to sync inputs between windows message updates and frame updates
		std::array<std::pair<char, std::pair<bool, bool>>, 256> keyState;
		std::array<std::pair<char, std::pair<bool, bool>>, 256> deferredState;

		// delta stuff needed
		int mouseWheelDelta{0};
		glm::vec2 mousePos{ 0, 0};
		glm::vec2 mouseDelta{ 0, 0 };

	};

}
