#pragma once

#include "core/snapshot.h"

#include <functional>
#include <memory>
#include <string>

namespace simagicfix {

// Experimental bridge between LMU telemetry and SimPro's in-memory game_data
// buffers. SimPro renders the wheel dashboard natively and does not use the
// dashboard HTTP endpoint, so the only way to feed TC/TC Cut/ABS/MAP/SoC/ARB
// into the native renderer is to update its game_data buffers directly.
class SimProMemoryPatcher {
public:
    using SnapshotProvider = std::function<LmuSnapshot()>;

    explicit SimProMemoryPatcher(SnapshotProvider provider);
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
