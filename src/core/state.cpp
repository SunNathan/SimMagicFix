#include "core/state.h"

#include "core/paths.h"
#include <nlohmann/json.hpp>

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
        state.memory_patch = json.value("memory_patch", false);
        state.version = json.value("version", "");
        state.install_date = json.value("install_date", "");
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
    json["memory_patch"] = state.memory_patch;
    json["version"] = state.version;
    json["install_date"] = state.install_date;
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
