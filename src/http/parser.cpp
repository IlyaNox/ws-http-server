#include "http/parser.hpp"

#include <cctype>
#include <utility>

namespace http {
namespace {

constexpr auto npos = std::string_view::npos;

ParseResult bad_request() {
  ParseResult r;
  r.status = ParseStatus::BadRequest;
  return r;
}

std::string to_lower(std::string_view s) {
  std::string out(s);
  for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

bool is_token(std::string_view s) {
  if (s.empty()) return false;
  for (char ch : s) {
    if (std::isalnum(static_cast<unsigned char>(ch))) continue;
    if (std::string_view("!#$%&'*+-.^_`|~").find(ch) != npos) continue;
    return false;
  }
  return true;
}

// Только цифры, без знака; отсекаем слишком большие значения без переполнения.
bool parse_size(std::string_view s, std::size_t& out) {
  if (s.empty()) return false;
  std::size_t v = 0;
  for (char ch : s) {
    if (ch < '0' || ch > '9') return false;
    if (v > kMaxBodyBytes) return false;
    v = v * 10 + static_cast<std::size_t>(ch - '0');
  }
  out = v;
  return true;
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string url_decode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      out += ' ';
    } else if (s[i] == '%' && i + 2 < s.size()) {
      const int hi = hex_value(s[i + 1]);
      const int lo = hex_value(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += static_cast<char>(hi * 16 + lo);
        i += 2;
      } else {
        out += '%';
      }
    } else {
      out += s[i];
    }
  }
  return out;
}

}  // namespace

std::unordered_map<std::string, std::string> parse_query(std::string_view query) {
  std::unordered_map<std::string, std::string> out;
  while (!query.empty()) {
    const auto amp = query.find('&');
    const std::string_view pair = query.substr(0, amp);
    query = (amp == npos) ? std::string_view{} : query.substr(amp + 1);
    if (pair.empty()) continue;
    const auto eq = pair.find('=');
    auto key = url_decode(pair.substr(0, eq));
    auto value = (eq == npos) ? std::string{} : url_decode(pair.substr(eq + 1));
    out.emplace(std::move(key), std::move(value));  // первое значение побеждает
  }
  return out;
}

ParseResult parse_request(std::string_view buf) {
  const auto head_end = buf.find("\r\n\r\n");
  if (head_end == npos) {
    if (buf.size() > kMaxHeaderBytes) return bad_request();
    return ParseResult{};  // Incomplete
  }
  if (head_end > kMaxHeaderBytes) return bad_request();

  const std::string_view head = buf.substr(0, head_end);

  // --- строка запроса: METHOD SP target SP version ---
  const auto line_end = head.find("\r\n");
  const std::string_view line = head.substr(0, line_end);
  std::string_view rest = (line_end == npos) ? std::string_view{} : head.substr(line_end + 2);

  const auto sp1 = line.find(' ');
  if (sp1 == npos) return bad_request();
  const auto sp2 = line.find(' ', sp1 + 1);
  if (sp2 == npos) return bad_request();

  const std::string_view method = line.substr(0, sp1);
  const std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  const std::string_view version = line.substr(sp2 + 1);

  if (!is_token(method)) return bad_request();
  if (target.empty() || target.front() != '/' || target.find_first_of(" \t\r\n") != npos)
    return bad_request();
  if (version != "HTTP/1.1" && version != "HTTP/1.0") return bad_request();

  Request req;
  req.method = std::string(method);
  req.target = std::string(target);
  const auto q = target.find('?');
  req.path = std::string(target.substr(0, q));
  if (q != npos) req.query = std::string(target.substr(q + 1));

  // --- заголовки ---
  while (!rest.empty()) {
    const auto e = rest.find("\r\n");
    const std::string_view h = rest.substr(0, e);
    rest = (e == npos) ? std::string_view{} : rest.substr(e + 2);

    if (h.find_first_of("\r\n") != npos) return bad_request();
    const auto colon = h.find(':');
    if (colon == npos) return bad_request();
    const std::string_view name = h.substr(0, colon);
    if (!is_token(name)) return bad_request();  // в т.ч. пробел перед ':'

    const std::string_view value = trim(h.substr(colon + 1));
    const std::string key = to_lower(name);
    auto [it, inserted] = req.headers.emplace(key, std::string(value));
    if (!inserted) {
      if (key == "content-length") {
        if (it->second != value) return bad_request();
      } else {
        it->second += ", ";
        it->second += value;
      }
    }
  }

  // chunked не поддерживаем (защита от request smuggling)
  if (req.headers.count("transfer-encoding")) return bad_request();

  // --- тело ---
  std::size_t content_length = 0;
  if (const auto it = req.headers.find("content-length"); it != req.headers.end()) {
    if (!parse_size(it->second, content_length) || content_length > kMaxBodyBytes)
      return bad_request();
  }

  const std::size_t body_start = head_end + 4;
  if (buf.size() - body_start < content_length) return ParseResult{};  // Incomplete
  req.body = std::string(buf.substr(body_start, content_length));

  // --- keep-alive ---
  std::string conn;
  if (const auto it = req.headers.find("connection"); it != req.headers.end())
    conn = to_lower(it->second);
  const bool has_close = conn.find("close") != std::string::npos;
  const bool has_keep_alive = conn.find("keep-alive") != std::string::npos;
  req.keep_alive = (version == "HTTP/1.1") ? !has_close : (has_keep_alive && !has_close);

  ParseResult result;
  result.status = ParseStatus::Complete;
  result.consumed = body_start + content_length;
  result.request = std::move(req);
  return result;
}

}  // namespace http