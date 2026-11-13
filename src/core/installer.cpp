#include "core/installer.h"

#include "core/autostart.h"
#include "core/paths.h"
#include "core/state.h"

#include <set>

namespace simagicfix {

namespace {

bool ensure_directories() {
    std::error_code ec;
    fs::create_directories(app_data_dir(), ec);
    if (ec) {
        return false;
    }
    fs::create_directories(logs_dir(), ec);
    if (ec) {
        return false;
    }
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
    const std::string file = path_to_utf8(result.file);
    for (auto& existing : state.patched) {
        if (existing.file == file) {
            existing.name = result.name;
            existing.backup = result.backup;
            existing.original_crc = result.original_crc;
            existing.patched_crc = result.patched_crc;
            existing.fixes = result.applied;
            return;
        }
    }
    PatchedDashboard dash;
    dash.file = file;
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
    fs::path source = current_exe_path();
    // When the CLI is used to install, prefer the GUI executable sitting next
    // to it so the installed program is the user-facing one. Never install the
    // console binary under the GUI name.
    if (source.filename() == L"SimagicFixCLI.exe") {
        const fs::path gui = source.parent_path() / L"SimagicFix.exe";
        if (!fs::is_regular_file(gui, ec)) {
            return false;
        }
        source = gui;
    }
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
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
    state.version = "0.2.0";
    if (state.install_date.empty()) {
        state.install_date = now_string();
    }
    if (state.simpro_rpc_port <= 0) {
        state.simpro_rpc_port = 56789;
    }
    if (state.compat_rpc_port <= 0) {
        state.compat_rpc_port = 56788;
    }

    if (options.compat_mode) {
        if (!state.compat_mode && simpro_rpc_port() != state.compat_rpc_port) {
            std::string config_error;
            if (!backup_simpro_appconfig(config_error)) {
                report.error = "Cannot back up SimPro config: " + config_error;
                return report;
            }
        }
        std::string config_error;
        if (!write_simpro_rpc_port(state.compat_rpc_port, config_error)) {
            report.error = "Cannot change SimPro RPC port: " + config_error;
            return report;
        }
        state.compat_mode = true;
        state.bridge_port = 56789;
    } else {
        if (state.compat_mode) {
            std::string config_error;
            if (!write_simpro_rpc_port(state.simpro_rpc_port, config_error)) {
                report.error = "Cannot restore SimPro RPC port: " + config_error;
                return report;
            }
        }
        state.compat_mode = false;
        state.bridge_port = 56799;
    }
    save_state(state);

    for (const auto& dashboard : options.dashboards) {
        if (!fs::is_regular_file(dashboard)) {
            continue;
        }
        DashPatchResult result = patch_dashboard(dashboard);
        report.patched.push_back(result);
        if (!result.ok) {
            // Persist what was already patched so Uninstall/Restore can still
            // revert it.
            save_state(state);
            report.error = result.error;
            return report;
        }
        append_patched(state, result);
        save_state(state);
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
    std::set<std::string> seen;
    for (const auto& dashboard : state.patched) {
        if (seen.insert(dashboard.file).second) {
            options.dashboards.push_back(utf8_to_path(dashboard.file));
        }
    }
    for (const auto& dashboard : scan_dashboards()) {
        const std::string file = path_to_utf8(dashboard.file);
        if (dashboard.compatible && is_lmu_dashboard_name(dashboard.name) &&
            seen.insert(file).second) {
            options.dashboards.push_back(dashboard.file);
        }
    }
    return install(options);
}

bool restore_all() {
    AppState state = load_state();
    std::vector<PatchedDashboard> remaining;
    for (const auto& dashboard : state.patched) {
        if (!restore_dashboard(dashboard)) {
            remaining.push_back(dashboard);
        }
    }
    state.patched = std::move(remaining);
    save_state(state);
    return state.patched.empty();
}

bool restore_selected(const std::vector<fs::path>& files) {
    AppState state = load_state();
    std::set<std::string> wanted;
    for (const auto& file : files) {
        wanted.insert(path_to_utf8(file));
    }
    std::vector<PatchedDashboard> remaining;
    bool ok = true;
    for (const auto& dashboard : state.patched) {
        if (wanted.find(dashboard.file) == wanted.end()) {
            remaining.push_back(dashboard);
            continue;
        }
        if (!restore_dashboard(dashboard)) {
            remaining.push_back(dashboard);
            ok = false;
        }
    }
    state.patched = std::move(remaining);
    save_state(state);
    return ok;
}

bool uninstall(bool restore_dashboards) {
    bool restored = true;
    AppState state = load_state();
    if (state.compat_mode) {
        std::string config_error;
        if (!write_simpro_rpc_port(state.simpro_rpc_port, config_error)) {
            restored = false;
        }
    }
    if (restore_dashboards) {
        restored = restore_all() && restored;
    }
    disable_autostart();
    remove_start_menu_shortcut();
    state = load_state();
    state.installed = false;
    state.compat_mode = false;
    state.bridge_port = 56799;
    save_state(state);
    return restored;
}

bool schedule_delete_self() {
    const std::wstring path = installed_exe_path().wstring();
    return MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
}

}  // namespace simagicfix
