#pragma once

#include "core/dash_patcher.h"

namespace simagicfix {

struct InstallOptions {
    bool auto_start = true;
    std::vector<fs::path> dashboards;
};

struct InstallReport {
    bool ok = false;
    std::string error;
    std::vector<DashPatchResult> patched;
};

InstallReport install(const InstallOptions& options);
InstallReport repair();
bool restore_all();
bool uninstall(bool restore_dashboards);

bool install_self(const fs::path& target);
bool schedule_delete_self();

}  // namespace simagicfix
