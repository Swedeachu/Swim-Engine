#include "Engine/IO/AsyncIoService.h"

static_assert(sizeof(Swim::IO::IoReadRange) == sizeof(std::uint64_t) * 2);
