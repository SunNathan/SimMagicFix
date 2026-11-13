#include "core/state.h"

#include "core/paths.h"
#include "json.hpp"

namespace simagicfix {

AppState load_state() {
    AppState state;
    const fs::path path = state_path();
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return state;
    }
    try {
        std::ifstream input(path);
        nlohmann::json json;
        input >> json;
        state.installed = json.value("installed", false);
        state.auto_start = json.value("auto_start", false);
        state.compat_mode = json.value("compat_mode", false);
        state.bridge_port = json.value("bridge_port", 56799);
        state.simpro_rpc_port = json.value("simpro_rpc_port", 56789);
        state.compat_rpc_port = json.value("compat_rpc_port", 56788);
        state.version = json.value("version", "");
        state.install_date = json.value("install_date", "");
        if (json.contains("selection") && json["selection"].is_array()) {
            for (const auto& item : json["selection"]) {
                state.selection.push_back(item.get<std::string>());
            }
        }
        if (json.contains("patched") && json["patched"].is_array()) {
            for (const auto& item : json["patched"]) {
                PatchedDashboard dash;
                dash.file = item.value("file", "");
                dash.name = item.value("name", "");
                dash.backup = item.value("backup", "");
                dash.original_crc = item.value("original_crc", 0u);
                dash.patched_crc = item.value("patched_crc", 0u);
                if (item.contains("fixes") && item["fixes"].is_array()) {
                    for (const auto& fix : item["fixes"]) {
                        dash.fixes.push_back(fix.get<std::string>());
                    }
                }
                state.patched.push_back(std::move(dash));
            }
        }
    } catch (...) {
        return AppState{};
    }
    return state;
}

bool save_state(const AppState& state) {
    std::error_code ec;
    fs::create_directories(app_data_dir(), ec);
    nlohmann::json json;
    json["installed"] = state.installed;
    json["auto_start"] = state.auto_start;
    json["compat_mode"] = state.compat_mode;
    json["bridge_port"] = state.bridge_port;
    json["simpro_rpc_port"] = state.simpro_rpc_port;
    json["compat_rpc_port"] = state.compat_rpc_port;
    json["version"] = state.version;
    json["install_date"] = state.install_date;
    json["selection"] = state.selection;
    json["patched"] = nlohmann::json::array();
    for (const auto& dash : state.patched) {
        nlohmann::json item;
        item["file"] = dash.file;
        item["name"] = dash.name;
        item["backup"] = dash.backup;
        item["original_crc"] = dash.original_crc;
        item["patched_crc"] = dash.patched_crc;
        item["fixes"] = dash.fixes;
        json["patched"].push_back(std::move(item));
    }
    const fs::path path = state_path();
    const fs::path temp = path.wstring() + L".tmp";
    std::ofstream output(temp);
    if (!output) {
        return false;
    }
    output << json.dump(2);
    output.close();
    if (!output) {
        return false;
    }
    return MoveFileExW(temp.wstring().c_str(), path.wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

}  // namespace simagicfix
