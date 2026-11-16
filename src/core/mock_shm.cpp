#include "core/mock_shm.h"

#include "core/lmu_types.h"

namespace simagicfix {

using namespace lmu;

struct MockShm::Impl {
    HANDLE handle = nullptr;
    SharedMemoryLayout* view = nullptr;
    int tick = 0;

    ~Impl() { close(); }

    void close() {
        if (view != nullptr) {
            UnmapViewOfFile(view);
            view = nullptr;
        }
        if (handle != nullptr) {
            CloseHandle(handle);
            handle = nullptr;
        }
    }

    bool create() {
        close();
        SetLastError(0);
        handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                    sizeof(SharedMemoryLayout), L"LMU_Data");
        if (handle == nullptr) {
            return false;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            close();
            return false;
        }
        view = static_cast<SharedMemoryLayout*>(
            MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryLayout)));
        if (view == nullptr) {
            close();
            return false;
        }
        std::memset(view, 0, sizeof(SharedMemoryLayout));

        auto& telemetry = view->data.telemetry;
        telemetry.activeVehicles = 1;
        telemetry.playerVehicleIdx = 0;
        telemetry.playerHasVehicle = true;

        TelemInfoV01& info = telemetry.telemInfo[0];
        strncpy_s(info.mVehicleName, sizeof(info.mVehicleName), "Mock Hypercar",
                  _TRUNCATE);
        info.mVehicleClass = IP_VehicleClass::Hypercar;
        info.mElapsedTime = 0.0;
        info.mTC = 5;
        info.mTCMax = 12;
        info.mTCCut = 3;
        info.mTCCutMax = 12;
        info.mTCSlip = 2;
        info.mTCSlipMax = 10;
        info.mABS = 4;
        info.mABSMax = 12;
        info.mMotorMap = 7;
        info.mMotorMapMax = 10;
        info.mMigration = 2;
        info.mMigrationMax = 6;
        info.mFrontAntiSway = 4;
        info.mFrontAntiSwayMax = 10;
        info.mRearAntiSway = 2;
        info.mRearAntiSwayMax = 10;
        info.mTCActive = false;
        info.mABSActive = false;
        info.mSoC = 0.64f;
        info.mVirtualEnergy = 1.5f;
        info.mRegen = 20.0f;
        info.mBatteryChargeFraction = 0.64;
        info.mElectricBoostMotorState = 2;
        info.mFuel = 30.0;
        info.mEngineOilTemp = 90.0;
        info.mEngineWaterTemp = 85.0;

        // Known values for the automated mock test.
        return true;
    }

    void update() {
        if (view == nullptr) {
            return;
        }
        ++tick;
        TelemInfoV01& info = view->data.telemetry.telemInfo[0];
        info.mElapsedTime = tick * 0.2;
        info.mTC = 1 + (tick % 12);
        info.mTCCut = 1 + (tick % 12);
        info.mABS = 1 + (tick % 12);
        info.mMotorMap = 1 + (tick % 10);
        info.mMigration = 1 + (tick % 6);
        info.mFrontAntiSway = 1 + (tick % 10);
        info.mRearAntiSway = 1 + (tick % 10);
        info.mSoC = static_cast<float>((tick * 7) % 100) / 100.0f;
        info.mBatteryChargeFraction = info.mSoC;
        info.mTCActive = (tick % 4) == 0;
        info.mABSActive = (tick % 5) == 0;
    }
};

MockShm::MockShm() : impl_(std::make_unique<Impl>()) {}
MockShm::~MockShm() = default;

bool MockShm::create() { return impl_->create(); }
void MockShm::close() { impl_->close(); }

void MockShm::update() { impl_->update(); }

void MockShm::run(int seconds) {
    if (!impl_->create()) {
        std::cerr << "Cannot create LMU_Data mock mapping. Is LMU already running?\n";
        return;
    }
    std::cout << "Mock LMU_Data created. Known values: TC=5, TC Cut=3, ABS=4, "
                 "MAP=7, SoC=64%.\n";
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(std::max(1, seconds));
    while (std::chrono::steady_clock::now() < deadline) {
        impl_->update();
        Sleep(200);
    }
    std::cout << "Mock LMU_Data closed.\n";
}

}  // namespace simagicfix
