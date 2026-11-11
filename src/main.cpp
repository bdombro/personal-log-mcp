#include "app.hpp"
#include <mcp_http.hpp>

int main(int argc, char* argv[]) {
    auto app = personal_log::make_personal_log_app();
    slimr::enable_http(app, slimr::HttpServerOptions{.host = "127.0.0.1", .port = 8082});
    return app.run(argc, argv);
}
