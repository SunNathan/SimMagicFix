#pragma once

#include "core/common.h"

namespace simagicfix {

// Plain snapshot of the values we need. The LMU SDK types never leave
// lmu_reader.cpp / mock_shm.cpp, which keeps the rest of the code independent
// from the proprietary header.
struct LmuSnapshot {
    bool valid = false;
    bool player_has_vehicle = false;
    uint8_t active_vehicles = 0;
    uint8_t player_index = 0;

    std::string vehicle_name;
    uint8_t vehicle_class = 0xFF;
    double elapsed_time = 0.0;

    int tc = 0;
    int tc_max = 0;
    int tc_cut = 0;
    int tc_cut_max = 0;
    int tc_slip = 0;
    int tc_slip_max = 0;

    int abs = 0;
    int abs_max = 0;

    int motor_map = 0;
    int motor_map_max = 0;

    int migration = 0;
    int migration_max = 0;

    int front_arb = 0;
    int front_arb_max = 0;
    int rear_arb = 0;
    int rear_arb_max = 0;

    bool tc_active = false;
    bool abs_active = false;

    float soc = 0.0f;              // LMU mSoC (0..1 or 0..100 depending on build)
    float virtual_energy = 0.0f;   // LMU mVirtualEnergy
    float regen = 0.0f;            // kW
    double battery_fraction = 0.0; // 0..1
    uint8_t boost_state = 0;       // 0 unavailable, 1 inactive, 2 propulsion, 3 regen

    double fuel = 0.0;
    double oil_temp = 0.0;
    double water_temp = 0.0;

    // Normalised state of charge, in percent.
    double soc_percent() const {
        if (soc > 0.0f) {
            return soc <= 1.0f ? static_cast<double>(soc) * 100.0
                               : static_cast<double>(soc);
        }
        return battery_fraction * 100.0;
    }
};

}  // namespace simagicfix
