#pragma once

#include "core/common.h"
#include "core/simpro_hook_shared.h"
#include "core/snapshot.h"

namespace simagicfix {

// Owns the shared memory and injects SimagicFixHook.dll into SimPro.
class SimProHook {
public:
    SimProHook();
    ~SimProHook();

    SimProHook(const SimProHook&) = delete;
    SimProHook& operator=(const SimProHook&) = delete;

    bool start(DWORD pid, const fs::path& dll_path,
               const std::atomic<bool>* cancel = nullptr);
    void update(const LmuSnapshot& snapshot);
    void set_active(bool active);
    void stop();

    bool installed() const;
    HookError last_error() const;
    std::string last_error_text() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Owns the SimPro PID discovery, injection and shared-memory updates.
class SimProHookService {
public:
    using SnapshotProvider = std::function<LmuSnapshot()>;

    explicit SimProHookService(SnapshotProvider provider);
    ~SimProHookService();

    SimProHookService(const SimProHookService&) = delete;
    SimProHookService& operator=(const SimProHookService&) = delete;

    bool start();
    void stop();
    bool running() const;
    bool installed() const;
    std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace simagicfix
