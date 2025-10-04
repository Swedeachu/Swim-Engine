#pragma once

namespace Game
{

	class ChromaHelper
	{

	public:

		ChromaHelper() = default;

		static std::string strf(float value, int precision = 2);

		static float startHueFromSeed(uint32_t seed);

		// Initialize chroma with optional seed hue (0–1 range)
		explicit ChromaHelper(float startHue)
			: startHue(startHue)
		{}

		// Advances internal timer and updates the current RGB color.
		void Update(double dt);

		// Returns the current RGB color (linear space)
		glm::vec3 GetRGB() const { return currentRGB; }

		// Returns the current RGBA (alpha always = 1)
		glm::vec4 GetRGBA() const { return glm::vec4(currentRGB, 1.0f); }

		// Converts HSV -> RGB (helper function)
		static glm::vec3 HSVtoRGB(float h, float s, float v);

	private:

		double elapsed = 0.0;
		float startHue = 0.0f;
		glm::vec3 currentRGB = glm::vec3(1.0f, 0.0f, 0.0f); // Default red

	};

}
