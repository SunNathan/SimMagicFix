#include "core/bridge.h"

#include "core/lmu_reader.h"
#include "core/merge.h"
#include "core/paths.h"
#include "core/simpro_client.h"
#include "httplib.h"
#include "json.hpp"

#include <cmath>

namespace simagicfix {

namespace {

bool is_local_origin(const std::string& origin) {
    return origin.rfind("http://localhost:", 0) == 0 ||
           origin.rfind("http://127.0.0.1:", 0) == 0;
}

void set_cors(const httplib::Request& request, httplib::Response& response) {
    const auto origin = request.headers.find("Origin");
    // SimPro's CEF may use localhost, 127.0.0.1 or a file:// (null) origin.
    // Remote web pages are not allowed to read the local telemetry.
    const bool local = origin == request.headers.end() || origin->second == "null" ||
                       is_local_origin(origin->second);
    if (local) {
        response.set_header("Access-Control-Allow-Origin", "*");
        response.set_header("Vary", "Origin");
    }
    response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    const std::string requested_headers =
        request.get_header_value("Access-Control-Request-Headers");
    response.set_header("Access-Control-Allow-Headers",
                        requested_headers.empty() ? "Content-Type"
                                                  : requested_headers);
    if (request.get_header_value("Access-Control-Request-Private-Network") ==
        "true") {
        response.set_header("Access-Control-Allow-Private-Network", "true");
    }
    response.set_header("Cache-Control", "no-store");
}

nlohmann::json parse_or_object(const std::string& text) {
    try {
        return nlohmann::json::parse(text);
    } catch (...) {
        return nlohmann::json::object();
    }
}

}  // namespace

struct BridgeService::Impl {
    Options options;
    std::atomic<bool> running{false};
    std::atomic<bool> lmu_available{false};
    std::thread reader_thread;
    std::vector<std::unique_ptr<httplib::Server>> servers;
    std::vector<std::thread> server_threads;
    std::vector<int> active_ports;
    std::unique_ptr<LmuReader> reader;
    mutable std::shared_mutex snapshot_mutex;
    LmuSnapshot snapshot;
    mutable std::mutex error_mutex;
    std::string last_error;
    std::mutex cache_mutex;
    mutable std::mutex ports_mutex;
    std::string cache_key;
    std::string cache_body;
    std::chrono::steady_clock::time_point cache_time{};

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

    std::vector<int> get_ports() const {
        std::lock_guard<std::mutex> lock(ports_mutex);
        return active_ports;
    }

    bool is_simpro_ui_origin(const std::string& origin) const {
        return origin ==
                   "http://localhost:" + std::to_string(options.simpro_http_port) ||
               origin ==
                   "http://127.0.0.1:" + std::to_string(options.simpro_http_port);
    }

    LmuSnapshot get_snapshot() const {
        std::shared_lock<std::shared_mutex> lock(snapshot_mutex);
        return snapshot;
    }

    void store_snapshot(const LmuSnapshot& value) {
        std::unique_lock<std::shared_mutex> lock(snapshot_mutex);
        snapshot = value;
    }

    LmuSnapshot make_mock_snapshot() const {
        LmuSnapshot s;
        s.valid = true;
        s.player_has_vehicle = true;
        s.active_vehicles = 1;
        s.player_index = 0;
        s.vehicle_name = "Mock Hypercar";
        s.vehicle_class = 0;
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const double seconds =
            std::chrono::duration<double>(now).count();
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
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= next_process_check) {
                next_process_check = now + std::chrono::seconds(1);
                if (!process_running(L"Le Mans Ultimate.exe")) {
                    if (reader) {
                        reader->close();
                    }
                    lmu_available.store(false);
                    store_snapshot(LmuSnapshot{});
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    continue;
                }
            }

            LmuSnapshot value;
            if (reader->read(value)) {
                store_snapshot(value);
                lmu_available.store(true);
            } else {
                lmu_available.store(false);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (reader) {
            reader->close();
        }
    }

    void handle_jsonrpc(const httplib::Request& request, httplib::Response& response) {
        set_cors(request, response);
        const std::string body = request.body;
        nlohmann::json request_json = parse_or_object(body);
        const std::string method =
            request_json.contains("method") && request_json["method"].is_string()
                ? request_json["method"].get<std::string>()
                : "";
        const std::string origin = request.get_header_value("Origin");
        const bool local = origin.empty() || origin == "null" ||
                           is_local_origin(origin);
        if (!local) {
            nlohmann::json error;
            error["jsonrpc"] = "2.0";
            error["id"] = 1;
            if (request_json.contains("id")) {
                error["id"] = request_json["id"];
            }
            error["error"] = {{"code", -32600}, {"message", "origin not allowed"}};
            response.status = 403;
            response.set_content(error.dump(), "application/json");
            return;
        }

        // The dashboards only need "query". Other methods are allowed only for
        // the SimPro web UI, so a random local page cannot drive SimPro.
        if (method != "query" && !origin.empty() && !is_simpro_ui_origin(origin)) {
            nlohmann::json error;
            error["jsonrpc"] = "2.0";
            error["id"] = 1;
            if (request_json.contains("id")) {
                error["id"] = request_json["id"];
            }
            error["error"] = {{"code", -32601}, {"message", "method not allowed"}};
            response.status = 403;
            response.set_content(error.dump(), "application/json");
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (method == "query") {
            std::lock_guard<std::mutex> lock(cache_mutex);
            if (body == cache_key &&
                now - cache_time < std::chrono::milliseconds(25)) {
                response.status = 200;
                response.set_content(cache_body, "application/json");
                return;
            }
        }

        const auto simpro_body =
            options.mock ? std::optional<std::string>{}
                         : post_json_to_simpro(options.simpro_port, body);
        nlohmann::json simpro = nlohmann::json::object();
        if (simpro_body.has_value()) {
            simpro = parse_or_object(*simpro_body);
        }

        if (!simpro_body.has_value() && method != "query") {
            nlohmann::json error;
            error["jsonrpc"] = "2.0";
            error["id"] = 1;
            if (request_json.contains("id")) {
                error["id"] = request_json["id"];
            }
            error["error"] = {{"code", -32000}, {"message", "SimPro unavailable"}};
            response.status = 502;
            response.set_content(error.dump(), "application/json");
            return;
        }

        if (simpro.contains("error")) {
            if (!simpro.contains("id") && request_json.contains("id")) {
                simpro["id"] = request_json["id"];
            }
            if (!simpro.contains("jsonrpc")) {
                simpro["jsonrpc"] = "2.0";
            }
            response.status = 200;
            response.set_content(simpro.dump(), "application/json");
            return;
        }

        if (method == "query") {
            const LmuSnapshot value = get_snapshot();
            nlohmann::json result = build_response(simpro, value, options.mock);
            if (request_json.contains("id")) {
                result["id"] = request_json["id"];
            }
            if (request_json.contains("jsonrpc")) {
                result["jsonrpc"] = request_json["jsonrpc"];
            }
            const std::string serialized = result.dump();
            {
                std::lock_guard<std::mutex> lock(cache_mutex);
                cache_key = body;
                cache_body = serialized;
                cache_time = now;
            }
            response.status = 200;
            response.set_content(serialized, "application/json");
            return;
        }

        // Forward non-query methods (get_config, set_config, ...) unchanged.
        if (request_json.contains("id")) {
            simpro["id"] = request_json["id"];
        }
        if (request_json.contains("jsonrpc")) {
            simpro["jsonrpc"] = request_json["jsonrpc"];
        }
        response.status = 200;
        response.set_content(simpro.dump(), "application/json");
    }

    void handle_status(const httplib::Request& request, httplib::Response& response) {
        set_cors(request, response);
        const LmuSnapshot value = get_snapshot();
        nlohmann::json status;
        status["running"] = running.load();
        status["mock"] = options.mock;
        status["lmu_available"] = lmu_available.load();
        status["port"] = options.port;
        {
            std::lock_guard<std::mutex> lock(ports_mutex);
            status["ports"] = active_ports;
        }
        status["simpro_port"] = options.simpro_port;
        status["simpro_http_port"] = options.simpro_http_port;
        status["vehicle"] = value.vehicle_name;
        status["valid"] = value.valid;
        status["tc"] = value.tc;
        status["tc_max"] = value.tc_max;
        status["tc_cut"] = value.tc_cut;
        status["tc_cut_max"] = value.tc_cut_max;
        status["abs"] = value.abs;
        status["abs_max"] = value.abs_max;
        status["map"] = value.motor_map;
        status["map_max"] = value.motor_map_max;
        status["soc"] = value.soc_percent();
        status["tc_active"] = value.tc_active;
        status["abs_active"] = value.abs_active;
        status["error"] = get_error();
        response.status = 200;
        response.set_content(status.dump(), "application/json");
    }

    void configure_server(httplib::Server& server) {
        server.Post("/jsonrpc", [this](const httplib::Request& request,
                                       httplib::Response& response) {
            handle_jsonrpc(request, response);
        });
        server.Options("/jsonrpc", [](const httplib::Request& request,
                                      httplib::Response& response) {
            set_cors(request, response);
            response.status = 204;
        });
        server.Options("/status", [](const httplib::Request& request,
                                     httplib::Response& response) {
            set_cors(request, response);
            response.status = 204;
        });
        server.Get("/status", [this](const httplib::Request& request,
                                     httplib::Response& response) {
            handle_status(request, response);
        });
        server.set_exception_handler([](const httplib::Request&,
                                        httplib::Response& response,
                                        std::exception_ptr ep) {
            try {
                if (ep) {
                    std::rethrow_exception(ep);
                }
            } catch (const std::exception& e) {
                response.status = 500;
                response.set_content(e.what(), "text/plain");
                return;
            } catch (...) {
            }
            response.status = 500;
            response.set_content("internal error", "text/plain");
        });
    }

    bool start() {
        if (running.load()) {
            return true;
        }

        std::vector<int> wanted{options.port};
        if (options.fallback_port > 0 && options.fallback_port != options.port) {
            wanted.push_back(options.fallback_port);
        }

        for (size_t i = 0; i < wanted.size(); ++i) {
            const int port = wanted[i];
            auto server = std::make_unique<httplib::Server>();
            configure_server(*server);
            if (!server->bind_to_port("127.0.0.1", port)) {
                if (i == 0) {
                    set_error("Port " + std::to_string(port) +
                              " is already in use");
                    servers.clear();
                    {
                        std::lock_guard<std::mutex> lock(ports_mutex);
                        active_ports.clear();
                    }
                    return false;
                }
                set_error("Fallback port " + std::to_string(port) +
                          " is already in use");
                continue;
            }
            servers.push_back(std::move(server));
            {
                std::lock_guard<std::mutex> lock(ports_mutex);
                active_ports.push_back(port);
            }
        }

        if (servers.empty()) {
            set_error("No port available");
            return false;
        }

        running.store(true);
        for (auto& server : servers) {
            httplib::Server* raw = server.get();
            server_threads.emplace_back(
                [raw]() { raw->listen_after_bind(); });
        }
        for (auto& server : servers) {
            server->wait_until_ready();
        }
        reader_thread = std::thread([this]() { reader_loop(); });
        return true;
    }

    void stop() {
        if (!running.exchange(false)) {
            return;
        }
        for (auto& server : servers) {
            server->stop();
        }
        for (auto& thread : server_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        server_threads.clear();
        servers.clear();
        {
            std::lock_guard<std::mutex> lock(ports_mutex);
            active_ports.clear();
        }
        if (reader_thread.joinable()) {
            reader_thread.join();
        }
        if (reader) {
            reader->close();
        }
    }
};

BridgeService::BridgeService(Options options)
    : impl_(std::make_unique<Impl>(options)) {}
BridgeService::~BridgeService() = default;

bool BridgeService::start() { return impl_->start(); }
void BridgeService::stop() { impl_->stop(); }
bool BridgeService::running() const { return impl_->running.load(); }
bool BridgeService::lmu_available() const { return impl_->lmu_available.load(); }
LmuSnapshot BridgeService::snapshot() const { return impl_->get_snapshot(); }
std::string BridgeService::last_error() const { return impl_->get_error(); }
int BridgeService::port() const { return impl_->options.port; }
std::vector<int> BridgeService::ports() const { return impl_->get_ports(); }

}  // namespace simagicfix
