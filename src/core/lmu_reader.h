#pragma once

#include "core/snapshot.h"

namespace simagicfix {

// Read-only access to LMU's native shared memory ("LMU_Data").
// The implementation uses the official SDK header and its lock/waiter helpers.
class LmuReader {
public:
    LmuReader();
    ~LmuReader();

    LmuReader(const LmuReader&) = delete;
    LmuReader& operator=(const LmuReader&) = delete;

    bool open();
    void close();
    bool is_open() const;

    // Reads the player's telemetry. Returns false when the game is not in a
    // session or the mapping is no longer available.
    bool read(LmuSnapshot& snapshot);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace simagicfix
