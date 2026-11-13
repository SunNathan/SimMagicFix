#include "core/merge.h"

#include <cmath>

namespace simagicfix {

namespace {

nlohmann::json parse_or_object(const std::string& text) {
    try {
        return nlohmann::json::parse(text);
    } catch (...) {
        return nlohmann::json::object();
    }
}

void set_default(nlohmann::json& object, const char* key,
                 const nlohmann::json& value) {
    if (!object.contains(key) || object[key].is_null()) {
        object[key] = value;
    }
}

void fill_defaults(nlohmann::json& gd, const LmuSnapshot& snapshot, bool mock) {
    const double elapsed = snapshot.elapsed_time;
    const double speed = 120.0 + 40.0 * std::sin(elapsed * 0.5);
    const double rpm = 5000.0 + 2000.0 * std::sin(elapsed * 2.0);
    const int gear = 1 + (static_cast<int>(elapsed) % 6);

    set_default(gd, "speed", speed);
    set_default(gd, "rpm", rpm);
    set_default(gd, "gear", gear);
    set_default(gd, "maxGears", 6);
    set_default(gd, "fuel", 30.0);
    set_default(gd, "fuelPerLap", 2.4);
    set_default(gd, "fuelPercent", 60.0);
    set_default(gd, "completedLaps", static_cast<int>(elapsed / 90.0));
    set_default(gd, "position", 3);
    set_default(gd, "gapAhead", 1.2);
    set_default(gd, "gapBehind", -0.8);
    set_default(gd, "currentLapTime", "1:23:456");
    set_default(gd, "lastLapTime", "1:23:456");
    set_default(gd, "bestLapTime", "1:22:000");
    set_default(gd, "allTimeBest", "1:22:000");
    set_default(gd, "brakeBias", 55.0);
    set_default(gd, "clutch", 0.0);
    set_default(gd, "throttle", 0.0);
    set_default(gd, "brake", 0.0);
    set_default(gd, "oilPressure", 4.0);
    set_default(gd, "oilTemperature", 90.0);
    set_default(gd, "waterTemperature", 85.0);
    set_default(gd, "tyrePressure", nlohmann::json::array({2.0, 2.0, 2.0, 2.0}));
    set_default(gd, "tyreTemperature",
                nlohmann::json::array({80.0, 80.0, 80.0, 80.0}));
    set_default(gd, "tyreWear", nlohmann::json::array({100, 100, 100, 100}));
    set_default(gd, "wheelSlip", nlohmann::json::array({0.0, 0.0, 0.0, 0.0}));
    set_default(gd, "tractionLoss", 0.0);
    set_default(gd, "drsLevel", 0.0);
    set_default(gd, "ersMode", 2);
    set_default(gd, "pushToPass", 0);
    set_default(gd, "diffAdjOnThrottle", 3);
    set_default(gd, "diffEntry", 0);
    set_default(gd, "diffMiddle", 0);
    set_default(gd, "diffExit", 0);
    set_default(gd, "isGameRunning", 1);
    set_default(gd, "isGamePaused", 0);

    (void)mock;
}

void fill_v1_defaults(nlohmann::json& v1, const LmuSnapshot& snapshot) {
    const double elapsed = snapshot.elapsed_time;
    set_default(v1, "ang", 0);
    set_default(v1, "spd", static_cast<int>(120.0 + 40.0 * std::sin(elapsed * 0.5)));
    set_default(v1, "rpm", static_cast<int>(5000.0 + 2000.0 * std::sin(elapsed * 2.0)));
    set_default(v1, "rpm_led", 9);
    set_default(v1, "rpm_percentage", 50);
    set_default(v1, "gear", 1 + (static_cast<int>(elapsed) % 6));
    set_default(v1, "drs", 0);
    set_default(v1, "th", 0);
    set_default(v1, "brk", 0);
    set_default(v1, "cl", 0);
    set_default(v1, "gain_loss", 0);
    set_default(v1, "lap", static_cast<int>(elapsed / 90.0));
    set_default(v1, "flag", 0);
    set_default(v1, "tb", 0);
    set_default(v1, "brake_bias", 55.0);
    set_default(v1, "ersm", 2);
    set_default(v1, "p2p", 0);
    set_default(v1, "fxl", 0.0);
    set_default(v1, "favg", 0.0);
    set_default(v1, "diff", 0);
    set_default(v1, "tps", 0);
    set_default(v1, "gapa", 1.2);
    set_default(v1, "gapb", -0.8);
    set_default(v1, "entr", 0);
    set_default(v1, "mid", 0);
    set_default(v1, "hspd", 0);
    set_default(v1, "eb", 0);
    set_default(v1, "pit_flag", 0);
    set_default(v1, "drs_flag", 0);
    set_default(v1, "oil_temp", 90);
    set_default(v1, "oil_press", 4);
    set_default(v1, "water_temp", 85);
    set_default(v1, "pos", 3);
    set_default(v1, "fuel", 30);
    set_default(v1, "trun", "1:23:456");
    set_default(v1, "tbest", "1:22:000");
    set_default(v1, "tlast", "1:23:456");
    set_default(v1, "tt_ul", 80);
    set_default(v1, "tt_ur", 80);
    set_default(v1, "tt_dl", 80);
    set_default(v1, "tt_dr", 80);
    set_default(v1, "tti_ul", 80);
    set_default(v1, "tti_ur", 80);
    set_default(v1, "tti_dl", 80);
    set_default(v1, "tti_dr", 80);
    set_default(v1, "ttm_ul", 80);
    set_default(v1, "ttm_ur", 80);
    set_default(v1, "ttm_dl", 80);
    set_default(v1, "ttm_dr", 80);
    set_default(v1, "tto_ul", 80);
    set_default(v1, "tto_ur", 80);
    set_default(v1, "tto_dl", 80);
    set_default(v1, "tto_dr", 80);
    set_default(v1, "bt_ul", 400);
    set_default(v1, "bt_ur", 400);
    set_default(v1, "bt_dl", 300);
    set_default(v1, "bt_dr", 300);
    set_default(v1, "tp_ul", 2.0);
    set_default(v1, "tp_ur", 2.0);
    set_default(v1, "tp_dl", 2.0);
    set_default(v1, "tp_dr", 2.0);
    set_default(v1, "twp_ul", 100);
    set_default(v1, "twp_ur", 100);
    set_default(v1, "twp_dl", 100);
    set_default(v1, "twp_dr", 100);
}

}  // namespace

nlohmann::json build_response(const nlohmann::json& simpro_response,
                              const LmuSnapshot& snapshot, bool mock) {
    nlohmann::json response;
    if (simpro_response.is_object() && simpro_response.contains("result") &&
        simpro_response["result"].is_object()) {
        response = simpro_response;
    } else {
        response = nlohmann::json::object();
        response["jsonrpc"] = "2.0";
        response["id"] = 1;
        response["result"] = nlohmann::json::object();
    }

    if (!response.contains("jsonrpc")) {
        response["jsonrpc"] = "2.0";
    }
    if (!response.contains("id")) {
        response["id"] = 1;
    }
    if (!response["result"].is_object()) {
        response["result"] = nlohmann::json::object();
    }

    nlohmann::json gd = nlohmann::json::object();
    if (response["result"].contains("game_data")) {
        if (response["result"]["game_data"].is_string()) {
            gd = parse_or_object(response["result"]["game_data"].get<std::string>());
        } else if (response["result"]["game_data"].is_object()) {
            gd = response["result"]["game_data"];
        }
    }

    if (mock || gd.empty()) {
        fill_defaults(gd, snapshot, mock);
    }

    // Only override SimPro's values when LMU actually provides data. This
    // keeps the bridge transparent for other games (ACC, F1, iRacing...).
    if (mock || snapshot.valid) {
        gd["tcLevel"] = snapshot.tc;
        gd["tcCut"] = snapshot.tc_cut;
        gd["absLevel"] = snapshot.abs;
        gd["engineMap"] = snapshot.motor_map;
        gd["soc"] = snapshot.soc_percent();
        gd["ersPercent"] = snapshot.soc_percent();
        gd["migration"] = snapshot.migration;
        gd["frontAntiRollBar"] = snapshot.front_arb;
        gd["rearAntiRollBar"] = snapshot.rear_arb;
        gd["isTcActive"] = snapshot.tc_active ? 1 : 0;
        gd["isAbsActive"] = snapshot.abs_active ? 1 : 0;
        if (snapshot.oil_temp > 0.0) {
            gd["oilTemperature"] = snapshot.oil_temp;
        }
        if (snapshot.water_temp > 0.0) {
            gd["waterTemperature"] = snapshot.water_temp;
        }
        if (snapshot.fuel > 0.0) {
            gd["fuel"] = snapshot.fuel;
        }
    }
    response["result"]["game_data"] = gd.dump();

    nlohmann::json v1 = nlohmann::json::object();
    if (response["result"].contains("game_data_v1")) {
        if (response["result"]["game_data_v1"].is_string()) {
            v1 = parse_or_object(response["result"]["game_data_v1"].get<std::string>());
        } else if (response["result"]["game_data_v1"].is_object()) {
            v1 = response["result"]["game_data_v1"];
        }
    }
    if (mock || v1.empty()) {
        fill_v1_defaults(v1, snapshot);
    }
    if (mock || snapshot.valid) {
        v1["tc"] = snapshot.tc;
        v1["abs"] = snapshot.abs;
        v1["map"] = snapshot.motor_map;
        v1["tc2"] = snapshot.tc_cut;
        v1["soc"] = static_cast<int>(std::lround(snapshot.soc_percent()));
        v1["batt"] = static_cast<int>(std::lround(snapshot.soc_percent()));
        v1["farb"] = snapshot.front_arb;
        v1["rarb"] = snapshot.rear_arb;
        v1["tc_flag"] = snapshot.tc_active ? 1 : 0;
        v1["abs_flag"] = snapshot.abs_active ? 1 : 0;
    }
    response["result"]["game_data_v1"] = v1.dump();

    if (!response["result"].contains("axes") || !response["result"]["axes"].is_object()) {
        response["result"]["axes"] = nlohmann::json{
            {"brake", 0},          {"brake_angle_raw", 0},
            {"brake_force_raw", 0}, {"brake_raw", 0},
            {"clutch", 0},         {"clutch_raw", 0},
            {"handbrake", 0},      {"handbrake_raw", 0},
            {"throttle", 0},       {"throttle_raw", 0},
            {"wheel_pedal_left", 0}, {"wheel_pedal_right", 0}};
    }

    return response;
}

}  // namespace simagicfix
