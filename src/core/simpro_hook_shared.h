#pragma once

#include <windows.h>

#include <cstdint>

namespace simagicfix {

// Shared memory used by SimagicFix (writer) and SimagicFixHook.dll (reader).
constexpr wchar_t kHookSharedMemoryName[] = L"Local\\SimagicFixHook";
// Auto-reset event signaled when a new parent creates the shared memory.
constexpr wchar_t kHookReadyEventName[] = L"Local\\SimagicFixHookReady";

// SimPro 2.1.8 offsets. The hook is version-specific.
constexpr uintptr_t kSimProCopyFunctionRva = 0x469c90;
constexpr uintptr_t kSimProBuffer1Rva = 0x13215c0;
constexpr uintptr_t kSimProBuffer2Rva = 0x1322240;
constexpr uintptr_t kSimProBuffer3Rva = 0x1323638;

// Field offsets relative to the game_data sub-object base (rcx in the copy
// function).
constexpr uintptr_t kEngineMapOffset = 0x04;
constexpr uintptr_t kAbsLevelOffset = 0x70;
constexpr uintptr_t kTcLevelOffset = 0x74;
constexpr uintptr_t kTcCutOffset = 0x78;
constexpr uintptr_t kErsMaxOffset = 0xF4;
constexpr uintptr_t kErsStoredOffset = 0xF8;
constexpr uintptr_t kErsPercentOffset = 0xFC;
constexpr uintptr_t kFrontArbOffset = 0x20C;
constexpr uintptr_t kRearArbOffset = 0x210;

enum class HookError : LONG {
    None = 0,
    VersionMismatch = 1,
    SharedMemoryUnavailable = 2,
    MinHookInitFailed = 3,
    CreateHookFailed = 4,
    EnableHookFailed = 5,
    TargetNotExecutable = 6,
    InjectionFailed = 7,
    Timeout = 8,
};

struct HookSharedValues {
    // Seqlock: even = stable, odd = writer in progress.
    volatile LONG sequence;
    volatile LONG active;
    volatile LONG hook_installed;
    volatile LONG last_error;
    volatile ULONGLONG last_update_tick;
    volatile LONG heartbeat;

    int engine_map;
    float abs;
    float tc;
    float tc_cut;
    float soc;
    int soc_percent;
    int front_arb;
    int rear_arb;
};

}  // namespace simagicfix
