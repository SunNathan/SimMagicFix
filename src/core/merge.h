#pragma once

#include "core/snapshot.h"
#include "json.hpp"

namespace simagicfix {

// Merges the LMU values into SimPro's JSON-RPC response. When SimPro is not
// available, a self-contained fallback response is produced so dashboards can
// still be tested (especially in mock mode).
nlohmann::json build_response(const nlohmann::json& simpro_response,
                              const LmuSnapshot& snapshot, bool mock);

}  // namespace simagicfix
