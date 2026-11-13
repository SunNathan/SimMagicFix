#include "core/paths.h"

#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>

#include <array>
#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <regex>

#include "json.hpp"

namespace simagicfix {

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), size);
    return out;
}

std::string path_to_utf8(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

fs::path utf8_to_path(const std::string& value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),
                                  value.size()));
}

namespace {

std::wstring get_env_w(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(written);
    return value;
}

bool path_is_directory(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool path_is_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}

std::vector<fs::path> steam_roots() {
    std::vector<fs::path> roots;
    const std::wstring program_files_x86 = get_env_w(L"ProgramFiles(x86)");
    const std::wstring program_files = get_env_w(L"ProgramFiles");
    if (!program_files_x86.empty()) {
        roots.emplace_back(fs::path(program_files_x86) / L"Steam");
    }
    if (!program_files.empty()) {
        roots.emplace_back(fs::path(program_files) / L"Steam");
    }
    roots.emplace_back(L"C:/Program Files (x86)/Steam");
    roots.emplace_back(L"C:/Program Files/Steam");
    roots.emplace_back(L"D:/SteamLibrary");
    roots.emplace_back(L"E:/SteamLibrary");
    return roots;
}

void add_steam_library_paths(std::vector<fs::path>& libraries) {
    for (const auto& root : steam_roots()) {
        const fs::path vdf = root / L"steamapps" / L"libraryfolders.vdf";
        if (!path_is_file(vdf)) {
            continue;
        }
        std::ifstream input(vdf);
        std::string line;
        while (std::getline(input, line)) {
            const auto path_pos = line.find("\"path\"");
            if (path_pos == std::string::npos) {
                continue;
            }
            const auto first_quote = line.find('"', path_pos + 6);
            const auto second_quote =
                first_quote == std::string::npos ? std::string::npos
                                                 : line.find('"', first_quote + 1);
            if (second_quote == std::string::npos) {
                continue;
            }
            std::string raw = line.substr(first_quote + 1, second_quote - first_quote - 1);
            std::string unescaped;
            unescaped.reserve(raw.size());
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') {
                    unescaped.push_back('\\');
                    ++i;
                } else {
                    unescaped.push_back(raw[i]);
                }
            }
            libraries.emplace_back(utf8_to_wide(unescaped));
        }
    }
}

std::wstring read_registry_string(HKEY root, const std::wstring& subkey,
                                  const std::wstring& value_name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, value_name.c_str(), nullptr, &type, nullptr, &size) !=
            ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        RegCloseKey(key);
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, value_name.c_str(), nullptr, &type,
                         reinterpret_cast<LPBYTE>(value.data()), &size) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return {};
    }
    RegCloseKey(key);
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

std::vector<std::wstring> uninstall_subkeys() {
    return {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    };
}

fs::path find_simpro_in_registry() {
    for (const auto& base : uninstall_subkeys()) {
        HKEY root = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, base.c_str(), 0, KEY_READ, &root) !=
            ERROR_SUCCESS) {
            continue;
        }
        DWORD index = 0;
        wchar_t name[512] = {};
        while (RegEnumKeyW(root, index++, name, 512) == ERROR_SUCCESS) {
            const std::wstring subkey = base + L"\\" + name;
            const std::wstring display_name =
                read_registry_string(HKEY_LOCAL_MACHINE, subkey, L"DisplayName");
            if (display_name.empty()) {
                continue;
            }
            const std::wstring lower_name = lower(display_name);
            if (lower_name.find(L"simpro") == std::wstring::npos &&
                lower_name.find(L"simagic") == std::wstring::npos) {
                continue;
            }
            const std::wstring location =
                read_registry_string(HKEY_LOCAL_MACHINE, subkey, L"InstallLocation");
            if (!location.empty()) {
                const fs::path base_path(location);
                for (const auto& candidate :
                     {base_path / L"simpro.exe", base_path / L"bin" / L"simpro.exe",
                      base_path / L"SimPro.exe"}) {
                    if (path_is_file(candidate)) {
                        RegCloseKey(root);
                        return candidate;
                    }
                }
            }
            const std::wstring icon =
                read_registry_string(HKEY_LOCAL_MACHINE, subkey, L"DisplayIcon");
            if (!icon.empty()) {
                std::wstring clean = icon;
                const auto comma = clean.find(L',');
                if (comma != std::wstring::npos) {
                    clean.resize(comma);
                }
                const fs::path icon_path(clean);
                if (path_is_file(icon_path) &&
                    lower(icon_path.filename().wstring()) == L"simpro.exe") {
                    RegCloseKey(root);
                    return icon_path;
                }
            }
        }
        RegCloseKey(root);
    }
    return {};
}

}  // namespace

fs::path local_app_data() {
    const std::wstring value = get_env_w(L"LOCALAPPDATA");
    if (!value.empty()) {
        return fs::path(value);
    }
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) {
        fs::path path(known);
        CoTaskMemFree(known);
        return path;
    }
    return fs::temp_directory_path();
}

fs::path app_data_dir() { return local_app_data() / L"SimagicFix"; }
fs::path logs_dir() { return app_data_dir() / L"logs"; }
fs::path backups_dir() { return app_data_dir() / L"backups"; }
fs::path state_path() { return app_data_dir() / L"state.json"; }

fs::path simagic_config_dir() { return local_app_data() / L"Simagic" / L"config"; }
fs::path dashboard_dir() {
    return simagic_config_dir() / L"dash" / L"offical_dash" / L"dashboard";
}
fs::path simagic_appconfig_path() { return simagic_config_dir() / L"appconfig.json"; }

fs::path simpro_exe_path() {
    const std::wstring program_files_x86 = get_env_w(L"ProgramFiles(x86)");
    const std::wstring program_files = get_env_w(L"ProgramFiles");
    const std::wstring local = get_env_w(L"LOCALAPPDATA");
    std::vector<fs::path> candidates;
    if (!program_files_x86.empty()) {
        candidates.emplace_back(fs::path(program_files_x86) / L"simpro2" / L"bin" /
                                L"simpro.exe");
    }
    if (!program_files.empty()) {
        candidates.emplace_back(fs::path(program_files) / L"simpro2" / L"bin" /
                                L"simpro.exe");
    }
    if (!local.empty()) {
        candidates.emplace_back(fs::path(local) / L"Programs" / L"simpro2" / L"bin" /
                                L"simpro.exe");
    }
    for (const auto& candidate : candidates) {
        if (path_is_file(candidate)) {
            return candidate;
        }
    }
    return find_simpro_in_registry();
}

fs::path lmu_install_path() {
    std::vector<fs::path> libraries;
    for (const auto& root : steam_roots()) {
        libraries.push_back(root);
    }
    add_steam_library_paths(libraries);
    for (const auto& library : libraries) {
        const fs::path candidate =
            library / L"steamapps" / L"common" / L"Le Mans Ultimate";
        if (path_is_directory(candidate)) {
            return candidate;
        }
    }
    return {};
}

fs::path lmu_sdk_path() {
    const fs::path install = lmu_install_path();
    if (install.empty()) {
        return {};
    }
    return install / L"Support" / L"SharedMemoryInterface";
}

int simpro_rpc_port() {
    const fs::path config = simagic_appconfig_path();
    if (!path_is_file(config)) {
        return 56789;
    }
    try {
        std::ifstream input(config);
        nlohmann::json json;
        input >> json;
        if (json.contains("rpcPort") && json["rpcPort"].is_number_integer()) {
            return json["rpcPort"].get<int>();
        }
    } catch (...) {
    }
    return 56789;
}

bool process_running(const std::wstring& exe_name) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const std::wstring wanted = lower(exe_name);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (lower(entry.szExeFile) == wanted) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool close_process(const std::wstring& exe_name, int timeout_ms) {
    const std::wstring wanted = lower(exe_name);
    struct Context {
        const std::wstring* wanted;
    } context{&wanted};

    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* ctx = reinterpret_cast<Context*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == 0) {
                return TRUE;
            }
            const HANDLE process =
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!process) {
                return TRUE;
            }
            wchar_t path[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            bool matches = false;
            if (QueryFullProcessImageNameW(process, 0, path, &size)) {
                const fs::path exe(path);
                matches = lower(exe.filename().wstring()) == *ctx->wanted;
            }
            CloseHandle(process);
            if (matches) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!process_running(exe_name)) {
            return true;
        }
        Sleep(100);
    }
    return !process_running(exe_name);
}

bool start_process(const fs::path& exe) {
    if (!path_is_file(exe)) {
        return false;
    }
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr,
                      exe.parent_path().c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

fs::path current_exe_path() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD size =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            return {};
        }
        if (size < buffer.size() - 1) {
            buffer.resize(size);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

fs::path installed_exe_path() { return app_data_dir() / L"SimagicFix.exe"; }

fs::path start_menu_shortcut_path() {
    const std::wstring appdata = get_env_w(L"APPDATA");
    if (appdata.empty()) {
        return {};
    }
    return fs::path(appdata) / L"Microsoft" / L"Windows" / L"Start Menu" / L"Programs" /
           L"SimagicFix.lnk";
}

std::string now_string() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

}  // namespace simagicfix
