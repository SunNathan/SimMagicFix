#include "core/simpro_memory.h"

#include <windows.h>
#include <timeapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace simagicfix {

namespace {

constexpr uintptr_t kMaxAddress = 0x7fffffffffffULL;
constexpr size_t kMaxRegionSize = 64ULL * 1024ULL * 1024ULL;
constexpr size_t kMaxTotalScan = 4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr auto kScanInterval = std::chrono::minutes(5);

using Pattern = std::array<uint8_t, 12>;

Pattern make_pattern() {
    const float values[3] = {7.0f, 8200.0f, 110.0f};
    Pattern pattern{};
    std::memcpy(pattern.data(), values, sizeof(values));
    return pattern;
}

bool is_writable(DWORD protect) {
    return protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
           protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_EXECUTE_WRITECOPY;
}

bool read_bytes(HANDLE process, uintptr_t address, void* buffer, size_t size) {
    SIZE_T read = 0;
    return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer,
                             size, &read) &&
           read == size;
}

bool read_float(HANDLE process, uintptr_t address, float& value) {
    return read_bytes(process, address, &value, sizeof(value));
}

bool write_float(HANDLE process, uintptr_t address, float value) {
    SIZE_T written = 0;
    return WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), &value,
                              sizeof(value), &written) &&
           written == sizeof(value);
}

bool write_int(HANDLE process, uintptr_t address, int value) {
    SIZE_T written = 0;
    return WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), &value,
                              sizeof(value), &written) &&
           written == sizeof(value);
}

bool write_bytes(HANDLE process, uintptr_t address, const void* data,
                 size_t size) {
    SIZE_T written = 0;
    return WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), data,
                              size, &written) &&
           written == size;
}

struct FindContext {
    DWORD pid = 0;
};

DWORD find_simpro_pid() {
    FindContext context;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* context = reinterpret_cast<FindContext*>(param);
            wchar_t title[256] = {};
            GetWindowTextW(hwnd, title, 256);
            if (wcsstr(title, L"SimPro Manager") == nullptr) {
                return TRUE;
            }
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != 0) {
                context->pid = pid;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
    return context.pid;
}

}  // namespace

struct SimProMemoryPatcher::Impl {
    enum class Layout { None, Heap, Image };

    struct Target {
        uintptr_t max_gears = 0;
        Layout layout = Layout::None;
    };

    SnapshotProvider provider;
    PatchMode mode;
    std::atomic<bool> running{false};
    std::atomic<bool> available{false};
    std::thread thread;
    std::thread write_thread;
    std::thread aid_thread;
    std::thread ers_thread;
    std::thread arb_thread;
    std::thread engine_map_thread;
    std::atomic<uintptr_t> primary_image{0};
    std::atomic<DWORD> simpro_pid{0};
    std::atomic<int> engine_map_value{0};
    std::atomic<float> abs_value{0.0f};
    std::atomic<float> tc_value{0.0f};
    std::atomic<float> tc_cut_value{0.0f};
    std::atomic<float> soc_value{0.0f};
    std::atomic<int> soc_percent_value{0};
    std::atomic<int> front_arb_value{0};
    std::atomic<int> rear_arb_value{0};
    std::atomic<bool> values_active{false};
    DWORD original_priority_class = 0;
    mutable std::mutex error_mutex;
    std::string error;
    HANDLE process = nullptr;
    DWORD pid = 0;
    std::vector<Target> targets;
    Pattern pattern = make_pattern();

    Impl(SnapshotProvider value, PatchMode patch_mode)
        : provider(std::move(value)), mode(patch_mode) {}
    ~Impl() { stop(); }

    void set_error(const std::string& value) {
        std::lock_guard<std::mutex> lock(error_mutex);
        error = value;
    }

    std::string get_error() const {
        std::lock_guard<std::mutex> lock(error_mutex);
        return error;
    }

    void close_process() {
        if (process != nullptr) {
            CloseHandle(process);
            process = nullptr;
        }
        pid = 0;
        simpro_pid.store(0);
        primary_image.store(0);
        values_active.store(false);
        targets.clear();
        available.store(false);
    }

    bool open_process(DWORD new_pid) {
        close_process();
        pid = new_pid;
        simpro_pid.store(new_pid);
        process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                                  PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                              FALSE, pid);
        if (process == nullptr) {
            set_error("Cannot open SimPro process");
            return false;
        }
        return true;
    }

    Layout detect_layout(uintptr_t hit, bool module) {
        float fuel = 0.0f;
        float fuel_percent = 0.0f;
        float turbo = 0.0f;
        float brake_bias = 0.0f;

        if (module) {
            // Image/module layout: the one used by SimPro's native wheel
            // renderer. fuel +0xb8, fuelPercent +0xbc, turbo +0xc0,
            // brakeBias +0xe8.
            if (read_float(process, hit + 0xb8, fuel) &&
                read_float(process, hit + 0xbc, fuel_percent) &&
                read_float(process, hit + 0xc0, turbo) &&
                read_float(process, hit + 0xe8, brake_bias) &&
                fuel >= 0.0f && fuel <= 250.0f && fuel_percent >= 0.0f &&
                fuel_percent <= 100.0f && turbo > 1000.0f &&
                turbo <= 300000.0f && brake_bias >= 0.0f &&
                brake_bias <= 100.0f) {
                return Layout::Image;
            }
            return Layout::None;
        }

        // Heap layout: the dashboard editor telemetry buffer.
        // fuel +0x80, fuelPercent +0x84, turbo +0x88, brakeBias +0xd0.
        if (read_float(process, hit + 0x80, fuel) &&
            read_float(process, hit + 0x84, fuel_percent) &&
            read_float(process, hit + 0x88, turbo) &&
            read_float(process, hit + 0xd0, brake_bias) &&
            fuel >= 0.0f && fuel <= 250.0f && fuel_percent >= 0.0f &&
            fuel_percent <= 100.0f && turbo > 1000.0f &&
            turbo <= 300000.0f && brake_bias >= 0.0f &&
            brake_bias <= 100.0f) {
            return Layout::Heap;
        }

        return Layout::None;
    }

    void scan() {
        if (process == nullptr) {
            return;
        }

        std::vector<Target> found;
        size_t scanned = 0;
        uintptr_t address = 0;
        while (address < kMaxAddress && scanned < kMaxTotalScan &&
               running.load()) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi,
                               sizeof(mbi)) == 0) {
                break;
            }
            const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const size_t size = mbi.RegionSize;
            if (mbi.State == MEM_COMMIT && is_writable(mbi.Protect) && size > 0 &&
                size <= kMaxRegionSize) {
                std::vector<uint8_t> buffer(size);
                if (read_bytes(process, base, buffer.data(), size)) {
                    scanned += size;
                    for (size_t i = 0; i + pattern.size() <= size; ++i) {
                        if (std::memcmp(buffer.data() + i, pattern.data(),
                                        pattern.size()) == 0) {
                            const uintptr_t hit = base + i;
                            const Layout layout =
                                detect_layout(hit, mbi.Type == MEM_IMAGE);
                            if (layout != Layout::None) {
                                found.push_back(Target{hit, layout});
                            }
                        }
                    }
                }
            }
            if (size == 0) {
                break;
            }
            address = base + size;
        }

        if (!found.empty()) {
            uintptr_t primary = 0;
            for (const auto& target : found) {
                if (target.layout == Layout::Image) {
                    primary = target.max_gears;
                    break;
                }
            }
            primary_image.store(primary);
            targets = std::move(found);
            available.store(true);
            set_error("");
        } else if (targets.empty()) {
            available.store(false);
            set_error("SimPro game_data buffers not found");
        } else {
            // Keep the previous targets: a transient read failure must not
            // interrupt the live patch.
            set_error("");
        }
    }

    void loop() {
        timeBeginPeriod(1);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        auto next_scan = std::chrono::steady_clock::now();
        auto next_snapshot = std::chrono::steady_clock::now();
        const auto snapshot_interval =
            mode == PatchMode::HighPerformance
                ? std::chrono::milliseconds(1)
                : std::chrono::milliseconds(5);
        LmuSnapshot snapshot;

        while (running.load()) {
            const auto now = std::chrono::steady_clock::now();

            if (process != nullptr &&
                WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
                close_process();
                next_scan = now;
            }

            if (process == nullptr || now >= next_scan) {
                const DWORD new_pid = find_simpro_pid();
                if (new_pid == 0) {
                    close_process();
                    set_error("SimPro not running");
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }
                if (process == nullptr || new_pid != pid) {
                    if (!open_process(new_pid)) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(500));
                        continue;
                    }
                }
                try {
                    scan();
                } catch (...) {
                    targets.clear();
                    available.store(false);
                    set_error("SimPro memory scan failed");
                }
                next_scan = now + kScanInterval;
            }

            if (now >= next_snapshot) {
                snapshot = provider ? provider() : LmuSnapshot{};
                engine_map_value.store(snapshot.motor_map);
                abs_value.store(static_cast<float>(snapshot.abs));
                tc_value.store(static_cast<float>(snapshot.tc));
                tc_cut_value.store(static_cast<float>(snapshot.tc_cut));
                soc_value.store(snapshot.soc);
                soc_percent_value.store(
                    static_cast<int>(std::lround(snapshot.soc_percent())));
                front_arb_value.store(snapshot.front_arb);
                rear_arb_value.store(snapshot.rear_arb);
                values_active.store(snapshot.valid);
                next_snapshot = now + snapshot_interval;
            }

            if (process != nullptr && !targets.empty() && values_active.load()) {
                // The dedicated write thread owns the actual memory writes.
                // Keep this loop light so it does not steal CPU from it.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                // Do not burn a core while LMU is not in a live session.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        timeEndPeriod(1);
    }

    enum class BlockKind { Aid, Ers, Arb, EngineMap };

    void write_block(HANDLE handle, uintptr_t target, BlockKind kind) {
        switch (kind) {
            case BlockKind::Aid: {
                struct AidBlock {
                    float abs_level;
                    float tc_level;
                    float tc_cut;
                } aid{abs_value.load(), tc_value.load(), tc_cut_value.load()};
                write_bytes(handle, target + 0x68, &aid, sizeof(aid));
                break;
            }
            case BlockKind::Ers: {
                struct ErsBlock {
                    float ers_max;
                    float ers_stored;
                    int ers_percent;
                } ers{1.0f, soc_value.load(), soc_percent_value.load()};
                write_bytes(handle, target + 0xec, &ers, sizeof(ers));
                break;
            }
            case BlockKind::Arb: {
                struct ArbBlock {
                    int front;
                    int rear;
                } arb{front_arb_value.load(), rear_arb_value.load()};
                write_bytes(handle, target + 0x204, &arb, sizeof(arb));
                break;
            }
            case BlockKind::EngineMap:
                write_int(handle, target - 0x04, engine_map_value.load());
                break;
        }
    }

    void block_loop(BlockKind kind) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        HANDLE handle = nullptr;
        DWORD current_pid = 0;
        while (running.load()) {
            const uintptr_t target = primary_image.load();
            const DWORD target_pid = simpro_pid.load();
            if (target == 0 || target_pid == 0 || !values_active.load()) {
                if (handle != nullptr) {
                    CloseHandle(handle);
                    handle = nullptr;
                    current_pid = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (handle == nullptr || current_pid != target_pid) {
                if (handle != nullptr) {
                    CloseHandle(handle);
                }
                handle = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                                     FALSE, target_pid);
                current_pid = target_pid;
                if (handle == nullptr) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(50));
                    continue;
                }
            }
            write_block(handle, target, kind);
        }
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }

    void write_loop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        HANDLE handle = nullptr;
        DWORD current_pid = 0;
        while (running.load()) {
            const uintptr_t target = primary_image.load();
            const DWORD target_pid = simpro_pid.load();
            if (target == 0 || target_pid == 0 || !values_active.load()) {
                if (handle != nullptr) {
                    CloseHandle(handle);
                    handle = nullptr;
                    current_pid = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (handle == nullptr || current_pid != target_pid) {
                if (handle != nullptr) {
                    CloseHandle(handle);
                }
                handle = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                                     FALSE, target_pid);
                current_pid = target_pid;
                if (handle == nullptr) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(50));
                    continue;
                }
            }

            struct AidBlock {
                float abs_level;
                float tc_level;
                float tc_cut;
            } aid{abs_value.load(), tc_value.load(), tc_cut_value.load()};

            struct ErsBlock {
                float ers_max;
                float ers_stored;
                int ers_percent;
            } ers{1.0f, soc_value.load(), soc_percent_value.load()};

            struct ArbBlock {
                int front;
                int rear;
            } arb{front_arb_value.load(), rear_arb_value.load()};

            // Native wheel renderer layout. The first image target is the one
            // SimPro's RPC and native renderer read. Writing it in a tight
            // loop keeps every value ahead of SimPro's per-frame refresh.
            write_int(handle, target - 0x04, engine_map_value.load());
            write_bytes(handle, target + 0x204, &arb, sizeof(arb));
            write_bytes(handle, target + 0xec, &ers, sizeof(ers));
            // ABS/TC/TC Cut last so the TC is the freshest value.
            write_bytes(handle, target + 0x68, &aid, sizeof(aid));
        }
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }

    bool start() {
        if (running.load()) {
            return true;
        }
        running.store(true);
        thread = std::thread([this]() { loop(); });
        if (mode == PatchMode::HighPerformance) {
            original_priority_class = GetPriorityClass(GetCurrentProcess());
            SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
            aid_thread = std::thread([this]() { block_loop(BlockKind::Aid); });
            ers_thread = std::thread([this]() { block_loop(BlockKind::Ers); });
            arb_thread = std::thread([this]() { block_loop(BlockKind::Arb); });
            engine_map_thread =
                std::thread([this]() { block_loop(BlockKind::EngineMap); });
        } else {
            write_thread = std::thread([this]() { write_loop(); });
        }
        return true;
    }

    void stop() {
        if (!running.exchange(false)) {
            return;
        }
        if (thread.joinable()) {
            thread.join();
        }
        if (write_thread.joinable()) {
            write_thread.join();
        }
        if (aid_thread.joinable()) {
            aid_thread.join();
        }
        if (ers_thread.joinable()) {
            ers_thread.join();
        }
        if (arb_thread.joinable()) {
            arb_thread.join();
        }
        if (engine_map_thread.joinable()) {
            engine_map_thread.join();
        }
        if (original_priority_class != 0) {
            SetPriorityClass(GetCurrentProcess(), original_priority_class);
            original_priority_class = 0;
        }
        close_process();
    }
};

SimProMemoryPatcher::SimProMemoryPatcher(SnapshotProvider provider,
                                         PatchMode mode)
    : impl_(std::make_unique<Impl>(std::move(provider), mode)) {}
SimProMemoryPatcher::~SimProMemoryPatcher() = default;

bool SimProMemoryPatcher::start() { return impl_->start(); }
void SimProMemoryPatcher::stop() { impl_->stop(); }
bool SimProMemoryPatcher::running() const { return impl_->running.load(); }
bool SimProMemoryPatcher::available() const { return impl_->available.load(); }
std::string SimProMemoryPatcher::last_error() const {
    return impl_->get_error();
}

}  // namespace simagicfix
