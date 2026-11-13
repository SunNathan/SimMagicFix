#pragma once

#include "core/common.h"

namespace simagicfix {

fs::path local_app_data();
fs::path app_data_dir();
fs::path logs_dir();
fs::path backups_dir();
fs::path state_path();

fs::path simagic_config_dir();
fs::path dashboard_dir();
fs::path simagic_appconfig_path();

fs::path simpro_exe_path();
fs::path lmu_install_path();
fs::path lmu_sdk_path();

int simpro_rpc_port();
int simpro_http_port();

// Changes only the rpcPort field of SimPro's appconfig.json. The file is
// written atomically so a crash cannot leave a truncated config behind.
bool write_simpro_rpc_port(int port, std::string& error);
bool backup_simpro_appconfig(std::string& error);
fs::path simpro_appconfig_backup_path();

bool process_running(const std::wstring& exe_name);
bool close_process(const std::wstring& exe_name, int timeout_ms = 15000);
bool start_process(const fs::path& exe);

fs::path current_exe_path();
fs::path installed_exe_path();
fs::path start_menu_shortcut_path();

std::string now_string();

}  // namespace simagicfix
