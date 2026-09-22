#pragma once

namespace Swim::Render::Internal
{
	// Registry record for one bindless array element. The table never owns the
	// resource; the caller keeps it alive until the element's release retires.
	template <typename Resource> struct BindlessElement
	{
		Resource* Object = nullptr;
	};
} // namespace Swim::Render::Internal
