#include "core/simpro_hook_shared.h"

#include "MinHook.h"

#include <windows.h>

#include <atomic>
#include <cstring>

namespace {

using CopyFunction = void* (*)(void*, const void*);

std::atomic<simagicfix::HookSharedValues*> g_shared{nullptr};
CopyFunction g_original = nullptr;
uintptr_t g_buffer1 = 0;
uintptr_t g_buffer2 = 0;
uintptr_t g_buffer3 = 0;
HANDLE g_ready_event = nullptr;

void set_error(simagicfix::HookError error) {
    auto* shared = g_shared.load(std::memory_order_relaxed);
    if (shared != nullptr) {
        InterlockedExchange(&shared->last_error, static_cast<LONG>(error));
        InterlockedExchange(&shared->hook_installed, 0);
    }
}

bool is_executable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info,
                     sizeof(info)) == 0) {
        return false;
    }
    if (info.State != MEM_COMMIT) {
        return false;
    }
    const DWORD protect = info.Protect & 0xFF;
    return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_EXECUTE_WRITECOPY;
}

bool is_supported_binary(uintptr_t base) {
    // Expected prologue of the game_data copy function in SimPro 2.1.8:
    // 48 89 5C 24 08   mov [rsp+8], rbx
    // 57               push rdi
    // 48 83 EC 20      sub rsp, 20h
    // 8B 02            mov eax, [rdx]
    // 48 8B DA         mov rbx, rdx
    // 89 01            mov [rcx], eax
    static constexpr uint8_t kExpected[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC,
        0x20, 0x8B, 0x02, 0x48, 0x8B, 0xDA, 0x89, 0x01};
    const auto* code = reinterpret_cast<const uint8_t*>(
        base + simagicfix::kSimProCopyFunctionRva);
    if (std::memcmp(code, kExpected, sizeof(kExpected)) != 0) {
        return false;
    }

    const uintptr_t buffers[] = {simagicfix::kSimProBuffer1Rva,
                                 simagicfix::kSimProBuffer2Rva,
                                 simagicfix::kSimProBuffer3Rva};
    for (const uintptr_t rva : buffers) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(base + rva), &info,
                         sizeof(info)) == 0) {
            return false;
        }
        if (info.State != MEM_COMMIT) {
            return false;
        }
        const DWORD protect = info.Protect & 0xFF;
        if (protect != PAGE_READWRITE && protect != PAGE_WRITECOPY &&
            protect != PAGE_EXECUTE_READWRITE &&
            protect != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }
    }
    return true;
}

bool is_target_buffer(void* destination) {
    const auto address = reinterpret_cast<uintptr_t>(destination);
    return address == g_buffer1 || address == g_buffer2 ||
           address == g_buffer3;
}

void* __fastcall copy_detour(void* destination, const void* source) {
    void* result = g_original(destination, source);
    auto* shared = g_shared.load(std::memory_order_relaxed);
    if (shared == nullptr || destination == nullptr) {
        return result;
    }

    // Seqlock read: retry once if the writer was updating.
    LONG sequence = shared->sequence;
    if ((sequence & 1) != 0) {
        return result;
    }
    simagicfix::HookSharedValues values;
    std::memcpy(&values, static_cast<const void*>(shared), sizeof(values));
    if (sequence != shared->sequence) {
        return result;
    }

    if (!values.active || !values.hook_installed) {
        return result;
    }
    const ULONGLONG now = GetTickCount64();
    if (values.last_update_tick != 0 &&
        now - values.last_update_tick > 2000) {
        return result;
    }
    if (!is_target_buffer(destination)) {
        return result;
    }

    auto* base = static_cast<uint8_t*>(destination);
    *reinterpret_cast<int*>(base + simagicfix::kEngineMapOffset) =
        values.engine_map;
    *reinterpret_cast<float*>(base + simagicfix::kAbsLevelOffset) =
        values.abs;
    *reinterpret_cast<float*>(base + simagicfix::kTcLevelOffset) =
        values.tc;
    *reinterpret_cast<float*>(base + simagicfix::kTcCutOffset) =
        values.tc_cut;
    *reinterpret_cast<float*>(base + simagicfix::kErsMaxOffset) = 1.0f;
    *reinterpret_cast<float*>(base + simagicfix::kErsStoredOffset) =
        values.soc;
    *reinterpret_cast<int*>(base + simagicfix::kErsPercentOffset) =
        values.soc_percent;
    *reinterpret_cast<int*>(base + simagicfix::kFrontArbOffset) =
        values.front_arb;
    *reinterpret_cast<int*>(base + simagicfix::kRearArbOffset) =
        values.rear_arb;
    return result;
}

simagicfix::HookSharedValues* open_shared_once() {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE,
                                      simagicfix::kHookSharedMemoryName);
    if (mapping == nullptr) {
        return nullptr;
    }
    auto* view = static_cast<simagicfix::HookSharedValues*>(
        MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                      sizeof(simagicfix::HookSharedValues)));
    CloseHandle(mapping);
    return view;
}

simagicfix::HookSharedValues* open_shared() {
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (auto* view = open_shared_once()) {
            return view;
        }
        Sleep(100);
    }
    return nullptr;
}

HANDLE open_ready_event() {
    return OpenEventW(SYNCHRONIZE, FALSE,
                      simagicfix::kHookReadyEventName);
}

DWORD WINAPI hook_thread(LPVOID) {
    auto* shared = open_shared();
    g_shared.store(shared, std::memory_order_relaxed);
    if (shared == nullptr) {
        return 0;
    }

    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        set_error(simagicfix::HookError::TargetNotExecutable);
        return 0;
    }
    const auto base = reinterpret_cast<uintptr_t>(module);
    g_buffer1 = base + simagicfix::kSimProBuffer1Rva;
    g_buffer2 = base + simagicfix::kSimProBuffer2Rva;
    g_buffer3 = base + simagicfix::kSimProBuffer3Rva;

    if (!is_supported_binary(base)) {
        set_error(simagicfix::HookError::VersionMismatch);
        return 0;
    }

    const auto target =
        reinterpret_cast<LPVOID>(base + simagicfix::kSimProCopyFunctionRva);
    if (!is_executable(reinterpret_cast<uintptr_t>(target))) {
        set_error(simagicfix::HookError::TargetNotExecutable);
        return 0;
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        set_error(simagicfix::HookError::MinHookInitFailed);
        return 0;
    }

    status = MH_CreateHook(target, reinterpret_cast<LPVOID>(&copy_detour),
                           reinterpret_cast<LPVOID*>(&g_original));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        set_error(simagicfix::HookError::CreateHookFailed);
        return 0;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK && status != MH_ERROR_ENABLED) {
        set_error(simagicfix::HookError::EnableHookFailed);
        return 0;
    }

    InterlockedExchange(&shared->last_error,
                        static_cast<LONG>(simagicfix::HookError::None));
    InterlockedExchange(&shared->hook_installed, 1);

    // The parent may recreate or zero the shared mapping when it restarts.
    // Re-open the named mapping and re-announce the installed hook so a new
    // parent does not time out.
    g_ready_event = open_ready_event();
    int tick = 0;
    for (;;) {
        if (g_ready_event == nullptr) {
            g_ready_event = open_ready_event();
        }
        DWORD wait = WAIT_TIMEOUT;
        if (g_ready_event != nullptr) {
            wait = WaitForSingleObject(g_ready_event, 100);
        } else {
            Sleep(100);
        }

        auto* current = g_shared.load(std::memory_order_relaxed);
        if (wait == WAIT_OBJECT_0 || current == nullptr ||
            current->hook_installed == 0) {
            if (auto* fresh = open_shared_once()) {
                g_shared.store(fresh, std::memory_order_relaxed);
                InterlockedExchange(
                    &fresh->last_error,
                    static_cast<LONG>(simagicfix::HookError::None));
                InterlockedExchange(&fresh->hook_installed, 1);
            }
        }

        // Detect a new parent event object created after a full restart.
        if (++tick >= 10) {
            tick = 0;
            if (HANDLE fresh_event = open_ready_event()) {
                if (g_ready_event != nullptr) {
                    CloseHandle(g_ready_event);
                }
                g_ready_event = fresh_event;
            }
        }
    }
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, hook_thread, nullptr, 0,
                                     nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
