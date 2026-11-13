#include "core/autostart.h"
#include "core/bridge.h"
#include "core/dash_patcher.h"
#include "core/installer.h"
#include "core/mock_shm.h"
#include "core/paths.h"
#include "core/probe.h"
#include "core/simpro_client.h"
#include "core/state.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <set>

namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_stop{false};

BOOL WINAPI console_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_stop.store(true);
        return TRUE;
    }
    return FALSE;
}

void print_help() {
    std::cout
        << "SimagicFixCLI\n"
        << "  selftest                 Print LMU SDK struct sizes/offsets\n"
        << "  probe                    Dump live LMU values\n"
        << "  scan                     List compatible dashboards\n"
        << "  patch-dash [--all|FILE]  Patch value bindings (backup included)\n"
        << "  restore-dash             Restore original dashboards\n"
        << "  restore-unselected       Restore dashboards outside the saved selection\n"
        << "  mock-shm [SECONDS]       Create a fake LMU_Data mapping\n"
        << "  run [--mock] [--port N] [--fallback-port N] [--simpro-port N]\n"
        << "  install [--no-autostart] [--compat|--no-compat]\n"
        << "  compat on|off|status     Move SimPro RPC to 56788 for the bridge\n"
        << "  autostart --enable|--disable|--status\n";
}

std::vector<fs::path> compatible_dashboards() {
    std::vector<fs::path> paths;
    for (const auto& item : simagicfix::scan_dashboards()) {
        if (item.compatible || item.already_patched) {
            paths.push_back(item.file);
        }
    }
    return paths;
}

int command_scan() {
    const auto results = simagicfix::scan_dashboards();
    if (results.empty()) {
        std::cout << "No compatible dashboard found in "
                  << simagicfix::path_to_utf8(simagicfix::dashboard_dir()) << "\n";
        return 1;
    }
    for (const auto& item : results) {
        std::cout << item.name << " ["
                  << simagicfix::path_to_utf8(item.file) << "]\n";
        std::cout << "  compatible: " << (item.compatible ? "yes" : "no")
                  << "  already patched: " << (item.already_patched ? "yes" : "no")
                  << "\n";
        for (const auto& fix : item.fixes) {
            std::cout << "    - " << fix << "\n";
        }
    }
    return 0;
}

int command_patch(const std::vector<std::string>& args) {
    std::vector<fs::path> targets;
    if (!args.empty() && args[0] != "--all") {
        targets.emplace_back(simagicfix::utf8_to_wide(args[0]));
    } else {
        targets = compatible_dashboards();
    }
    if (targets.empty()) {
        std::cout << "No dashboard to patch.\n";
        return 1;
    }
    int failures = 0;
    for (const auto& target : targets) {
        const auto result = simagicfix::patch_dashboard(target);
        if (!result.ok) {
            std::cout << "FAILED " << result.name << ": " << result.error << "\n";
            ++failures;
            continue;
        }
        std::cout << "OK " << result.name;
        if (result.applied.empty()) {
            std::cout << " (already patched or no matching value)";
        } else {
            std::cout << " -> ";
            for (size_t i = 0; i < result.applied.size(); ++i) {
                if (i != 0) {
                    std::cout << ", ";
                }
                std::cout << result.applied[i];
            }
        }
        std::cout << "\n";
    }
    return failures == 0 ? 0 : 1;
}

int command_install(const std::vector<std::string>& args) {
    simagicfix::InstallOptions options;
    options.auto_start = true;
    for (const auto& arg : args) {
        if (arg == "--no-autostart") {
            options.auto_start = false;
        } else if (arg == "--compat") {
            options.compat_mode = true;
        } else if (arg == "--no-compat") {
            options.compat_mode = false;
        }
    }
    const simagicfix::AppState state = simagicfix::load_state();
    if (!state.selection.empty()) {
        for (const auto& file : state.selection) {
            options.dashboards.push_back(simagicfix::utf8_to_path(file));
        }
    } else {
        options.dashboards = compatible_dashboards();
    }
    const auto report = simagicfix::install(options);
    if (!report.ok) {
        std::cout << "Install failed: " << report.error << "\n";
        return 1;
    }
    std::cout << "Installed. Patched dashboards:\n";
    for (const auto& item : report.patched) {
        std::cout << "  - " << item.name << " (" << item.applied.size()
                  << " values)\n";
    }
    std::cout << "Auto-start: " << (options.auto_start ? "enabled" : "disabled")
              << "\n";
    std::cout << "Compatibility mode: "
              << (options.compat_mode ? "enabled (SimPro RPC 56788)" : "disabled")
              << "\n";
    return 0;
}

int command_compat(const std::vector<std::string>& args) {
    simagicfix::AppState state = simagicfix::load_state();
    const int original_port =
        state.simpro_rpc_port > 0 ? state.simpro_rpc_port : 56789;
    const int compat_port =
        state.compat_rpc_port > 0 ? state.compat_rpc_port : 56788;

    if (args.empty() || args[0] == "status") {
        const int current = simagicfix::simpro_rpc_port();
        std::cout << "SimPro RPC port : " << current << "\n";
        std::cout << "SimPro HTTP port: " << simagicfix::simpro_http_port()
                  << "\n";
        std::cout << "Expected port   : "
                  << (state.compat_mode ? compat_port : original_port) << "\n";
        const auto response = simagicfix::post_json_to_simpro(
            current,
            R"({"id":1,"jsonrpc":"2.0","method":"query","params":{"items":["devices"]}})",
            800);
        std::cout << "Reachable       : "
                  << (response.has_value() ? "yes" : "no") << "\n";
        return 0;
    }

    const bool enable = args[0] == "on";
    if (!enable && args[0] != "off") {
        print_help();
        return 1;
    }

    std::string error;
    if (enable && !state.compat_mode &&
        simagicfix::simpro_rpc_port() != compat_port) {
        if (!simagicfix::backup_simpro_appconfig(error)) {
            std::cout << "Cannot back up SimPro config: " << error << "\n";
            return 1;
        }
    }
    const int target = enable ? compat_port : original_port;
    if (!simagicfix::write_simpro_rpc_port(target, error)) {
        std::cout << "Cannot update SimPro config: " << error << "\n";
        return 1;
    }
    state.compat_mode = enable;
    state.simpro_rpc_port = original_port;
    state.compat_rpc_port = compat_port;
    state.bridge_port = enable ? 56789 : 56799;
    simagicfix::save_state(state);
    std::cout << "SimPro rpcPort set to " << target << ".\n";
    std::cout << "Restart SimPro Manager, then start SimagicFix.\n";
    return 0;
}

int command_restore_unselected() {
    const simagicfix::AppState state = simagicfix::load_state();
    const std::set<std::string> selected(state.selection.begin(),
                                         state.selection.end());
    std::vector<fs::path> targets;
    for (const auto& dashboard : state.patched) {
        if (selected.find(dashboard.file) == selected.end()) {
            targets.push_back(simagicfix::utf8_to_path(dashboard.file));
        }
    }
    if (targets.empty()) {
        std::cout << "No unselected patched dashboard.\n";
        return 0;
    }
    const bool ok = simagicfix::restore_selected(targets);
    std::cout << (ok ? "Restored " : "Restore failed for ")
              << targets.size() << " dashboard(s).\n";
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(console_handler, TRUE);

    if (argc < 2) {
        print_help();
        return 0;
    }
    const std::string command = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (command == "selftest") {
        return simagicfix::run_selftest();
    }
    if (command == "probe") {
        return simagicfix::run_probe();
    }
    if (command == "scan") {
        return command_scan();
    }
    if (command == "patch-dash") {
        return command_patch(args);
    }
    if (command == "restore-dash") {
        return simagicfix::restore_all() ? 0 : 1;
    }
    if (command == "restore-unselected") {
        return command_restore_unselected();
    }
    if (command == "mock-shm") {
        int seconds = 30;
        if (!args.empty()) {
            try {
                seconds = std::stoi(args[0]);
            } catch (...) {
            }
        }
        simagicfix::MockShm mock;
        mock.run(seconds);
        return 0;
    }
    if (command == "install") {
        return command_install(args);
    }
    if (command == "compat") {
        return command_compat(args);
    }
    if (command == "autostart") {
        if (args.empty() || args[0] == "--status") {
            std::cout << (simagicfix::is_autostart_enabled() ? "enabled" : "disabled")
                      << "\n";
            return 0;
        }
        if (args[0] == "--enable") {
            return simagicfix::enable_autostart(simagicfix::installed_exe_path()) ? 0 : 1;
        }
        if (args[0] == "--disable") {
            return simagicfix::disable_autostart() ? 0 : 1;
        }
        print_help();
        return 1;
    }
    if (command == "run") {
        simagicfix::BridgeService::Options options;
        options.port = 56799;
        options.fallback_port = 0;
        options.simpro_port = simagicfix::simpro_rpc_port();
        options.simpro_http_port = simagicfix::simpro_http_port();
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--mock") {
                options.mock = true;
            } else if (args[i] == "--port" && i + 1 < args.size()) {
                try {
                    options.port = std::stoi(args[++i]);
                } catch (...) {
                    std::cout << "Invalid --port value\n";
                    return 1;
                }
            } else if (args[i] == "--fallback-port" && i + 1 < args.size()) {
                try {
                    options.fallback_port = std::stoi(args[++i]);
                } catch (...) {
                    std::cout << "Invalid --fallback-port value\n";
                    return 1;
                }
            } else if (args[i] == "--simpro-port" && i + 1 < args.size()) {
                try {
                    options.simpro_port = std::stoi(args[++i]);
                } catch (...) {
                    std::cout << "Invalid --simpro-port value\n";
                    return 1;
                }
            }
        }
        simagicfix::BridgeService bridge(options);
        if (!bridge.start()) {
            std::cout << "Cannot start bridge: " << bridge.last_error() << "\n";
            return 1;
        }
        std::cout << "Bridge listening on ";
        const auto ports = bridge.ports();
        for (size_t i = 0; i < ports.size(); ++i) {
            if (i != 0) {
                std::cout << " + ";
            }
            std::cout << "127.0.0.1:" << ports[i];
        }
        std::cout << " (SimPro RPC " << options.simpro_port << ")"
                  << (options.mock ? " [mock]" : "") << "\n";
        std::cout << "Press Ctrl+C to stop.\n";
        while (!g_stop.load() && bridge.running()) {
            Sleep(250);
        }
        bridge.stop();
        return 0;
    }

    print_help();
    return 1;
}
