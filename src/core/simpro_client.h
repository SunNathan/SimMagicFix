#pragma once

#include "core/common.h"

namespace simagicfix {

// Sends a JSON-RPC request to SimPro Manager and returns the response body.
// Returns std::nullopt when SimPro is not running or does not answer in time.
std::optional<std::string> post_json_to_simpro(int port, const std::string& body,
                                               int timeout_ms = 800);

}  // namespace simagicfix
