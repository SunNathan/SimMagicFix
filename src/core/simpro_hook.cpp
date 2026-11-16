#include "core/simpro_hook.h"

#include "core/paths.h"

#include <cmath>
#include <cwchar>

namespace simagicfix {

namespace {

DWORD find_simpro_pid() {
    DWORD pid = 0;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            wchar_t title[256] = {};
            GetWindowTextW(hwnd, title, 256);
            if (std::wcsstr(title, L"SimPro Manager") == nullptr) {
                return TRUE;
            }
            DWORD thread_pid = 0;
            GetWindowThreadProcessId(hwnd, &thread_pid);
            if (thread_pid != 0) {
                *reinterpret_cast<DWORD*>(param) = thread_pid;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&pid));
    return pid;
}

bool inject_library(DWORD pid, const fs::path& dll_path) {
    std::error_code ec;
    if (!fs::is_regular_file(dll_path, ec)) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD |
                                     PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                     PROCESS_VM_READ,
                                 FALSE, pid);
    if (process == nullptr) {
        return false;
    }

    const std::wstring path = dll_path.wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool injected = false;
    if (remote != nullptr) {
        if (WriteProcessMemory(process, remote, path.c_str(), bytes, nullptr)) {
            HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
            if (kernel != nullptr) {
                const auto load_library =
                    reinterpret_cast<LPTHREAD_START_ROUTINE>(
                        GetProcAddress(kernel, "LoadLibraryW"));
                if (load_library != nullptr) {
                    HANDLE thread = CreateRemoteThread(
                        process, nullptr, 0, load_library, remote, 0, nullptr);
                    if (thread != nullptr) {
                        WaitForSingleObject(thread, 5000);
                        DWORD exit_code = 0;
                        GetExitCodeThread(thread, &exit_code);
                        injected = exit_code != 0;
                        CloseHandle(thread);
                    }
                }
            }
        }
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    }
    CloseHandle(process);
    return injected;
}

}  // namespace

struct SimProHook::Impl {
    HANDLE mapping = nullptr;
    HANDLE ready_event = nullptr;
    HookSharedValues* view = nullptr;
    mutable std::mutex mutex;

    ~Impl() { stop(); }

    bool ensure_mapping() {
        std::lock_guard<std::mutex> lock(mutex);
        if (view == nullptr) {
            mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                         PAGE_READWRITE, 0,
                                         sizeof(HookSharedValues),
                                         kHookSharedMemoryName);
            if (mapping == nullptr) {
                return false;
            }
            view = static_cast<HookSharedValues*>(
                MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                              sizeof(HookSharedValues)));
            if (view == nullptr) {
                CloseHandle(mapping);
                mapping = nullptr;
                return false;
            }
            std::memset(static_cast<void*>(view), 0,
                        sizeof(HookSharedValues));
        }
        if (ready_event == nullptr) {
            ready_event = CreateEventW(nullptr, FALSE, FALSE,
                                       kHookReadyEventName);
            if (ready_event == nullptr) {
                return false;
            }
        }
        SetEvent(ready_event);
        return true;
    }

    void set_error(HookError error) {
        if (view != nullptr) {
            InterlockedExchange(&view->last_error,
                                static_cast<LONG>(error));
        }
    }

    bool start(DWORD pid, const fs::path& dll_path) {
        if (!ensure_mapping()) {
            return false;
        }
        InterlockedExchange(&view->active, 0);
        InterlockedExchange(&view->hook_installed, 0);
        InterlockedExchange(&view->last_error,
                            static_cast<LONG>(HookError::None));
        SetEvent(ready_event);

        if (!inject_library(pid, dll_path)) {
            set_error(HookError::InjectionFailed);
            return false;
        }

        InterlockedExchange(&view->active, 1);
        for (int attempt = 0; attempt < 60; ++attempt) {
            if (view->hook_installed != 0) {
                return true;
            }
            if (view->last_error != 0) {
                InterlockedExchange(&view->active, 0);
                return false;
            }
            Sleep(50);
        }
        set_error(HookError::Timeout);
        InterlockedExchange(&view->active, 0);
        return false;
    }

    void update(const LmuSnapshot& snapshot) {
        if (view == nullptr) {
            return;
        }
        InterlockedIncrement(&view->sequence);
        view->engine_map = snapshot.motor_map;
        view->abs = static_cast<float>(snapshot.abs);
        view->tc = static_cast<float>(snapshot.tc);
        view->tc_cut = static_cast<float>(snapshot.tc_cut);
        view->soc = snapshot.soc;
        view->soc_percent =
            static_cast<int>(std::lround(snapshot.soc_percent()));
        view->front_arb = snapshot.front_arb;
        view->rear_arb = snapshot.rear_arb;
        view->last_update_tick = GetTickCount64();
        InterlockedIncrement(&view->sequence);
    }

    void set_active(bool active) {
        if (view != nullptr) {
            InterlockedExchange(&view->active, active ? 1 : 0);
        }
    }

    void stop() {
        set_active(false);
        std::lock_guard<std::mutex> lock(mutex);
        if (view != nullptr) {
            UnmapViewOfFile(view);
            view = nullptr;
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
            mapping = nullptr;
        }
        if (ready_event != nullptr) {
            CloseHandle(ready_event);
            ready_event = nullptr;
        }
    }

    bool installed() const {
        return view != nullptr && view->hook_installed != 0;
    }

    HookError last_error() const {
        if (view == nullptr) {
            return HookError::SharedMemoryUnavailable;
        }
        return static_cast<HookError>(view->last_error);
    }
};

SimProHook::SimProHook() : impl_(std::make_unique<Impl>()) {}
SimProHook::~SimProHook() = default;

bool SimProHook::start(DWORD pid, const fs::path& dll_path) {
    return impl_->start(pid, dll_path);
}

void SimProHook::update(const LmuSnapshot& snapshot) {
    impl_->update(snapshot);
}

void SimProHook::set_active(bool active) { impl_->set_active(active); }

void SimProHook::stop() { impl_->stop(); }

bool SimProHook::installed() const { return impl_->installed(); }

HookError SimProHook::last_error() const { return impl_->last_error(); }

std::string SimProHook::last_error_text() const {
    switch (impl_->last_error()) {
        case HookError::None:
            return {};
        case HookError::VersionMismatch:
            return "SimPro binary is not the supported 2.1.8 build";
        case HookError::SharedMemoryUnavailable:
            return "Hook shared memory unavailable";
        case HookError::MinHookInitFailed:
            return "MinHook initialization failed";
        case HookError::CreateHookFailed:
            return "Cannot create the SimPro hook";
        case HookError::EnableHookFailed:
            return "Cannot enable the SimPro hook";
        case HookError::TargetNotExecutable:
            return "SimPro hook target is not executable";
        case HookError::InjectionFailed:
            return "Cannot inject SimagicFixHook.dll into SimPro";
        case HookError::Timeout:
            return "Timed out waiting for the SimPro hook";
    }
    return "Unknown hook error";
}

struct SimProHookService::Impl {
    SnapshotProvider provider;
    std::atomic<bool> running{false};
    std::thread thread;
    SimProHook hook;
    fs::path dll_path;
    DWORD current_pid = 0;
    bool unsupported_binary = false;
    std::chrono::steady_clock::time_point next_retry{};
    mutable std::mutex error_mutex;
    std::string error;

    explicit Impl(SnapshotProvider value)
        : provider(std::move(value)),
          dll_path(current_exe_path().parent_path() / L"SimagicFixHook.dll") {}
    ~Impl() { stop(); }

    void set_error(const std::string& value) {
        std::lock_guard<std::mutex> lock(error_mutex);
        error = value;
    }

    std::string get_error() const {
        std::lock_guard<std::mutex> lock(error_mutex);
        return error;
    }

    bool start() {
        if (running.load()) {
            return true;
        }
        running.store(true);
        thread = std::thread([this]() { loop(); });
        return true;
    }

    void stop() {
        if (!running.exchange(false)) {
            return;
        }
        if (thread.joinable()) {
            thread.join();
        }
        hook.set_active(false);
        hook.stop();
        current_pid = 0;
    }

    bool installed() const { return hook.installed(); }

    void loop() {
        while (running.load()) {
            const DWORD pid = find_simpro_pid();
            if (pid == 0) {
                if (current_pid != 0) {
                    hook.set_active(false);
                    hook.stop();
                    current_pid = 0;
                    unsupported_binary = false;
                }
                set_error("SimPro not running");
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            if (pid != current_pid) {
                hook.set_active(false);
                hook.stop();
                current_pid = pid;
                unsupported_binary = false;
                next_retry = std::chrono::steady_clock::now();
            }

            if (!hook.installed() && !unsupported_binary) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= next_retry) {
                    if (hook.start(pid, dll_path)) {
                        set_error("");
                    } else {
                        set_error(hook.last_error_text());
                        if (hook.last_error() == HookError::VersionMismatch) {
                            unsupported_binary = true;
                        }
                        next_retry = now + std::chrono::seconds(5);
                    }
                }
            }

            if (hook.installed()) {
                const LmuSnapshot snapshot =
                    provider ? provider() : LmuSnapshot{};
                hook.set_active(snapshot.valid);
                if (snapshot.valid) {
                    hook.update(snapshot);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};

SimProHookService::SimProHookService(SnapshotProvider provider)
    : impl_(std::make_unique<Impl>(std::move(provider))) {}
SimProHookService::~SimProHookService() = default;

bool SimProHookService::start() { return impl_->start(); }
void SimProHookService::stop() { impl_->stop(); }
bool SimProHookService::running() const { return impl_->running.load(); }
bool SimProHookService::installed() const { return impl_->installed(); }
std::string SimProHookService::last_error() const {
    return impl_->get_error();
}

}  // namespace simagicfix
