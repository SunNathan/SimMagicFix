#pragma once

#include "core/state.h"

namespace simagicfix {

struct DashScanResult {
    fs::path file;
    std::string name;
    bool compatible = false;
    bool already_patched = false;
    std::vector<std::string> fixes;
};

struct DashPatchResult {
    bool ok = false;
    fs::path file;
    std::string name;
    std::string backup;
    uint32_t original_crc = 0;
    uint32_t patched_crc = 0;
    std::vector<std::string> applied;
    std::vector<std::string> skipped;
    std::string error;
};

// True when the built-in Windows tar.exe is available (needed to read/write
// the .dash ZIP archives).
bool tar_available();

// Scans the SimPro dashboard folder and reports which .dash files can be
// patched. Does not modify anything.
std::vector<DashScanResult> scan_dashboards();

// Patches value bindings only (never the layout). A backup is created in
// backups_dir() before writing.
DashPatchResult patch_dashboard(const fs::path& file);

// Restores a dashboard from the backup recorded in the state.
bool restore_dashboard(const PatchedDashboard& dashboard);

}  // namespace simagicfix
