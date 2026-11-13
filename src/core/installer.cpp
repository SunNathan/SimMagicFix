#include "core/installer.h"

#include "core/autostart.h"
#include "core/paths.h"
#include "core/state.h"

namespace simagicfix {

namespace {

bool ensure_directories() {
    std::error_code ec;
    fs::create_directories(app_data_dir(), ec);
    fs::create_directories(logs_dir(), ec);
    fs::create_directories(backups_dir(), ec);
    return !ec;
}

bool is_current_exe(const fs::path& target) {
    std::error_code ec;
    return fs::equivalent(current_exe_path(), target, ec);
}

void append_patched(AppState& state, const DashPatchResult& result) {
    if (!result.ok || result.applied.empty()) {
        return;
    }
    for (auto& existing : state.patched) {
        if (existing.file == result.file.string()) {
            existing.name = result.name;
            existing.backup = result.backup;
            existing.original_crc = result.original_crc;
            existing.patched_crc = result.patched_crc;
            existing.fixes = result.applied;
            return;
        }
    }
    PatchedDashboard dash;
    dash.file = result.file.string();
    dash.name = result.name;
    dash.backup = result.backup;
    dash.original_crc = result.original_crc;
    dash.patched_crc = result.patched_crc;
    dash.fixes = result.applied;
    state.patched.push_back(std::move(dash));
}

}  // namespace

bool install_self(const fs::path& target) {
    std::error_code ec;
    if (is_current_exe(target)) {
        return true;
    }
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(current_exe_path(), target, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

InstallReport install(const InstallOptions& options) {
    InstallReport report;
    if (!ensure_directories()) {
        report.error = "Cannot create application directories";
        return report;
    }
    if (!install_self(installed_exe_path())) {
        report.error = "Cannot install SimagicFix.exe";
        return report;
    }

    AppState state = load_state();
    state.installed = true;
    state.auto_start = options.auto_start;
    state.version = "0.1.0";
    state.install_date = now_string();

    for (const auto& dashboard : options.dashboards) {
        if (!fs::is_regular_file(dashboard)) {
            continue;
        }
        DashPatchResult result = patch_dashboard(dashboard);
        report.patched.push_back(result);
        if (!result.ok) {
            report.error = result.error;
            return report;
        }
        append_patched(state, result);
    }

    save_state(state);
    if (options.auto_start) {
        enable_autostart(installed_exe_path());
    } else {
        disable_autostart();
    }
    create_start_menu_shortcut(installed_exe_path());

    report.ok = true;
    return report;
}

InstallReport repair() {
    AppState state = load_state();
    InstallOptions options;
    options.auto_start = state.auto_start;
    for (const auto& dashboard : scan_dashboards()) {
        if (dashboard.compatible || dashboard.already_patched) {
            options.dashboards.push_back(dashboard.file);
        }
    }
    return install(options);
}

bool restore_all() {
    AppState state = load_state();
    bool ok = true;
    for (const auto& dashboard : state.patched) {
        if (!restore_dashboard(dashboard)) {
            ok = false;
        }
    }
    state.patched.clear();
    save_state(state);
    return ok;
}

bool uninstall(bool restore_dashboards) {
    if (restore_dashboards) {
        restore_all();
    }
    disable_autostart();
    remove_start_menu_shortcut();
    AppState state = load_state();
    state.installed = false;
    state.patched.clear();
    save_state(state);
    return true;
}

bool schedule_delete_self() {
    const std::wstring path = installed_exe_path().wstring();
    return MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
}

}  // namespace simagicfix
