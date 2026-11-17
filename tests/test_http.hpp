#pragma once

#include "app.hpp"
#include "db.hpp"
#include "mcp_http.hpp"
#include "mine.hpp"
#include "test.hpp"
#include "test_db.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace personal_log::testing {

struct HttpReply {
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;
};

class TcpFd {
  public:
    TcpFd() = default;
    explicit TcpFd(int fd) noexcept : fd_(fd) {}
    ~TcpFd() { reset(); }

    TcpFd(const TcpFd &) = delete;
    TcpFd &operator=(const TcpFd &) = delete;

    TcpFd(TcpFd &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    TcpFd &operator=(TcpFd &&other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

    void reset() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

  private:
    int fd_ = -1;
};

inline TcpFd connect_loopback(uint16_t port) {
    int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw < 0)
        return {};
    TcpFd fd(raw);

    struct timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    (void)::setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)::setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        return {};
    }
    if (::connect(fd.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
        return {};
    }
    return fd;
}

inline bool send_all(int fd, std::string_view data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

inline std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char &ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

inline std::string trim_ows(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

inline std::string decode_chunked_body(std::string_view raw) {
    std::string decoded;
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t lineEnd = raw.find("\r\n", pos);
        if (lineEnd == std::string_view::npos)
            break;
        std::string sizeHex(raw.substr(pos, lineEnd - pos));
        size_t semi = sizeHex.find(';');
        if (semi != std::string::npos)
            sizeHex.resize(semi);
        size_t chunkLen = 0;
        try {
            chunkLen = static_cast<size_t>(std::stoul(sizeHex, nullptr, 16));
        } catch (...) {
            break;
        }
        pos = lineEnd + 2;
        if (chunkLen == 0)
            break;
        if (pos + chunkLen > raw.size())
            break;
        decoded.append(raw.substr(pos, chunkLen));
        pos += chunkLen;
        if (pos + 2 <= raw.size() && raw.substr(pos, 2) == "\r\n") {
            pos += 2;
        }
    }
    return decoded;
}

inline HttpReply http_exchange(
    uint16_t port,
    std::string_view method,
    std::string_view path,
    std::string_view body = "",
    const std::vector<std::pair<std::string, std::string>> &extraHeaders = {}) {
    HttpReply reply;
    TcpFd fd = connect_loopback(port);
    REQUIRE(fd.valid());

    std::string req;
    req.reserve(256 + body.size());
    req += method;
    req += " ";
    req += path;
    req += " HTTP/1.1\r\n";
    req += "Host: 127.0.0.1:";
    req += std::to_string(port);
    req += "\r\n";
    req += "Connection: close\r\n";
    for (const auto &[k, v] : extraHeaders) {
        req += k;
        req += ": ";
        req += v;
        req += "\r\n";
    }
    if (!body.empty()) {
        req += "Content-Length: ";
        req += std::to_string(body.size());
        req += "\r\n";
    }
    req += "\r\n";
    req += body;

    REQUIRE(send_all(fd.get(), req));

    std::string raw;
    std::vector<char> buf(4096);
    while (true) {
        ssize_t n = ::recv(fd.get(), buf.data(), buf.size(), 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        raw.append(buf.data(), static_cast<size_t>(n));
    }

    size_t hdrEnd = raw.find("\r\n\r\n");
    REQUIRE(hdrEnd != std::string::npos);

    std::string_view head(raw.data(), hdrEnd);
    std::string rawBody = raw.substr(hdrEnd + 4);

    size_t firstLineEnd = head.find("\r\n");
    std::string_view statusLine =
        (firstLineEnd == std::string_view::npos) ? head : head.substr(0, firstLineEnd);
    size_t sp1 = statusLine.find(' ');
    REQUIRE(sp1 != std::string_view::npos);
    size_t sp2 = statusLine.find(' ', sp1 + 1);
    std::string_view codeStr = (sp2 == std::string_view::npos)
                                   ? statusLine.substr(sp1 + 1)
                                   : statusLine.substr(sp1 + 1, sp2 - (sp1 + 1));
    reply.status = std::stoi(std::string(codeStr));

    if (firstLineEnd != std::string_view::npos) {
        size_t cur = firstLineEnd + 2;
        while (cur < head.size()) {
            size_t next = head.find("\r\n", cur);
            std::string_view line =
                (next == std::string_view::npos) ? head.substr(cur) : head.substr(cur, next - cur);
            size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                reply.headers[ascii_lower(trim_ows(line.substr(0, colon)))] =
                    trim_ows(line.substr(colon + 1));
            }
            if (next == std::string_view::npos)
                break;
            cur = next + 2;
        }
    }

    auto teIt = reply.headers.find("transfer-encoding");
    if (teIt != reply.headers.end() && ascii_lower(teIt->second).find("chunked") != std::string::npos) {
        reply.body = decode_chunked_body(rawBody);
    } else {
        auto clIt = reply.headers.find("content-length");
        if (clIt != reply.headers.end()) {
            size_t len = static_cast<size_t>(std::stoul(clIt->second));
            reply.body = rawBody.substr(0, std::min(len, rawBody.size()));
        } else {
            reply.body = std::move(rawBody);
        }
    }

    return reply;
}

inline HttpReply post_mcp(uint16_t port, const slimr::Json &req) {
    auto dumped = req.dump();
    REQUIRE(dumped.has_value());
    std::vector<std::pair<std::string, std::string>> headers{
        {"Content-Type", "application/json"},
        {"Accept", "application/json, text/event-stream"},
        {"MCP-Protocol-Version", std::string(slimr::kMcpProtocolVersion)},
    };
    if (req.contains("method") && req["method"].is_string()) {
        std::string method = req["method"].get<std::string>();
        headers.emplace_back("Mcp-Method", method);
        if (req.contains("params") && req["params"].is_object()) {
            const auto &params = req["params"];
            if (method == "tools/call" && params.contains("name") && params["name"].is_string()) {
                headers.emplace_back("Mcp-Name", params["name"].get<std::string>());
            } else if (method == "resources/read" && params.contains("uri") && params["uri"].is_string()) {
                headers.emplace_back("Mcp-Name", params["uri"].get<std::string>());
            }
        }
    }
    return http_exchange(port, "POST", "/mcp", *dumped, headers);
}

inline int run_cli(slimr::PluginApp &app, std::initializer_list<std::string_view> args) {
    std::vector<std::string> owned;
    owned.reserve(args.size());
    for (std::string_view a : args) {
        owned.emplace_back(a);
    }
    std::vector<char *> ptrs;
    ptrs.reserve(owned.size());
    for (std::string &s : owned) {
        ptrs.push_back(s.data());
    }
    return app.run(static_cast<int>(ptrs.size()), ptrs.data());
}

TEST_CASE("enable_http CLI validation rejects bad port values, tool names, and unknown flags") {
    auto db = std::make_shared<AppDb>(":memory:");
    auto app = make_personal_log_app(db);
    slimr::enable_http(app, slimr::HttpServerOptions{.host = "127.0.0.1", .port = 10002});

    CHECK(run_cli(app, {"personal-log-mcp", "--version"}) == 0);
    CHECK(run_cli(app, {"personal-log-mcp", "http", "--port", "not-a-number"}) == 2);
    CHECK(run_cli(app, {"personal-log-mcp", "http", "--port", "70000"}) == 2);
    CHECK(run_cli(app, {"personal-log-mcp", "http", "--unknown-flag"}) == 2);
    const std::string toolsFlag = std::string("--") + "tools";
    const std::string openapiFlag = std::string("--") + "openapi";
    CHECK(run_cli(app, {"personal-log-mcp", "http", toolsFlag}) != 0);
    CHECK(run_cli(app, {"personal-log-mcp", "status"}) != 0);
    CHECK(run_cli(app, {"personal-log-mcp", openapiFlag}) != 0);
}

TEST_CASE("HTTP /tools/<name> runs a read-only tool and root tool routes return 404") {
    auto db = std::make_shared<AppDb>(":memory:");
    auto app = make_personal_log_app(db);
    slimr::McpHttpServer server(
        app,
        slimr::HttpServerOptions{
            .host = "127.0.0.1",
            .port = 0,
        });
    std::string err;
    REQUIRE(server.start(&err));
    uint16_t port = server.boundPort();
    REQUIRE(port > 0);

    CHECK(http_exchange(port, "GET", "/openapi.json").status == 404);
    CHECK(http_exchange(port, "GET", "/schema").status == 404);
    CHECK(http_exchange(port, "POST", "/status", "{}", {{"Content-Type", "application/json"}}).status == 404);

    auto openapi = http_exchange(port, "GET", "/tools/openapi.json");
    CHECK(openapi.status == 200);
    auto openapiJson = slimr::parse_json<slimr::Json>(openapi.body);
    REQUIRE(openapiJson.has_value());
    CHECK((*openapiJson)["paths"].contains("/tools/status"));
    CHECK((*openapiJson)["paths"]["/tools/status"].contains("post"));

    CHECK(http_exchange(port, "GET", "/tools/schema").status == 200);

    auto statusReply = http_exchange(
        port, "POST", "/tools/status", "{}", {{"Content-Type", "application/json"}});
    CHECK(statusReply.status == 200);
    CHECK(statusReply.body.find(PERSONAL_LOG_APP_VERSION) != std::string::npos);

    server.stop();
}

TEST_CASE("McpHttpServer serves July 2026-07-28 discovery, tools, resources, and rejects legacy initialize") {
    auto db = std::make_shared<AppDb>(":memory:");
    auto app = make_personal_log_app(db);

    slimr::McpHttpServer server(
        app,
        slimr::HttpServerOptions{
            .host = "127.0.0.1",
            .port = 0,
        });
    std::string err;
    REQUIRE(server.start(&err));
    uint16_t port = server.boundPort();
    REQUIRE(port > 0);

    // 1. server/discover
    auto discReply = post_mcp(port, make_mcp_req(1, "server/discover"));
    CHECK(discReply.status == 200);
    auto discJson = slimr::parse_json<slimr::Json>(discReply.body);
    REQUIRE(discJson.has_value());
    CHECK(!discJson->contains("error"));
    const auto &supported = (*discJson)["result"]["supportedVersions"];
    REQUIRE(supported.is_array());
    bool hasProtocol = false;
    for (const auto &v : supported.get_array()) {
        if (v.is_string() && v.get<std::string>() == "2026-07-28") {
            hasProtocol = true;
        }
    }
    CHECK(hasProtocol);
    CHECK((*discJson)["result"]["_meta"]["io.modelcontextprotocol/serverInfo"]["name"].get<std::string>() == "personal-log-mcp");
    CHECK((*discJson)["result"]["_meta"]["io.modelcontextprotocol/serverInfo"]["version"].get<std::string>() == PERSONAL_LOG_APP_VERSION);

    // 2. tools/list
    auto listReply = post_mcp(port, make_mcp_req(2, "tools/list"));
    CHECK(listReply.status == 200);
    auto listJson = slimr::parse_json<slimr::Json>(listReply.body);
    REQUIRE(listJson.has_value());
    const auto &tools = (*listJson)["result"]["tools"];
    REQUIRE(tools.is_array());
    std::vector<std::string> names;
    for (const auto &t : tools.get_array()) {
        names.push_back(t["name"].get<std::string>());
    }
    for (std::string_view expected : {
             "add", "get", "search", "update", "merge", "delete", "sql",
             "schema", "runs", "import", "backup", "mine", "status", "mcp_resource_read"}) {
        CHECK(std::find(names.begin(), names.end(), expected) != names.end());
    }

    // 3. tools/call round-trip (status, schema, add, search, get, sql, runs)
    {
        slimr::Json p = slimr::json_obj();
        p["name"] = "status";
        p["arguments"] = slimr::json_obj();
        auto r = post_mcp(port, make_mcp_req(3, "tools/call", std::move(p)));
        CHECK(r.status == 200);
        auto j = slimr::parse_json<slimr::Json>(r.body);
        REQUIRE(j.has_value());
        CHECK(!j->contains("error"));
        CHECK((*j)["result"]["content"][0]["text"].get<std::string>().find(PERSONAL_LOG_APP_VERSION) != std::string::npos);
    }
    {
        slimr::Json entry = slimr::json_obj();
        entry["date"] = "2030-06-15";
        entry["title"] = "HTTP round-trip entry";
        entry["tags"] = slimr::Json::array_t{"http", "test"};
        slimr::Json p = slimr::json_obj();
        p["name"] = "add";
        p["arguments"] = slimr::Json::object_t{{"entries", slimr::Json::array_t{entry}}};
        auto r = post_mcp(port, make_mcp_req(4, "tools/call", std::move(p)));
        CHECK(r.status == 200);
        auto j = slimr::parse_json<slimr::Json>(r.body);
        REQUIRE(j.has_value());
        CHECK((*j)["result"]["content"][0]["text"].get<std::string>().find("\"added\":[1]") != std::string::npos);
    }
    {
        slimr::Json p = slimr::json_obj();
        p["name"] = "search";
        p["arguments"] = slimr::Json::object_t{{"query", "round-trip"}};
        auto r = post_mcp(port, make_mcp_req(5, "tools/call", std::move(p)));
        CHECK(r.status == 200);
        auto j = slimr::parse_json<slimr::Json>(r.body);
        REQUIRE(j.has_value());
        CHECK((*j)["result"]["content"][0]["text"].get<std::string>().find("HTTP round-trip entry") != std::string::npos);
    }
    {
        slimr::Json p = slimr::json_obj();
        p["name"] = "sql";
        p["arguments"] = slimr::Json::object_t{{"query", "SELECT COUNT(*) AS cnt FROM entries"}};
        auto r = post_mcp(port, make_mcp_req(6, "tools/call", std::move(p)));
        CHECK(r.status == 200);
        auto j = slimr::parse_json<slimr::Json>(r.body);
        REQUIRE(j.has_value());
        CHECK((*j)["result"]["content"][0]["text"].get<std::string>().find("\"cnt\":1") != std::string::npos);
    }

    // 4. resources/read
    for (std::string_view uri : {"personal-log://conventions", "personal-log://recall"}) {
        slimr::Json p = slimr::json_obj();
        p["uri"] = std::string(uri);
        auto r = post_mcp(port, make_mcp_req(7, "resources/read", std::move(p)));
        CHECK(r.status == 200);
        auto j = slimr::parse_json<slimr::Json>(r.body);
        REQUIRE(j.has_value());
        CHECK(!j->contains("error"));
        CHECK(!(*j)["result"]["contents"][0]["text"].get<std::string>().empty());
    }

    // 5. Legacy initialize rejected
    auto initReply = post_mcp(port, make_mcp_req(8, "initialize"));
    CHECK(initReply.status == 404);
    auto initJson = slimr::parse_json<slimr::Json>(initReply.body);
    REQUIRE(initJson.has_value());
    CHECK((*initJson)["error"]["code"].get<int64_t>() == -32601);

    // 6. GET /mcp rejected with 405
    auto getReply = http_exchange(port, "GET", "/mcp");
    CHECK(getReply.status == 405);

    server.stop();
}

TEST_CASE("file-backed AppDb enables WAL and synchronous=NORMAL and serializes concurrent threads") {
    slimr::testing::TempDirGuard temp("plog_wal_");
    auto db = std::make_shared<AppDb>((temp.path / "log.db").string());

    std::string journalMode;
    int syncMode = -1;
    db->withLock([&](slimr::SqliteDb &sqlDb) {
        {
            sqlite3_stmt *raw = nullptr;
            REQUIRE(sqlite3_prepare_v2(sqlDb.handle(), "PRAGMA journal_mode;", -1, &raw, nullptr) == SQLITE_OK);
            std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, &sqlite3_finalize);
            REQUIRE(sqlite3_step(stmt.get()) == SQLITE_ROW);
            const unsigned char *txt = sqlite3_column_text(stmt.get(), 0);
            REQUIRE(txt != nullptr);
            journalMode = reinterpret_cast<const char *>(txt);
        }
        {
            sqlite3_stmt *raw = nullptr;
            REQUIRE(sqlite3_prepare_v2(sqlDb.handle(), "PRAGMA synchronous;", -1, &raw, nullptr) == SQLITE_OK);
            std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, &sqlite3_finalize);
            REQUIRE(sqlite3_step(stmt.get()) == SQLITE_ROW);
            syncMode = sqlite3_column_int(stmt.get(), 0);
        }
    });
    CHECK(journalMode == "wal");
    CHECK(syncMode == 1); // NORMAL

    constexpr int kThreads = 6;
    constexpr int kPerThread = 15;
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            try {
                for (int i = 0; i < kPerThread; ++i) {
                    NewEntry e = make_entry(
                        "2030.07.01",
                        "Concurrent entry t" + std::to_string(t) + " i" + std::to_string(i),
                        {"concurrency"});
                    auto added = db->add({std::move(e)});
                    if (added.added.size() != 1) {
                        failures.fetch_add(1);
                    }
                    auto found = db->search(SearchFilters{.query = "Concurrent"});
                    if (found.empty()) {
                        failures.fetch_add(1);
                    }
                    (void)db->runs(5);
                }
            } catch (...) {
                failures.fetch_add(1);
            }
        });
    }
    for (auto &w : workers)
        w.join();

    CHECK(failures.load() == 0);
    auto countRes = db->sql("SELECT COUNT(*) AS cnt FROM entries");
    REQUIRE(countRes.rows.size() == 1);
    CHECK(countRes.rows[0].at("cnt").get<int64_t>() == kThreads * kPerThread);
}

TEST_CASE("McpHttpServer handles concurrent HTTP requests against file-backed AppDb without data races") {
    slimr::testing::TempDirGuard temp("plog_http_conc_");
    auto db = std::make_shared<AppDb>((temp.path / "log.db").string());
    REQUIRE(db->add({make_entry("2030.08.01", "Seeded entry", {"seed"})}).added.size() == 1);

    auto app = make_personal_log_app(db);
    slimr::McpHttpServer server(
        app,
        slimr::HttpServerOptions{
            .host = "127.0.0.1",
            .port = 0,
        });
    std::string err;
    REQUIRE(server.start(&err));
    uint16_t port = server.boundPort();
    REQUIRE(port > 0);

    constexpr int kClients = 8;
    constexpr int kRequestsPerClient = 8;
    std::atomic<int> failures{0};
    std::vector<std::thread> clients;
    clients.reserve(kClients);

    for (int c = 0; c < kClients; ++c) {
        clients.emplace_back([&, c]() {
            try {
                for (int r = 0; r < kRequestsPerClient; ++r) {
                    int id = c * 100 + r + 1;
                    HttpReply reply;
                    if (r % 4 == 0) {
                        reply = post_mcp(port, make_mcp_req(id, "server/discover"));
                    } else if (r % 4 == 1) {
                        slimr::Json p = slimr::json_obj();
                        p["name"] = "search";
                        p["arguments"] = slimr::Json::object_t{{"query", "Seeded"}};
                        reply = post_mcp(port, make_mcp_req(id, "tools/call", std::move(p)));
                    } else if (r % 4 == 2) {
                        slimr::Json p = slimr::json_obj();
                        p["name"] = "sql";
                        p["arguments"] = slimr::Json::object_t{
                            {"query", "SELECT COUNT(*) AS cnt FROM entries"}};
                        reply = post_mcp(port, make_mcp_req(id, "tools/call", std::move(p)));
                    } else {
                        slimr::Json entry = slimr::json_obj();
                        entry["date"] = "2030-08-02";
                        entry["title"] = "HTTP conc c" + std::to_string(c) + " r" + std::to_string(r);
                        entry["tags"] = slimr::Json::array_t{"http-conc"};
                        slimr::Json p = slimr::json_obj();
                        p["name"] = "add";
                        p["arguments"] = slimr::Json::object_t{
                            {"entries", slimr::Json::array_t{entry}}};
                        reply = post_mcp(port, make_mcp_req(id, "tools/call", std::move(p)));
                    }
                    if (reply.status != 200) {
                        failures.fetch_add(1);
                        continue;
                    }
                    auto parsed = slimr::parse_json<slimr::Json>(reply.body);
                    if (!parsed.has_value() || parsed->contains("error")) {
                        failures.fetch_add(1);
                    }
                }
            } catch (...) {
                failures.fetch_add(1);
            }
        });
    }
    for (auto &client : clients)
        client.join();
    server.stop();

    CHECK(failures.load() == 0);
    // 1 seeded + 8 clients * 2 add requests (r = 3, 7) = 17
    auto countRes = db->sql("SELECT COUNT(*) AS cnt FROM entries");
    REQUIRE(countRes.rows.size() == 1);
    CHECK(countRes.rows[0].at("cnt").get<int64_t>() == 1 + kClients * 2);
}

namespace {

class SlowFakeToolsRunner : public ToolRunner {
  public:
    bool pauseOnGoogle = false;
    std::mutex mu;
    std::condition_variable cv;
    bool enteredGoogle = false;
    bool allowContinue = false;

    void waitEntered() {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [&] { return enteredGoogle; });
    }

    void release() {
        std::lock_guard<std::mutex> lock(mu);
        allowContinue = true;
        cv.notify_all();
    }

    slimr::Json google(const slimr::Json &request) override {
        if (pauseOnGoogle) {
            std::unique_lock<std::mutex> lock(mu);
            enteredGoogle = true;
            cv.notify_all();
            cv.wait(lock, [&] { return allowContinue; });
        }
        std::string service = request.contains("service") ? request.at("service").get<std::string>() : "";
        if (service == "drive") {
            return slimr::Json::object_t{{"files", slimr::Json::array_t{}}};
        }
        if (service == "gmail") {
            return slimr::Json::object_t{{"messages", slimr::Json::array_t{}}};
        }
        return slimr::Json::object_t{{"items", slimr::Json::array_t{}}};
    }

    slimr::Json driveUpload(
        const std::string &, const std::string &, const std::string &, const std::string &) override {
        return slimr::Json::object_t{{"id", "drv-test"}};
    }

    std::string gmailAttachment(
        const std::string &, const std::string &, const std::string &) override {
        return "";
    }

    GmailMessage gmailMessage(const std::string &id) override {
        return GmailMessage{.id = id};
    }

    std::vector<WhatsAppChat> whatsappChats() override { return {}; }

    std::vector<WhatsAppMessage> whatsappExport(
        const std::vector<std::string> &, const std::string &, const std::string &) override {
        return {};
    }

    slimr::Json obsidian(const std::string &, const slimr::Json &) override {
        return slimr::Json::object_t{};
    }

    LlmReply llm(
        const std::string &, const std::string &, const std::string &, const std::string &) override {
        return LlmReply{.text = "{}"};
    }

    std::optional<std::vector<double>> jev(const std::vector<JevQuestion> &) override {
        return std::nullopt;
    }
};

struct ScopedMineConfigEnv {
    std::optional<std::string> prev;
    explicit ScopedMineConfigEnv(const std::string &path) {
        if (const char *old = std::getenv("PERSONAL_LOG_MINE_CONFIG")) {
            prev = old;
        }
        ::setenv("PERSONAL_LOG_MINE_CONFIG", path.c_str(), 1);
    }
    ~ScopedMineConfigEnv() {
        if (prev.has_value()) {
            ::setenv("PERSONAL_LOG_MINE_CONFIG", prev->c_str(), 1);
        } else {
            ::unsetenv("PERSONAL_LOG_MINE_CONFIG");
        }
    }
    ScopedMineConfigEnv(const ScopedMineConfigEnv &) = delete;
    ScopedMineConfigEnv &operator=(const ScopedMineConfigEnv &) = delete;
};

struct ScopedToolRunner {
    explicit ScopedToolRunner(std::shared_ptr<ToolRunner> r) { setTools(std::move(r)); }
    ~ScopedToolRunner() { setTools(nullptr); }
    ScopedToolRunner(const ScopedToolRunner &) = delete;
    ScopedToolRunner &operator=(const ScopedToolRunner &) = delete;
};

inline std::string write_disabled_schedule_mine_config(const std::filesystem::path &dir) {
    auto cfgPath = dir / "mine.json";
    std::ofstream out(cfgPath);
    out << R"json({
  "calendars": {"source1@example.com": "Example"},
  "suggestCalendarId": "suggestions@example.com",
  "deadlinesTasklist": "example-deadlines",
  "whatsappChats": [],
  "keepDomains": [],
  "gmailAccount": "alex@example.com",
  "medicalExpensesNote": "Records/Example.md",
  "medicalExpensesSince": "2030.01.01",
  "medsNotes": {},
  "kidsDrive": {},
  "kidsSchoolFolder": "School",
  "contactGroups": [],
  "whoNames": {"alex": "Alex"},
  "familyAliases": ["alex"],
  "familySurname": "morgan",
  "selfName": "Alex",
  "timeZone": "Etc/UTC",
  "eventSuggestionNote": "Suggested",
  "deadlineNote": "(added)",
        "schedule" : { "enabled" : false, "time" : "06:00", "backup" : false }
})json";
    out.close();
    return cfgPath.string();
}

} // namespace

TEST_CASE("mine and backup are single-flight off execMutex and emit progress over NDJSON and MCP") {
    slimr::testing::TempDirGuard temp("plog_single_flight_");
    ScopedMineConfigEnv cfgEnv(write_disabled_schedule_mine_config(temp.path));

    auto db = std::make_shared<AppDb>((temp.path / "log.db").string());
    REQUIRE(db->add({make_entry("2030.09.01", "Non-blocked search target", {"sf"})}).added.size() == 1);

    auto runner = std::make_shared<SlowFakeToolsRunner>();
    runner->pauseOnGoogle = true;
    ScopedToolRunner runnerGuard(runner);

    auto app = make_personal_log_app(db);
    slimr::McpHttpServer server(
        app,
        slimr::HttpServerOptions{
            .host = "127.0.0.1",
            .port = 0,
        });
    std::string err;
    REQUIRE(server.start(&err));
    uint16_t port = server.boundPort();
    REQUIRE(port > 0);

    // 1. Slow mine in-flight: search stays unblocked, concurrent mine gets 409 / MCP error, NDJSON progress streams
    HttpReply firstMineReply;
    std::thread mineThread([&]() {
        firstMineReply = http_exchange(
            port,
            "POST",
            "/tools/mine",
            R"({"since":"2030-09-01","until":"2030-09-01","dryRun":true})",
            {{"Content-Type", "application/json"}, {"Accept", "application/x-ndjson"}});
    });

    runner->waitEntered();

    auto searchReply = http_exchange(
        port,
        "POST",
        "/tools/search",
        R"({"query":"Non-blocked"})",
        {{"Content-Type", "application/json"}});
    CHECK(searchReply.status == 200);
    CHECK(searchReply.body.find("Non-blocked search target") != std::string::npos);

    auto secondMineHttp = http_exchange(
        port,
        "POST",
        "/tools/mine",
        R"({"dryRun":true})",
        {{"Content-Type", "application/json"}});
    CHECK(secondMineHttp.status == 409);
    CHECK(secondMineHttp.body.find("already_running") != std::string::npos);

    slimr::Json mcpMineParams = slimr::json_obj();
    mcpMineParams["name"] = "mine";
    mcpMineParams["arguments"] = slimr::Json::object_t{{"dryRun", true}};
    auto secondMineMcp = post_mcp(port, make_mcp_req(42, "tools/call", std::move(mcpMineParams)));
    CHECK(secondMineMcp.status == 200);
    auto secondMineMcpJson = slimr::parse_json<slimr::Json>(secondMineMcp.body);
    REQUIRE(secondMineMcpJson.has_value());
    REQUIRE(secondMineMcpJson->contains("error"));
    CHECK((*secondMineMcpJson)["error"]["message"].get<std::string>().find("already_running") != std::string::npos);

    runner->release();
    mineThread.join();

    CHECK(firstMineReply.status == 200);
    CHECK(firstMineReply.body.find(R"("type":"progress")") != std::string::npos);
    CHECK(firstMineReply.body.find(R"("message":"collect")") != std::string::npos);
    CHECK(firstMineReply.body.find(R"("message":"triage")") != std::string::npos);
    CHECK(firstMineReply.body.find(R"("message":"done")") != std::string::npos);

    // 2. Concurrent backup returns 409 already_running
    {
        std::lock_guard<std::mutex> lock(runner->mu);
        runner->enteredGoogle = false;
        runner->allowContinue = false;
    }
    HttpReply firstBackupReply;
    std::thread backupThread([&]() {
        firstBackupReply = http_exchange(
            port,
            "POST",
            "/tools/backup",
            "{}",
            {{"Content-Type", "application/json"}});
    });
    runner->waitEntered();

    auto secondBackupHttp = http_exchange(
        port,
        "POST",
        "/tools/backup",
        "{}",
        {{"Content-Type", "application/json"}});
    CHECK(secondBackupHttp.status == 409);
    CHECK(secondBackupHttp.body.find("already_running") != std::string::npos);

    runner->release();
    backupThread.join();
    CHECK(firstBackupReply.status == 200);

    server.stop();

    // 3. dispatchMcpJson emits notifications/progress when _meta.progressToken is provided
    runner->pauseOnGoogle = false;
    std::vector<slimr::Json> notifs;
    slimr::RequestContext notifCtx;
    notifCtx.notificationChannel = std::make_shared<slimr::McpNotificationChannel>(
        [&](const slimr::Json &n) { notifs.push_back(n); });
    slimr::Json req = make_mcp_req(99, "tools/call", slimr::Json::object_t{
                                                         {"name", "mine"},
                                                         {"arguments", slimr::Json::object_t{{"since", "2030-09-01"}, {"until", "2030-09-01"}, {"dryRun", true}}},
                                                     });
    req["params"]["_meta"]["progressToken"] = "mine-pt-1";
    auto resp = app.dispatchMcpJson(req, notifCtx);
    CHECK(!resp.contains("error"));
    REQUIRE(notifs.size() >= 3);
    CHECK(notifs[0]["method"].get<std::string>() == "notifications/progress");
    CHECK(notifs[0]["params"]["progressToken"].get<std::string>() == "mine-pt-1");
    CHECK(notifs[0]["params"]["message"].get<std::string>() == "collect");
    CHECK(notifs.back()["params"]["message"].get<std::string>() == "done");
}

} // namespace personal_log::testing
