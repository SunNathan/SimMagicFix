// Optional local ABI check against the official Le Mans Ultimate SDK header.
// Build only when LMU_SDK_DIR points to the game's SharedMemoryInterface
// directory. This translation unit does not include SimagicFix's own types, so
// the two definitions never collide.

#include <cstddef>
#include <iostream>

#include "SharedMemoryInterface.hpp"

int main() {
    std::cout << "Official LMU SDK layout\n";
    std::cout << "  sizeof(TelemVect3)                = "
              << sizeof(TelemVect3) << "\n";
    std::cout << "  sizeof(TelemWheelV01)             = "
              << sizeof(TelemWheelV01) << "\n";
    std::cout << "  sizeof(TelemInfoV01)              = "
              << sizeof(TelemInfoV01) << "\n";
    std::cout << "  offsetof(TelemInfoV01, mTC)       = "
              << offsetof(TelemInfoV01, mTC) << "\n";
    std::cout << "  offsetof(TelemInfoV01, mABS)      = "
              << offsetof(TelemInfoV01, mABS) << "\n";
    std::cout << "  offsetof(TelemInfoV01, mMotorMap) = "
              << offsetof(TelemInfoV01, mMotorMap) << "\n";
    std::cout << "  offsetof(TelemInfoV01, mSoC)      = "
              << offsetof(TelemInfoV01, mSoC) << "\n";
    std::cout << "  sizeof(SharedMemoryTelemetryData) = "
              << sizeof(SharedMemoryTelemetryData) << "\n";
    std::cout << "  sizeof(SharedMemoryObjectOut)     = "
              << sizeof(SharedMemoryObjectOut) << "\n";
    std::cout << "  offsetof(SharedMemoryObjectOut, telemetry) = "
              << offsetof(SharedMemoryObjectOut, telemetry) << "\n";
    std::cout << "  sizeof(SharedMemoryLayout)        = "
              << sizeof(SharedMemoryLayout) << "\n";
    return 0;
}
