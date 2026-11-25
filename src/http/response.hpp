#pragma once
#include <string>
#include <string_view>

namespace http {

struct Response {
  int status = 200;
  std::string body;  // JSON
  bool keep_alive = true;
};

std::string_view reason_phrase(int status);

// Готовые байты для отправки в сокет.
std::string serialize(const Response& response);

}  // namespace http