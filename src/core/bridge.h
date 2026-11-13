#pragma once

#include "core/snapshot.h"

namespace simagicfix {

class BridgeService {
public:
    struct Options {
        int port = 56799;
        int simpro_port = 56789;
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace simagicfix
