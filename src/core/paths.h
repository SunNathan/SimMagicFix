#pragma once

#include "core/common.h"

namespace simagicfix {

fs::path local_app_data();
fs::path app_data_dir();
fs::path logs_dir();
fs::path state_path();

fs::path simagic_config_dir();
fs::path simagic_appconfig_path();

fs::path simpro_exe_path();
fs::path lmu_install_path();
fs::path lmu_sdk_path();

int simpro_rpc_port();

bool process_running(const std::wstring& exe_name);
bool close_process(const std::wstring& exe_name, int timeout_ms = 15000);
bool start_process(const fs::path& exe);

fs::path current_exe_path();
fs::path installed_exe_path();
fs::path hook_dll_path();
fs::path start_menu_shortcut_path();

std::string now_string();

}  // namespace simagicfix
