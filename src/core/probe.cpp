#include "core/probe.h"

#include "core/lmu_reader.h"
#include "core/lmu_sdk.h"
#include "core/paths.h"

#include <cstddef>

namespace simagicfix {

namespace {

void print_field(const char* name, int value, int max_value) {
    std::cout << "  " << std::left << std::setw(14) << name << value;
    if (max_value > 0) {
        std::cout << " / " << max_value;
    }
    std::cout << "\n";
}

}  // namespace

int run_selftest() {
    std::cout << "LMU SDK struct layout\n";
    std::cout << "  sizeof(TelemVect3)              = " << sizeof(TelemVect3) << "\n";
    std::cout << "  sizeof(TelemWheelV01)           = " << sizeof(TelemWheelV01) << "\n";
    std::cout << "  sizeof(TelemInfoV01)            = " << sizeof(TelemInfoV01) << "\n";
    std::cout << "  offsetof(TelemInfoV01, mTC)     = "
              << offsetof(TelemInfoV01, mTC) << "\n";
    std::cout << "  offsetof(TelemInfoV01, mABS)    = "
              << offsetof(TelemInfoV01, mABS) << "\n";
    std::cout << "  offsetof(TelemInfoV01, mMotorMap)= "
              << offsetof(TelemInfoV01, mMotorMap) << "\n";
    std::cout << "  offsetof(TelemInfoV01, mSoC)    = "
              << offsetof(TelemInfoV01, mSoC) << "\n";
    std::cout << "  sizeof(SharedMemoryTelemetryData)= "
              << sizeof(SharedMemoryTelemetryData) << "\n";
    std::cout << "  sizeof(SharedMemoryObjectOut)   = "
              << sizeof(SharedMemoryObjectOut) << "\n";
    std::cout << "  offsetof(SharedMemoryObjectOut, telemetry) = "
              << offsetof(SharedMemoryObjectOut, telemetry) << "\n";
    std::cout << "  sizeof(SharedMemoryLayout)      = "
              << sizeof(SharedMemoryLayout) << "\n";

    static_assert(sizeof(TelemInfoV01) > 1000, "Unexpected TelemInfoV01 size");
    static_assert(offsetof(TelemInfoV01, mTC) > 0, "Unexpected mTC offset");
    static_assert(offsetof(TelemInfoV01, mSoC) > offsetof(TelemInfoV01, mTC),
                  "Unexpected mSoC offset");
    return 0;
}

int run_probe() {
    std::cout << "LMU shared memory probe\n";
    std::cout << "  LMU install : "
              << (lmu_install_path().empty() ? "not found"
                                             : wide_to_utf8(lmu_install_path().wstring()))
              << "\n";
    std::cout << "  SimPro exe  : "
              << (simpro_exe_path().empty() ? "not found"
                                            : wide_to_utf8(simpro_exe_path().wstring()))
              << "\n";
    std::cout << "  SimPro RPC  : " << simpro_rpc_port() << "\n";

    LmuReader reader;
    if (!reader.open()) {
        std::cout << "  LMU_Data    : not available (start LMU and enter a session)\n";
        return 1;
    }

    LmuSnapshot snapshot;
    if (!reader.read(snapshot)) {
        std::cout << "  LMU_Data    : available, but no player vehicle\n";
        return 1;
    }

    std::cout << "  Player      : " << snapshot.vehicle_name << "\n";
    std::cout << "  Class       : " << static_cast<int>(snapshot.vehicle_class) << "\n";
    std::cout << "  Vehicles    : " << static_cast<int>(snapshot.active_vehicles)
              << " (player index " << static_cast<int>(snapshot.player_index) << ")\n";
    std::cout << "  Elapsed     : " << snapshot.elapsed_time << " s\n";
    std::cout << "  Values:\n";
    print_field("TC", snapshot.tc, snapshot.tc_max);
    print_field("TC Cut", snapshot.tc_cut, snapshot.tc_cut_max);
    print_field("TC Slip", snapshot.tc_slip, snapshot.tc_slip_max);
    print_field("ABS", snapshot.abs, snapshot.abs_max);
    print_field("Motor Map", snapshot.motor_map, snapshot.motor_map_max);
    print_field("Migration", snapshot.migration, snapshot.migration_max);
    print_field("Front ARB", snapshot.front_arb, snapshot.front_arb_max);
    print_field("Rear ARB", snapshot.rear_arb, snapshot.rear_arb_max);
    std::cout << "  TC active   : " << (snapshot.tc_active ? "yes" : "no") << "\n";
    std::cout << "  ABS active  : " << (snapshot.abs_active ? "yes" : "no") << "\n";
    std::cout << "  SoC         : " << snapshot.soc << " ("
              << snapshot.soc_percent() << " %)\n";
    std::cout << "  Battery     : " << snapshot.battery_fraction * 100.0 << " %\n";
    std::cout << "  Virtual E.  : " << snapshot.virtual_energy << "\n";
    std::cout << "  Regen       : " << snapshot.regen << " kW\n";
    std::cout << "  Boost state : " << static_cast<int>(snapshot.boost_state) << "\n";
    std::cout << "  Fuel        : " << snapshot.fuel << " L\n";
    std::cout << "  Oil/Water   : " << snapshot.oil_temp << " / "
              << snapshot.water_temp << " C\n";
    return 0;
}

}  // namespace simagicfix
