#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace http {

struct Request {
  std::string method;
  std::string target;                                    // как пришло: "/items?limit=5"
  std::string path;                                      // "/items"
  std::string query;                                     // "limit=5"
  std::unordered_map<std::string, std::string> headers;  // имена в нижнем регистре
  std::string body;
  bool keep_alive = true;
};

enum class ParseStatus {
  Complete,    // запрос разобран целиком
  Incomplete,  // данных пока мало, нужно прочитать ещё
  BadRequest   // ошибка, отвечать 400 и закрывать соединение
};

struct ParseResult {
  ParseStatus status = ParseStatus::Incomplete;
  std::size_t consumed = 0;  // сколько байт буфера занял запрос (для keep-alive)
  Request request;
};

inline constexpr std::size_t kMaxHeaderBytes = 16 * 1024;
inline constexpr std::size_t kMaxBodyBytes = 1024 * 1024;

// Разбирает ОДИН запрос из начала буфера. Остаток буфера (после consumed)
// может содержать следующий запрос (keep-alive / pipelining).
ParseResult parse_request(std::string_view buffer);

// "offset=10&limit=5" -> {offset: "10", limit: "5"}. Значения декодируются (%20, +).
std::unordered_map<std::string, std::string> parse_query(std::string_view query);

}  // namespace http