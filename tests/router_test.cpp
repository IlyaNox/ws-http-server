#include "api/router.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

using nlohmann::json;

namespace {

http::Request make_request(const std::string& method, const std::string& target,
                           const std::string& body = "") {
  http::Request r;
  r.method = method;
  r.target = target;
  const auto q = target.find('?');
  r.path = target.substr(0, q);
  if (q != std::string::npos) r.query = target.substr(q + 1);
  r.body = body;
  return r;
}

http::Response call(Store& s, const std::string& method, const std::string& target,
                    const std::string& body = "") {
  return api::handle(make_request(method, target, body), s);
}

std::string item_path(std::uint64_t id) { return "/items/" + std::to_string(id); }

}  // namespace

// ---------- health и общее поведение ----------

TEST(Api, Health) {
  Store s;
  auto r = call(s, "GET", "/health");
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(json::parse(r.body).at("status").get<std::string>(), "ok");
}

TEST(Api, KeepAliveIsPropagated) {
  Store s;
  auto req = make_request("GET", "/health");
  req.keep_alive = false;
  EXPECT_FALSE(api::handle(req, s).keep_alive);
  req.keep_alive = true;
  EXPECT_TRUE(api::handle(req, s).keep_alive);
}

TEST(Api, UnknownPathIs404) {
  Store s;
  s.create("a", 1);
  EXPECT_EQ(call(s, "GET", "/nope").status, 404);
  EXPECT_EQ(call(s, "GET", "/items/1/unknown").status, 404);
  EXPECT_EQ(call(s, "POST", "/items/1/increment/extra").status, 404);
  EXPECT_EQ(call(s, "GET", "/items/abc/increment").status, 404);
  EXPECT_TRUE(json::parse(call(s, "GET", "/nope").body).contains("error"));
}

TEST(Api, WrongMethodIs405) {
  Store s;
  EXPECT_EQ(call(s, "POST", "/health").status, 405);
  EXPECT_EQ(call(s, "DELETE", "/items").status, 405);
  EXPECT_EQ(call(s, "PUT", "/items").status, 405);
  EXPECT_EQ(call(s, "POST", "/items/1").status, 405);
  EXPECT_EQ(call(s, "GET", "/items/1/increment").status, 405);
}

// ---------- создание и чтение ----------

TEST(ApiItems, CreateReturns201WithItem) {
  Store s;
  auto r = call(s, "POST", "/items", R"({"name":"a","value":5})");
  ASSERT_EQ(r.status, 201);
  auto j = json::parse(r.body);
  EXPECT_GE(j.at("id").get<std::uint64_t>(), 1u);
  EXPECT_EQ(j.at("name").get<std::string>(), "a");
  EXPECT_EQ(j.at("value").get<std::int64_t>(), 5);
  EXPECT_EQ(j.at("version").get<std::uint64_t>(), 1u);
}

TEST(ApiItems, CreateRejectsInvalidBodies) {
  Store s;
  const std::vector<std::string> bad = {
      "",
      "not json",
      "[]",
      "{}",
      R"({"name":"a"})",
      R"({"value":1})",
      R"({"name":1,"value":1})",
      R"({"name":"a","value":"1"})",
      R"({"name":"a","value":1.5})",
      R"({"name":"a","value":99999999999999999999})",
  };
  for (const auto& b : bad) {
    SCOPED_TRACE(b);
    EXPECT_EQ(call(s, "POST", "/items", b).status, 400);
  }
  EXPECT_TRUE(s.list(0, 10).empty());
}

TEST(ApiItems, GetExistingMissingAndBadId) {
  Store s;
  const auto id = s.create("a", 1).id;
  auto r = call(s, "GET", item_path(id));
  ASSERT_EQ(r.status, 200);
  EXPECT_EQ(json::parse(r.body).at("name").get<std::string>(), "a");

  EXPECT_EQ(call(s, "GET", "/items/9999").status, 404);
  EXPECT_EQ(call(s, "GET", "/items/abc").status, 404);
  EXPECT_EQ(call(s, "GET", "/items/-1").status, 404);
}

TEST(ApiItems, ListWithPagination) {
  Store s;
  for (int i = 0; i < 5; ++i) s.create("n" + std::to_string(i), i);

  auto all = json::parse(call(s, "GET", "/items").body);
  ASSERT_TRUE(all.is_array());
  EXPECT_EQ(all.size(), 5u);

  auto page = json::parse(call(s, "GET", "/items?offset=1&limit=2").body);
  ASSERT_EQ(page.size(), 2u);
  EXPECT_EQ(page[0].at("id").get<std::uint64_t>(), 2u);
  EXPECT_EQ(page[1].at("id").get<std::uint64_t>(), 3u);

  auto empty = json::parse(call(s, "GET", "/items?offset=10").body);
  EXPECT_TRUE(empty.is_array());
  EXPECT_TRUE(empty.empty());

  EXPECT_EQ(call(s, "GET", "/items?limit=abc").status, 400);
  EXPECT_EQ(call(s, "GET", "/items?offset=-1").status, 400);
}

// ---------- PUT ----------

TEST(ApiItems, PutReplacesAndBumpsVersion) {
  Store s;
  const auto id = s.create("a", 1).id;
  auto r = call(s, "PUT", item_path(id), R"({"name":"b","value":2})");
  ASSERT_EQ(r.status, 200);
  auto j = json::parse(r.body);
  EXPECT_EQ(j.at("name").get<std::string>(), "b");
  EXPECT_EQ(j.at("value").get<std::int64_t>(), 2);
  EXPECT_EQ(j.at("version").get<std::uint64_t>(), 2u);
}

TEST(ApiItems, PutValidationAndVersionConflict) {
  Store s;
  const auto id = s.create("a", 1).id;

  EXPECT_EQ(call(s, "PUT", "/items/9999", R"({"name":"b","value":2})").status, 404);
  EXPECT_EQ(call(s, "PUT", item_path(id), R"({"name":"b"})").status, 400);
  EXPECT_EQ(call(s, "PUT", item_path(id), R"({"name":"b","value":2,"version":"x"})").status, 400);
  EXPECT_EQ(call(s, "PUT", item_path(id), R"({"name":"b","value":2,"version":99})").status, 409);
  EXPECT_EQ(s.get(id)->name, "a");  // при конфликте объект не изменился

  EXPECT_EQ(call(s, "PUT", item_path(id), R"({"name":"b","value":2,"version":1})").status, 200);
  EXPECT_EQ(s.get(id)->version, 2u);
}

// ---------- PATCH ----------

TEST(ApiItems, PatchUpdatesOnlyGivenFields) {
  Store s;
  const auto id = s.create("a", 1).id;

  auto r = call(s, "PATCH", item_path(id), R"({"value":10})");
  ASSERT_EQ(r.status, 200);
  auto j = json::parse(r.body);
  EXPECT_EQ(j.at("name").get<std::string>(), "a");
  EXPECT_EQ(j.at("value").get<std::int64_t>(), 10);
  EXPECT_EQ(j.at("version").get<std::uint64_t>(), 2u);

  r = call(s, "PATCH", item_path(id), R"({"name":"z"})");
  ASSERT_EQ(r.status, 200);
  j = json::parse(r.body);
  EXPECT_EQ(j.at("name").get<std::string>(), "z");
  EXPECT_EQ(j.at("value").get<std::int64_t>(), 10);
  EXPECT_EQ(j.at("version").get<std::uint64_t>(), 3u);
}

TEST(ApiItems, PatchValidationAndConflict) {
  Store s;
  const auto id = s.create("a", 1).id;

  EXPECT_EQ(call(s, "PATCH", "/items/9999", R"({"value":1})").status, 404);
  EXPECT_EQ(call(s, "PATCH", item_path(id), "{}").status, 400);
  EXPECT_EQ(call(s, "PATCH", item_path(id), R"({"value":"x"})").status, 400);
  EXPECT_EQ(call(s, "PATCH", item_path(id), R"({"name":5})").status, 400);
  EXPECT_EQ(call(s, "PATCH", item_path(id), R"({"value":1,"version":5})").status, 409);
  EXPECT_EQ(s.get(id)->value, 1);
}

// ---------- DELETE ----------

TEST(ApiItems, DeleteReturns204ThenNotFound) {
  Store s;
  const auto id = s.create("a", 1).id;
  auto r = call(s, "DELETE", item_path(id));
  EXPECT_EQ(r.status, 204);
  EXPECT_TRUE(r.body.empty());
  EXPECT_EQ(call(s, "GET", item_path(id)).status, 404);
  EXPECT_EQ(call(s, "DELETE", item_path(id)).status, 404);
}

// ---------- increment ----------

TEST(ApiItems, IncrementAddsDelta) {
  Store s;
  const auto id = s.create("c", 10).id;
  const auto path = item_path(id) + "/increment";

  auto r = call(s, "POST", path, R"({"delta":5})");
  ASSERT_EQ(r.status, 200);
  EXPECT_EQ(json::parse(r.body).at("value").get<std::int64_t>(), 15);

  r = call(s, "POST", path, R"({"delta":-20})");
  EXPECT_EQ(json::parse(r.body).at("value").get<std::int64_t>(), -5);

  r = call(s, "POST", path);  // пустое тело = +1
  ASSERT_EQ(r.status, 200);
  EXPECT_EQ(json::parse(r.body).at("value").get<std::int64_t>(), -4);
}

TEST(ApiItems, IncrementValidation) {
  Store s;
  const auto id = s.create("c", 0).id;
  const auto path = item_path(id) + "/increment";

  EXPECT_EQ(call(s, "POST", path, "not json").status, 400);
  EXPECT_EQ(call(s, "POST", path, "{}").status, 400);
  EXPECT_EQ(call(s, "POST", path, R"({"delta":"x"})").status, 400);
  EXPECT_EQ(call(s, "POST", path, R"({"delta":1.5})").status, 400);
  EXPECT_EQ(call(s, "POST", "/items/9999/increment", R"({"delta":1})").status, 404);
  EXPECT_EQ(s.get(id)->value, 0);
}

// ---------- гонки ----------

TEST(ApiConcurrent, IncrementEndpointIsExact) {
  Store s;
  const auto id = s.create("counter", 0).id;
  const auto path = item_path(id) + "/increment";

  constexpr int kThreads = 8, kIters = 2000;
  std::atomic<int> failures{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t)
    ts.emplace_back([&] {
      for (int i = 0; i < kIters; ++i)
        if (call(s, "POST", path, R"({"delta":1})").status != 200) ++failures;
    });
  for (auto& t : ts) t.join();

  EXPECT_EQ(failures.load(), 0);
  EXPECT_EQ(s.get(id)->value, kThreads * kIters);
}
