#pragma once

#include "core/common.h"

namespace simagicfix {

struct InstallOptions {
    bool auto_start = true;
};

struct InstallReport {
    bool ok = false;
    std::string error;
};

InstallReport install(const InstallOptions& options);
bool uninstall();

bool install_self(const fs::path& target);
bool schedule_delete_self();

}  // namespace simagicfix
