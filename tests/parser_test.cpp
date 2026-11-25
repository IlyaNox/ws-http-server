#include "http/parser.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "http/response.hpp"

using namespace http;

TEST(Parser, SimpleGet) {
  const std::string raw = "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n";
  auto r = parse_request(raw);
  ASSERT_EQ(r.status, ParseStatus::Complete);
  EXPECT_EQ(r.consumed, raw.size());
  EXPECT_EQ(r.request.method, "GET");
  EXPECT_EQ(r.request.path, "/health");
  EXPECT_EQ(r.request.query, "");
  EXPECT_EQ(r.request.headers.at("host"), "localhost");
  EXPECT_TRUE(r.request.keep_alive);
  EXPECT_TRUE(r.request.body.empty());
}

TEST(Parser, HeaderNamesLowercasedAndValuesTrimmed) {
  auto r =
      parse_request("GET / HTTP/1.1\r\nContent-Type:   application/json  \r\nX-Custom:v\r\n\r\n");
  ASSERT_EQ(r.status, ParseStatus::Complete);
  EXPECT_EQ(r.request.headers.at("content-type"), "application/json");
  EXPECT_EQ(r.request.headers.at("x-custom"), "v");
}

TEST(Parser, SplitsPathAndQuery) {
  auto r = parse_request("GET /items?offset=10&limit=5 HTTP/1.1\r\n\r\n");
  ASSERT_EQ(r.status, ParseStatus::Complete);
  EXPECT_EQ(r.request.target, "/items?offset=10&limit=5");
  EXPECT_EQ(r.request.path, "/items");
  EXPECT_EQ(r.request.query, "offset=10&limit=5");
  auto q = parse_query(r.request.query);
  EXPECT_EQ(q.at("offset"), "10");
  EXPECT_EQ(q.at("limit"), "5");
}

TEST(Parser, QueryDecoding) {
  auto q = parse_query("a=b%20c+d&flag&x=1&x=2");
  EXPECT_EQ(q.at("a"), "b c d");
  EXPECT_EQ(q.at("flag"), "");
  EXPECT_EQ(q.at("x"), "1");  // первое значение побеждает
}

static std::string post_raw(const std::string& body) {
  return "POST /items HTTP/1.1\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" +
         body;
}

TEST(Parser, PostWithBody) {
  const std::string body = R"({"name":"x","value":1})";
  const std::string raw = post_raw(body);
  auto r = parse_request(raw);
  ASSERT_EQ(r.status, ParseStatus::Complete);
  EXPECT_EQ(r.request.body, body);
  EXPECT_EQ(r.consumed, raw.size());
}

TEST(Parser, IncompleteHeaders) {
  EXPECT_EQ(parse_request("GET /health HTTP/1.1\r\nHost: x\r\n").status, ParseStatus::Incomplete);
  EXPECT_EQ(parse_request("").status, ParseStatus::Incomplete);
}

TEST(Parser, IncompleteBody) {
  auto r = parse_request("POST /a HTTP/1.1\r\nContent-Length: 10\r\n\r\nabc");
  EXPECT_EQ(r.status, ParseStatus::Incomplete);
}

// Запрос приходит по одному байту: до последнего байта Incomplete, потом Complete.
TEST(Parser, ByteByByteDelivery) {
  const std::string raw = post_raw(R"({"name":"x","value":1})");
  for (std::size_t i = 0; i < raw.size(); ++i)
    ASSERT_EQ(parse_request(std::string_view(raw).substr(0, i)).status, ParseStatus::Incomplete)
        << "prefix length " << i;
  EXPECT_EQ(parse_request(raw).status, ParseStatus::Complete);
}

// Два запроса в одном буфере: разбираем по очереди через consumed.
TEST(Parser, TwoPipelinedRequests) {
  const std::string first = "GET /health HTTP/1.1\r\n\r\n";
  const std::string second = post_raw("{}");
  const std::string raw = first + second;

  auto r1 = parse_request(raw);
  ASSERT_EQ(r1.status, ParseStatus::Complete);
  EXPECT_EQ(r1.consumed, first.size());
  EXPECT_EQ(r1.request.path, "/health");

  auto r2 = parse_request(std::string_view(raw).substr(r1.consumed));
  ASSERT_EQ(r2.status, ParseStatus::Complete);
  EXPECT_EQ(r2.request.method, "POST");
  EXPECT_EQ(r2.request.body, "{}");
}

TEST(Parser, MalformedRequestsAreRejected) {
  const std::vector<std::string> bad = {
      "GET /health\r\n\r\n",                              // нет версии
      "GET /health HTTP/2.0\r\n\r\n",                     // версия не поддерживается
      "GET health HTTP/1.1\r\n\r\n",                      // target без '/'
      "GET /a HTTP/1.1\r\nNoColonHeader\r\n\r\n",         // заголовок без ':'
      "GET /a HTTP/1.1\r\nHost : x\r\n\r\n",              // пробел перед ':'
      "POST /a HTTP/1.1\r\nContent-Length: abc\r\n\r\n",  // не число
      "POST /a HTTP/1.1\r\nContent-Length: -1\r\n\r\n",   // отрицательное
      "POST /a HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nxx",  // конфликт
      "POST /a HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",   // chunked не поддержан
      "POST /a HTTP/1.1\r\nContent-Length: 99999999999\r\n\r\n",  // слишком большое
      "\r\n\r\n",
  };
  for (const auto& raw : bad) {
    SCOPED_TRACE(raw);
    EXPECT_EQ(parse_request(raw).status, ParseStatus::BadRequest);
  }
}

TEST(Parser, TooLargeInputIsRejected) {
  EXPECT_EQ(parse_request("GET /" + std::string(kMaxHeaderBytes + 1, 'a')).status,
            ParseStatus::BadRequest);
  const std::string big =
      "POST /a HTTP/1.1\r\nContent-Length: " + std::to_string(kMaxBodyBytes + 1) + "\r\n\r\n";
  EXPECT_EQ(parse_request(big).status, ParseStatus::BadRequest);
}

TEST(Parser, KeepAliveRules) {
  EXPECT_TRUE(parse_request("GET / HTTP/1.1\r\n\r\n").request.keep_alive);
  EXPECT_FALSE(parse_request("GET / HTTP/1.1\r\nConnection: close\r\n\r\n").request.keep_alive);
  EXPECT_FALSE(parse_request("GET / HTTP/1.0\r\n\r\n").request.keep_alive);
  EXPECT_TRUE(parse_request("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n").request.keep_alive);
}

// ---------- сериализация ответа ----------

TEST(Response, JsonWithContentLength) {
  Response r;
  r.status = 200;
  r.body = R"({"status":"ok"})";
  EXPECT_EQ(serialize(r),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\n"
            "Connection: keep-alive\r\n\r\n{\"status\":\"ok\"}");
}

TEST(Response, NoContentHasNoBodyHeaders) {
  Response r;
  r.status = 204;
  EXPECT_EQ(serialize(r), "HTTP/1.1 204 No Content\r\nConnection: keep-alive\r\n\r\n");
}

TEST(Response, ReasonPhrasesAndConnectionClose) {
  Response r;
  r.status = 404;
  r.body = "{}";
  r.keep_alive = false;
  const auto s = serialize(r);
  EXPECT_EQ(s.rfind("HTTP/1.1 404 Not Found\r\n", 0), 0u);
  EXPECT_NE(s.find("Connection: close\r\n"), std::string::npos);
  EXPECT_EQ(reason_phrase(409), "Conflict");
  EXPECT_EQ(reason_phrase(503), "Service Unavailable");
}