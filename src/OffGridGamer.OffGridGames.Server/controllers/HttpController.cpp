#include "controllers.hpp"

namespace ogg::controllers {

ogg::net::HttpResponse handle_http_request(const ogg::net::HttpRequest& req) {
    if (req.path == "/status" || req.path == "/") {
        return { .status_code = 200, .content_type = "text/plain", .body = "OGG.Server Active" };
    }
    if (req.path == "/health") {
        return { .status_code = 200, .content_type = "application/json", .body = "{\"status\":\"ok\"}" };
    }

    return { .status_code = 404, .content_type = "text/plain", .body = "404 Not Found" };
}

} // namespace ogg::controllers
