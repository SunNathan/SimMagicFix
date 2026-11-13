#pragma once

// Clean-room ABI-compatible definitions for the Le Mans Ultimate shared memory
// interface. This header is used when the official SDK header is not available
// (for example on a GitHub Actions runner). It is not a copy of the official
// header: only the fields/offsets required by SimagicFix are declared here.
//
// The official SDK header remains the reference for local validation. Use
// -DLMU_SDK_DIR=<game>/Support/SharedMemoryInterface to build against it.

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#pragma pack(push, 4)

struct TelemVect3 {
    double x;
    double y;
    double z;
};

static_assert(sizeof(TelemVect3) == 24, "Unexpected TelemVect3 size");

struct TelemWheelV01 {
    // Opaque wheel block: SimagicFix only needs the stride.
    uint8_t _data[260];
};

static_assert(sizeof(TelemWheelV01) == 260, "Unexpected TelemWheelV01 size");

enum class IP_VehicleClass : uint8_t {
    Hypercar = 0x00,
    LMP2_ELMS = 0x02,
    LMP2,
    LMP3,
    GTE,
    GT3,
    PaceCar = 0x08,
    Unknown = 0xFF
};

enum class IP_VehicleChampionship : uint8_t {
    WEC_2023 = 0x00,
    WEC_2024,
    WEC_2025,
    WEC_2026,
    ELMS_2025 = 0x10,
    ELMS_2026,
    Unknown = 0xFF
};

struct TelemInfoV01 {
    // Time / identity
    int32_t mID;                 // 0
    double mDeltaTime;           // 4
    double mElapsedTime;         // 12
    int32_t mLapNumber;          // 20
    double mLapStartET;          // 24
    char mVehicleName[64];       // 32
    char mTrackName[64];         // 96
    uint8_t _pad0[204];          // 160..363

    // Engine temperatures (used by the probe)
    double mEngineWaterTemp;     // 364
    double mEngineOilTemp;       // 372
    uint8_t _pad1[144];          // 380..523

    // Fuel (used by the bridge when available)
    double mFuel;                // 524
    uint8_t _pad2[172];          // 532..703

    // Hybrid / battery
    double mBatteryChargeFraction;       // 704
    double mElectricBoostMotorTorque;    // 712
    double mElectricBoostMotorRPM;       // 720
    double mElectricBoostMotorTemperature; // 728
    double mElectricBoostWaterTemperature; // 736
    uint8_t mElectricBoostMotorState;    // 744
    uint8_t mLapInvalidated;             // 745
    uint8_t mABSActive;                  // 746
    uint8_t mTCActive;                   // 747
    uint8_t mSpeedLimiterActive;         // 748
    uint8_t mWiperState;                 // 749

    // Driving aids (the values SimagicFix maps to SimPro)
    uint8_t mTC;                 // 750
    uint8_t mTCMax;              // 751
    uint8_t mTCSlip;             // 752
    uint8_t mTCSlipMax;          // 753
    uint8_t mTCCut;              // 754
    uint8_t mTCCutMax;           // 755
    uint8_t mABS;                // 756
    uint8_t mABSMax;             // 757
    uint8_t mMotorMap;           // 758
    uint8_t mMotorMapMax;        // 759
    uint8_t mMigration;          // 760
    uint8_t mMigrationMax;       // 761
    uint8_t mFrontAntiSway;      // 762
    uint8_t mFrontAntiSwayMax;   // 763
    uint8_t mRearAntiSway;       // 764
    uint8_t mRearAntiSwayMax;    // 765
    uint8_t mLiftAndCoastProgress; // 766
    uint8_t mTrackLimitsSteps;   // 767

    // Hybrid / energy
    float mRegen;                // 768
    float mSoC;                  // 772
    float mVirtualEnergy;        // 776
    float mTimeGapCarAhead;      // 780
    float mTimeGapCarBehind;     // 784
    float mTimeGapPlaceAhead;    // 788
    float mTimeGapPlaceBehind;   // 792

    // Vehicle metadata
    char mVehicleModel[30];      // 796
    IP_VehicleClass mVehicleClass;             // 826
    IP_VehicleChampionship mVehicleChampionship; // 827
    uint8_t mExpansion[20];      // 828..847

    // The official struct stores four TelemWheelV01 values here. SimagicFix
    // does not read them, so an opaque block with the same stride is enough.
    TelemWheelV01 mWheel[4];     // 848..1887
};

static_assert(sizeof(TelemInfoV01) == 1888, "Unexpected TelemInfoV01 size");
static_assert(offsetof(TelemInfoV01, mElapsedTime) == 12, "Unexpected mElapsedTime offset");
static_assert(offsetof(TelemInfoV01, mVehicleName) == 32, "Unexpected mVehicleName offset");
static_assert(offsetof(TelemInfoV01, mEngineWaterTemp) == 364, "Unexpected mEngineWaterTemp offset");
static_assert(offsetof(TelemInfoV01, mEngineOilTemp) == 372, "Unexpected mEngineOilTemp offset");
static_assert(offsetof(TelemInfoV01, mFuel) == 524, "Unexpected mFuel offset");
static_assert(offsetof(TelemInfoV01, mBatteryChargeFraction) == 704, "Unexpected battery offset");
static_assert(offsetof(TelemInfoV01, mABSActive) == 746, "Unexpected mABSActive offset");
static_assert(offsetof(TelemInfoV01, mTCActive) == 747, "Unexpected mTCActive offset");
static_assert(offsetof(TelemInfoV01, mTC) == 750, "Unexpected mTC offset");
static_assert(offsetof(TelemInfoV01, mABS) == 756, "Unexpected mABS offset");
static_assert(offsetof(TelemInfoV01, mMotorMap) == 758, "Unexpected mMotorMap offset");
static_assert(offsetof(TelemInfoV01, mSoC) == 772, "Unexpected mSoC offset");
static_assert(offsetof(TelemInfoV01, mVirtualEnergy) == 776, "Unexpected mVirtualEnergy offset");
static_assert(offsetof(TelemInfoV01, mVehicleClass) == 826, "Unexpected mVehicleClass offset");
static_assert(offsetof(TelemInfoV01, mWheel) == 848, "Unexpected mWheel offset");

struct SharedMemoryTelemetryData {
    uint8_t activeVehicles;
    uint8_t playerVehicleIdx;
    uint8_t playerHasVehicle;
    uint8_t _pad0;
    TelemInfoV01 telemInfo[104];
};

static_assert(sizeof(SharedMemoryTelemetryData) == 196356,
              "Unexpected SharedMemoryTelemetryData size");

struct SharedMemoryObjectOut {
    // generic + paths + scoring, not needed by SimagicFix
    uint8_t _header[128464];
    SharedMemoryTelemetryData telemetry;
    uint8_t _tail[4];
};

static_assert(offsetof(SharedMemoryObjectOut, telemetry) == 128464,
              "Unexpected telemetry offset");
static_assert(sizeof(SharedMemoryObjectOut) == 324824,
              "Unexpected SharedMemoryObjectOut size");

struct SharedMemoryLayout {
    SharedMemoryObjectOut data;
};

static_assert(sizeof(SharedMemoryLayout) == 324824,
              "Unexpected SharedMemoryLayout size");

#pragma pack(pop)

// Minimal lock/waiter implementation compatible with the official SDK API.
class SharedMemoryLock {
public:
    static std::optional<SharedMemoryLock> MakeSharedMemoryLock() {
        SharedMemoryLock lock;
        if (lock.Init()) {
            return std::move(lock);
        }
        return std::nullopt;
    }

    bool Lock(DWORD timeout = INFINITE) {
        if (data_ == nullptr) {
            return false;
        }
        for (int spins = 0; spins < 4000; ++spins) {
            if (InterlockedCompareExchange(&data_->busy, 1, 0) == 0) {
                return true;
            }
            YieldProcessor();
        }
        InterlockedIncrement(&data_->waiters);
        bool acquired = false;
        while (true) {
            if (InterlockedCompareExchange(&data_->busy, 1, 0) == 0) {
                acquired = true;
                break;
            }
            if (event_ == nullptr ||
                WaitForSingleObject(event_, timeout) != WAIT_OBJECT_0) {
                break;
            }
        }
        InterlockedDecrement(&data_->waiters);
        return acquired;
    }

    void Unlock() {
        if (data_ == nullptr) {
            return;
        }
        InterlockedExchange(&data_->busy, 0);
        if (data_->waiters > 0 && event_ != nullptr) {
            SetEvent(event_);
        }
    }

    ~SharedMemoryLock() { Close(); }

    SharedMemoryLock(SharedMemoryLock&& other) noexcept
        : map_(std::exchange(other.map_, nullptr)),
          event_(std::exchange(other.event_, nullptr)),
          data_(std::exchange(other.data_, nullptr)) {}

    SharedMemoryLock& operator=(SharedMemoryLock&& other) noexcept {
        if (this != &other) {
            Close();
            map_ = std::exchange(other.map_, nullptr);
            event_ = std::exchange(other.event_, nullptr);
            data_ = std::exchange(other.data_, nullptr);
        }
        return *this;
    }

    SharedMemoryLock(const SharedMemoryLock&) = delete;
    SharedMemoryLock& operator=(const SharedMemoryLock&) = delete;

private:
    struct LockData {
        volatile LONG waiters;
        volatile LONG busy;
    };

    SharedMemoryLock() = default;

    bool Init() {
        map_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(LockData), "LMU_SharedMemoryLockData");
        if (map_ == nullptr) {
            return false;
        }
        data_ = static_cast<LockData*>(
            MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LockData)));
        if (data_ == nullptr) {
            Close();
            return false;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            data_->waiters = 0;
            data_->busy = 0;
        }
        event_ = CreateEventA(nullptr, FALSE, FALSE, "LMU_SharedMemoryLockEvent");
        if (event_ == nullptr) {
            Close();
            return false;
        }
        return true;
    }

    void Close() {
        if (event_ != nullptr) {
            CloseHandle(event_);
            event_ = nullptr;
        }
        if (data_ != nullptr) {
            UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (map_ != nullptr) {
            CloseHandle(map_);
            map_ = nullptr;
        }
    }

    HANDLE map_ = nullptr;
    HANDLE event_ = nullptr;
    LockData* data_ = nullptr;
};

class SharedMemoryUpdateWaiter {
public:
    static std::optional<SharedMemoryUpdateWaiter> MakeSharedMemoryUpdateWaiter() {
        SharedMemoryUpdateWaiter waiter;
        if (waiter.Init()) {
            return std::move(waiter);
        }
        return std::nullopt;
    }

    bool WaitForNextFrame(DWORD timeout = INFINITE) {
        if (hold_ == nullptr || data_ == nullptr) {
            return false;
        }
        if (WaitForSingleObject(hold_, timeout) != WAIT_OBJECT_0) {
            return false;
        }
        return WaitForSingleObject(data_, timeout) == WAIT_OBJECT_0;
    }

    ~SharedMemoryUpdateWaiter() { Close(); }

    SharedMemoryUpdateWaiter(SharedMemoryUpdateWaiter&& other) noexcept
        : hold_(std::exchange(other.hold_, nullptr)),
          data_(std::exchange(other.data_, nullptr)) {}

    SharedMemoryUpdateWaiter& operator=(SharedMemoryUpdateWaiter&& other) noexcept {
        if (this != &other) {
            Close();
            hold_ = std::exchange(other.hold_, nullptr);
            data_ = std::exchange(other.data_, nullptr);
        }
        return *this;
    }

    SharedMemoryUpdateWaiter(const SharedMemoryUpdateWaiter&) = delete;
    SharedMemoryUpdateWaiter& operator=(const SharedMemoryUpdateWaiter&) = delete;

private:
    SharedMemoryUpdateWaiter() = default;

    bool Init() {
        hold_ = OpenEventA(SYNCHRONIZE, FALSE, "LMU_Data_HoldEvent");
        if (hold_ == nullptr) {
            return false;
        }
        data_ = OpenEventA(SYNCHRONIZE, FALSE, "LMU_Data_DataEvent");
        if (data_ == nullptr) {
            Close();
            return false;
        }
        return true;
    }

    void Close() {
        if (hold_ != nullptr) {
            CloseHandle(hold_);
            hold_ = nullptr;
        }
        if (data_ != nullptr) {
            CloseHandle(data_);
            data_ = nullptr;
        }
    }

    HANDLE hold_ = nullptr;
    HANDLE data_ = nullptr;
};
