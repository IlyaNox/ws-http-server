#include "http/response.hpp"

namespace http {

std::string_view reason_phrase(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 409:
      return "Conflict";
    case 500:
      return "Internal Server Error";
    case 503:
      return "Service Unavailable";
    default:
      return "Unknown";
  }
}

std::string serialize(const Response& r) {
  std::string out;
  out += "HTTP/1.1 ";
  out += std::to_string(r.status);
  out += ' ';
  out += reason_phrase(r.status);
  out += "\r\n";
  const bool has_body = (r.status != 204);  // у 204 нет тела и Content-Length
  if (has_body) {
    out += "Content-Type: application/json\r\n";
    out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
  }
  out += r.keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
  out += "\r\n";
  if (has_body) out += r.body;
  return out;
}

}  // namespace http