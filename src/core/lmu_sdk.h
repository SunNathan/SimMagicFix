#pragma once

// Wrapper around the official Le Mans Ultimate SDK header.
// Keeping this include in one place makes the rest of the code independent
// from the proprietary header.
#include "core/common.h"

#include <cstdint>
#include <optional>
#include <utility>

#define _CRT_SECURE_NO_WARNINGS
#pragma warning(push, 0)
#include "SharedMemoryInterface.hpp"
#pragma warning(pop)
