#pragma once

#include "core/snapshot.h"

#include <memory>
#include <string>

namespace simagicfix {

// Reads LMU shared memory and publishes the latest telemetry snapshot.
// No HTTP server or JSON-RPC proxy: the hook only needs the snapshot.
class TelemetryService {
public:
    struct Options {
        bool mock = false;
        int poll_ms = 20;
    };

    explicit TelemetryService(Options options);
    ~TelemetryService();

    TelemetryService(const TelemetryService&) = delete;
    TelemetryService& operator=(const TelemetryService&) = delete;

    bool start();
    void stop();
    bool running() const;
    bool lmu_available() const;
    LmuSnapshot snapshot() const;
    std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace simagicfix
