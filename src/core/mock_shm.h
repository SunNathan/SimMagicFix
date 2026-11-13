#pragma once

#include "core/common.h"

namespace simagicfix {

// Creates a fake "LMU_Data" shared memory segment with known values.
// Used to validate the reader/struct layout without launching the game.
class MockShm {
public:
    MockShm();
    ~MockShm();

    MockShm(const MockShm&) = delete;
    MockShm& operator=(const MockShm&) = delete;

    bool create();
    void close();
    void update();
    void run(int seconds);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace simagicfix
