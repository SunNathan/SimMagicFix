#include "core/telemetry.h"

#include "core/lmu_reader.h"
#include "core/paths.h"

#include <cmath>

namespace simagicfix {

struct TelemetryService::Impl {
    Options options;
    std::atomic<bool> running{false};
    std::atomic<bool> lmu_available{false};
    std::thread reader_thread;
    std::unique_ptr<LmuReader> reader;
    mutable std::shared_mutex snapshot_mutex;
    LmuSnapshot snapshot;
    mutable std::mutex error_mutex;
    std::string last_error;

    explicit Impl(Options value) : options(value) {}
    ~Impl() { stop(); }

    void set_error(const std::string& message) {
        std::lock_guard<std::mutex> lock(error_mutex);
        last_error = message;
    }

    std::string get_error() const {
        std::lock_guard<std::mutex> lock(error_mutex);
        return last_error;
    }

    LmuSnapshot get_snapshot() const {
        std::shared_lock<std::shared_mutex> lock(snapshot_mutex);
        return snapshot;
    }

    void store_snapshot(const LmuSnapshot& value) {
        std::unique_lock<std::shared_mutex> lock(snapshot_mutex);
        snapshot = value;
    }

    bool wait_for(std::chrono::milliseconds total) {
        const auto deadline = std::chrono::steady_clock::now() + total;
        while (running.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return true;
            }
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now);
            std::this_thread::sleep_for(
                std::min(remaining, std::chrono::milliseconds(50)));
        }
        return false;
    }

    LmuSnapshot make_mock_snapshot() const {
        LmuSnapshot s;
        s.valid = true;
        s.player_has_vehicle = true;
        s.active_vehicles = 1;
        s.player_index = 0;
        s.vehicle_name = "Voiture test";
        s.vehicle_class = 0;
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const double seconds = std::chrono::duration<double>(now).count();
        s.elapsed_time = seconds;
        s.tc = 1 + static_cast<int>(seconds) % 12;
        s.tc_max = 12;
        s.tc_cut = 1 + static_cast<int>(seconds * 1.3) % 12;
        s.tc_cut_max = 12;
        s.tc_slip = 2;
        s.tc_slip_max = 10;
        s.abs = 1 + static_cast<int>(seconds * 0.7) % 12;
        s.abs_max = 12;
        s.motor_map = 1 + static_cast<int>(seconds * 0.4) % 10;
        s.motor_map_max = 10;
        s.migration = 1 + static_cast<int>(seconds * 0.2) % 6;
        s.migration_max = 6;
        s.front_arb = 1 + static_cast<int>(seconds * 0.3) % 10;
        s.front_arb_max = 10;
        s.rear_arb = 1 + static_cast<int>(seconds * 0.5) % 10;
        s.rear_arb_max = 10;
        s.tc_active = (static_cast<int>(seconds * 2) % 4) == 0;
        s.abs_active = (static_cast<int>(seconds * 2) % 5) == 0;
        s.soc = static_cast<float>(std::fmod(seconds * 7.0, 100.0) / 100.0);
        s.battery_fraction = s.soc;
        s.virtual_energy = 1.5f;
        s.regen = 20.0f;
        s.boost_state = 2;
        s.fuel = 30.0;
        s.oil_temp = 90.0;
        s.water_temp = 85.0;
        return s;
    }

    void reader_loop() {
        auto next_retry = std::chrono::steady_clock::now();
        auto next_process_check = std::chrono::steady_clock::now();
        while (running.load()) {
            if (options.mock) {
                store_snapshot(make_mock_snapshot());
                lmu_available.store(true);
                if (!wait_for(std::chrono::milliseconds(5))) {
                    break;
                }
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= next_process_check) {
                next_process_check = now + std::chrono::seconds(2);
                if (!process_running(L"Le Mans Ultimate.exe")) {
                    if (reader) {
                        reader->close();
                    }
                    lmu_available.store(false);
                    store_snapshot(LmuSnapshot{});
                    if (!wait_for(std::chrono::milliseconds(200))) {
                        break;
                    }
                    continue;
                }
            }

            if (!reader) {
                reader = std::make_unique<LmuReader>();
            }
            if (!reader->is_open()) {
                const auto retry_now = std::chrono::steady_clock::now();
                if (retry_now >= next_retry) {
                    reader->open();
                    next_retry = retry_now + std::chrono::seconds(1);
                }
                if (!reader->is_open()) {
                    lmu_available.store(false);
                    if (!wait_for(std::chrono::milliseconds(200))) {
                        break;
                    }
                    continue;
                }
            }

            LmuSnapshot value;
            if (reader->read(value)) {
                store_snapshot(value);
                lmu_available.store(true);
                if (!wait_for(std::chrono::milliseconds(1))) {
                    break;
                }
            } else {
                lmu_available.store(false);
                if (!wait_for(std::chrono::milliseconds(
                        options.poll_ms > 0 ? options.poll_ms : 20))) {
                    break;
                }
            }
        }
        if (reader) {
            reader->close();
        }
    }

    bool start() {
        if (running.load()) {
            return true;
        }
        running.store(true);
        reader_thread = std::thread([this]() { reader_loop(); });
        return true;
    }

    void stop() {
        if (!running.exchange(false)) {
            return;
        }
        if (reader_thread.joinable()) {
            reader_thread.join();
        }
        if (reader) {
            reader->close();
        }
    }
};

TelemetryService::TelemetryService(Options options)
    : impl_(std::make_unique<Impl>(options)) {}
TelemetryService::~TelemetryService() = default;

bool TelemetryService::start() { return impl_->start(); }
void TelemetryService::stop() { impl_->stop(); }
bool TelemetryService::running() const { return impl_->running.load(); }
bool TelemetryService::lmu_available() const {
    return impl_->lmu_available.load();
}
LmuSnapshot TelemetryService::snapshot() const { return impl_->get_snapshot(); }
std::string TelemetryService::last_error() const { return impl_->get_error(); }

}  // namespace simagicfix
