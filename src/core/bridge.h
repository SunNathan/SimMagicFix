#pragma once

#include "core/snapshot.h"

namespace simagicfix {

class BridgeService {
public:
    struct Options {
        int port = 56799;
        int fallback_port = 0;  // optional second listener (legacy dashboards)
        int simpro_port = 56789;
        int simpro_http_port = 56791;  // SimPro web UI origin
        bool mock = false;
    };

    explicit BridgeService(Options options);
    ~BridgeService();

    BridgeService(const BridgeService&) = delete;
    BridgeService& operator=(const BridgeService&) = delete;

    bool start();
    void stop();
    bool running() const;
    bool lmu_available() const;

    LmuSnapshot snapshot() const;
    std::string last_error() const;
    int port() const;
    std::vector<int> ports() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace simagicfix
