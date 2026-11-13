#include "core/simpro_client.h"

#include "httplib.h"

namespace simagicfix {

std::optional<std::string> post_json_to_simpro(int port, const std::string& body,
                                               int timeout_ms) {
    try {
        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(0, timeout_ms * 1000);
        client.set_read_timeout(0, timeout_ms * 1000);
        client.set_write_timeout(0, timeout_ms * 1000);
        const auto response = client.Post("/jsonrpc", body, "application/json");
        if (!response || response->status != 200) {
            return std::nullopt;
        }
        return response->body;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace simagicfix
