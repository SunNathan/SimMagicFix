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

bool copy_hook_dll(const fs::path& source_dir, const fs::path& target_dir) {
    std::error_code ec;
    const fs::path source = source_dir / L"SimagicFixHook.dll";
    if (!fs::is_regular_file(source, ec)) {
        return false;
    }
    fs::create_directories(target_dir, ec);
    if (ec) {
        return false;
    }
    fs::copy_file(source, target_dir / L"SimagicFixHook.dll",
                  fs::copy_options::overwrite_existing, ec);
    return !ec;
}

std::wstring escape_powershell_literal(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size());
    for (const wchar_t c : value) {
        if (c == L'\'') {
            escaped += L"''";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

bool schedule_cleanup_after_exit(const fs::path& exe, const fs::path& dll) {
    const std::wstring exe_path = escape_powershell_literal(exe.wstring());
    const std::wstring dll_path = escape_powershell_literal(dll.wstring());
    std::wstring command =
        L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \""
        L"Wait-Process -Id " +
        std::to_wstring(GetCurrentProcessId()) +
        L" -ErrorAction SilentlyContinue; "
        L"Remove-Item -LiteralPath '" +
        exe_path +
        L"' -Force -ErrorAction SilentlyContinue; "
        L"Remove-Item -LiteralPath '" +
        dll_path + L"' -Force -ErrorAction SilentlyContinue\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(
        nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process);
    if (started != FALSE) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    const BOOL exe_ok =
        MoveFileExW(exe.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    MoveFileExW(dll.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return exe_ok != FALSE;
}

std::string cleanup_source_files(const fs::path& source_exe,
                                 const fs::path& source_dll,
                                 const fs::path& installed_exe) {
    std::error_code ec;
    if (source_exe.empty() || source_dll.empty()) {
        return {};
    }
    if (fs::equivalent(source_exe, installed_exe, ec)) {
        return {};
    }
    ec.clear();
    if (fs::equivalent(source_exe.parent_path(), installed_exe.parent_path(),
                       ec)) {
        return {};
    }

    ec.clear();
    fs::remove(source_dll, ec);
    const bool dll_removed = !ec;

    if (!schedule_cleanup_after_exit(source_exe, source_dll)) {
        return "Impossible de programmer la suppression des fichiers source.";
    }
    if (!dll_removed) {
        return "La DLL source n'a pas pu etre supprimee immediatement ; "
               "elle sera supprimee apres fermeture de l'application.";
    }
    return {};
}

}  // namespace

bool install_self(const fs::path& target) {
    std::error_code ec;
    if (is_current_exe(target)) {
        return fs::is_regular_file(target.parent_path() / L"SimagicFixHook.dll",
                                   ec);
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
    if (ec) {
        return false;
    }
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    return copy_hook_dll(source.parent_path(), target.parent_path());
}

InstallReport install(const InstallOptions& options) {
    InstallReport report;
    if (!ensure_directories()) {
        report.error = "Cannot create application directories";
        return report;
    }

    const fs::path source_exe = current_exe_path();
    const fs::path source_dir = source_exe.parent_path();
    const fs::path source_dll = source_dir / L"SimagicFixHook.dll";
    const fs::path cleanup_exe =
        source_exe.filename() == L"SimagicFixCLI.exe"
            ? source_dir / L"SimagicFix.exe"
            : source_exe;

    if (!install_self(installed_exe_path())) {
        report.error = "Cannot install SimagicFix files";
        return report;
    }

    AppState state = load_state();
    state.installed = true;
    state.auto_start = options.auto_start;
    state.version = "0.4.1";
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

    if (!options.keep_source) {
        report.warning = cleanup_source_files(cleanup_exe, source_dll,
                                              installed_exe_path());
    }

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
