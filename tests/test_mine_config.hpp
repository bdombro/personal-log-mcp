#pragma once

#include "app.hpp"
#include "mine_config.hpp"
#include "test.hpp"

namespace personal_log::testing {

struct ScopedMineEnv {
    std::string name;
    std::optional<std::string> previous;

    ScopedMineEnv(std::string key, const std::optional<std::string>& value)
        : name(std::move(key)) {
        if (const char* old = std::getenv(name.c_str())) previous = old;
        if (value) setenv(name.c_str(), value->c_str(), 1);
        else unsetenv(name.c_str());
    }

    ~ScopedMineEnv() {
        if (previous) setenv(name.c_str(), previous->c_str(), 1);
        else unsetenv(name.c_str());
    }
};

inline void writeMineFixture(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
    file.close();
    REQUIRE(file.good());
}

TEST_SUITE("mining configuration") {
    TEST_CASE("selects explicit path before XDG and HOME without reading a file") {
        ScopedMineEnv path("PERSONAL_LOG_MINE_CONFIG", "/example/explicit.json");
        ScopedMineEnv xdg("XDG_CONFIG_HOME", "/example/config");
        ScopedMineEnv home("HOME", "/example/home");
        CHECK(mineConfigPath() == "/example/explicit.json");
        unsetenv("PERSONAL_LOG_MINE_CONFIG");
        CHECK(mineConfigPath() == "/example/config/personal-log/mine.json");
        unsetenv("XDG_CONFIG_HOME");
        CHECK(mineConfigPath() == "/example/home/.config/personal-log/mine.json");
    }

    TEST_CASE("loads configured sources and destinations with validated calendar override") {
        ScopedMineEnv overrideId("PERSONAL_LOG_SUGGEST_CALENDAR_ID", std::nullopt);
        const auto config = loadMineConfig();
        CHECK(config.gmailAccount == "alex@example.com");
        CHECK(config.selfName == "Alex");
        CHECK(config.whoNames.at("riley") == "Riley");
        CHECK(config.medsNotes.at("riley") == "People/Riley Morgan.md");
        CHECK(config.deadlinesTasklist == "example-deadlines");
        CHECK(suggestCalendarId(config) == "suggestions@example.com");
        setenv("PERSONAL_LOG_SUGGEST_CALENDAR_ID", "  override@example.com \n", 1);
        CHECK(suggestCalendarId(config) == "override@example.com");
        setenv("PERSONAL_LOG_SUGGEST_CALENDAR_ID", "source1@example.com", 1);
        CHECK_THROWS(suggestCalendarId(config));
        setenv("PERSONAL_LOG_SUGGEST_CALENDAR_ID", "primary", 1);
        CHECK_THROWS(suggestCalendarId(config));
    }

    TEST_CASE("rejects malformed, incomplete, mistyped and unsafe config without echoing content") {
        slimr::testing::TempDirGuard temp("mine_config_");
        const auto path = temp.path / "mine.json";
        const auto config = loadMineConfig();
        CHECK_THROWS(loadMineConfig(path));
        for (const auto& text : {"{private-marker", "{}", R"({"calendars":false})"}) {
            writeMineFixture(path, text);
            try {
                (void)loadMineConfig(path);
                FAIL("Invalid configuration accepted");
            } catch (const std::runtime_error& error) {
                CHECK(std::string(error.what()).find("private-marker") == std::string::npos);
            }
        }
        auto invalid = config;
        invalid.gmailAccount = " \t";
        writeMineFixture(path, slimr::write_json(invalid));
        CHECK_THROWS(loadMineConfig(path));
        invalid = config;
        invalid.medicalExpensesSince = "not-a-date";
        writeMineFixture(path, slimr::write_json(invalid));
        CHECK_THROWS(loadMineConfig(path));
        invalid = config;
        invalid.suggestCalendarId = "bad/id";
        writeMineFixture(path, slimr::write_json(invalid));
        CHECK_THROWS(loadMineConfig(path));
        auto unknown = slimr::write_json(config);
        unknown.insert(1, R"("unknownSetting":true,)");
        writeMineFixture(path, unknown);
        CHECK_THROWS(loadMineConfig(path));
    }

    TEST_CASE("missing and invalid config fail before database creation while discovery and status work") {
        slimr::testing::TempDirGuard temp("mine_missing_");
        const auto configPath = temp.path / "mine.json";
        const auto dbPath = temp.path / "data/log.db";
        ScopedMineEnv path("PERSONAL_LOG_MINE_CONFIG", configPath.string());
        ScopedMineEnv db("PERSONAL_LOG_DB", dbPath.string());
        auto app = make_personal_log_app();
        CHECK(app.tools().contains("mine"));
        slimr::Json request = slimr::Json::object_t{
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
            {"params", slimr::Json::object_t{
                {"name", "status"},
                {"arguments", slimr::Json::object_t{}},
                {"_meta", slimr::Json::object_t{
                    {"io.modelcontextprotocol/protocolVersion", std::string(slimr::kMcpProtocolVersion)},
                    {"io.modelcontextprotocol/clientCapabilities", slimr::json_obj()},
                }},
            }},
        };
        CHECK(!app.dispatchMcpJson(request).contains("error"));
        request["params"]["name"] = "mine";
        for (bool createInvalid : {false, true}) {
            if (createInvalid) writeMineFixture(configPath, "{}");
            const auto response = app.dispatchMcpJson(request);
            const auto result = slimr::write_json(response);
            CHECK(result.find("configuration") != std::string::npos);
            CHECK(!std::filesystem::exists(dbPath));
        }
    }

    TEST_CASE("private guides override the generic embedded resources independently of mining config") {
        slimr::testing::TempDirGuard temp("mine_guides_");
        ScopedMineEnv xdg("XDG_CONFIG_HOME", temp.path.string());
        ScopedMineEnv config("PERSONAL_LOG_MINE_CONFIG", (temp.path / "missing.json").string());
        const auto directory = temp.path / "personal-log";
        std::filesystem::create_directory(directory);
        writeMineFixture(directory / "conventions.md", "# Synthetic private conventions\n");
        writeMineFixture(directory / "recall.md", "# Synthetic private recall\n");
        auto app = make_personal_log_app();
        CHECK(app.resources[0].load() == "# Synthetic private conventions\n");
        CHECK(app.resources[1].load() == "# Synthetic private recall\n");
    }
}

} // namespace personal_log::testing
