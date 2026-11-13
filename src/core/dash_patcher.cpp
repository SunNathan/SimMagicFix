#include "core/dash_patcher.h"

#include "core/paths.h"

#include <set>

namespace simagicfix {

namespace {

struct Rule {
    const char* label;
    const char* old_value;
    bool old_is_string;
    const char* new_expression;
    const char* description;
    bool caddy_only;
};

// Value-only rules. They never add, remove or move a widget: they only replace
// the expression that feeds an existing value box.
const Rule kRules[] = {
    {"TC 1", "2", true, "r.tcLevel.toFixed(0)", "TC", false},
    {"TC 2", "3", true, "r.tcCut.toFixed(0)", "TC Cut", false},
    {"TC A", "5", true, "r.tcLevel.toFixed(0)", "TC", false},
    {"TC B", "3", true, "r.tcCut.toFixed(0)", "TC Cut", false},
    {"TC-LA", "4", true, "r.tcLevel.toFixed(0)", "TC", false},
    {"TC-LO", "3", true, "r.tcCut.toFixed(0)", "TC Cut", false},
    {"TCLO", "1", true, "r.tcLevel.toFixed(0)", "TC", false},
    {"TCLA", "4", true, "r.tcCut.toFixed(0)", "TC Cut", false},
    {"soc", "94", true, "(r.soc||0).toFixed(0)", "SoC", false},
    {"SOC", "82", true, "(r.soc||0).toFixed(0)", "SoC", false},
    {"SoC", "100%", true, "(r.soc||0).toFixed(0)+\"%\"", "SoC", true},
    {"MAP", "r.completedLaps", false, "(r.engineMap||0).toFixed(0)", "Engine map", false},
    {"MAP", "8", true, "(r.engineMap||0).toFixed(0)", "Engine map", false},
    {"MIG", "0", true, "(r.migration||0).toFixed(0)", "Migration", true},
    {"FRONT", "4", true, "(r.frontAntiRollBar||0).toFixed(0)", "Front ARB", true},
    {"REAR", "2", true, "(r.rearAntiRollBar||0).toFixed(0)", "Rear ARB", true},
};

bool run_process(const std::wstring& command, std::string& error) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = command;
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        error = "Cannot start tar.exe";
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code != 0) {
        error = "tar.exe failed (exit code " + std::to_string(exit_code) + ")";
        return false;
    }
    return true;
}

std::wstring quote(const fs::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

std::wstring temp_root() {
    return (fs::temp_directory_path() / L"SimagicFix").wstring();
}

fs::path make_temp_dir(const std::string& name, const std::string& suffix) {
    const fs::path root =
        fs::path(temp_root()) /
        (utf8_to_wide(name) + L"-" + utf8_to_wide(suffix) + L"-" +
         std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

bool extract_dash(const fs::path& file, const fs::path& target, std::string& error) {
    std::error_code ec;
    fs::create_directories(target, ec);
    const std::wstring command =
        L"tar.exe -xf " + quote(file) + L" -C " + quote(target);
    return run_process(command, error);
}

bool create_dash(const fs::path& source_dir, const fs::path& zip_file,
                 std::string& error) {
    const std::wstring command = L"tar.exe --format zip -cf " + quote(zip_file) +
                                 L" -C " + quote(source_dir) + L" .";
    return run_process(command, error);
}

bool is_js_file(const fs::path& path) {
    return path.extension() == L".js" &&
           path.filename().wstring().find(L"main") != std::wstring::npos;
}

bool is_caddy_dashboard(const std::string& name) {
    return name.find("Caddy") != std::string::npos;
}

bool apply_rule(std::string& js, const Rule& rule, bool caddy) {
    if (rule.caddy_only && !caddy) {
        return false;
    }
    const std::string label_token = std::string("children:\"") + rule.label + "\"";
    const size_t label_pos = js.find(label_token);
    if (label_pos == std::string::npos) {
        return false;
    }
    if (js.find(label_token, label_pos + 1) != std::string::npos) {
        return false;  // ambiguous label
    }
    const std::string old_token =
        rule.old_is_string ? std::string("children:\"") + rule.old_value + "\""
                           : std::string("children:") + rule.old_value;
    const size_t value_pos = js.find(old_token, label_pos);
    if (value_pos == std::string::npos || value_pos > label_pos + 400) {
        return false;
    }
    const std::string new_token = std::string("children:") + rule.new_expression;
    js.replace(value_pos, old_token.size(), new_token);
    return true;
}

bool already_patched(const std::string& js) {
    return js.find("56799") != std::string::npos &&
           (js.find("r.tcLevel") != std::string::npos ||
            js.find("r.absLevel") != std::string::npos ||
            js.find("r.engineMap") != std::string::npos ||
            js.find("r.soc") != std::string::npos);
}

uint32_t file_hash(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return 0;
    }
    uint32_t hash = 2166136261u;
    char buffer[8192];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
        const auto count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 16777619u;
        }
    }
    return hash;
}

std::string safe_timestamp() {
    std::string value = now_string();
    for (char& c : value) {
        if (c == ':' || c == ' ' || c == '/') {
            c = '-';
        }
    }
    return value;
}

struct JsScan {
    bool already = false;
    std::set<std::string> fixes;
};

JsScan scan_js_dir(const fs::path& root, bool caddy) {
    JsScan result;
    std::error_code ec;
    for (const auto& item : fs::recursive_directory_iterator(root, ec)) {
        if (!item.is_regular_file() || !is_js_file(item.path())) {
            continue;
        }
        std::ifstream input(item.path(), std::ios::binary);
        std::string js((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
        if (already_patched(js)) {
            result.already = true;
        }
        for (const auto& rule : kRules) {
            std::string copy = js;
            if (apply_rule(copy, rule, caddy)) {
                result.fixes.insert(rule.description);
            }
        }
    }
    return result;
}

}  // namespace

bool tar_available() {
    wchar_t path[MAX_PATH] = {};
    return SearchPathW(nullptr, L"tar.exe", nullptr, MAX_PATH, path, nullptr) > 0;
}

std::vector<DashScanResult> scan_dashboards() {
    std::vector<DashScanResult> results;
    if (!tar_available()) {
        return results;
    }
    const fs::path folder = dashboard_dir();
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return results;
    }
    for (const auto& item : fs::directory_iterator(folder, ec)) {
        if (!item.is_regular_file() || item.path().extension() != L".dash") {
            continue;
        }
        const std::string name = item.path().stem().string();
        const fs::path temp = make_temp_dir(name, "scan");
        std::string error;
        if (!extract_dash(item.path(), temp, error)) {
            fs::remove_all(temp, ec);
            continue;
        }
        const JsScan scan = scan_js_dir(temp, is_caddy_dashboard(name));
        fs::remove_all(temp, ec);
        if (scan.fixes.empty() && !scan.already) {
            continue;
        }
        DashScanResult result;
        result.file = item.path();
        result.name = name;
        result.compatible = !scan.fixes.empty();
        result.already_patched = scan.already;
        result.fixes.assign(scan.fixes.begin(), scan.fixes.end());
        results.push_back(std::move(result));
    }
    std::sort(results.begin(), results.end(),
              [](const DashScanResult& a, const DashScanResult& b) {
                  return a.name < b.name;
              });
    return results;
}

DashPatchResult patch_dashboard(const fs::path& file) {
    DashPatchResult result;
    result.file = file;
    result.name = file.stem().string();
    result.original_crc = file_hash(file);
    if (!tar_available()) {
        result.error = "tar.exe not found (Windows 10/11 is required)";
        return result;
    }

    std::error_code ec;
    const fs::path temp = make_temp_dir(result.name, "patch");
    if (!extract_dash(file, temp, result.error)) {
        fs::remove_all(temp, ec);
        return result;
    }

    const bool caddy = is_caddy_dashboard(result.name);
    std::set<std::string> applied;
    bool changed = false;

    for (const auto& item : fs::recursive_directory_iterator(temp, ec)) {
        if (!item.is_regular_file() || !is_js_file(item.path())) {
            continue;
        }
        std::ifstream input(item.path(), std::ios::binary);
        std::string js((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
        for (const auto& rule : kRules) {
            if (apply_rule(js, rule, caddy)) {
                applied.insert(rule.description);
                changed = true;
            }
        }
        size_t port_pos = 0;
        while ((port_pos = js.find("56789", port_pos)) != std::string::npos) {
            js.replace(port_pos, 5, "56799");
            port_pos += 5;
            changed = true;
        }
        std::ofstream output(item.path(), std::ios::binary | std::ios::trunc);
        output.write(js.data(), static_cast<std::streamsize>(js.size()));
    }

    if (!changed) {
        fs::remove_all(temp, ec);
        result.ok = true;
        result.patched_crc = result.original_crc;
        return result;
    }

    fs::create_directories(backups_dir(), ec);
    const fs::path backup =
        backups_dir() / (result.name + "-" + safe_timestamp() + ".dash.bak");
    fs::copy_file(file, backup, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        fs::remove_all(temp, ec);
        result.error = "Cannot create backup: " + ec.message();
        return result;
    }
    result.backup = backup.string();

    const fs::path new_zip = file.wstring() + L".new.zip";
    if (!create_dash(temp, new_zip, result.error)) {
        fs::remove_all(temp, ec);
        return result;
    }
    if (!MoveFileExW(new_zip.wstring().c_str(), file.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove_all(temp, ec);
        fs::remove(new_zip, ec);
        result.error = "Cannot replace dashboard archive";
        return result;
    }
    fs::remove_all(temp, ec);

    // Verify the new archive by extracting it again.
    const fs::path verify = make_temp_dir(result.name, "verify");
    if (!extract_dash(file, verify, result.error)) {
        fs::remove_all(verify, ec);
        return result;
    }
    const JsScan scan = scan_js_dir(verify, caddy);
    fs::remove_all(verify, ec);
    if (!scan.already && scan.fixes.size() < applied.size()) {
        // Put the original file back so a failed verification never leaves a
        // half-patched dashboard behind.
        fs::copy_file(backup, file, fs::copy_options::overwrite_existing, ec);
        result.error = "Dashboard verification failed (original restored)";
        return result;
    }

    result.ok = true;
    result.patched_crc = file_hash(file);
    result.applied.assign(applied.begin(), applied.end());
    return result;
}

bool restore_dashboard(const PatchedDashboard& dashboard) {
    std::error_code ec;
    if (dashboard.backup.empty() || !fs::is_regular_file(dashboard.backup, ec)) {
        return false;
    }
    fs::copy_file(dashboard.backup, dashboard.file,
                  fs::copy_options::overwrite_existing, ec);
    return !ec;
}

}  // namespace simagicfix
