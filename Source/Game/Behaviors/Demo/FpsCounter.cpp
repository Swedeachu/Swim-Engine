#include "PCH.h"
#include "FpsCounter.h"
#include "Engine/Components/Transform.h"
#include "Engine/SwimEngine.h"

#include <cmath>       
#include <algorithm>   
#include <cstdint>     

namespace Game
{

	glm::vec3 FpsCounter::HSVtoRGB(float h, float s, float v)
	{
		// Wrap hue to [0,1)
		h = h - std::floor(h);

		// Clamp s and v to legal range
		s = std::clamp(s, 0.0f, 1.0f);
		v = std::clamp(v, 0.0f, 1.0f);

		// Chroma = v*s (max color amplitude), X = second-largest component
		const float c = v * s;
		const float h6 = h * 6.0f; // sector in [0,6)
		const float x = c * (1.0f - std::fabs(std::fmod(h6, 2.0f) - 1.0f));
		const float m = v - c;     // add to shift into [0,v]

		float r = 0.0f, g = 0.0f, b = 0.0f;
		const int sector = static_cast<int>(std::floor(h6)) % 6;

		// Each sector corresponds to a different primary/secondary blend.
		switch (sector)
		{
			case 0: { r = c; g = x; b = 0.0f; break; }
			case 1: { r = x; g = c; b = 0.0f; break; }
			case 2: { r = 0.0f; g = c; b = x; break; }
			case 3: { r = 0.0f; g = x; b = c; break; }
			case 4: { r = x; g = 0.0f; b = c; break; }
			case 5: default: { r = c; g = 0.0f; b = x; break; }
		}

		return glm::vec3(r + m, g + m, b + m);
	}

	int FpsCounter::Init()
	{
		engine = Engine::SwimEngine::GetInstance();
		tc = &scene->GetRegistry().get<Engine::TextComponent>(entity);

		// Derive a stable hue seed from the entity id so multiple counters
		// don't match phases. Using golden-ratio conjugate for good dispersion.
		if (chroma)
		{
			const uint32_t eid = static_cast<uint32_t>(entity);
			const float seed01 = (eid & 0xFFFFu) / 65536.0f;      // [0,1)
			const float PHI_CONJ = 0.61803398875f;                // 1/theta
			chromaStartHue = std::fmod(seed01 * PHI_CONJ, 1.0f);  // [0,1)
		}

		return 0;
	}

	void FpsCounter::Update(double dt)
	{
		if (!tc)
		{
			return;
		}

		const int fps = engine->GetFPS();

		// Avoid needlessly dirtying text every frame if fps value hasn't changed.
		const std::string newText = "FPS: " + std::to_string(fps);
		if (newText != tc->GetText())
		{
			tc->SetText(newText);
		}

		if (chroma)
		{
			// Advance phase in seconds.
			chromaTime += dt;

			// How fast to cycle the hue (cycles per second).
			// 0.10 -> full rainbow every ~10 seconds (pleasant, not too fast).
			const float cyclesPerSecond = 0.10f;

			// Hue in [0,1). Add a stable per-entity offset so multiple counters differ.
			float hue = chromaStartHue + static_cast<float>(chromaTime) * cyclesPerSecond;
			hue = hue - std::floor(hue);

			// Saturation/Value: slightly <1.0 saturation for softer tones.
			const float sat = 0.85f;
			const float val = 1.00f;

			const glm::vec3 rgb = HSVtoRGB(hue, sat, val);
			tc->fillColor = glm::vec4(rgb, 1.0f);
		}
	}

}
