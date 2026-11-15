#include "core/autostart.h"
#include "core/bridge.h"
#include "core/installer.h"
#include "core/mock_shm.h"
#include "core/paths.h"
#include "core/probe.h"
#include "core/simpro_memory.h"
#include "core/state.h"

#include <atomic>
#include <csignal>
#include <iostream>

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
        << "  mock-shm [SECONDS]       Create a fake LMU_Data mapping\n"
        << "  patch-memory [SECONDS] [--hook] [--mock]\n"
        << "                           Write LMU aids into SimPro game_data\n"
        << "  run [--mock] [--port N] [--simpro-port N]\n"
        << "  install [--no-autostart]\n"
        << "  autostart --enable|--disable|--status\n";
}

int command_install(const std::vector<std::string>& args) {
    simagicfix::InstallOptions options;
    options.auto_start = true;
    for (const auto& arg : args) {
        if (arg == "--no-autostart") {
            options.auto_start = false;
        }
    }
    const auto report = simagicfix::install(options);
    if (!report.ok) {
        std::cout << "Install failed: " << report.error << "\n";
        return 1;
    }
    std::cout << "Installed.\n";
    std::cout << "Auto-start: " << (options.auto_start ? "enabled" : "disabled")
              << "\n";
    return 0;
}

int command_patch_memory(const std::vector<std::string>& args) {
    int seconds = 10;
    bool high_performance = false;
    bool hook = false;
    bool mock = false;
    for (const auto& arg : args) {
        if (arg == "--max") {
            high_performance = true;
            continue;
        }
        if (arg == "--hook") {
            hook = true;
            continue;
        }
        if (arg == "--mock") {
            mock = true;
            continue;
        }
        try {
            seconds = std::stoi(arg);
        } catch (...) {
        }
    }
    simagicfix::BridgeService::Options options;
    options.port = 56799;
    options.simpro_port = simagicfix::simpro_rpc_port();
    options.mock = mock;
    options.poll_ms = (hook || high_performance) ? 2 : 20;
    simagicfix::BridgeService bridge(options);
    if (!bridge.start()) {
        std::cout << "Cannot start bridge: " << bridge.last_error() << "\n";
        return 1;
    }
    const simagicfix::PatchMode mode =
        hook ? simagicfix::PatchMode::Hook
             : (high_performance ? simagicfix::PatchMode::HighPerformance
                                 : simagicfix::PatchMode::LowCpu);
    simagicfix::SimProMemoryPatcher patcher(
        [&bridge]() { return bridge.snapshot(); }, mode);
    if (!patcher.start()) {
        std::cout << "Cannot start memory patcher\n";
        bridge.stop();
        return 1;
    }
    std::cout << "Memory patcher started ("
              << (hook ? "hook" : (high_performance ? "high performance"
                                                    : "low performance"))
              << " mode, SimPro RPC " << options.simpro_port << ")\n";
    for (int i = 0; i < seconds; ++i) {
        Sleep(1000);
        const auto snapshot = bridge.snapshot();
        std::cout << "t=" << (i + 1)
                  << " available=" << (patcher.available() ? "yes" : "no")
                  << " hook=" << (patcher.hook_installed() ? "yes" : "no")
                  << " error=\"" << patcher.last_error() << "\""
                  << " lmu=" << (snapshot.valid ? snapshot.vehicle_name : "-")
                  << " tc=" << snapshot.tc << "/" << snapshot.tc_max
                  << " abs=" << snapshot.abs << "/" << snapshot.abs_max
                  << " map=" << snapshot.motor_map << "/"
                  << snapshot.motor_map_max
                  << " soc=" << static_cast<int>(snapshot.soc_percent())
                  << "\n";
    }
    const bool available = patcher.available();
    patcher.stop();
    bridge.stop();
    return available ? 0 : 1;
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
    if (command == "patch-memory") {
        return command_patch_memory(args);
    }
    if (command == "install") {
        return command_install(args);
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
        options.simpro_port = simagicfix::simpro_rpc_port();
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
        std::cout << "Bridge listening on 127.0.0.1:" << bridge.port()
                  << " (SimPro " << options.simpro_port << ")"
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
