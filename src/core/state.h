#pragma once

#include "core/common.h"

namespace simagicfix {

struct AppState {
    bool installed = false;
    bool auto_start = false;
    bool memory_patch = false;
    int bridge_port = 56799;
    std::string version;
    std::string install_date;
};

AppState load_state();
bool save_state(const AppState& state);

}  // namespace simagicfix
