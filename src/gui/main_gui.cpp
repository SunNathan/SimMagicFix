#include "core/autostart.h"
#include "core/bridge.h"
#include "core/installer.h"
#include "core/paths.h"
#include "core/simpro_memory.h"
#include "core/state.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <functional>
#include <mutex>
#include <thread>

#pragma comment(linker, \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
    "language='*'\"")

namespace {

using namespace simagicfix;

constexpr wchar_t kMainClass[] = L"SimagicFixMain";
constexpr wchar_t kTestClass[] = L"SimagicFixTest";
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_TASK_DONE = WM_APP + 2;
constexpr UINT WM_APP_SHOW = WM_APP + 3;

enum ControlId {
    IDC_TITLE = 100,
    IDC_STATUS_SIMPRO,
    IDC_STATUS_LMU,
    IDC_STATUS_BRIDGE,
    IDC_STATUS_MEMORY,
    IDC_AUTOSTART,
    IDC_PROGRESS,
    IDC_INSTALL,
    IDC_CANCEL,
    IDC_TEST,
    IDC_LOGS,
    IDC_QUIT,
    IDC_UNINSTALL,
    IDC_MEMORY_PATCH,
    IDC_MODE_LOW,
    IDC_MODE_HIGH,
    IDC_TEST_SOURCE,
    IDC_TEST_TC,
    IDC_TEST_TCCUT,
    IDC_TEST_ABS,
    IDC_TEST_MAP,
    IDC_TEST_SOC,
    IDC_TEST_FARB,
    IDC_TEST_RARB,
    IDC_TEST_DEMO,
    IDC_TEST_CLOSE,
};

enum class TaskKind { None, Install };

enum MenuId {
    IDM_OPEN = 200,
    IDM_TEST,
    IDM_AUTOSTART,
    IDM_LOGS,
    IDM_UNINSTALL,
    IDM_QUIT,
};

struct App {
    HINSTANCE instance = nullptr;
    HWND main = nullptr;
    HWND test = nullptr;
    HWND status_simpro = nullptr;
    HWND status_lmu = nullptr;
    HWND status_bridge = nullptr;
    HWND status_memory = nullptr;
    HWND autostart = nullptr;
    HWND progress = nullptr;
    HWND install = nullptr;
    HWND cancel = nullptr;
    HWND test_button = nullptr;
    HWND logs_button = nullptr;
    HWND quit_button = nullptr;
    HWND uninstall_button = nullptr;
    HWND memory_patch_checkbox = nullptr;
    HWND mode_low = nullptr;
    HWND mode_high = nullptr;

    HFONT font = nullptr;
    HFONT bold = nullptr;
    HFONT value_font = nullptr;

    AppState state;
    bool installed = false;
    bool tray_only = false;
    bool quitting = false;

    std::unique_ptr<BridgeService> bridge;
    std::unique_ptr<SimProMemoryPatcher> memory_patcher;
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

    std::chrono::steady_clock::time_point last_bridge_retry{};

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

void start_memory_patcher() {
    if (g_app.memory_patcher && g_app.memory_patcher->running()) {
        return;
    }
    const PatchMode mode = g_app.state.memory_patch_mode == 1
                               ? PatchMode::HighPerformance
                               : PatchMode::LowCpu;
    g_app.memory_patcher = std::make_unique<SimProMemoryPatcher>(
        []() {
            return g_app.bridge ? g_app.bridge->snapshot() : LmuSnapshot{};
        },
        mode);
    if (!g_app.memory_patcher->start()) {
        log_line("Memory patcher start failed");
    } else {
        log_line(std::string("Memory patcher started (") +
                 (mode == PatchMode::HighPerformance ? "high performance"
                                                     : "low performance") +
                 " mode)");
    }
}

void stop_memory_patcher() {
    if (g_app.memory_patcher) {
        g_app.memory_patcher->stop();
        g_app.memory_patcher.reset();
        log_line("Memory patcher stopped");
    }
}

void start_bridge() {
    if (g_app.bridge && g_app.bridge->running()) {
        return;
    }
    BridgeService::Options options;
    options.port = g_app.state.bridge_port > 0 ? g_app.state.bridge_port : 56799;
    options.simpro_port = simpro_rpc_port();
    options.mock = false;
    options.poll_ms = g_app.state.memory_patch_mode == 1 ? 2 : 20;
    g_app.bridge = std::make_unique<BridgeService>(options);
    if (!g_app.bridge->start()) {
        log_line("Bridge start failed: " + g_app.bridge->last_error());
    } else {
        log_line("Bridge started on port " + std::to_string(options.port));
        if (g_app.state.memory_patch) {
            start_memory_patcher();
        }
    }
}

void stop_bridge() {
    stop_memory_patcher();
    if (g_app.bridge) {
        g_app.bridge->stop();
        g_app.bridge.reset();
        log_line("Bridge stopped");
    }
}

void update_tray() {
    if (!g_app.tray_added) {
        return;
    }
    std::wstring tip = L"SimagicFix - ";
    if (g_app.bridge && g_app.bridge->running()) {
        tip += L"actif";
        if (!is_autostart_enabled()) {
            tip += L" (demarrage auto desactive)";
        }
    } else {
        tip += L"bridge arrete";
    }
    wcsncpy_s(g_app.nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_app.nid);
}

void add_tray(HWND hwnd) {
    if (g_app.tray_added) {
        return;
    }
    g_app.nid = {};
    g_app.nid.cbSize = sizeof(g_app.nid);
    g_app.nid.hWnd = hwnd;
    g_app.nid.uID = 1;
    g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_app.nid.uCallbackMessage = WM_APP_TRAY;
    g_app.tray_icon = static_cast<HICON>(
        LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_TRAY_ICON), IMAGE_ICON,
                   16, 16, LR_DEFAULTCOLOR));
    g_app.tray_icon_owned = g_app.tray_icon != nullptr;
    if (g_app.tray_icon == nullptr) {
        g_app.tray_icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    g_app.nid.hIcon = g_app.tray_icon;
    wcsncpy_s(g_app.nid.szTip, L"SimagicFix", _TRUNCATE);
    g_app.tray_added = Shell_NotifyIconW(NIM_ADD, &g_app.nid) != FALSE;
    update_tray();
}

void remove_tray() {
    if (g_app.tray_added) {
        Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
        g_app.tray_added = false;
    }
    if (g_app.tray_icon_owned && g_app.tray_icon != nullptr) {
        DestroyIcon(g_app.tray_icon);
    }
    g_app.tray_icon = nullptr;
    g_app.tray_icon_owned = false;
}

void refresh_status() {
    if (g_app.main == nullptr) {
        return;
    }
    const fs::path simpro = simpro_exe_path();
    const fs::path lmu = lmu_install_path();
    set_text(g_app.status_simpro,
             simpro.empty() ? "SimPro Manager : non detecte"
                            : "SimPro Manager : detecte");
    set_text(g_app.status_lmu,
             lmu.empty() ? "Le Mans Ultimate : non detecte"
                         : "Le Mans Ultimate : detecte");

    std::string bridge_text = "Bridge : ";
    if (g_app.bridge && g_app.bridge->running()) {
        bridge_text += "actif (port " + std::to_string(g_app.bridge->port()) + ")";
        if (g_app.bridge->lmu_available()) {
            bridge_text += " - LMU detecte";
        }
    } else {
        bridge_text += "inactif";
    }
    set_text(g_app.status_bridge, bridge_text);

    std::string memory_text = "Patch memoire : ";
    if (g_app.memory_patcher && g_app.memory_patcher->running()) {
        memory_text += g_app.memory_patcher->available()
                           ? "actif"
                           : "en attente (SimPro non detecte)";
    } else {
        memory_text += "arrete";
    }
    set_text(g_app.status_memory, memory_text);

    if (g_app.memory_patch_checkbox != nullptr && g_app.installed) {
        SendMessageW(g_app.memory_patch_checkbox, BM_SETCHECK,
                     g_app.state.memory_patch ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g_app.mode_low != nullptr && g_app.mode_high != nullptr) {
        const bool high = g_app.state.memory_patch_mode == 1;
        SendMessageW(g_app.mode_low, BM_SETCHECK,
                     high ? BST_UNCHECKED : BST_CHECKED, 0);
        SendMessageW(g_app.mode_high, BM_SETCHECK,
                     high ? BST_CHECKED : BST_UNCHECKED, 0);
        const BOOL enabled =
            (g_app.installed && g_app.state.memory_patch) ? TRUE : FALSE;
        EnableWindow(g_app.mode_low, enabled);
        EnableWindow(g_app.mode_high, enabled);
    }

    if (g_app.installed) {
        const bool autostart = is_autostart_enabled();
        SendMessageW(g_app.autostart, BM_SETCHECK,
                     autostart ? BST_CHECKED : BST_UNCHECKED, 0);
        if ((!g_app.bridge || !g_app.bridge->running()) && !g_app.task_running) {
            const auto now = std::chrono::steady_clock::now();
            if (now - g_app.last_bridge_retry > std::chrono::seconds(5)) {
                g_app.last_bridge_retry = now;
                start_bridge();
            }
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
    ShowWindow(g_app.test_button, show_manage);
    ShowWindow(g_app.logs_button, show_manage);
    ShowWindow(g_app.quit_button, show_manage);
    ShowWindow(g_app.uninstall_button, show_manage);
    ShowWindow(g_app.memory_patch_checkbox, show_manage);
    ShowWindow(g_app.mode_low, show_manage);
    ShowWindow(g_app.mode_high, show_manage);
    ShowWindow(g_app.status_memory, show_manage);
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
        MessageBoxW(g_app.main, L"Impossible d'ouvrir le dossier des logs.",
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

void update_test_window() {
    if (g_app.test == nullptr || !IsWindowVisible(g_app.test)) {
        return;
    }
    LmuSnapshot snapshot;
    bool valid = false;
    if (g_app.bridge && g_app.bridge->lmu_available()) {
        snapshot = g_app.bridge->snapshot();
        valid = snapshot.valid;
    }
    const bool demo = IsDlgButtonChecked(g_app.test, IDC_TEST_DEMO) == BST_CHECKED;
    if (!valid || demo) {
        const double seconds =
            static_cast<double>(GetTickCount64()) / 1000.0;
        snapshot.valid = true;
        snapshot.tc = 1 + static_cast<int>(seconds) % 12;
        snapshot.tc_max = 12;
        snapshot.tc_cut = 1 + static_cast<int>(seconds * 1.3) % 12;
        snapshot.tc_cut_max = 12;
        snapshot.abs = 1 + static_cast<int>(seconds * 0.7) % 12;
        snapshot.abs_max = 12;
        snapshot.motor_map = 1 + static_cast<int>(seconds * 0.4) % 10;
        snapshot.motor_map_max = 10;
        snapshot.soc = static_cast<float>(std::fmod(seconds * 7.0, 100.0) / 100.0);
        snapshot.battery_fraction = snapshot.soc;
        snapshot.vehicle_name = "Demo";
    }
    set_text(GetDlgItem(g_app.test, IDC_TEST_SOURCE),
             "Source : " + (valid && !demo ? snapshot.vehicle_name : std::string("Demo")));
    set_text(GetDlgItem(g_app.test, IDC_TEST_TC),
             "TC        " + std::to_string(snapshot.tc) + " / " +
                 std::to_string(snapshot.tc_max));
    set_text(GetDlgItem(g_app.test, IDC_TEST_TCCUT),
             "TC Cut    " + std::to_string(snapshot.tc_cut) + " / " +
                 std::to_string(snapshot.tc_cut_max));
    set_text(GetDlgItem(g_app.test, IDC_TEST_ABS),
             "ABS       " + std::to_string(snapshot.abs) + " / " +
                 std::to_string(snapshot.abs_max));
    set_text(GetDlgItem(g_app.test, IDC_TEST_MAP),
             "MAP       " + std::to_string(snapshot.motor_map) + " / " +
                 std::to_string(snapshot.motor_map_max));
    set_text(GetDlgItem(g_app.test, IDC_TEST_SOC),
             "SoC       " + std::to_string(static_cast<int>(snapshot.soc_percent())) +
                 " %");
    set_text(GetDlgItem(g_app.test, IDC_TEST_FARB),
             "ARB avant " + std::to_string(snapshot.front_arb) + " / " +
                 std::to_string(snapshot.front_arb_max));
    set_text(GetDlgItem(g_app.test, IDC_TEST_RARB),
             "ARB arriere " + std::to_string(snapshot.rear_arb) + " / " +
                 std::to_string(snapshot.rear_arb_max));
}

void show_test() {
    if (g_app.test == nullptr) {
        return;
    }
    ShowWindow(g_app.test, SW_SHOW);
    SetForegroundWindow(g_app.test);
}

LRESULT CALLBACK test_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            g_app.test = hwnd;
            HWND title = CreateWindowExW(0, L"STATIC", L"Test des valeurs",
                                         WS_CHILD | WS_VISIBLE, 20, 12, 360, 28, hwnd,
                                         nullptr, g_app.instance, nullptr);
            set_font(title, g_app.bold);
            const struct {
                int id;
                const wchar_t* text;
                int y;
            } labels[] = {
                {IDC_TEST_SOURCE, L"Source : -", 50},
                {IDC_TEST_TC, L"TC -", 78},
                {IDC_TEST_TCCUT, L"TC Cut -", 103},
                {IDC_TEST_ABS, L"ABS -", 128},
                {IDC_TEST_MAP, L"MAP -", 153},
                {IDC_TEST_SOC, L"SoC -", 178},
                {IDC_TEST_FARB, L"ARB avant -", 203},
                {IDC_TEST_RARB, L"ARB arriere -", 228},
            };
            for (const auto& label : labels) {
                HWND control = CreateWindowExW(
                    0, L"STATIC", label.text, WS_CHILD | WS_VISIBLE, 20, label.y,
                    360, 26, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(label.id)),
                    g_app.instance, nullptr);
                set_font(control, g_app.value_font);
            }
            HWND demo = CreateWindowExW(
                0, L"BUTTON", L"Mode demo", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                20, 266, 160, 24, hwnd, reinterpret_cast<HMENU>(IDC_TEST_DEMO),
                g_app.instance, nullptr);
            set_font(demo, g_app.font);
            HWND close = CreateWindowExW(
                0, L"BUTTON", L"Fermer", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 300,
                266, 100, 28, hwnd, reinterpret_cast<HMENU>(IDC_TEST_CLOSE),
                g_app.instance, nullptr);
            set_font(close, g_app.font);
            SetTimer(hwnd, 1, 100, nullptr);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_TIMER:
            update_test_window();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wparam) == IDC_TEST_CLOSE) {
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
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
            start_bridge();
            switch_mode(true);
            add_tray(g_app.main);
            update_tray();
            MessageBoxW(g_app.main,
                        L"SimagicFix installe.\nLe bridge est actif.",
                        L"SimagicFix", MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_app.main, w("Installation echouee : " + g_app.task_message).c_str(),
                        L"SimagicFix", MB_ICONERROR);
        }
    }

    g_app.task_kind = TaskKind::None;
}

void start_install() {
    if (g_app.task_running) {
        return;
    }
    InstallOptions options;
    options.auto_start =
        SendMessageW(g_app.autostart, BM_GETCHECK, 0, 0) == BST_CHECKED;

    log_line("Install requested");
    run_task(TaskKind::Install, [options]() {
        const InstallReport report = install(options);
        g_app.task_ok = report.ok;
        g_app.task_message = report.ok ? "" : report.error;
        log_line(report.ok ? "Install OK" : "Install failed: " + report.error);
    });
}

void do_uninstall() {
    if (g_app.task_running) {
        return;
    }
    const int answer = MessageBoxW(
        g_app.main,
        L"Desinstaller SimagicFix ?\n\n"
        L"- le bridge et le patch memoire seront arretes\n"
        L"- le demarrage automatique sera retire\n"
        L"- le raccourci du menu Demarrer sera supprime",
        L"SimagicFix", MB_YESNO | MB_ICONWARNING);
    if (answer != IDYES) {
        return;
    }
    stop_bridge();
    const bool ok = uninstall();
    remove_tray();
    log_line(ok ? "Uninstall completed" : "Uninstall completed with errors");
    std::error_code ec;
    const bool running_installed =
        fs::equivalent(current_exe_path(), installed_exe_path(), ec);
    if (running_installed) {
        schedule_delete_self();
    }
    MessageBoxW(g_app.main,
                L"SimagicFix a ete desinstalle.",
                L"SimagicFix",
                ok ? MB_ICONINFORMATION : MB_ICONWARNING);
    g_app.quitting = true;
    DestroyWindow(g_app.main);
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
        case IDM_TEST:
            show_test();
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
            g_app.quitting = true;
            DestroyWindow(g_app.main);
            break;
        default:
            break;
    }
}

void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_OPEN, L"Ouvrir SimagicFix");
    AppendMenuW(menu, MF_STRING, IDM_TEST, L"Tester les valeurs");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (is_autostart_enabled() ? MF_CHECKED : 0),
                IDM_AUTOSTART, L"Demarrage automatique");
    AppendMenuW(menu, MF_STRING, IDM_LOGS, L"Ouvrir les logs");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_UNINSTALL, L"Desinstaller");
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
            g_app.value_font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                           FALSE, DEFAULT_CHARSET,
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                           L"Consolas");

            HWND title = CreateWindowExW(0, L"STATIC", L"SimagicFix - telemetrie LMU",
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
                {IDC_STATUS_BRIDGE, L"Bridge : ...", 108},
                {IDC_STATUS_MEMORY, L"Patch memoire : ...", 134},
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
                } else if (status.id == IDC_STATUS_BRIDGE) {
                    g_app.status_bridge = control;
                } else {
                    g_app.status_memory = control;
                }
            }

            g_app.memory_patch_checkbox = CreateWindowExW(
                0, L"BUTTON",
                L"Patch memoire SimPro (experimental)",
                WS_CHILD | BS_AUTOCHECKBOX, 20, 170, 600, 26, hwnd,
                reinterpret_cast<HMENU>(IDC_MEMORY_PATCH), g_app.instance,
                nullptr);
            set_font(g_app.memory_patch_checkbox, g_app.font);

            g_app.mode_low = CreateWindowExW(
                0, L"BUTTON", L"Faible performance (1 coeur)",
                WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP, 40, 198, 580, 24, hwnd,
                reinterpret_cast<HMENU>(IDC_MODE_LOW), g_app.instance, nullptr);
            set_font(g_app.mode_low, g_app.font);
            g_app.mode_high = CreateWindowExW(
                0, L"BUTTON", L"Performance maximale (experimental)",
                WS_CHILD | BS_AUTORADIOBUTTON, 40, 224, 580, 24, hwnd,
                reinterpret_cast<HMENU>(IDC_MODE_HIGH), g_app.instance, nullptr);
            set_font(g_app.mode_high, g_app.font);

            g_app.autostart = CreateWindowExW(
                0, L"BUTTON",
                L"Lancer SimagicFix au demarrage de Windows",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 256, 600, 26, hwnd,
                reinterpret_cast<HMENU>(IDC_AUTOSTART), g_app.instance, nullptr);
            set_font(g_app.autostart, g_app.font);

            g_app.progress = CreateWindowExW(
                0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE, 20, 290, 600, 16,
                hwnd, reinterpret_cast<HMENU>(IDC_PROGRESS), g_app.instance, nullptr);

            g_app.install = CreateWindowExW(
                0, L"BUTTON", L"Installer SimagicFix",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 380, 320, 140, 34, hwnd,
                reinterpret_cast<HMENU>(IDC_INSTALL), g_app.instance, nullptr);
            set_font(g_app.install, g_app.font);
            g_app.cancel = CreateWindowExW(
                0, L"BUTTON", L"Annuler", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 530,
                320, 90, 34, hwnd, reinterpret_cast<HMENU>(IDC_CANCEL),
                g_app.instance, nullptr);
            set_font(g_app.cancel, g_app.font);

            g_app.test_button = CreateWindowExW(
                0, L"BUTTON", L"Tester", WS_CHILD | BS_PUSHBUTTON, 20,
                320, 110, 34, hwnd, reinterpret_cast<HMENU>(IDC_TEST),
                g_app.instance, nullptr);
            g_app.logs_button = CreateWindowExW(
                0, L"BUTTON", L"Ouvrir les logs", WS_CHILD | BS_PUSHBUTTON, 140, 320,
                150, 34, hwnd, reinterpret_cast<HMENU>(IDC_LOGS), g_app.instance,
                nullptr);
            g_app.uninstall_button = CreateWindowExW(
                0, L"BUTTON", L"Desinstaller", WS_CHILD | BS_PUSHBUTTON, 300, 320,
                140, 34, hwnd, reinterpret_cast<HMENU>(IDC_UNINSTALL),
                g_app.instance, nullptr);
            g_app.quit_button = CreateWindowExW(
                0, L"BUTTON", L"Quitter", WS_CHILD | BS_PUSHBUTTON, 450, 320, 110, 34,
                hwnd, reinterpret_cast<HMENU>(IDC_QUIT), g_app.instance, nullptr);
            set_font(g_app.test_button, g_app.font);
            set_font(g_app.logs_button, g_app.font);
            set_font(g_app.uninstall_button, g_app.font);
            set_font(g_app.quit_button, g_app.font);

            switch_mode(g_app.installed);
            SendMessageW(g_app.autostart, BM_SETCHECK,
                         is_autostart_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
            SetTimer(hwnd, 1, 1000, nullptr);

            if (g_app.installed) {
                start_bridge();
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
                        g_app.quitting = true;
                        DestroyWindow(hwnd);
                    }
                    break;
                case IDC_TEST:
                    show_test();
                    break;
                case IDC_LOGS:
                    open_logs();
                    break;
                case IDC_QUIT:
                    g_app.quitting = true;
                    DestroyWindow(hwnd);
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
                        stop_bridge();
                        start_bridge();
                        refresh_status();
                    }
                    break;
                case IDC_MODE_LOW:
                case IDC_MODE_HIGH:
                    if (g_app.installed) {
                        const int mode = id == IDC_MODE_HIGH ? 1 : 0;
                        if (g_app.state.memory_patch_mode != mode) {
                            g_app.state.memory_patch_mode = mode;
                            save_state(g_app.state);
                            stop_bridge();
                            start_bridge();
                        }
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
                MessageBoxW(hwnd, L"Une operation est en cours. Patiente quelques instants.",
                            L"SimagicFix", MB_ICONINFORMATION);
                return 0;
            }
            if (g_app.installed && !g_app.quitting) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            if (g_app.task_thread.joinable()) {
                g_app.task_thread.join();
            }
            stop_bridge();
            remove_tray();
            if (g_app.small_icon != nullptr) {
                DestroyIcon(g_app.small_icon);
                g_app.small_icon = nullptr;
            }
            if (g_app.test != nullptr) {
                DestroyWindow(g_app.test);
                g_app.test = nullptr;
            }
            if (g_app.font) {
                DeleteObject(g_app.font);
            }
            if (g_app.bold) {
                DeleteObject(g_app.bold);
            }
            if (g_app.value_font) {
                DeleteObject(g_app.value_font);
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    g_app.instance = instance;

    const wchar_t* command_line = GetCommandLineW();
    g_app.tray_only = wcsstr(command_line, L"--tray") != nullptr;

    // Single instance first: a second launch must not disturb the running app.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SimagicFixSingleton");
    if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            HWND existing = FindWindowW(kMainClass, nullptr);
            if (existing != nullptr) {
                PostMessageW(existing, WM_APP_SHOW, 0, 0);
                break;
            }
            Sleep(100);
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

    WNDCLASSEXW test_class{};
    test_class.cbSize = sizeof(test_class);
    test_class.lpfnWndProc = test_proc;
    test_class.hInstance = instance;
    test_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    test_class.hIcon = app_icon;
    test_class.hIconSm = g_app.small_icon;
    test_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    test_class.lpszClassName = kTestClass;
    RegisterClassExW(&test_class);

    HWND main_window = CreateWindowExW(
        0, kMainClass, L"SimagicFix",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
        CW_USEDEFAULT, 640, 460, nullptr, nullptr, instance, nullptr);
    if (main_window == nullptr) {
        return 1;
    }

    CreateWindowExW(WS_EX_TOOLWINDOW, kTestClass, L"SimagicFix - test",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                    CW_USEDEFAULT, 560, 380, main_window, nullptr, instance, nullptr);

    if (g_app.installed && g_app.tray_only) {
        ShowWindow(main_window, SW_HIDE);
    } else if (!g_app.installed || !g_app.tray_only) {
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
