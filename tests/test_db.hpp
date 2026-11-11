#pragma once

#include "app.hpp"
#include "db.hpp"
#include "import.hpp"
#include "test.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace personal_log::testing {

inline NewEntry make_entry(
    std::string date = "2030.11.04",
    std::string title = "Riley OT school observation",
    std::vector<std::string> tags = {"riley", "health/therapy"},
    std::vector<std::string> details = {},
    std::vector<Person> people = {},
    std::vector<Source> sources = {},
    std::vector<Link> links = {}
) {
    return NewEntry{
        .date = std::move(date),
        .title = std::move(title),
        .tags = std::move(tags),
        .details = std::move(details),
        .people = std::move(people),
        .sources = std::move(sources),
        .links = std::move(links)
    };
}

// ============================================================================
// 1. ftsQuery
// ============================================================================
TEST_SUITE("ftsQuery") {
    TEST_CASE("quotes words, keeps phrases, OR and prefixes") {
        CHECK(ftsQuery("ear \"tube surgery\" OR grommet*") == "\"ear\" \"tube surgery\" OR \"grommet\"*");
        CHECK(ftsQuery("OR a\" OR") == "\"a\"");
        CHECK(ftsQuery("   ") == "");
    }
}

// ============================================================================
// 2. AppDb core tests
// ============================================================================
TEST_SUITE("AppDb") {
    TEST_CASE("add, get, and skip entries whose source is already logged") {
        AppDb db(":memory:");
        auto first = db.add({
            make_entry("2030.11.04", "Riley OT school observation", {"riley", "health/therapy"},
                       {}, {Person{.name = "Ciara Baxter", .contact = std::nullopt}},
                       {Source{.kind = "gm", .id = "abc"}})
        });
        CHECK(first.added.size() == 1);

        auto again = db.add({
            make_entry("2030.11.04", "Dup", {"riley", "health/therapy"},
                       {}, {}, {Source{.kind = "gm", .id = "abc"}}),
            make_entry("2030.11.04", "New one")
        });
        CHECK(again.added.size() == 1);
        REQUIRE(again.skipped.size() == 1);
        CHECK(again.skipped[0].index == 0);
        CHECK(again.skipped[0].title == "Dup");
        CHECK(again.skipped[0].existingId == first.added[0]);
        CHECK(again.skipped[0].source == "gm:abc");

        auto got = db.get(first.added[0]);
        REQUIRE(got.has_value());
        CHECK(got->title == "Riley OT school observation");
        CHECK(got->tags == std::vector<std::string>{"riley", "health/therapy"});

        auto bySrc = db.getBySource(Source{.kind = "gm", .id = "abc"});
        REQUIRE(bySrc.has_value());
        CHECK(bySrc->id == first.added[0]);
    }

    TEST_CASE("search by keywords, tags (with prefix), person and dates") {
        AppDb db(":memory:");
        db.add({
            make_entry("2030.10.21", "Casey OT observation", {"casey", "health/therapy"}),
            make_entry("2030.11.04", "Riley OT school observation", {"riley", "health/therapy"},
                       {"At the school, with Ciara"}, {Person{.name = "Ciara Baxter", .contact = std::nullopt}}),
            make_entry("2029.03.01", "Dentist", {"riley", "health/dental"})
        });

        auto obs = db.search(SearchFilters{.query = "observation"});
        std::vector<std::string> obsDates;
        for (const auto& e : obs) obsDates.push_back(e.date);
        std::sort(obsDates.begin(), obsDates.end());
        CHECK(obsDates == std::vector<std::string>{"2030.10.21", "2030.11.04"});

        CHECK(db.search(SearchFilters{.query = "ciara"}).size() == 1);
        CHECK(db.search(SearchFilters{.tags = {"health"}}).size() == 3);
        CHECK(db.search(SearchFilters{.tags = {"riley", "health/dental"}}).size() == 1);
        CHECK(db.search(SearchFilters{.person = "Baxter"}).size() == 1);
        CHECK(db.search(SearchFilters{.from = "2030.01.01", .to = "2030.10.31"}).size() == 1);
        CHECK(db.search(SearchFilters{.to = "2030.11"}).size() == 3);
    }

    TEST_CASE("update, merge and delete keep the search index current") {
        AppDb db(":memory:");
        auto res = db.add({
            make_entry("2030.11.04", "Riley OT school observation", {"riley", "health/therapy"},
                       {}, {}, {Source{.kind = "cal", .id = "c1"}}),
            make_entry("2030.11.04", "OT visit at school", {"riley", "health/therapy"},
                       {"Report to follow"}, {}, {Source{.kind = "gm", .id = "g1"}})
        });
        REQUIRE(res.added.size() == 2);
        int64_t keep = res.added[0];
        int64_t dup = res.added[1];

        EntryPatch p1;
        p1.title = "Riley OT observation";
        p1.addTags = {"school"};
        p1.removeTags = {"health/therapy"};
        db.update(keep, p1);

        auto s1 = db.search(SearchFilters{.query = "school", .tags = {"school"}});
        REQUIRE(!s1.empty());
        CHECK(s1[0].id == keep);

        auto merged = db.merge(keep, dup);
        REQUIRE(merged.has_value());
        std::vector<std::string> sourceIds;
        for (const auto& s : merged->sources) sourceIds.push_back(s.id);
        std::sort(sourceIds.begin(), sourceIds.end());
        CHECK(sourceIds == std::vector<std::string>{"c1", "g1"});

        bool hasReport = false;
        for (const auto& l : merged->details) {
            if (l.find("Report to follow") != std::string::npos) hasReport = true;
        }
        CHECK(hasReport);
        CHECK(!db.get(dup).has_value());
        CHECK(db.search(SearchFilters{.query = "visit"}).empty());

        db.add({make_entry("2030.11.04", "Other", {}, {}, {}, {Source{.kind = "cal", .id = "zz"}})});
        EntryPatch pBad;
        pBad.addSources = {Source{.kind = "cal", .id = "zz"}};
        CHECK_THROWS_WITH(db.update(keep, pBad), doctest::Contains("already belongs"));

        CHECK(db.deleteEntry(keep) == true);
        CHECK(db.deleteEntry(keep) == false);

        auto remaining = db.search(SearchFilters{});
        REQUIRE(remaining.size() == 1);
        CHECK(remaining[0].title == "Other");
    }

    TEST_CASE("sql is read-only and limited") {
        slimr::testing::TempDirGuard temp("plog_test_");
        std::string dbPath = (temp.path / "log.db").string();
        AppDb fileDb(dbPath);
        fileDb.add({make_entry("2030.11.04", "Riley OT school observation"), make_entry("2030.11.04", "Second")});

        auto res = fileDb.sql("SELECT title FROM v_entries ORDER BY id", 1);
        CHECK(res.rows.size() == 1);
        CHECK(res.rows[0]["title"].get<std::string>() == "Riley OT school observation");
        CHECK(res.truncated == true);

        CHECK_THROWS_WITH(fileDb.sql("DELETE FROM entries"), doctest::Contains("Only SELECT"));
        CHECK_THROWS(fileDb.sql("WITH x AS (SELECT 1) DELETE FROM entries"));
        CHECK(fileDb.search(SearchFilters{}).size() == 2);
    }

    TEST_CASE("runs track the last successful window") {
        AppDb db(":memory:");
        int64_t id = db.runStart("2030.11.04", "2030.11.08");
        CHECK(!db.lastSuccessfulUntil().has_value());

        slimr::Json stats = slimr::Json::object_t{{"added", 3}};
        db.runFinish(id, "ok", stats);
        CHECK(db.lastSuccessfulUntil() == "2030.11.08");

        auto list = db.runs();
        REQUIRE(!list.empty());
        CHECK(list[0].stats["added"].get<int64_t>() == 3);
    }
}

// ============================================================================
// 3. parseYearNote
// ============================================================================
TEST_SUITE("parseYearNote") {
    TEST_CASE("parses entries, people, links, quotes, ids and month-only dates") {
        const std::string NOTE =
            "# 2030\n"
            "\n"
            "Family log for 2030. Tags and views: [[Log]].\n"
            "\n"
            "## June\n"
            "\n"
            "- 2030.06.03 Example medicine started, Example Clinic #alex #health/meds %% cal:abc gm:19e8 %%\n"
            "\t- First consultation with Dr Taylor Example\n"
            "\t- 👥 [[Taylor Example (Example Clinic)]], [[Jordan Example|Jordan]]\n"
            "\t- ✉️ [care plan](https://mail.google.com/mail/u/?authuser=x#all/19e8)\n"
            "\t- 📎 [2029.03.24 - Break schedule](https://drive.google.com/file/d/1/view)\n"
            "\t- 💬 2030.06.03 Jamie: \"Example attributed quote\"\n"
            "- 2028.07 Example shuttle booked #alex #trip\n"
            "stray text\n";

        auto parsed = parseYearNote(NOTE);
        REQUIRE(parsed.entries.size() == 2);

        const auto& meds = parsed.entries[0];
        CHECK(meds.date == "2030.06.03");
        CHECK(meds.title == "Example medicine started, Example Clinic");
        CHECK(meds.tags == std::vector<std::string>{"alex", "health/meds"});
        CHECK(meds.details == std::vector<std::string>{
            "First consultation with Dr Taylor Example",
            "💬 2030.06.03 Jamie: \"Example attributed quote\""
        });
        REQUIRE(meds.people.size() == 2);
        CHECK(meds.people[0].name == "Taylor Example (Example Clinic)");
        CHECK(!meds.people[0].contact.has_value());
        CHECK(meds.people[1].name == "Jordan Example");
        CHECK(!meds.people[1].contact.has_value());

        REQUIRE(meds.sources.size() == 2);
        CHECK(meds.sources[0].kind == "cal");
        CHECK(meds.sources[0].id == "abc");
        CHECK(meds.sources[1].kind == "gm");
        CHECK(meds.sources[1].id == "19e8");

        REQUIRE(meds.links.size() == 2);
        CHECK(meds.links[0].kind == "email");
        CHECK(meds.links[0].label == "care plan");
        CHECK(meds.links[0].url == "https://mail.google.com/mail/u/?authuser=x#all/19e8");
        CHECK(meds.links[1].kind == "file");
        CHECK(meds.links[1].label == "2029.03.24 - Break schedule");
        CHECK(meds.links[1].url == "https://drive.google.com/file/d/1/view");

        const auto& trip = parsed.entries[1];
        CHECK(trip.date == "2028.07");
        CHECK(trip.title == "Example shuttle booked");
        CHECK(trip.tags == std::vector<std::string>{"alex", "trip"});

        REQUIRE(parsed.unparsed.size() == 1);
        CHECK(parsed.unparsed[0].line == 14);
        CHECK(parsed.unparsed[0].text == "stray text");
    }
}

// ============================================================================
// 4. App & MCP Tools
// ============================================================================
TEST_SUITE("app tools and resources") {
    TEST_CASE("exports all required tools, resources, and status schema") {
        auto db = std::make_shared<AppDb>(":memory:");
        auto app = make_personal_log_app(db);

        CHECK(app.tools().contains("status"));
        CHECK(app.tools().contains("add"));
        CHECK(app.tools().contains("get"));
        CHECK(app.tools().contains("search"));
        CHECK(app.tools().contains("update"));
        CHECK(app.tools().contains("merge"));
        CHECK(app.tools().contains("delete"));
        CHECK(app.tools().contains("sql"));
        CHECK(app.tools().contains("schema"));
        CHECK(app.tools().contains("runs"));
        CHECK(app.tools().contains("import"));
        CHECK(app.tools().contains("backup"));
        CHECK(app.tools().contains("mine"));

        const auto& statusTool = app.tools().at("status");
        CHECK(statusTool.inputSchema.contains("properties"));
        CHECK(statusTool.inputSchema["properties"].contains("json"));

        REQUIRE(app.resources.size() == 2);
        CHECK(app.resources[0].uri == "personal-log://conventions");
        CHECK(!app.resources[0].load().empty());
        CHECK(app.resources[1].uri == "personal-log://recall");
        CHECK(!app.resources[1].load().empty());

        // Status tool call
        slimr::Json req = slimr::Json::object_t{};
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "tools/call";
        req["params"]["name"] = "status";
        req["params"]["arguments"] = slimr::Json::object_t{};
        auto resp = app.dispatchMcpJson(req);
        CHECK(!resp.contains("error"));
        CHECK(resp["result"]["content"][0]["text"].get<std::string>().find("2.0.0") != std::string::npos);

        // Schema tool call
        req["id"] = 2;
        req["params"]["name"] = "schema";
        resp = app.dispatchMcpJson(req);
        CHECK(!resp.contains("error"));
        CHECK(resp["result"]["content"][0]["text"].get<std::string>().find("v_entries") != std::string::npos);
    }

    TEST_CASE("MCP tool calls round-trip add and search") {
        auto db = std::make_shared<AppDb>(":memory:");
        auto app = make_personal_log_app(db);

        slimr::Json addReq = slimr::Json::object_t{};
        addReq["jsonrpc"] = "2.0";
        addReq["id"] = 10;
        addReq["method"] = "tools/call";
        addReq["params"]["name"] = "add";
        slimr::Json newEntry = slimr::Json::object_t{};
        newEntry["date"] = "2030-11-04";
        newEntry["title"] = "Test MCP Add";
        newEntry["tags"] = slimr::Json::array_t{"#test", "family"};
        newEntry["details"] = slimr::Json::array_t{"Some detail line"};
        addReq["params"]["arguments"] = slimr::Json::object_t{
            {"entries", slimr::Json::array_t{newEntry}}
        };

        auto resp = app.dispatchMcpJson(addReq);
        CHECK(!resp.contains("error"));
        CHECK(resp["result"]["content"][0]["text"].get<std::string>().find("\"added\":[1]") != std::string::npos);

        slimr::Json searchReq = slimr::Json::object_t{};
        searchReq["jsonrpc"] = "2.0";
        searchReq["id"] = 11;
        searchReq["method"] = "tools/call";
        searchReq["params"]["name"] = "search";
        searchReq["params"]["arguments"] = slimr::Json::object_t{
            {"query", "Test MCP"}
        };
        auto searchResp = app.dispatchMcpJson(searchReq);
        CHECK(!searchResp.contains("error"));
        CHECK(searchResp["result"]["content"][0]["text"].get<std::string>().find("Test MCP Add") != std::string::npos);
    }

    TEST_CASE("import tool with dryRun and execution") {
        slimr::testing::TempDirGuard temp("plog_import_");
        std::string notePath = (temp.path / "2030.md").string();
        std::ofstream out(notePath);
        out << "- 2030.05.01 Test May Event #alex #activity\n"
            << "\t- Detail test\n"
            << "- 2030.05.02 Second Event #jamie\n";
        out.close();

        auto db = std::make_shared<AppDb>(":memory:");
        auto app = make_personal_log_app(db);

        // dry-run
        slimr::Json req = slimr::Json::object_t{};
        req["jsonrpc"] = "2.0";
        req["id"] = 20;
        req["method"] = "tools/call";
        req["params"]["name"] = "import";
        req["params"]["arguments"] = slimr::Json::object_t{
            {"dir", temp.path.string()},
            {"dryRun", true}
        };
        auto resp = app.dispatchMcpJson(req);
        CHECK(!resp.contains("error"));
        auto parsed = slimr::parse_json<slimr::Json>(resp["result"]["content"][0]["text"].get<std::string>());
        REQUIRE(parsed.has_value());
        CHECK((*parsed)["files"].get<int64_t>() == 1);
        CHECK((*parsed)["entries"].get<int64_t>() == 2);
        CHECK((*parsed)["imported"].get<int64_t>() == 0);
        CHECK(db->search(SearchFilters{}).empty());

        // execution
        req["id"] = 21;
        req["params"]["arguments"]["dryRun"] = false;
        resp = app.dispatchMcpJson(req);
        CHECK(!resp.contains("error"));
        parsed = slimr::parse_json<slimr::Json>(resp["result"]["content"][0]["text"].get<std::string>());
        REQUIRE(parsed.has_value());
        CHECK((*parsed)["imported"].get<int64_t>() == 2);
        CHECK(db->search(SearchFilters{}).size() == 2);
    }
}

} // namespace personal_log::testing
