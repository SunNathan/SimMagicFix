#pragma once

#include "core/common.h"

namespace simagicfix {

struct PatchedDashboard {
    std::string file;    // absolute path of the .dash
    std::string name;    // display name (file stem)
    std::string backup;  // absolute path of the backup
    uint32_t original_crc = 0;
    uint32_t patched_crc = 0;
    std::vector<std::string> fixes;
};

struct AppState {
    bool installed = false;
    bool auto_start = false;
    int bridge_port = 56799;
    std::string version;
    std::string install_date;
    std::vector<PatchedDashboard> patched;
    std::vector<std::string> selection;
};

AppState load_state();
bool save_state(const AppState& state);

}  // namespace simagicfix
