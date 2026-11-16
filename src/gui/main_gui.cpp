#include "core/autostart.h"
#include "core/installer.h"
#include "core/paths.h"
#include "core/simpro_hook.h"
#include "core/state.h"
#include "core/telemetry.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>

#include <chrono>
#include <cwchar>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#pragma comment(linker, \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
    "language='*'\"")

namespace {

using namespace simagicfix;

constexpr wchar_t kMainClass[] = L"SimagicFixMain";
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_TASK_DONE = WM_APP + 2;
constexpr UINT WM_APP_SHOW = WM_APP + 3;

UINT g_taskbar_created = 0;

enum ControlId {
    IDC_TITLE = 100,
    IDC_STATUS_SIMPRO,
    IDC_STATUS_LMU,
    IDC_STATUS_HOOK,
    IDC_STATUS_TELEMETRY,
    IDC_AUTOSTART,
    IDC_PROGRESS,
    IDC_INSTALL,
    IDC_CANCEL,
    IDC_LOGS,
    IDC_QUIT,
    IDC_UNINSTALL,
    IDC_MEMORY_PATCH,
};

enum class TaskKind { None, Install, Uninstall };

enum MenuId {
    IDM_OPEN = 200,
    IDM_AUTOSTART,
    IDM_LOGS,
    IDM_UNINSTALL,
    IDM_QUIT,
};

struct App {
    HINSTANCE instance = nullptr;
    HWND main = nullptr;
    HWND status_simpro = nullptr;
    HWND status_lmu = nullptr;
    HWND status_hook = nullptr;
    HWND status_telemetry = nullptr;
    HWND autostart = nullptr;
    HWND progress = nullptr;
    HWND install = nullptr;
    HWND cancel = nullptr;
    HWND logs_button = nullptr;
    HWND quit_button = nullptr;
    HWND uninstall_button = nullptr;
    HWND memory_patch_checkbox = nullptr;

    HFONT font = nullptr;
    HFONT bold = nullptr;

    AppState state;
    std::string last_hook_error;
    bool installed = false;
    bool tray_only = false;
    bool quitting = false;
    bool keep_source = false;

    std::unique_ptr<TelemetryService> telemetry;
    std::unique_ptr<SimProHookService> hook_service;
    NOTIFYICONDATAW nid{};
    bool tray_added = false;
    HICON small_icon = nullptr;
    HICON tray_icon = nullptr;
    bool tray_icon_owned = false;

    std::thread task_thread;
    TaskKind task_kind = TaskKind::None;
    bool task_running = false;
    bool task_ok = false;
    std::string task_message;
    std::chrono::steady_clock::time_point last_tray_retry{};
    bool tray_hint_shown = false;

    std::mutex log_mutex;
};

App g_app;

std::wstring w(const std::string& value) { return utf8_to_wide(value); }

void log_line(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_app.log_mutex);
    std::error_code ec;
    fs::create_directories(logs_dir(), ec);
    std::ofstream output(logs_dir() / "app.log", std::ios::app);
    if (output) {
        output << now_string() << "  " << message << "\n";
    }
}

void set_text(HWND control, const std::string& text) {
    if (control != nullptr) {
        SetWindowTextW(control, w(text).c_str());
    }
}

void set_font(HWND control, HFONT font) {
    if (control != nullptr && font != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void stop_services() {
    const auto start = std::chrono::steady_clock::now();
    const bool had_services =
        g_app.hook_service != nullptr || g_app.telemetry != nullptr;
    if (g_app.hook_service) {
        log_line("Arrêt : service du hook");
        g_app.hook_service->stop();
        g_app.hook_service.reset();
        log_line("Arrêt : service du hook terminé");
    }
    if (g_app.telemetry) {
        log_line("Arrêt : télémétrie");
        g_app.telemetry->stop();
        g_app.telemetry.reset();
        log_line("Arrêt : télémétrie terminée");
    }
    g_app.last_hook_error.clear();
    if (had_services) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        log_line("Services arrêtés en " + std::to_string(elapsed) + " ms");
    }
}

void start_services() {
    if (g_app.hook_service) {
        return;
    }
    if (!g_app.telemetry) {
        TelemetryService::Options options;
        options.mock = false;
        options.poll_ms = 5;
        g_app.telemetry = std::make_unique<TelemetryService>(options);
        if (!g_app.telemetry->start()) {
            log_line("Échec du démarrage de la télémétrie : " +
                     g_app.telemetry->last_error());
        }
    }
    g_app.last_hook_error.clear();
    g_app.hook_service = std::make_unique<SimProHookService>(
        []() {
            return g_app.telemetry ? g_app.telemetry->snapshot() : LmuSnapshot{};
        });
    g_app.hook_service->start();
    log_line("Service du hook démarré");
}

void apply_patch_state() {
    if (g_app.installed && g_app.state.memory_patch) {
        start_services();
    } else {
        stop_services();
    }
}

void update_tray() {
    if (!g_app.tray_added) {
        return;
    }
    std::wstring tip = L"SimagicFix - ";
    if (g_app.hook_service && g_app.hook_service->installed()) {
        tip += L"hook actif";
    } else if (g_app.hook_service) {
        tip += L"hook en attente";
    } else {
        tip += L"patch désactivé";
    }
    if (!is_autostart_enabled()) {
        tip += L" (démarrage auto désactivé)";
    }
    wcsncpy_s(g_app.nid.szTip, tip.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_MODIFY, &g_app.nid)) {
        g_app.tray_added = false;
        log_line("Échec de la mise à jour de l'icône de notification");
    }
}

void add_tray(HWND hwnd) {
    if (g_app.tray_added) {
        return;
    }
    if (g_app.tray_icon == nullptr) {
        g_app.tray_icon = static_cast<HICON>(
            LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_TRAY_ICON),
                       IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        g_app.tray_icon_owned = g_app.tray_icon != nullptr;
        if (g_app.tray_icon == nullptr) {
            g_app.tray_icon = LoadIconW(nullptr, IDI_APPLICATION);
        }
    }
    if (g_app.nid.cbSize == 0 || g_app.nid.hWnd != hwnd) {
        g_app.nid = {};
        g_app.nid.cbSize = sizeof(g_app.nid);
        g_app.nid.hWnd = hwnd;
        g_app.nid.uID = 1;
        g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_app.nid.uCallbackMessage = WM_APP_TRAY;
    }
    g_app.nid.hIcon = g_app.tray_icon;
    wcsncpy_s(g_app.nid.szTip, L"SimagicFix", _TRUNCATE);
    g_app.tray_added = Shell_NotifyIconW(NIM_ADD, &g_app.nid) != FALSE;
    if (!g_app.tray_added) {
        // The icon may already be registered (for example when simulating
        // TaskbarCreated); remove it and retry once.
        Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
        g_app.tray_added = Shell_NotifyIconW(NIM_ADD, &g_app.nid) != FALSE;
    }
    if (g_app.tray_added) {
        log_line("Icône de notification ajoutée");
        update_tray();
    } else {
        log_line("Échec de l'ajout de l'icône de notification");
    }
}

void show_tray_hint() {
    if (g_app.tray_hint_shown || !g_app.tray_added) {
        return;
    }
    g_app.tray_hint_shown = true;
    g_app.nid.uFlags = NIF_INFO;
    wcsncpy_s(g_app.nid.szInfoTitle, L"SimagicFix", _TRUNCATE);
    wcsncpy_s(g_app.nid.szInfo,
              L"SimagicFix continue en arrière-plan. Utilisez Quitter pour "
              L"fermer complètement.",
              _TRUNCATE);
    g_app.nid.dwInfoFlags = NIIF_INFO;
    if (!Shell_NotifyIconW(NIM_MODIFY, &g_app.nid)) {
        g_app.tray_added = false;
    } else {
        log_line("Notification d'arrière-plan affichée");
    }
    g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}

void remove_tray() {
    if (g_app.tray_added) {
        Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
        g_app.tray_added = false;
        log_line("Icône de notification retirée");
    }
    if (g_app.tray_icon_owned && g_app.tray_icon != nullptr) {
        DestroyIcon(g_app.tray_icon);
    }
    g_app.tray_icon = nullptr;
    g_app.tray_icon_owned = false;
}

void request_exit(HWND hwnd) {
    if (g_app.quitting) {
        return;
    }
    g_app.quitting = true;
    remove_tray();
    log_line("Arrêt demandé : forçage dans 5 s si nécessaire");
    std::thread([]() {
        Sleep(5000);
        ExitProcess(0);
    }).detach();
    DestroyWindow(hwnd);
}

void refresh_status() {
    if (g_app.main == nullptr || g_app.task_running) {
        return;
    }
    const fs::path simpro = simpro_exe_path();
    const fs::path lmu = lmu_install_path();
    set_text(g_app.status_simpro,
             simpro.empty() ? "SimPro Manager : non détecté"
                            : "SimPro Manager : détecté");
    set_text(g_app.status_lmu,
             lmu.empty() ? "Le Mans Ultimate : non détecté"
                         : "Le Mans Ultimate : détecté");

    std::string hook_text = "Hook SimPro : ";
    if (!g_app.hook_service) {
        hook_text += "désactivé";
    } else if (g_app.hook_service->installed()) {
        hook_text += "actif";
    } else {
        const std::string error = g_app.hook_service->last_error();
        hook_text += "en attente";
        if (!error.empty()) {
            hook_text += " (" + error + ")";
            if (error != g_app.last_hook_error) {
                g_app.last_hook_error = error;
                log_line("Hook : " + error);
            }
        }
    }
    set_text(g_app.status_hook, hook_text);

    std::string telemetry_text = "Télémétrie LMU : ";
    if (!g_app.telemetry) {
        telemetry_text += "inactive";
    } else if (g_app.telemetry->lmu_available()) {
        telemetry_text += "active";
    } else {
        telemetry_text += "en attente";
    }
    set_text(g_app.status_telemetry, telemetry_text);

    if (g_app.memory_patch_checkbox != nullptr && g_app.installed) {
        SendMessageW(g_app.memory_patch_checkbox, BM_SETCHECK,
                     g_app.state.memory_patch ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(g_app.memory_patch_checkbox, TRUE);
    }
    if (g_app.installed) {
        const bool autostart = is_autostart_enabled();
        SendMessageW(g_app.autostart, BM_SETCHECK,
                     autostart ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g_app.installed && !g_app.tray_added) {
        const auto now = std::chrono::steady_clock::now();
        if (now - g_app.last_tray_retry >= std::chrono::seconds(5)) {
            g_app.last_tray_retry = now;
            add_tray(g_app.main);
        }
    }
    update_tray();
}

void switch_mode(bool installed) {
    g_app.installed = installed;
    const int show_install = installed ? SW_HIDE : SW_SHOW;
    const int show_manage = installed ? SW_SHOW : SW_HIDE;
    ShowWindow(g_app.install, show_install);
    ShowWindow(g_app.cancel, show_install);
    ShowWindow(g_app.logs_button, show_manage);
    ShowWindow(g_app.quit_button, show_manage);
    ShowWindow(g_app.uninstall_button, show_manage);
    ShowWindow(g_app.memory_patch_checkbox, show_manage);
    ShowWindow(g_app.status_hook, show_manage);
    ShowWindow(g_app.status_telemetry, show_manage);
    SetWindowTextW(g_app.main,
                   installed ? L"SimagicFix" : L"SimagicFix - installation");
    refresh_status();
}

void open_logs() {
    std::error_code ec;
    fs::create_directories(logs_dir(), ec);
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", logs_dir().c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(g_app.main, L"Impossible d'ouvrir le dossier des journaux.",
                    L"SimagicFix", MB_ICONERROR);
    }
}

void show_main() {
    if (g_app.main != nullptr) {
        ShowWindow(g_app.main, SW_SHOW);
        ShowWindow(g_app.main, SW_RESTORE);
        SetForegroundWindow(g_app.main);
    }
}

void run_task(TaskKind kind, std::function<void()> task) {
    if (g_app.task_running) {
        return;
    }
    g_app.task_running = true;
    g_app.task_kind = kind;
    g_app.task_ok = false;
    g_app.task_message.clear();
    KillTimer(g_app.main, 1);
    EnableWindow(g_app.install, FALSE);
    EnableWindow(g_app.uninstall_button, FALSE);
    EnableWindow(g_app.quit_button, FALSE);
    EnableWindow(g_app.cancel, FALSE);
    ShowWindow(g_app.progress, SW_SHOW);
    SendMessageW(g_app.progress, PBM_SETMARQUEE, TRUE, 30);
    g_app.task_thread = std::thread([task = std::move(task)]() {
        try {
            task();
        } catch (const std::exception& e) {
            g_app.task_ok = false;
            g_app.task_message = e.what();
        } catch (...) {
            g_app.task_ok = false;
            g_app.task_message = "Erreur inconnue";
        }
        PostMessageW(g_app.main, WM_APP_TASK_DONE, 0, 0);
    });
}

void finish_task() {
    if (g_app.task_thread.joinable()) {
        g_app.task_thread.join();
    }
    g_app.task_running = false;
    ShowWindow(g_app.progress, SW_HIDE);
    SendMessageW(g_app.progress, PBM_SETMARQUEE, FALSE, 0);
    EnableWindow(g_app.install, TRUE);
    EnableWindow(g_app.uninstall_button, TRUE);
    EnableWindow(g_app.quit_button, TRUE);
    EnableWindow(g_app.cancel, TRUE);

    if (g_app.task_kind == TaskKind::Install) {
        if (g_app.task_ok) {
            g_app.state = load_state();
            apply_patch_state();
            switch_mode(true);
            add_tray(g_app.main);
            update_tray();
            MessageBoxW(g_app.main,
                        L"SimagicFix installé.\nLe hook SimPro est actif.",
                        L"SimagicFix", MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_app.main,
                        w("Installation échouée : " + g_app.task_message).c_str(),
                        L"SimagicFix", MB_ICONERROR);
        }
    } else if (g_app.task_kind == TaskKind::Uninstall) {
        if (g_app.task_ok) {
            remove_tray();
            MessageBoxW(g_app.main, L"SimagicFix a été désinstallé.",
                        L"SimagicFix", MB_ICONINFORMATION);
            request_exit(g_app.main);
        } else {
            if (g_app.installed && !g_app.tray_added) {
                add_tray(g_app.main);
            }
            log_line(g_app.tray_added
                         ? "Désinstallation échouée : icône de notification "
                           "maintenue"
                         : "Désinstallation échouée : icône de notification "
                           "indisponible");
            MessageBoxW(
                g_app.main,
                w("Désinstallation échouée : " + g_app.task_message).c_str(),
                L"SimagicFix", MB_ICONERROR);
        }
    }

    g_app.task_kind = TaskKind::None;
    if (!g_app.quitting && g_app.main != nullptr) {
        SetTimer(g_app.main, 1, 1000, nullptr);
    }
}

void start_install() {
    if (g_app.task_running) {
        return;
    }
    InstallOptions options;
    options.auto_start =
        SendMessageW(g_app.autostart, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options.keep_source = g_app.keep_source;

    std::error_code ec;
    const bool running_installed =
        fs::equivalent(current_exe_path(), installed_exe_path(), ec);
    if (!running_installed && !options.keep_source) {
        const int answer = MessageBoxW(
            g_app.main,
            L"Installation de SimagicFix\n\n"
            L"SimagicFix va être installé dans :\n"
            L"%LOCALAPPDATA%\\SimagicFix\n\n"
            L"Les fichiers SimagicFix.exe et SimagicFixHook.dll présents dans "
            L"le dossier actuel seront supprimés après l'installation. "
            L"L'exe sera supprimé à la fermeture de l'application.\n\n"
            L"L'application continuera de fonctionner depuis son emplacement "
            L"installé et le raccourci du menu Démarrer.\n\n"
            L"Continuer ?",
            L"SimagicFix - installation", MB_OKCANCEL | MB_ICONINFORMATION);
        if (answer != IDOK) {
            return;
        }
    }

    log_line("Installation demandée");
    run_task(TaskKind::Install, [options]() {
        const InstallReport report = install(options);
        g_app.task_ok = report.ok;
        g_app.task_message = report.ok ? "" : report.error;
        log_line(report.ok ? "Installation réussie"
                           : "Échec de l'installation : " + report.error);
        if (!report.warning.empty()) {
            log_line("Avertissement d'installation : " + report.warning);
        }
    });
}

void do_uninstall() {
    if (g_app.task_running) {
        return;
    }
    const int answer = MessageBoxW(
        g_app.main,
        L"Désinstaller SimagicFix ?\n\n"
        L"- le hook SimPro sera arrêté\n"
        L"- le démarrage automatique sera retiré\n"
        L"- le raccourci du menu Démarrer sera supprimé",
        L"SimagicFix", MB_YESNO | MB_ICONWARNING);
    if (answer != IDYES) {
        return;
    }

    bool close_simpro = false;
    if (process_running(L"simpro.exe")) {
        const int simpro = MessageBoxW(
            g_app.main,
            L"SimPro Manager est encore lancé.\n\n"
            L"SimagicFix doit fermer SimPro Manager avant de continuer.\n\n"
            L"OK : fermer SimPro et continuer\n"
            L"Annuler : annuler la désinstallation",
            L"SimagicFix", MB_OKCANCEL | MB_ICONWARNING);
        if (simpro != IDOK) {
            return;
        }
        close_simpro = true;
    }

    log_line("Désinstallation demandée");
    run_task(TaskKind::Uninstall, [close_simpro]() {
        if (close_simpro) {
            log_line("Désinstallation : fermeture de SimPro");
            if (!close_process(L"simpro.exe", 15000)) {
                g_app.task_ok = false;
                g_app.task_message =
                    "Impossible de fermer SimPro Manager. Fermez-le "
                    "manuellement puis réessayez.";
                log_line("Désinstallation : impossible de fermer SimPro");
                return;
            }
            log_line("Désinstallation : SimPro fermé");
        }
        log_line("Désinstallation : arrêt des services");
        stop_services();
        log_line("Désinstallation : services arrêtés");
        const bool ok = uninstall();
        g_app.task_ok = ok;
        g_app.task_message =
            ok ? "" : "La désinstallation a rencontré une erreur.";
        log_line(ok ? "Désinstallation terminée"
                    : "Désinstallation terminée avec des erreurs");
        std::error_code ec;
        const bool running_installed =
            fs::equivalent(current_exe_path(), installed_exe_path(), ec);
        if (running_installed) {
            schedule_delete_self();
        }
    });
}

void toggle_autostart() {
    const bool enabled = is_autostart_enabled();
    if (enabled) {
        disable_autostart();
    } else {
        enable_autostart(installed_exe_path());
    }
    g_app.state.auto_start = is_autostart_enabled();
    save_state(g_app.state);
    refresh_status();
}

void handle_menu(int id) {
    switch (id) {
        case IDM_OPEN:
            show_main();
            break;
        case IDM_AUTOSTART:
            toggle_autostart();
            break;
        case IDM_LOGS:
            open_logs();
            break;
        case IDM_UNINSTALL:
            do_uninstall();
            break;
        case IDM_QUIT:
            request_exit(g_app.main);
            break;
        default:
            break;
    }
}

void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_OPEN, L"Ouvrir SimagicFix");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (is_autostart_enabled() ? MF_CHECKED : 0),
                IDM_AUTOSTART, L"Démarrage automatique");
    AppendMenuW(menu, MF_STRING, IDM_LOGS, L"Ouvrir les journaux");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_UNINSTALL, L"Désinstaller");
    AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Quitter");

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(hwnd);
    const int command =
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, 0,
                       hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    if (command != 0) {
        handle_menu(command);
    }
}

LRESULT CALLBACK main_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (g_taskbar_created != 0 && message == g_taskbar_created) {
        log_line("Explorateur redémarré : réajout de l'icône");
        remove_tray();
        if (g_app.installed) {
            add_tray(hwnd);
        }
        return 0;
    }
    switch (message) {
        case WM_CREATE: {
            g_app.main = hwnd;
            g_app.font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, L"Segoe UI");
            g_app.bold = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, L"Segoe UI");

            HWND title = CreateWindowExW(0, L"STATIC", L"SimagicFix - télémétrie LMU",
                                         WS_CHILD | WS_VISIBLE, 20, 14, 600, 30, hwnd,
                                         reinterpret_cast<HMENU>(IDC_TITLE),
                                         g_app.instance, nullptr);
            set_font(title, g_app.bold);

            const struct {
                int id;
                const wchar_t* text;
                int y;
            } statuses[] = {
                {IDC_STATUS_SIMPRO, L"SimPro Manager : ...", 56},
                {IDC_STATUS_LMU, L"Le Mans Ultimate : ...", 82},
                {IDC_STATUS_HOOK, L"Hook SimPro : ...", 108},
                {IDC_STATUS_TELEMETRY, L"Télémétrie LMU : ...", 134},
            };
            for (const auto& status : statuses) {
                HWND control = CreateWindowExW(
                    0, L"STATIC", status.text, WS_CHILD | WS_VISIBLE, 20, status.y,
                    600, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(status.id)),
                    g_app.instance, nullptr);
                set_font(control, g_app.font);
                if (status.id == IDC_STATUS_SIMPRO) {
                    g_app.status_simpro = control;
                } else if (status.id == IDC_STATUS_LMU) {
                    g_app.status_lmu = control;
                } else if (status.id == IDC_STATUS_HOOK) {
                    g_app.status_hook = control;
                } else {
                    g_app.status_telemetry = control;
                }
            }

            g_app.memory_patch_checkbox = CreateWindowExW(
                0, L"BUTTON", L"Activer le patch mémoire SimPro (hook)",
                WS_CHILD | BS_AUTOCHECKBOX, 20, 170, 600, 26, hwnd,
                reinterpret_cast<HMENU>(IDC_MEMORY_PATCH), g_app.instance,
                nullptr);
            set_font(g_app.memory_patch_checkbox, g_app.font);

            g_app.autostart = CreateWindowExW(
                0, L"BUTTON",
                L"Lancer SimagicFix au démarrage de Windows",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 202, 600, 26, hwnd,
                reinterpret_cast<HMENU>(IDC_AUTOSTART), g_app.instance, nullptr);
            set_font(g_app.autostart, g_app.font);

            g_app.progress = CreateWindowExW(
                0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE, 20, 238, 600, 16,
                hwnd, reinterpret_cast<HMENU>(IDC_PROGRESS), g_app.instance, nullptr);

            g_app.install = CreateWindowExW(
                0, L"BUTTON", L"Installer SimagicFix",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 380, 272, 140, 34, hwnd,
                reinterpret_cast<HMENU>(IDC_INSTALL), g_app.instance, nullptr);
            set_font(g_app.install, g_app.font);
            g_app.cancel = CreateWindowExW(
                0, L"BUTTON", L"Annuler", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 530,
                272, 90, 34, hwnd, reinterpret_cast<HMENU>(IDC_CANCEL),
                g_app.instance, nullptr);
            set_font(g_app.cancel, g_app.font);

            g_app.logs_button = CreateWindowExW(
                0, L"BUTTON", L"Ouvrir les journaux", WS_CHILD | BS_PUSHBUTTON, 20, 272,
                150, 34, hwnd, reinterpret_cast<HMENU>(IDC_LOGS), g_app.instance,
                nullptr);
            g_app.uninstall_button = CreateWindowExW(
                0, L"BUTTON", L"Désinstaller", WS_CHILD | BS_PUSHBUTTON, 180, 272,
                140, 34, hwnd, reinterpret_cast<HMENU>(IDC_UNINSTALL),
                g_app.instance, nullptr);
            g_app.quit_button = CreateWindowExW(
                0, L"BUTTON", L"Quitter", WS_CHILD | BS_PUSHBUTTON, 450, 272, 110, 34,
                hwnd, reinterpret_cast<HMENU>(IDC_QUIT), g_app.instance, nullptr);
            set_font(g_app.logs_button, g_app.font);
            set_font(g_app.uninstall_button, g_app.font);
            set_font(g_app.quit_button, g_app.font);

            switch_mode(g_app.installed);
            SendMessageW(g_app.autostart, BM_SETCHECK,
                         is_autostart_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
            SetTimer(hwnd, 1, 1000, nullptr);

            if (g_app.installed) {
                apply_patch_state();
                add_tray(hwnd);
            } else {
                SendMessageW(g_app.autostart, BM_SETCHECK, BST_CHECKED, 0);
                ShowWindow(hwnd, SW_SHOW);
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_TIMER:
            refresh_status();
            return 0;
        case WM_APP_TRAY:
            if (lparam == WM_LBUTTONDBLCLK) {
                show_main();
            } else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
                show_tray_menu(hwnd);
            }
            return 0;
        case WM_APP_SHOW:
            if (g_app.installed && !g_app.tray_added) {
                add_tray(hwnd);
            }
            show_main();
            return 0;
        case WM_APP_TASK_DONE:
            finish_task();
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            switch (id) {
                case IDC_INSTALL:
                    start_install();
                    break;
                case IDC_CANCEL:
                    if (g_app.installed) {
                        ShowWindow(hwnd, SW_HIDE);
                    } else {
                        request_exit(hwnd);
                    }
                    break;
                case IDC_LOGS:
                    open_logs();
                    break;
                case IDC_QUIT:
                    request_exit(hwnd);
                    break;
                case IDC_UNINSTALL:
                    do_uninstall();
                    break;
                case IDC_AUTOSTART:
                    if (g_app.installed) {
                        toggle_autostart();
                    }
                    break;
                case IDC_MEMORY_PATCH:
                    if (g_app.installed) {
                        g_app.state.memory_patch =
                            SendMessageW(g_app.memory_patch_checkbox,
                                         BM_GETCHECK, 0, 0) == BST_CHECKED;
                        save_state(g_app.state);
                        apply_patch_state();
                        refresh_status();
                    }
                    break;
                default:
                    break;
            }
            return 0;
        }
        case WM_CLOSE:
            if (g_app.task_running) {
                MessageBoxW(hwnd, L"Une opération est en cours. Patientez quelques instants.",
                            L"SimagicFix", MB_ICONINFORMATION);
                return 0;
            }
            if (g_app.installed) {
                if (!g_app.tray_added) {
                    add_tray(hwnd);
                }
                if (!g_app.tray_added) {
                    MessageBoxW(
                        hwnd,
                        L"Impossible d'ajouter l'icône de notification. "
                        L"La fenêtre reste ouverte.",
                        L"SimagicFix", MB_ICONERROR);
                    return 0;
                }
                ShowWindow(hwnd, SW_HIDE);
                log_line("Fenêtre masquée dans la zone de notification");
                show_tray_hint();
                return 0;
            }
            request_exit(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            log_line("Arrêt : suppression de l'icône de notification");
            remove_tray();
            if (g_app.task_thread.joinable()) {
                log_line("Arrêt : attente de la tâche en cours");
                g_app.task_thread.join();
                log_line("Arrêt : tâche terminée");
            }
            log_line("Arrêt : interruption des services");
            stop_services();
            log_line("Arrêt : services arrêtés");
            if (g_app.small_icon != nullptr) {
                DestroyIcon(g_app.small_icon);
                g_app.small_icon = nullptr;
            }
            if (g_app.font) {
                DeleteObject(g_app.font);
            }
            if (g_app.bold) {
                DeleteObject(g_app.bold);
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE,
                    _In_ LPWSTR, _In_ int) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    g_app.instance = instance;
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    const fs::path hook_dll = hook_dll_path();
    std::error_code hook_ec;
    if (!fs::is_regular_file(hook_dll, hook_ec)) {
        log_line("SimagicFixHook.dll introuvable : " + path_to_utf8(hook_dll));
        const std::wstring message =
            L"SimagicFixHook.dll est introuvable.\n\n"
            L"Chemin attendu :\n" +
            hook_dll.wstring() +
            L"\n\nRéinstallez SimagicFix.";
        MessageBoxW(nullptr, message.c_str(), L"SimagicFix",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    const wchar_t* command_line = GetCommandLineW();
    g_app.tray_only = wcsstr(command_line, L"--tray") != nullptr;
    g_app.keep_source = wcsstr(command_line, L"--keep-source") != nullptr;

    // Single instance first: a second launch must not disturb the running app.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SimagicFixSingleton");
    if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        bool activated = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            HWND existing = FindWindowW(kMainClass, nullptr);
            if (existing != nullptr) {
                DWORD_PTR result = 0;
                const LRESULT sent = SendMessageTimeoutW(
                    existing, WM_APP_SHOW, 0, 0,
                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result);
                activated = sent != 0;
                break;
            }
            Sleep(100);
        }
        if (!activated) {
            MessageBoxW(nullptr,
                        L"SimagicFix est déjà lancé mais ne répond pas.\n\n"
                        L"Fermez-le via le gestionnaire des tâches, puis "
                        L"relancez SimagicFix.",
                        L"SimagicFix", MB_ICONWARNING);
        }
        CloseHandle(mutex);
        return 0;
    }

    g_app.state = load_state();
    g_app.installed = g_app.state.installed;

    HICON app_icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (app_icon == nullptr) {
        app_icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    g_app.small_icon = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16,
                   LR_DEFAULTCOLOR));

    WNDCLASSEXW main_class{};
    main_class.cbSize = sizeof(main_class);
    main_class.lpfnWndProc = main_proc;
    main_class.hInstance = instance;
    main_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    main_class.hIcon = app_icon;
    main_class.hIconSm = g_app.small_icon;
    main_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    main_class.lpszClassName = kMainClass;
    RegisterClassExW(&main_class);

    HWND main_window = CreateWindowExW(
        0, kMainClass, L"SimagicFix",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
        CW_USEDEFAULT, 640, 380, nullptr, nullptr, instance, nullptr);
    if (main_window == nullptr) {
        return 1;
    }

    if (g_app.installed && g_app.tray_only) {
        ShowWindow(main_window, SW_HIDE);
    } else {
        ShowWindow(main_window, SW_SHOW);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (mutex != nullptr) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return static_cast<int>(message.wParam);
}
