#include "Engine/Platform/PlatformSystem.h"
#include <iostream>

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	Swim::Platform::PlatformSystem platform;
	if (!platform.Initialize())
	{
		return 1;
	}

	Swim::Platform::WindowDesc desc{};
	desc.Title = "Swim Platform - HelloWindow";
	desc.Width = 1280;
	desc.Height = 720;
	desc.Resizable = true;
	desc.HighPixelDensity = true;

	auto window = platform.GetWindowSystem().Create(desc);
	if (!window)
	{
		return 2;
	}

	bool running = true;
	while (running)
	{
		platform.PumpEvents(
			[&](const Swim::Platform::WindowEvent& event)
			{
				if (event.Type == Swim::Platform::WindowEventType::CloseRequested)
				{
					running = false;
				}
			},
			[](const Swim::Platform::InputEvent& event)
			{
				(void)event;
			}
		);
	}

	return 0;
}
