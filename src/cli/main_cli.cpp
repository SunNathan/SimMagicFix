#include "core/autostart.h"
#include "core/installer.h"
#include "core/mock_shm.h"
#include "core/paths.h"
#include "core/probe.h"
#include "core/simpro_hook.h"
#include "core/telemetry.h"

#include <atomic>
#include <csignal>
#include <iostream>

namespace {

std::atomic<bool> g_stop{false};

BOOL WINAPI console_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        g_stop.store(true);
        return TRUE;
    }
    return FALSE;
}

void print_help() {
    std::cout
        << "SimagicFixCLI - outils de diagnostic\n"
        << "  install [--no-autostart] [--keep-source]   Installer SimagicFix\n"
        << "  autostart --enable|--disable|--status      Gérer le démarrage automatique\n"
        << "  patch-memory [SECONDES] [--mock]           Tester le hook SimPro\n"
        << "  probe                                      Lire la télémétrie LMU\n"
        << "  selftest                                   Vérifier les structures LMU\n"
        << "  mock-shm [SECONDES]                        Créer une télémétrie simulée\n";
}

int command_install(const std::vector<std::string>& args) {
    simagicfix::InstallOptions options;
    options.auto_start = true;
    for (const auto& arg : args) {
        if (arg == "--no-autostart") {
            options.auto_start = false;
        } else if (arg == "--keep-source") {
            options.keep_source = true;
        }
    }
    const auto report = simagicfix::install(options);
    if (!report.ok) {
        std::cout << "Échec de l'installation : " << report.error << "\n";
        return 1;
    }
    if (!report.warning.empty()) {
        std::cout << "Avertissement : " << report.warning << "\n";
    }
    std::cout << "Installé.\n";
    std::cout << "Démarrage automatique : "
              << (options.auto_start ? "activé" : "désactivé") << "\n";
    return 0;
}

int command_patch_memory(const std::vector<std::string>& args) {
    int seconds = 10;
    bool mock = false;
    for (const auto& arg : args) {
        if (arg == "--mock") {
            mock = true;
            continue;
        }
        try {
            seconds = std::stoi(arg);
        } catch (...) {
        }
    }

    simagicfix::TelemetryService::Options options;
    options.mock = mock;
    options.poll_ms = 5;
    simagicfix::TelemetryService telemetry(options);
    telemetry.start();

    simagicfix::SimProHookService service(
        [&telemetry]() { return telemetry.snapshot(); });
    if (!service.start()) {
        std::cerr << "Échec du service du hook : " << service.last_error() << "\n";
        telemetry.stop();
        return 1;
    }
    std::cout << "Service du hook démarré (SimPro 2.1.8)\n";

    for (int i = 0; i < seconds && !g_stop.load(); ++i) {
        Sleep(1000);
        const auto snapshot = telemetry.snapshot();
        std::cout << "t=" << (i + 1)
                  << " installé=" << (service.installed() ? "oui" : "non")
                  << " erreur=\"" << service.last_error() << "\""
                  << " lmu=" << (snapshot.valid ? snapshot.vehicle_name : "-")
                  << " tc=" << snapshot.tc << "/" << snapshot.tc_max
                  << " abs=" << snapshot.abs << "/" << snapshot.abs_max
                  << " map=" << snapshot.motor_map << "/"
                  << snapshot.motor_map_max
                  << " soc=" << static_cast<int>(snapshot.soc_percent())
                  << "\n";
    }

    const bool installed = service.installed();
    service.stop();
    telemetry.stop();
    return installed ? 0 : 1;
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
            std::cout
                << (simagicfix::is_autostart_enabled() ? "activé" : "désactivé")
                << "\n";
            return 0;
        }
        if (args[0] == "--enable") {
            return simagicfix::enable_autostart(
                       simagicfix::installed_exe_path())
                       ? 0
                       : 1;
        }
        if (args[0] == "--disable") {
            return simagicfix::disable_autostart() ? 0 : 1;
        }
        print_help();
        return 1;
    }

    print_help();
    return 1;
}
