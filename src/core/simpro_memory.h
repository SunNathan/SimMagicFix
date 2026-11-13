#pragma once

#include "core/snapshot.h"

#include <functional>
#include <memory>
#include <string>

namespace simagicfix {

enum class PatchMode {
    LowCpu,          // One combined writer thread, 20 ms LMU polling.
    HighPerformance  // One writer thread per value group, 2 ms LMU polling.
};

// Experimental bridge between LMU telemetry and SimPro's in-memory game_data
// buffers. SimPro renders the wheel dashboard natively and does not use the
// dashboard HTTP endpoint, so the only way to feed TC/TC Cut/ABS/MAP/SoC/ARB
// into the native renderer is to update its game_data buffers directly.
class SimProMemoryPatcher {
public:
    using SnapshotProvider = std::function<LmuSnapshot()>;

    explicit SimProMemoryPatcher(
        SnapshotProvider provider,
        PatchMode mode = PatchMode::LowCpu);
    ~SimProMemoryPatcher();

    SimProMemoryPatcher(const SimProMemoryPatcher&) = delete;
    SimProMemoryPatcher& operator=(const SimProMemoryPatcher&) = delete;

    bool start();
    void stop();
    bool running() const;
    bool available() const;
    std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace simagicfix
