#include "core/lmu_reader.h"

#include "core/lmu_sdk.h"

namespace simagicfix {

struct LmuReader::Impl {
    HANDLE map_handle = nullptr;
    const SharedMemoryLayout* view = nullptr;
    std::optional<SharedMemoryLock> lock;
    std::optional<SharedMemoryUpdateWaiter> waiter;

    ~Impl() { close(); }

    void close() {
        waiter.reset();
        lock.reset();
        if (view != nullptr) {
            UnmapViewOfFile(view);
            view = nullptr;
        }
        if (map_handle != nullptr) {
            CloseHandle(map_handle);
            map_handle = nullptr;
        }
    }

    bool open() {
        close();
        map_handle = OpenFileMappingW(FILE_MAP_READ, FALSE, L"LMU_Data");
        if (map_handle == nullptr) {
            return false;
        }
        view = static_cast<const SharedMemoryLayout*>(
            MapViewOfFile(map_handle, FILE_MAP_READ, 0, 0, sizeof(SharedMemoryLayout)));
        if (view == nullptr) {
            close();
            return false;
        }
        lock = SharedMemoryLock::MakeSharedMemoryLock();
        waiter = SharedMemoryUpdateWaiter::MakeSharedMemoryUpdateWaiter();
        return true;
    }
};

LmuReader::LmuReader() : impl_(std::make_unique<Impl>()) {}
LmuReader::~LmuReader() = default;

bool LmuReader::open() { return impl_->open(); }
void LmuReader::close() { impl_->close(); }
bool LmuReader::is_open() const { return impl_->view != nullptr; }

bool LmuReader::read(LmuSnapshot& snapshot) {
    snapshot = LmuSnapshot{};
    if (impl_->view == nullptr) {
        return false;
    }

    // Sync with the game's update gate when available. A timeout is normal
    // when the game is paused or not in realtime.
    if (impl_->waiter.has_value()) {
        impl_->waiter->WaitForNextFrame(20);
    }

    bool locked = false;
    if (impl_->lock.has_value()) {
        locked = impl_->lock->Lock(50);
        if (!locked) {
            // Another consumer is stuck; skip this sample instead of blocking
            // the bridge (and the whole application) forever.
            return false;
        }
    }

    const SharedMemoryTelemetryData& telemetry = impl_->view->data.telemetry;
    const bool valid = telemetry.playerHasVehicle &&
                       telemetry.activeVehicles != 0 &&
                       telemetry.playerVehicleIdx < 104 &&
                       telemetry.activeVehicles <= 104;
    const uint8_t active_vehicles = telemetry.activeVehicles;
    const uint8_t player_index = telemetry.playerVehicleIdx;
    TelemInfoV01 info{};
    if (valid) {
        std::memcpy(&info, &telemetry.telemInfo[telemetry.playerVehicleIdx],
                    sizeof(info));
    }
    if (locked) {
        impl_->lock->Unlock();
    }
    if (!valid) {
        return false;
    }

    snapshot.valid = true;
    snapshot.player_has_vehicle = true;
    snapshot.active_vehicles = active_vehicles;
    snapshot.player_index = player_index;

    size_t name_length = 0;
    while (name_length < sizeof(info.mVehicleName) &&
           info.mVehicleName[name_length] != '\0') {
        ++name_length;
    }
    snapshot.vehicle_name.assign(info.mVehicleName, name_length);
    snapshot.vehicle_class = static_cast<uint8_t>(info.mVehicleClass);
    snapshot.elapsed_time = info.mElapsedTime;

    snapshot.tc = info.mTC;
    snapshot.tc_max = info.mTCMax;
    snapshot.tc_cut = info.mTCCut;
    snapshot.tc_cut_max = info.mTCCutMax;
    snapshot.tc_slip = info.mTCSlip;
    snapshot.tc_slip_max = info.mTCSlipMax;

    snapshot.abs = info.mABS;
    snapshot.abs_max = info.mABSMax;

    snapshot.motor_map = info.mMotorMap;
    snapshot.motor_map_max = info.mMotorMapMax;

    snapshot.migration = info.mMigration;
    snapshot.migration_max = info.mMigrationMax;

    snapshot.front_arb = info.mFrontAntiSway;
    snapshot.front_arb_max = info.mFrontAntiSwayMax;
    snapshot.rear_arb = info.mRearAntiSway;
    snapshot.rear_arb_max = info.mRearAntiSwayMax;

    snapshot.tc_active = info.mTCActive;
    snapshot.abs_active = info.mABSActive;

    snapshot.soc = info.mSoC;
    snapshot.virtual_energy = info.mVirtualEnergy;
    snapshot.regen = info.mRegen;
    snapshot.battery_fraction = info.mBatteryChargeFraction;
    snapshot.boost_state = info.mElectricBoostMotorState;

    snapshot.fuel = info.mFuel;
    snapshot.oil_temp = info.mEngineOilTemp;
    snapshot.water_temp = info.mEngineWaterTemp;

    return true;
}

}  // namespace simagicfix
