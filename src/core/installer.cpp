#include "core/installer.h"

#include "core/autostart.h"
#include "core/paths.h"
#include "core/state.h"

namespace simagicfix {

namespace {

bool ensure_directories() {
    std::error_code ec;
    fs::create_directories(app_data_dir(), ec);
    if (ec) {
        return false;
    }
    fs::create_directories(logs_dir(), ec);
    return !ec;
}

bool is_current_exe(const fs::path& target) {
    std::error_code ec;
    return fs::equivalent(current_exe_path(), target, ec);
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
    state.bridge_port = 56799;
    state.version = "0.2.0";
    if (state.install_date.empty()) {
        state.install_date = now_string();
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

bool uninstall() {
    disable_autostart();
    remove_start_menu_shortcut();
    AppState state = load_state();
    state.installed = false;
    save_state(state);
    return true;
}

bool schedule_delete_self() {
    const std::wstring path = installed_exe_path().wstring();
    return MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
}

}  // namespace simagicfix
