#pragma once

#include "mine.hpp"
#include "db.hpp"
#include "test.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <regex>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

namespace personal_log::testing {

// ============================================================================
// Test HTTP Server for fetchWithRetry
// ============================================================================

struct TestHttpServer {
    int server_fd = -1;
    int port = 0;
    std::thread worker;
    std::atomic<bool> stop_flag{false};

    TestHttpServer(std::function<std::string(int hit, const std::string& req)> handler) {
        server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(server_fd >= 0);
        int opt = 1;
        ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        REQUIRE(::bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);
        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(server_fd, (struct sockaddr*)&addr, &len) == 0);
        port = ntohs(addr.sin_port);
        REQUIRE(::listen(server_fd, 10) == 0);

        worker = std::thread([this, handler = std::move(handler)]() {
            int hit = 0;
            while (!stop_flag.load()) {
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 50000; // 50ms
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(server_fd, &rfds);
                int sel = ::select(server_fd + 1, &rfds, nullptr, nullptr, &tv);
                if (sel <= 0) continue;
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                int client_fd = ::accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) continue;
                hit++;
                char buf[1024];
                ssize_t n = ::read(client_fd, buf, sizeof(buf) - 1);
                std::string req(buf, n > 0 ? n : 0);
                std::string resp = handler(hit, req);
                (void)::write(client_fd, resp.data(), resp.size());
                ::close(client_fd);
            }
        });
    }

    ~TestHttpServer() {
        stop_flag.store(true);
        if (worker.joinable()) worker.join();
        if (server_fd >= 0) ::close(server_fd);
    }
};

// ============================================================================
// 1. modelFor
// ============================================================================
TEST_SUITE("modelFor") {
    TEST_CASE("modelFor: triage uses the fast model, other steps the default; env overrides; fallbacks") {
        const std::vector<const char*> names = {
            "ASSISTANT_LLM_DEFAULT",
            "ASSISTANT_LLM_FAST",
            "ASSISTANT_LLM_DEFAULT_FALLBACK",
            "ASSISTANT_LLM_FAST_FALLBACK",
        };
        std::map<std::string, std::optional<std::string>> saved;
        for (auto name : names) {
            const char* v = std::getenv(name);
            saved[name] = v ? std::optional<std::string>(v) : std::nullopt;
            unsetenv(name);
        }

        struct RestoreEnv {
            std::map<std::string, std::optional<std::string>>& s;
            ~RestoreEnv() {
                for (const auto& [k, v] : s) {
                    if (v) setenv(k.c_str(), v->c_str(), 1);
                    else unsetenv(k.c_str());
                }
            }
        } restore{saved};

        auto triage = modelFor("triage");
        CHECK(triage.model == DEFAULT_LLM.fast);
        CHECK(triage.fallback == std::nullopt);
        CHECK(triage.effort == "off");

        auto draft = modelFor("draft");
        CHECK(draft.model == DEFAULT_LLM.defaultModel);
        CHECK(draft.fallback == std::nullopt);
        CHECK(draft.effort == "low");

        setenv("ASSISTANT_LLM_DEFAULT", "gemini-flash-latest", 1);
        setenv("ASSISTANT_LLM_FAST", "gemini-flash-lite-latest", 1);

        auto triage2 = modelFor("triage");
        CHECK(triage2.model == "gemini-flash-lite-latest");
        CHECK(triage2.fallback == "deepseek-flash");
        CHECK(triage2.effort == "off");

        auto contacts = modelFor("contacts");
        CHECK(contacts.model == "gemini-flash-latest");
        CHECK(contacts.fallback == "deepseek-v4-pro");
        CHECK(contacts.effort == "low");

        setenv("ASSISTANT_LLM_FAST_FALLBACK", "none", 1);
        CHECK(modelFor("triage").fallback == std::nullopt);
    }
}

// ============================================================================
// 2. fetchWithRetry
// ============================================================================
TEST_SUITE("fetchWithRetry") {
    TEST_CASE("fetchWithRetry retries 5xx then returns the success") {
        std::atomic<int> hits{0};
        TestHttpServer srv([&](int h, const std::string&) -> std::string {
            hits.store(h);
            if (h < 3) {
                return "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 4\r\n\r\nbusy";
            }
            return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
        });
        slimr::HttpRequest req;
        req.method = "POST";
        auto res = fetchWithRetry("http://127.0.0.1:" + std::to_string(srv.port) + "/", req, 5000, {1, 1});
        CHECK(res.status == 200);
        CHECK(res.body == "ok");
        CHECK(hits.load() == 3);
    }

    TEST_CASE("fetchWithRetry gives up after the last attempt on network errors") {
        slimr::HttpRequest req;
        req.method = "POST";
        CHECK_THROWS(fetchWithRetry("http://127.0.0.1:1/", req, 2000, {1, 1}));
    }

    TEST_CASE("fetchWithRetry returns 4xx without retrying") {
        std::atomic<int> hits{0};
        TestHttpServer srv([&](int h, const std::string&) -> std::string {
            hits.store(h);
            return "HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\n\r\nno";
        });
        slimr::HttpRequest req;
        req.method = "POST";
        auto res = fetchWithRetry("http://127.0.0.1:" + std::to_string(srv.port) + "/", req, 5000, {1, 1});
        CHECK(res.status == 401);
        CHECK(res.body == "no");
        CHECK(hits.load() == 1);
    }
}

// ============================================================================
// 3. childEnv
// ============================================================================
TEST_SUITE("childEnv") {
    TEST_CASE("childEnv passes only PATH, HOME and the listed keys") {
        std::map<std::string, std::string> source = {
            {"PATH", "/bin"},
            {"HOME", "/h"},
            {"DEEPSEEK_API_KEY", "secret"},
            {"JEV_API_KEY", "s2"},
            {"GWS_PROFILE", "p"}
        };
        auto empty = childEnv({}, source);
        CHECK(empty.size() == 2);
        CHECK(empty["PATH"] == "/bin");
        CHECK(empty["HOME"] == "/h");

        auto google = childEnv(CHILD_ENV_KEYS.google, source);
        CHECK(google.size() == 3);
        CHECK(google["PATH"] == "/bin");
        CHECK(google["HOME"] == "/h");
        CHECK(google["GWS_PROFILE"] == "p");

        auto agy = childEnv(CHILD_ENV_KEYS.agy, source);
        CHECK(agy.size() == 2);
        CHECK(agy["PATH"] == "/bin");
        CHECK(agy["HOME"] == "/h");
    }
}

// ============================================================================
// 4. warningCategory
// ============================================================================
TEST_SUITE("warningCategory") {
    TEST_CASE("warningCategory keeps only the category, never ids, titles or message text") {
        CHECK(warningCategory("gmail 19abc: Request failed for jane@example.com") == "gmail");
        CHECK(warningCategory("deadline \"Pay Taylor Example\": boom") == "deadline");
        CHECK(warningCategory("triage line rejected: {...}") == "triage line rejected");
        CHECK(warningCategory("kids' documents: Riley/x.pdf: fail") == "kids' documents");
        CHECK(warningCategory("whatsapp chat not found: Example class chat") == "whatsapp chat not found");
        CHECK(warningCategory("draft updated missing entry #12") == "draft updated missing entry");
        CHECK(warningCategory("JEV_API_KEY not set") == "warning");
    }
}

// ============================================================================
// 5. scrub
// ============================================================================
TEST_SUITE("scrub") {
    TEST_CASE("scrub drops URLs along with emails, phones and secrets") {
        CHECK(scrub("Met at school, see https://evil.example/x?a=1 and www.evil.example, mail scrub@example.com") == "Met at school, see and mail");
        CHECK(scrub("Call +353 87 123 4567 now") == "Call now");
    }
}

// ============================================================================
// 6. sanitizers
// ============================================================================
TEST_SUITE("sanitizers") {
    TEST_CASE("sanitizeLinks keeps https only") {
        std::vector<std::string> warnings;
        std::vector<Link> input = {
            Link{.kind = "link", .label = "a", .url = "https://a.example"},
            Link{.kind = "link", .label = "b", .url = "http://b.example"},
            Link{.kind = "link", .label = "c", .url = "not a url"},
            Link{.kind = "link", .label = "d", .url = "data:text/html,x"},
        };
        auto kept = sanitizeLinks(input, warnings);
        REQUIRE(kept.size() == 1);
        CHECK(kept[0].label == "a");
        CHECK(warnings.size() == 3);
    }

    TEST_CASE("sanitizeContact accepts people/c<digits> only") {
        std::vector<std::string> warnings;
        CHECK(sanitizeContact("people/c1234567890", warnings) == "people/c1234567890");
        CHECK(sanitizeContact(std::nullopt, warnings) == std::nullopt);
        CHECK(sanitizeContact("", warnings) == std::nullopt);

        std::vector<std::string> bad = {
            "people/c12x", "people/12", "https://x.example", "people/c1\nmore", "contacts/c1"
        };
        for (const auto& b : bad) {
            CHECK(sanitizeContact(b, warnings) == std::nullopt);
        }
        CHECK(warnings.size() == 5);
    }
}

// ============================================================================
// 7. expenseRow
// ============================================================================
TEST_SUITE("expenseRow") {
    TEST_CASE("expenseRow strips newlines and control characters from every text field") {
        using namespace std::string_literals;
        TriageExpense exp{
            .date = "2030.11.05",
            .who = "Riley",
            .provider = "Dr\nX\r|\0Y\x1b[31m"s,
            .item = "Line1\n| evil | row |\nLine2",
            .amount = 10.0,
            .currency = "EUR",
            .qualifies = "yes",
        };
        TriageItem item{
            .date = "2030.11.05",
            .title = "T",
            .ids = TriageIds{.gm = "19ab"},
            .expense = exp,
        };
        auto r = expenseRow(item);
        REQUIRE(r.has_value());
        CHECK(r->provider == "Dr X / Y [31m");
        CHECK(r->item == "Line1 / evil / row / Line2");

        std::string line = formatExpenseRow(*r);
        for (char c : r->provider) {
            CHECK(static_cast<unsigned char>(c) >= 32);
            CHECK(static_cast<unsigned char>(c) != 127);
        }
        for (char c : r->item) {
            CHECK(static_cast<unsigned char>(c) >= 32);
            CHECK(static_cast<unsigned char>(c) != 127);
        }
        for (char c : line) {
            CHECK(static_cast<unsigned char>(c) >= 32);
            CHECK(static_cast<unsigned char>(c) != 127);
        }
    }
}

// ============================================================================
// 8. addExpenseRows & replaceMedsSection
// ============================================================================

const std::string HEADER = "| Date | Who | Provider | Item | Amount (€) | Reimbursed (€) | Qualifies | Claimed | Source |";
const std::string EXPENSES =
    std::string("# Medical Expenses\n\n## 2030\n") + HEADER + "\n" +
    "|---|---|---|---|---|---|---|---|---|\n" +
    "| 2030.10.22 | Riley | Example Clinic (Taylor Example, OT) | OT review | 350.00 |  | ask | no | [✉️](x) |\n" +
    "| 2030.11.20 | Alex | Example General Clinic | GP consult | 60.00 |  | yes | no | [✉️](y) |\n\n" +
    "**Totals:** qualifying €60.00; ask €350.00; not qualifying €0.00; estimated relief 20% of qualifying = €12.00 (€82.00 if the OT items are accepted). Reimbursements not deducted.\n\n" +
    "### Likely missing\n- stuff\n\n## 2029\n" + HEADER + "\n" +
    "|---|---|---|---|---|---|---|---|---|\n" +
    "| 2029.10.30 | Alex | Example General Clinic | GP consult | 55.00 |  | yes | no | [✉️](z) |\n\n" +
    "**Totals:** qualifying €55.00; ask €0.00; not qualifying €0.00; estimated relief 20% of qualifying = €11.00 (€11.00 if the retainers are accepted).\n\n" +
    "## Questions\n1. Who claims?\n";

const std::string RILEY =
    "# Riley Morgan\n\n## Meds\n\n### Current\n- Example medicine A\n\n"
    "# Meds Former\n- Example medicine B\n\n## Doctors\nSee Example clinicians\n";

inline ExpenseRow make_expense_row(
    std::string date = "2030.11.05",
    std::string who = "Riley",
    std::string provider = "Example Clinic (Taylor Example, OT)",
    std::string item = "OT session",
    double amount = 120.0,
    std::string qualifies = "ask",
    std::string source = "[✉️](https://mail.google.com/mail/u/?authuser=alex@example.com#all/t9)"
) {
    return ExpenseRow{
        .date = std::move(date),
        .who = std::move(who),
        .provider = std::move(provider),
        .item = std::move(item),
        .amount = amount,
        .qualifies = std::move(qualifies),
        .source = std::move(source)
    };
}

TEST_SUITE("addExpenseRows") {
    TEST_CASE("inserts in date order and recomputes the year's totals") {
        auto r1 = make_expense_row();
        auto r2 = make_expense_row("2030.11.05", "Riley", "Pharmacy", "OT session", 120.0, "yes", "WhatsApp 2030.11.05");
        auto res = addExpenseRows(EXPENSES, {r1, r2});
        CHECK(res.added.size() == 2);

        std::vector<std::string> lines;
        std::istringstream ss(res.content);
        std::string l;
        while (std::getline(ss, l)) lines.push_back(l);

        int idx_added = -1, idx_before = -1, idx_after = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            if (lines[i].starts_with("| 2030.11.05 | Riley | Example Clinic")) idx_added = i;
            if (lines[i].starts_with("| 2030.10.22")) idx_before = i;
            if (lines[i].starts_with("| 2030.11.20")) idx_after = i;
        }
        CHECK(idx_added > idx_before);
        CHECK(idx_added < idx_after);

        std::regex ot_re(R"(\| OT session \| 120\.00 \|  \| ask \| no \| \[✉️\])");
        CHECK(std::regex_search(res.content, ot_re));

        std::regex tot_re(R"(\*\*Totals:\*\* qualifying €180\.00; ask €470\.00; not qualifying €0\.00; estimated relief 20% of qualifying = €36\.00 \(€130\.00 if the OT items are accepted\)\. Reimbursements not deducted\.)");
        CHECK(std::regex_search(res.content, tot_re));

        CHECK(res.content.find("qualifying €55.00; ask €0.00") != std::string::npos);
    }

    TEST_CASE("skips a payment already listed (same date, who, provider, amount)") {
        auto r = make_expense_row("2030.10.22", "Riley", "Example Clinic", "OT review", 350.0);
        auto res = addExpenseRows(EXPENSES, {r});
        CHECK(res.added.size() == 0);
        CHECK(res.skipped.size() == 1);
    }

    TEST_CASE("starts a new year table above older years") {
        auto r = make_expense_row("2031.01.10", "Riley", "Example Clinic (Taylor Example, OT)", "OT session", 1000.0, "yes");
        auto res = addExpenseRows(EXPENSES, {r});

        std::vector<std::string> lines;
        std::istringstream ss(res.content);
        std::string l;
        while (std::getline(ss, l)) lines.push_back(l);

        int idx_2031 = -1, idx_2030 = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            if (lines[i].starts_with("## 2031")) idx_2031 = i;
            if (lines[i].starts_with("## 2030")) idx_2030 = i;
        }
        CHECK(idx_2031 < idx_2030);

        std::regex row_re(R"(\| 2031\.01\.10 \| Riley .* \| 1,000\.00 \|)");
        CHECK(std::regex_search(res.content, row_re));

        std::regex tot_re(R"(qualifying €1,000\.00; ask €0\.00; not qualifying €0\.00; .* = €200\.00 \(€200\.00 if)");
        CHECK(std::regex_search(res.content, tot_re));
    }
}

TEST_SUITE("replaceMedsSection") {
    TEST_CASE("replaces only the Meds section") {
        std::string out = replaceMedsSection(RILEY, "## Meds\n\n### Current\n- Example medicine C\n");
        CHECK(out.find("Example medicine C") != std::string::npos);
        CHECK(out.find("Example medicine B") == std::string::npos);
        CHECK(out.find("## Doctors\nSee Example clinicians") != std::string::npos);
    }

    TEST_CASE("folds a following '# Meds Former' heading in") {
        std::string note = "## Meds\n- Example medicine 2\n\n# Meds Former\n- Example medicine B\n\n## Health Log\nquery\n";
        std::string out = replaceMedsSection(note, "## Meds\n\n### Current\n- Example medicine 2\n\n### Prior\n- Example medicine B\n");
        std::string expected = "## Meds\n\n### Current\n- Example medicine 2\n\n### Prior\n- Example medicine B\n\n## Health Log\nquery\n";
        CHECK(out == expected);
    }

    TEST_CASE("throws without a Meds section") {
        CHECK_THROWS_WITH_AS(replaceMedsSection("# Note\n", "## Meds\n"), "no ## Meds section", std::runtime_error);
    }
}

// ============================================================================
// 9. prune
// ============================================================================
TEST_SUITE("prune") {
    TEST_CASE("keeps 30 newest and one per month for 12 months") {
        std::vector<std::string> files;
        for (int m = 1; m <= 12; m++) {
            for (int d = 1; d <= 28; d += 9) {
                char buf[64];
                snprintf(buf, sizeof(buf), "log-2030-%02d-%02d.db.gz", m, d);
                files.push_back(buf);
            }
        }
        auto gone = prune(files);
        CHECK(files.size() - gone.size() == 34); // 30 newest plus newest of Jan-Apr
        CHECK(std::find(gone.begin(), gone.end(), "log-2030-01-28.db.gz") == gone.end()); // newest of Jan kept
        CHECK(std::find(gone.begin(), gone.end(), "log-2030-01-01.db.gz") != gone.end());
    }
}

// ============================================================================
// 10. Fake World & FollowUp Tools Runner
// ============================================================================

struct World {
    std::map<std::string, std::string> notes;
    std::vector<std::string> writes;
    std::vector<slimr::Json> google;
    struct Upload {
        std::string name;
        std::string folder;
    };
    std::vector<Upload> uploads;
    std::vector<std::string> attachments;
    std::vector<std::string> driveFiles;
    struct Group {
        std::string resourceName;
        std::string name;
    };
    std::vector<Group> groups;
};

inline World newWorld() {
    World w;
    w.notes["Records/Example expenses.md"] = EXPENSES;
    w.notes["People/Riley Morgan.md"] = RILEY;
    w.groups.push_back(World::Group{.resourceName = "contactGroups/friends", .name = "Example group 2"});
    return w;
}

class FollowUpToolsRunner : public ToolRunner {
public:
    World& world;
    slimr::Json triageExtra;
    std::function<slimr::Json(const std::string&, const std::string&, const std::string&, const std::string&)> customDriveUpload;

    FollowUpToolsRunner(World& w, slimr::Json te = slimr::Json::object_t{})
        : world(w), triageExtra(std::move(te)) {}

    slimr::Json google(const slimr::Json& req) override {
        world.google.push_back(req);
        std::string service = req.contains("service") ? req.at("service").get<std::string>() : "";
        std::string method = req.contains("method") ? req.at("method").get<std::string>() : "";
        std::string resStr;
        if (req.contains("resource") && req.at("resource").is_array()) {
            for (size_t i = 0; i < req.at("resource").get<slimr::Json::array_t>().size(); ++i) {
                if (i > 0) resStr += ".";
                resStr += req.at("resource").get<slimr::Json::array_t>()[i].get<std::string>();
            }
        }
        std::string key = service + "." + resStr + "." + method;
        slimr::Json params = req.contains("params") ? req.at("params") : slimr::Json::object_t{};

        if (key == "gmail.users.messages.list") {
            return slimr::Json{
                {"pages", slimr::Json::array_t{
                    slimr::Json{{"messages", slimr::Json::array_t{
                        slimr::Json{{"id", "m1"}, {"threadId", "a1"}}
                    }}}
                }}
            };
        }
        if (key == "people.people.connections.list") {
            if (params.contains("pageToken") && !params.at("pageToken").get<std::string>().empty()) {
                return slimr::Json{
                    {"connections", slimr::Json::array_t{
                        slimr::Json{{"resourceName", "people/c3"}, {"names", slimr::Json::array_t{slimr::Json{{"displayName", "Synthetic6 Synthetic8"}}}}},
                        slimr::Json{{"resourceName", "people/c4"}, {"names", slimr::Json::array_t{slimr::Json{{"displayName", "Synthetic2 Synthetic10 Synthetic1"}}}}},
                        slimr::Json{{"resourceName", "people/c5"}, {"names", slimr::Json::array_t{slimr::Json{{"displayName", "Synthetic5 (Example Clinic)"}}}}}
                    }}
                };
            }
            return slimr::Json{
                {"connections", slimr::Json::array_t{
                    slimr::Json{{"resourceName", "people/c1"}, {"names", slimr::Json::array_t{slimr::Json{{"displayName", "Taylor Example"}}}}},
                    slimr::Json{{"resourceName", "people/c2"}, {"names", slimr::Json::array_t{slimr::Json{{"displayName", "Synthetic4"}}}}}
                }},
                {"nextPageToken", "p2"}
            };
        }
        if (key == "people.contactGroups.list") {
            slimr::Json::array_t grps;
            for (const auto& g : world.groups) {
                grps.push_back(slimr::Json{{"resourceName", g.resourceName}, {"name", g.name}});
            }
            return slimr::Json{{"contactGroups", grps}};
        }
        if (key == "people.contactGroups.create") {
            std::string name = req.at("body").at("contactGroup").at("name").get<std::string>();
            world.groups.push_back({.resourceName = "contactGroups/" + name, .name = name});
            return slimr::Json{{"resourceName", "contactGroups/" + name}};
        }
        if (key == "people.people.createContact") {
            return slimr::Json{{"resourceName", "people/new1"}};
        }
        if (key == "drive.files.list") {
            std::string q = params.contains("q") ? params.at("q").get<std::string>() : "";
            if (q.find("vnd.google-apps.folder") != std::string::npos) {
                std::string folderId = "id-root";
                size_t q1 = q.find('\'');
                if (q1 != std::string::npos) {
                    size_t q2 = q.find('\'', q1 + 1);
                    if (q2 != std::string::npos) {
                        folderId = "id-" + q.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
                return slimr::Json{{"files", slimr::Json::array_t{slimr::Json{{"id", folderId}}}}};
            }
            slimr::Json::array_t files;
            for (const auto& n : world.driveFiles) {
                if (q.find("'" + n + "'") != std::string::npos) {
                    files.push_back(slimr::Json{{"id", n}});
                }
            }
            return slimr::Json{{"files", files}};
        }
        if (key == "calendar.calendarList.list") return slimr::Json{{"items", slimr::Json::array_t{}}};
        if (key == "calendar.events.list" || key == "tasks.tasks.list") {
            return slimr::Json{{"pages", slimr::Json::array_t{slimr::Json{{"items", slimr::Json::array_t{}}}}}, {"items", slimr::Json::array_t{}}};
        }
        return slimr::Json{{"id", "created"}};
    }

    slimr::Json driveUpload(const std::string& file, const std::string& name, const std::string& folderPath, const std::string& mimeType) override {
        if (customDriveUpload) {
            return customDriveUpload(file, name, folderPath, mimeType);
        }
        world.uploads.push_back({.name = name, .folder = folderPath});
        return slimr::Json{
            {"id", "d1"},
            {"webViewLink", "https://drive.google.com/file/d/d1/view"}
        };
    }

    std::string gmailAttachment(const std::string& messageId, const std::string& attachmentId, const std::string&) override {
        world.attachments.push_back(messageId + "/" + attachmentId);
        return "";
    }

    GmailMessage gmailMessage(const std::string& id) override {
        return GmailMessage{
            .id = id,
            .threadId = "a1",
            .from = "Taylor <sender@example.com>",
            .to = "source1@example.com",
            .date = "Thu, 5 Nov 2030 09:00:00 +0000",
            .subject = "Riley's OT report",
            .body = "Please find Riley's report and receipt attached.",
            .attachments = {GmailAttachment{
                .filename = "Report.pdf",
                .mimeType = "application/pdf",
                .size = 1000,
                .attachmentId = "att1"
            }}
        };
    }

    std::vector<WhatsAppChat> whatsappChats() override { return {}; }
    std::vector<WhatsAppMessage> whatsappExport(const std::vector<std::string>&, const std::string&, const std::string&) override { return {}; }

    slimr::Json obsidian(const std::string& command, const slimr::Json& input) override {
        if (command == "read") {
            std::string path = input.at("note").get<std::string>();
            auto it = world.notes.find(path);
            if (it == world.notes.end()) throw std::runtime_error("note not found: " + path);
            return slimr::Json{{"path", path}, {"content", it->second}};
        }
        if (command == "write") {
            std::string path = input.at("path").get<std::string>();
            std::string content = input.at("content").get<std::string>();
            world.writes.push_back(path);
            world.notes[path] = content;
            return slimr::Json{{"path", path}};
        }
        return slimr::Json::object_t{};
    }

    std::optional<std::vector<double>> jev(const std::vector<JevQuestion>& qs) override {
        return std::vector<double>(qs.size(), 0.9);
    }

    LlmReply llm(const std::string& model, const std::string&, const std::string& system, const std::string&) override {
        LlmUsage usage{.input = 1, .output = 1, .reasoning = 0};
        if (model == modelFor("triage").model) {
            slimr::Json triage = slimr::Json{
                {"date", "2030.11.05"},
                {"title", "Riley OT report"},
                {"tags", slimr::Json::array_t{"riley", "health/therapy"}},
                {"people", slimr::Json::array_t{"Taylor Example"}},
                {"ids", slimr::Json{{"gm", "a1"}}},
                {"subject", "Riley's OT report"},
                {"attachments", slimr::Json::array_t{"Report.pdf"}},
                {"critical", true},
                {"expense", slimr::Json{
                    {"date", "2030.11.05"},
                    {"who", "Riley"},
                    {"provider", "Example Clinic (Taylor Example, OT)"},
                    {"item", "OT review report"},
                    {"amount", 200.0},
                    {"currency", "EUR"},
                    {"qualifies", "ask"}
                }}
            };
            if (!triageExtra.is_null() && triageExtra.is_object()) {
                for (const auto& [k, v] : triageExtra.get<slimr::Json::object_t>()) {
                    triage[k] = v;
                }
            }
            return LlmReply{.text = slimr::write_json(triage), .usage = usage};
        }
        if (system.starts_with("# Meds section")) {
            return LlmReply{
                .text = "## Meds\n\n### Current\n#### morning\n- Example medicine C\n\n### Prior\n- Example medicine B",
                .usage = usage
            };
        }
        if (system.starts_with("# Contacts")) {
            return LlmReply{
                .text = R"({"name":"Synthetic7 Synthetic9","create":true,"group":"Example group 6","howWeKnow":"Riley's GP, mobile 087 123 4567, synthetic7@example.com"})",
                .usage = usage
            };
        }
        slimr::Json draft = slimr::Json{
            {"add", slimr::Json::array_t{
                slimr::Json{
                    {"date", "2030.11.05"},
                    {"title", "Riley OT report"},
                    {"tags", slimr::Json::array_t{"riley", "health/therapy"}},
                    {"people", slimr::Json::array_t{
                        slimr::Json{{"name", "Taylor Example"}},
                        slimr::Json{{"name", "Synthetic7 Synthetic9"}},
                        slimr::Json{{"name", "Synthetic4"}},
                        slimr::Json{{"name", "Dr Synthetic6 Synthetic8"}},
                        slimr::Json{{"name", "Synthetic2 Synthetic10"}},
                        slimr::Json{{"name", "Synthetic5 Bloggs"}}
                    }},
                    {"sources", slimr::Json::array_t{
                        slimr::Json{{"kind", "gm"}, {"id", "a1"}}
                    }}
                },
                slimr::Json{
                    {"date", "2030.11.06"},
                    {"title", "Riley Medikinet up to 30mg"},
                    {"tags", slimr::Json::array_t{"riley", "health/meds"}},
                    {"sources", slimr::Json::array_t{
                        slimr::Json{{"kind", "wa"}, {"id", "w1"}}
                    }}
                }
            }}
        };
        return LlmReply{.text = slimr::write_json(draft), .usage = usage};
    }
};

TEST_SUITE("mine follow-ups") {
    struct EnvGuard {
        EnvGuard() {
            setenv("PERSONAL_LOG_SUGGEST_CALENDAR_ID", "sugg", 1);
        }
        ~EnvGuard() {
            unsetenv("PERSONAL_LOG_SUGGEST_CALENDAR_ID");
            setTools(nullptr);
        }
    };

    TEST_CASE("an expense whose Gmail id is not hex is skipped with a warning") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        auto world = newWorld();
        auto runner = std::make_shared<FollowUpToolsRunner>(world, slimr::Json{{"ids", slimr::Json{{"gm", "x\"/><script>"}}}});
        setTools(runner);

        auto result = mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06"});
        bool hasWarning = false;
        for (const auto& w : result.warnings) {
            if (w == "medical expenses: row skipped, Gmail id is not hex") hasWarning = true;
        }
        CHECK(hasWarning);
        for (const auto& w : world.writes) {
            CHECK(w.find("script") == std::string::npos);
        }
    }

    TEST_CASE("real run adds the expense, rebuilds Meds, files the report and links contacts") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        auto world = newWorld();
        auto runner = std::make_shared<FollowUpToolsRunner>(world);
        setTools(runner);

        auto result = mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06"});
        for (const auto& w : result.warnings) {
            CHECK((w.starts_with("JEV") || w.starts_with("whatsapp")));
        }

        std::string expenses = world.notes["Records/Example expenses.md"];
        std::regex exp_row_re(R"(\| 2030\.11\.05 \| Riley \| Example Clinic \(Taylor Example, OT\) \| OT review report \| 200\.00 \|)");
        CHECK(std::regex_search(expenses, exp_row_re));
        CHECK(expenses.find("ask €550.00") != std::string::npos);

        std::string riley = world.notes["People/Riley Morgan.md"];
        CHECK(riley.find("#### morning\n- Example medicine C") != std::string::npos);
        CHECK(riley.find("## Doctors\nSee Example clinicians") != std::string::npos);

        REQUIRE(world.attachments.size() == 1);
        CHECK(world.attachments[0] == "m1/att1");
        REQUIRE(world.uploads.size() == 1);
        CHECK(world.uploads[0].name == "2030.11.05 - Riley OT report.pdf");
        CHECK(world.uploads[0].folder == "Documents/Riley/health");

        auto entry = db.getBySource(Source{.kind = "gm", .id = "a1"});
        REQUIRE(entry.has_value());
        bool hasLink = false;
        for (const auto& l : entry->links) {
            if (l.kind == "file" && l.url.ends_with("/d1/view")) hasLink = true;
        }
        CHECK(hasLink);

        std::map<std::string, std::optional<std::string>> people;
        for (const auto& p : entry->people) {
            people[p.name] = p.contact;
        }
        CHECK(people["Taylor Example"] == "people/c1");
        CHECK(people["Synthetic7 Synthetic9"] == "people/new1");
        CHECK(people["Synthetic4"] == std::nullopt); // never matched on first name alone
        CHECK(people["Dr Synthetic6 Synthetic8"] == "people/c3"); // title ignored
        CHECK(people["Synthetic2 Synthetic10"] == std::nullopt); // similar contact
        CHECK(people["Synthetic5 Bloggs"] == std::nullopt); // first name with only context contact

        int createContactCalls = 0;
        slimr::Json createdBody;
        for (const auto& g : world.google) {
            if (g.contains("method") && g.at("method").get<std::string>() == "createContact") {
                createContactCalls++;
                createdBody = g.contains("body") ? g.at("body") : slimr::Json::object_t{};
            }
        }
        CHECK(createContactCalls == 1);
        CHECK(createdBody.at("biographies")[0].at("value").get<std::string>() == "How we know them: Riley's GP, mobile");
        CHECK(createdBody.at("memberships")[0].at("contactGroupMembership").at("contactGroupResourceName").get<std::string>() == "contactGroups/Example group 6");

        bool hasCliniciansGroup = false;
        for (const auto& grp : world.groups) {
            if (grp.name == "Example group 6") hasCliniciansGroup = true;
        }
        CHECK(hasCliniciansGroup);
        CHECK(result.stats.contains("usage"));
        CHECK(result.stats.at("usage").contains("meds"));
    }

    TEST_CASE("skips an expense already listed and a file already in Drive") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        auto world = newWorld();
        std::string modified = EXPENSES;
        size_t pos = modified.find("| 2030.11.20 |");
        if (pos != std::string::npos) {
            modified.insert(pos, "| 2030.11.05 | Riley | Example Clinic | OT report | 200.00 |  | ask | no | [✉️](q) |\n");
        }
        world.notes["Records/Example expenses.md"] = modified;
        world.driveFiles.push_back("2030.11.05 - Riley OT report.pdf");

        auto runner = std::make_shared<FollowUpToolsRunner>(world);
        setTools(runner);

        auto result = mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06"});
        CHECK(std::find(world.writes.begin(), world.writes.end(), "Records/Example expenses.md") == world.writes.end());
        CHECK(world.uploads.empty());

        bool expenseSkip = false, driveSkip = false;
        for (const auto& f : result.followUps) {
            if (f.starts_with("expense: skip")) expenseSkip = true;
            if (f.starts_with("drive: skip")) driveSkip = true;
        }
        CHECK(expenseSkip);
        CHECK(driveSkip);
    }

    TEST_CASE("a school document goes to the kid's school folder") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        auto world = newWorld();
        auto runner = std::make_shared<FollowUpToolsRunner>(world, slimr::Json{
            {"docFolder", "school"},
            {"title", "Riley support plan"}
        });
        setTools(runner);

        mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06"});
        REQUIRE(world.uploads.size() == 1);
        CHECK(world.uploads[0].name == "2030.11.05 - Riley support plan.pdf");
        CHECK(world.uploads[0].folder == "Documents/Riley/Example School");
    }

    TEST_CASE("a failing follow-up becomes a warning, the rest still run") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        auto world = newWorld();
        world.notes.erase("Records/Example expenses.md");
        auto runner = std::make_shared<FollowUpToolsRunner>(world);
        runner->customDriveUpload = [](const std::string&, const std::string&, const std::string&, const std::string&) -> slimr::Json {
            throw std::runtime_error("Drive quota");
        };
        setTools(runner);

        auto result = mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06"});
        bool medWarning = false, driveWarning = false;
        for (const auto& w : result.warnings) {
            if (w.starts_with("medical expenses: note not found")) medWarning = true;
            if (w.starts_with("kids' documents: ") && w.find("Drive quota") != std::string::npos) driveWarning = true;
        }
        CHECK(medWarning);
        CHECK(driveWarning);
        CHECK(std::find(world.writes.begin(), world.writes.end(), "People/Riley Morgan.md") != world.writes.end()); // Meds still rebuilt

        auto runs = db.runs();
        REQUIRE(runs.size() >= 1);
        CHECK(runs[0].status == "ok");
    }

    TEST_CASE("dry run lists planned follow-ups and writes nothing") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        auto world = newWorld();
        auto runner = std::make_shared<FollowUpToolsRunner>(world);
        setTools(runner);

        auto result = mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06", .dryRun = true});
        CHECK(world.writes.empty());
        CHECK(world.uploads.empty());
        for (const auto& g : world.google) {
            std::string m = g.contains("method") ? g.at("method").get<std::string>() : "";
            CHECK(m != "createContact");
            CHECK(m != "create");
        }

        std::string plan;
        for (const auto& f : result.followUps) {
            plan += f + "\n";
        }
        CHECK(plan.find("contact: link Taylor Example -> people/c1") != std::string::npos);
        CHECK(plan.find("contact: create Synthetic7 Synthetic9 [Example group 6]") != std::string::npos);
        CHECK(plan.find("drive: upload Report.pdf -> Documents/Riley/health/2030.11.05 - Riley OT report.pdf") != std::string::npos);
        CHECK(plan.find("expense: add 2030.11.05 Riley Example Clinic (Taylor Example, OT) €200.00 (ask)") != std::string::npos);
        CHECK(plan.find("meds: rebuild ## Meds in People/Riley Morgan.md") != std::string::npos);
    }
}

// ============================================================================
// 11. FakeToolsRunner & Mine Pipeline
// ============================================================================

class FakeToolsRunner : public ToolRunner {
public:
    std::vector<slimr::Json>& googleCalls;
    slimr::Json draft;
    std::optional<std::vector<double>> jevScores;
    std::function<LlmReply(const std::string&, const std::string&, const std::string&, const std::string&)> customLlm;

    FakeToolsRunner(std::vector<slimr::Json>& calls, slimr::Json d, std::optional<std::vector<double>> js = std::nullopt)
        : googleCalls(calls), draft(std::move(d)), jevScores(std::move(js)) {}

    slimr::Json google(const slimr::Json& req) override {
        googleCalls.push_back(req);
        std::string service = req.contains("service") ? req.at("service").get<std::string>() : "";
        std::string method = req.contains("method") ? req.at("method").get<std::string>() : "";
        std::string resStr;
        if (req.contains("resource") && req.at("resource").is_array()) {
            for (size_t i = 0; i < req.at("resource").get<slimr::Json::array_t>().size(); ++i) {
                if (i > 0) resStr += ".";
                resStr += req.at("resource").get<slimr::Json::array_t>()[i].get<std::string>();
            }
        }
        std::string key = service + "." + resStr + "." + method;

        if (key == "gmail.users.messages.list") {
            return slimr::Json{
                {"pages", slimr::Json::array_t{
                    slimr::Json{{"messages", slimr::Json::array_t{
                        slimr::Json{{"id", "m1"}, {"threadId", "t1"}},
                        slimr::Json{{"id", "m2"}, {"threadId", "t2"}}
                    }}}
                }}
            };
        }
        if (key == "people.people.connections.list") {
            return slimr::Json{{"pages", slimr::Json::array_t{slimr::Json{{"connections", slimr::Json::array_t{}}}}}};
        }
        if (key == "calendar.events.list") {
            if (req.contains("params") && req.at("params").contains("calendarId") &&
                req.at("params").at("calendarId").get<std::string>() == "source1@example.com") {
                return slimr::Json{
                    {"pages", slimr::Json::array_t{
                        slimr::Json{{"items", slimr::Json::array_t{
                            slimr::Json{
                                {"id", "ev1"},
                                {"summary", "Dentist Riley"},
                                {"start", slimr::Json{{"dateTime", "2030-11-05T10:00:00Z"}}},
                                {"end", slimr::Json{{"dateTime", "2030-11-05T11:00:00Z"}}}
                            }
                        }}}
                    }}
                };
            }
            return slimr::Json{{"pages", slimr::Json::array_t{slimr::Json{{"items", slimr::Json::array_t{}}}}}};
        }
        if (key == "tasks.tasks.list") {
            return slimr::Json{
                {"items", slimr::Json::array_t{
                    slimr::Json{{"title", "Pay school fee"}, {"due", "2030-11-20T00:00:00.000Z"}}
                }}
            };
        }
        if (key == "calendar.calendarList.list") {
            return slimr::Json{{"items", slimr::Json::array_t{slimr::Json{{"id", "sugg"}, {"summary", "AI-Suggests"}}}}};
        }
        return slimr::Json{{"id", "created"}};
    }

    slimr::Json driveUpload(const std::string&, const std::string&, const std::string&, const std::string&) override {
        return slimr::Json::object_t{};
    }

    std::string gmailAttachment(const std::string&, const std::string&, const std::string&) override {
        return "";
    }

    GmailMessage gmailMessage(const std::string& id) override {
        if (id == "m1") {
            return GmailMessage{
                .id = "m1",
                .threadId = "t1",
                .from = "Office <office@source1.example.com>",
                .to = "source1@example.com",
                .date = "Wed, 4 Nov 2030 09:00:00 +0000",
                .subject = "School closed 12th",
                .body = "The school will be closed on the 12th for the strike.",
                .attachments = {},
            };
        }
        return GmailMessage{
            .id = "m2",
            .threadId = "t2",
            .from = "Deals <sender@example.com>",
            .to = "source1@example.com",
            .date = "Wed, 4 Nov 2030 09:00:00 +0000",
            .subject = "50% off",
            .body = "Buy now",
            .attachments = {},
        };
    }

    std::vector<WhatsAppChat> whatsappChats() override {
        return {WhatsAppChat{.jid = "e@lid", .name = "Jamie", .is_group = false, .message_count = 10}};
    }

    std::vector<WhatsAppMessage> whatsappExport(const std::vector<std::string>&, const std::string&, const std::string&) override {
        return {WhatsAppMessage{
            .id = "w1",
            .chat_jid = "e@lid",
            .chat_name = "Jamie",
            .sender_name = "Jamie",
            .is_from_me = false,
            .ts_utc = "2030-11-05 18:00:00",
            .content = "Riley has a swim party on Sat 14th at 2",
        }};
    }

    slimr::Json obsidian(const std::string&, const slimr::Json&) override {
        return slimr::Json::object_t{};
    }

    std::optional<std::vector<double>> jev(const std::vector<JevQuestion>& qs) override {
        if (jevScores) return *jevScores;
        return std::vector<double>(qs.size(), 0.02);
    }

    LlmReply llm(const std::string& model, const std::string& effort, const std::string& system, const std::string& user) override {
        if (customLlm) {
            return customLlm(model, effort, system, user);
        }
        auto triageChoice = modelFor("triage");
        if (model == triageChoice.model || (triageChoice.fallback && model == *triageChoice.fallback)) {
            return LlmReply{
                .text = R"({"date":"2030.11.12","title":"School closed for strike","tags":["casey","riley","school"],"details":[],"ids":{"gm":"t1"},"subject":"School closed 12th"})",
                .usage = {.input = 10, .output = 5, .reasoning = 0}
            };
        }
        return LlmReply{
            .text = slimr::write_json(draft),
            .usage = {.input = 20, .output = 10, .reasoning = 0}
        };
    }
};

TEST_SUITE("mine pipeline") {
    struct EnvGuard {
        EnvGuard() {
            setenv("PERSONAL_LOG_SUGGEST_CALENDAR_ID", "sugg", 1);
        }
        ~EnvGuard() {
            unsetenv("PERSONAL_LOG_SUGGEST_CALENDAR_ID");
            setTools(nullptr);
        }
    };

    TEST_CASE("fails loudly, before any call, when the AI-Suggests calendar id override is malformed") {
        EnvGuard guard;
        setenv("PERSONAL_LOG_SUGGEST_CALENDAR_ID", "bad id/../x", 1);
        auto db = AppDb::open(":memory:");
        std::vector<slimr::Json> calls;
        auto runner = std::make_shared<FakeToolsRunner>(calls, slimr::Json::object_t{});
        setTools(runner);

        CHECK_THROWS_WITH_AS(mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06"}),
                             "Invalid mining suggestion calendar id",
                             std::runtime_error);
        CHECK(calls.empty());
        CHECK(db.runs().empty());
    }

    TEST_CASE("never looks the AI-Suggests calendar up by name") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        std::vector<slimr::Json> calls;
        auto runner = std::make_shared<FakeToolsRunner>(calls, slimr::Json::object_t{});
        setTools(runner);

        mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06", .dryRun = true});
        bool hasCalendarList = false;
        for (const auto& c : calls) {
            if (c.contains("resource") && c.at("resource").is_array() &&
                !c.at("resource").get<slimr::Json::array_t>().empty() &&
                c.at("resource").get<slimr::Json::array_t>()[0].get<std::string>() == "calendarList") {
                hasCalendarList = true;
            }
        }
        CHECK(!hasCalendarList);
    }

    TEST_CASE("drops non-https links and malformed contacts with warnings, keeps the rest") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        std::vector<slimr::Json> calls;
        slimr::Json draft = slimr::Json{
            {"add", slimr::Json::array_t{
                slimr::Json{
                    {"date", "2030.11.12"},
                    {"title", "School closed for strike"},
                    {"sources", slimr::Json::array_t{
                        slimr::Json{{"kind", "gm"}, {"id", "t1"}}
                    }},
                    {"people", slimr::Json::array_t{
                        slimr::Json{{"name", "Taylor Example"}, {"contact", "people/c123"}},
                        slimr::Json{{"name", "Evil"}, {"contact", "https://evil.example/x"}}
                    }},
                    {"links", slimr::Json::array_t{
                        slimr::Json{{"kind", "link"}, {"label", "ok"}, {"url", "https://example.com/a"}},
                        slimr::Json{{"kind", "link"}, {"label", "http"}, {"url", "http://example.com/a"}},
                        slimr::Json{{"kind", "link"}, {"label", "js"}, {"url", "javascript:alert(1)"}}
                    }}
                }
            }}
        };
        auto runner = std::make_shared<FakeToolsRunner>(calls, draft);
        setTools(runner);

        auto result = mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06"});
        auto entry = db.getBySource(Source{.kind = "gm", .id = "t1"});
        REQUIRE(entry.has_value());

        std::vector<std::string> linkLabels;
        for (const auto& l : entry->links) linkLabels.push_back(l.label);
        CHECK(linkLabels.size() == 2);
        CHECK(linkLabels[0] == "ok");
        CHECK(linkLabels[1] == "School closed 12th");

        std::map<std::string, std::optional<std::string>> people;
        for (const auto& p : entry->people) people[p.name] = p.contact;
        CHECK(people["Taylor Example"] == "people/c123");
        CHECK(people["Evil"] == std::nullopt);

        int linkDropped = 0, contactDropped = 0;
        for (const auto& w : result.warnings) {
            if (w.starts_with("link dropped")) linkDropped++;
            if (w.starts_with("contact dropped")) contactDropped++;
        }
        CHECK(linkDropped == 2);
        CHECK(contactDropped == 1);

        auto runs = db.runs();
        REQUIRE(runs.size() >= 1);
        CHECK(runs[0].status == "ok");
    }

    TEST_CASE("adds entries with email links, updates, creates deadlines and suggestions, records the run") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        auto existing = db.add({NewEntry{
            .date = "2030.11.04",
            .title = "Riley OT school observation",
            .tags = {"riley"}
        }}).added[0];

        std::vector<slimr::Json> calls;
        slimr::Json draft = slimr::Json{
            {"add", slimr::Json::array_t{
                slimr::Json{
                    {"date", "2030.11.12"},
                    {"title", "School closed for strike"},
                    {"tags", slimr::Json::array_t{"casey", "riley", "school"}},
                    {"sources", slimr::Json::array_t{slimr::Json{{"kind", "gm"}, {"id", "t1"}}}}
                },
                slimr::Json{
                    {"date", "2030.11.05"},
                    {"title", "Riley dentist"},
                    {"tags", slimr::Json::array_t{"riley", "health/dental"}},
                    {"sources", slimr::Json::array_t{slimr::Json{{"kind", "cal"}, {"id", "ev1"}}}}
                }
            }},
            {"update", slimr::Json::array_t{
                slimr::Json{
                    {"id", existing},
                    {"addDetails", slimr::Json::array_t{"Report to follow"}}
                }
            }},
            {"suggestEvents", slimr::Json::array_t{
                slimr::Json{{"title", "Riley swim party"}, {"start", "2099-11-14T14:00"}}
            }},
            {"deadlines", slimr::Json::array_t{
                slimr::Json{{"title", "Pay school fee"}, {"due", "2030-11-20"}},
                slimr::Json{{"title", "Return swim form"}, {"due", "2099-11-13"}}
            }}
        };
        auto runner = std::make_shared<FakeToolsRunner>(calls, draft);
        setTools(runner);

        auto result = mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06"});
        CHECK(result.added.size() == 2);

        auto strike = db.getBySource(Source{.kind = "gm", .id = "t1"});
        REQUIRE(strike.has_value());
        REQUIRE(!strike->links.empty());
        CHECK(strike->links[0].label == "School closed 12th");
        CHECK(strike->links[0].url.ends_with("#all/t1"));

        auto exEntry = db.get(existing);
        REQUIRE(exEntry.has_value());
        REQUIRE(!exEntry->details.empty());
        CHECK(exEntry->details[0] == "Report to follow");

        REQUIRE(result.deadlinesCreated.size() == 1);
        CHECK(result.deadlinesCreated[0] == "2099-11-13 Return swim form");

        REQUIRE(result.eventsSuggested.size() == 1);
        CHECK(result.eventsSuggested[0] == "2099-11-14T14:00 Riley swim party");

        std::string insertCalId;
        for (const auto& c : calls) {
            std::string m = c.contains("method") ? c.at("method").get<std::string>() : "";
            std::string r0;
            if (c.contains("resource") && c.at("resource").is_array() && !c.at("resource").get<slimr::Json::array_t>().empty()) {
                r0 = c.at("resource").get<slimr::Json::array_t>()[0].get<std::string>();
            }
            if (m == "insert" && r0 == "events") {
                if (c.contains("params") && c.at("params").contains("calendarId")) {
                    insertCalId = c.at("params").at("calendarId").get<std::string>();
                }
            }
        }
        CHECK(insertCalId == "sugg");
        CHECK(result.stats["emails"].get<int64_t>() == 1);

        auto runs = db.runs();
        REQUIRE(runs.size() >= 1);
        CHECK(runs[0].status == "ok");
        auto lastUntil = db.lastSuccessfulUntil();
        REQUIRE(lastUntil.has_value());
        CHECK(*lastUntil == "2030.11.06");
    }

    TEST_CASE("dry run writes nothing and records no run") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        std::vector<slimr::Json> calls;
        slimr::Json draft = slimr::Json{
            {"add", slimr::Json::array_t{
                slimr::Json{
                    {"date", "2030.11.12"},
                    {"title", "x"},
                    {"sources", slimr::Json::array_t{slimr::Json{{"kind", "gm"}, {"id", "t1"}}}}
                }
            }}
        };
        auto runner = std::make_shared<FakeToolsRunner>(calls, draft);
        setTools(runner);

        auto result = mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06", .dryRun = true});
        REQUIRE(result.draft.has_value());
        CHECK(result.draft->add.size() == 1);
        CHECK(db.search({}).empty());
        CHECK(db.runs().empty());
    }

    TEST_CASE("a model failure marks the run as an error") {
        EnvGuard guard;
        auto db = AppDb::open(":memory:");
        std::vector<slimr::Json> calls;
        auto runner = std::make_shared<FakeToolsRunner>(calls, slimr::Json::object_t{});
        runner->customLlm = [](const std::string&, const std::string&, const std::string&, const std::string&) -> LlmReply {
            throw std::runtime_error("DeepSeek down");
        };
        setTools(runner);

        CHECK_THROWS_WITH_AS(mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06"}),
                             "DeepSeek down",
                             std::runtime_error);
        auto runs = db.runs();
        REQUIRE(runs.size() >= 1);
        CHECK(runs[0].status == "error");
    }

    TEST_CASE("a failing model falls back to the step's fallback model, with one warning per step") {
        EnvGuard guard;
        const std::vector<const char*> envNames = {
            "ASSISTANT_LLM_DEFAULT",
            "ASSISTANT_LLM_FAST",
            "ASSISTANT_LLM_DEFAULT_FALLBACK",
            "ASSISTANT_LLM_FAST_FALLBACK",
        };
        std::map<std::string, std::optional<std::string>> saved;
        for (auto n : envNames) {
            const char* v = std::getenv(n);
            saved[n] = v ? std::optional<std::string>(v) : std::nullopt;
        }

        struct RestoreLocalEnv {
            std::map<std::string, std::optional<std::string>>& s;
            ~RestoreLocalEnv() {
                for (const auto& [k, v] : s) {
                    if (v) setenv(k.c_str(), v->c_str(), 1);
                    else unsetenv(k.c_str());
                }
            }
        } restore{saved};

        setenv("ASSISTANT_LLM_DEFAULT", "gemini-flash-latest", 1);
        setenv("ASSISTANT_LLM_FAST", "gemini-flash-lite-latest", 1);
        setenv("ASSISTANT_LLM_DEFAULT_FALLBACK", "deepseek-v4-pro", 1);
        setenv("ASSISTANT_LLM_FAST_FALLBACK", "deepseek-flash", 1);

        auto db = AppDb::open(":memory:");
        std::vector<slimr::Json> calls;
        slimr::Json draft = slimr::Json{
            {"add", slimr::Json::array_t{
                slimr::Json{
                    {"date", "2030.11.12"},
                    {"title", "x"},
                    {"sources", slimr::Json::array_t{slimr::Json{{"kind", "gm"}, {"id", "t1"}}}}
                }
            }}
        };
        auto runner = std::make_shared<FakeToolsRunner>(calls, draft);
        std::vector<std::string> asked;
        runner->customLlm = [&](const std::string& model, const std::string&, const std::string&, const std::string&) -> LlmReply {
            asked.push_back(model);
            if (model.starts_with("gemini-")) {
                throw std::runtime_error("Gemini error: 402 credits depleted");
            }
            if (model == "deepseek-flash") {
                return LlmReply{
                    .text = R"({"date":"2030.11.12","title":"School closed for strike","tags":["casey","riley","school"],"details":[],"ids":{"gm":"t1"},"subject":"School closed 12th"})",
                    .usage = {.input = 10, .output = 5, .reasoning = 0}
                };
            }
            return LlmReply{
                .text = slimr::write_json(draft),
                .usage = {.input = 20, .output = 10, .reasoning = 0}
            };
        };
        setTools(runner);

        auto result = mine(db, MineOptions{.since = "2030-11-04", .until = "2030-11-06", .dryRun = true});
        REQUIRE(result.draft.has_value());
        CHECK(result.draft->add.size() == 1);

        CHECK(std::find(asked.begin(), asked.end(), "deepseek-flash") != asked.end());
        CHECK(std::find(asked.begin(), asked.end(), "deepseek-v4-pro") != asked.end());

        std::vector<std::string> fallbackSteps;
        for (const auto& w : result.warnings) {
            if (w.find("failed, used") != std::string::npos) {
                size_t cpos = w.find(':');
                if (cpos != std::string::npos) fallbackSteps.push_back(w.substr(0, cpos));
            }
        }
        CHECK(fallbackSteps.size() == 2);
        CHECK(fallbackSteps[0] == "triage");
        CHECK(fallbackSteps[1] == "draft");

        CHECK(result.stats.contains("usage"));
        auto usage = result.stats.at("usage");
        CHECK(usage.contains("triage (fallback)"));
        CHECK(usage.at("triage (fallback)").at("model").get<std::string>() == "deepseek-flash");
        CHECK(!usage.contains("triage"));
    }
}

} // namespace personal_log::testing
