#include "api/router.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace api {
namespace {

using json = nlohmann::json;

constexpr std::size_t kDefaultLimit = 100;
constexpr std::size_t kMaxLimit = 1000;

http::Response make_response(int status, const json& body, bool keep_alive) {
  http::Response r;
  r.status = status;
  r.body = body.dump(-1, ' ', false, json::error_handler_t::replace);
  r.keep_alive = keep_alive;
  return r;
}

http::Response error_response(int status, std::string_view message, bool keep_alive) {
  json body = json::object();
  body["error"] = std::string(message);
  return make_response(status, body, keep_alive);
}

json to_json(const Item& item) {
  json j = json::object();
  j["id"] = item.id;
  j["name"] = item.name;
  j["value"] = item.value;
  j["version"] = item.version;
  return j;
}

std::optional<std::uint64_t> parse_u64(std::string_view s) {
  if (s.empty()) return std::nullopt;
  std::uint64_t v = 0;
  const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
  return v;
}

std::optional<json> parse_object(const std::string& body) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return std::nullopt;
  return j;
}

enum class Field { Missing, Ok, Invalid };

Field read_string(const json& j, const char* key, std::string& out) {
  const auto it = j.find(key);
  if (it == j.end()) return Field::Missing;
  if (!it->is_string()) return Field::Invalid;
  out = it->get<std::string>();
  return Field::Ok;
}

Field read_int(const json& j, const char* key, std::int64_t& out) {
  const auto it = j.find(key);
  if (it == j.end()) return Field::Missing;
  if (!it->is_number_integer()) return Field::Invalid;
  if (it->is_number_unsigned() &&
      it->get<std::uint64_t>() >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return Field::Invalid;
  out = it->get<std::int64_t>();
  return Field::Ok;
}

// false, если version передан, но это не неотрицательное целое
bool read_optional_version(const json& j, std::optional<std::uint64_t>& out) {
  const auto it = j.find("version");
  if (it == j.end()) return true;
  if (!it->is_number_unsigned()) return false;
  out = it->get<std::uint64_t>();
  return true;
}

http::Response from_result(const StoreResult& r, bool keep_alive) {
  switch (r.status) {
    case Status::Ok:
      return make_response(200, to_json(*r.item), keep_alive);
    case Status::NotFound:
      return error_response(404, "item not found", keep_alive);
    case Status::Conflict:
      return error_response(409, "version conflict", keep_alive);
  }
  return error_response(500, "internal error", keep_alive);
}

// ---------- обработчики ----------

http::Response list_items(const http::Request& req, Store& store) {
  const auto query = http::parse_query(req.query);
  std::size_t offset = 0;
  std::size_t limit = kDefaultLimit;

  if (const auto it = query.find("offset"); it != query.end()) {
    const auto v = parse_u64(it->second);
    if (!v) return error_response(400, "offset must be a non-negative integer", req.keep_alive);
    offset = static_cast<std::size_t>(*v);
  }
  if (const auto it = query.find("limit"); it != query.end()) {
    const auto v = parse_u64(it->second);
    if (!v) return error_response(400, "limit must be a non-negative integer", req.keep_alive);
    limit = static_cast<std::size_t>(*v);
  }
  if (limit > kMaxLimit) limit = kMaxLimit;

  json arr = json::array();
  for (const auto& item : store.list(offset, limit)) arr.push_back(to_json(item));
  return make_response(200, arr, req.keep_alive);
}

http::Response create_item(const http::Request& req, Store& store) {
  const auto body = parse_object(req.body);
  if (!body) return error_response(400, "body must be a JSON object", req.keep_alive);

  std::string name;
  std::int64_t value = 0;
  if (read_string(*body, "name", name) != Field::Ok || read_int(*body, "value", value) != Field::Ok)
    return error_response(400, "name (string) and value (integer) are required", req.keep_alive);

  return make_response(201, to_json(store.create(std::move(name), value)), req.keep_alive);
}

http::Response get_item(const http::Request& req, Store& store, std::uint64_t id) {
  const auto item = store.get(id);
  if (!item) return error_response(404, "item not found", req.keep_alive);
  return make_response(200, to_json(*item), req.keep_alive);
}

http::Response replace_item(const http::Request& req, Store& store, std::uint64_t id) {
  const auto body = parse_object(req.body);
  if (!body) return error_response(400, "body must be a JSON object", req.keep_alive);

  std::string name;
  std::int64_t value = 0;
  if (read_string(*body, "name", name) != Field::Ok || read_int(*body, "value", value) != Field::Ok)
    return error_response(400, "name (string) and value (integer) are required", req.keep_alive);

  std::optional<std::uint64_t> version;
  if (!read_optional_version(*body, version))
    return error_response(400, "version must be a non-negative integer", req.keep_alive);

  return from_result(store.replace(id, std::move(name), value, version), req.keep_alive);
}

http::Response patch_item(const http::Request& req, Store& store, std::uint64_t id) {
  const auto body = parse_object(req.body);
  if (!body) return error_response(400, "body must be a JSON object", req.keep_alive);

  std::optional<std::string> name;
  std::optional<std::int64_t> value;

  std::string name_tmp;
  const Field name_state = read_string(*body, "name", name_tmp);
  if (name_state == Field::Invalid)
    return error_response(400, "name must be a string", req.keep_alive);
  if (name_state == Field::Ok) name = std::move(name_tmp);

  std::int64_t value_tmp = 0;
  const Field value_state = read_int(*body, "value", value_tmp);
  if (value_state == Field::Invalid)
    return error_response(400, "value must be an integer", req.keep_alive);
  if (value_state == Field::Ok) value = value_tmp;

  if (!name && !value)
    return error_response(400, "nothing to update: pass name and/or value", req.keep_alive);

  std::optional<std::uint64_t> version;
  if (!read_optional_version(*body, version))
    return error_response(400, "version must be a non-negative integer", req.keep_alive);

  return from_result(store.patch(id, std::move(name), value, version), req.keep_alive);
}

http::Response delete_item(const http::Request& req, Store& store, std::uint64_t id) {
  if (!store.remove(id)) return error_response(404, "item not found", req.keep_alive);
  http::Response r;
  r.status = 204;
  r.keep_alive = req.keep_alive;
  return r;
}

http::Response increment_item(const http::Request& req, Store& store, std::uint64_t id) {
  std::int64_t delta = 1;  // пустое тело = +1
  if (!req.body.empty()) {
    const auto body = parse_object(req.body);
    if (!body) return error_response(400, "body must be a JSON object", req.keep_alive);
    if (read_int(*body, "delta", delta) != Field::Ok)
      return error_response(400, "delta (integer) is required", req.keep_alive);
  }
  const auto item = store.increment(id, delta);
  if (!item) return error_response(404, "item not found", req.keep_alive);
  return make_response(200, to_json(*item), req.keep_alive);
}

// ---------- маршрутизация ----------

http::Response route(const http::Request& req, Store& store) {
  const bool ka = req.keep_alive;
  const std::string_view path = req.path;
  const std::string& m = req.method;
  const auto not_allowed = [&] { return error_response(405, "method not allowed", ka); };

  if (path == "/health") {
    if (m != "GET") return not_allowed();
    http::Response r;
    r.status = 200;
    r.body = R"({"status":"ok"})";
    r.keep_alive = ka;
    return r;
  }

  if (path == "/items") {
    if (m == "GET") return list_items(req, store);
    if (m == "POST") return create_item(req, store);
    return not_allowed();
  }

  constexpr std::string_view prefix = "/items/";
  if (path.substr(0, prefix.size()) == prefix) {
    const std::string_view rest = path.substr(prefix.size());
    const auto slash = rest.find('/');
    const auto id = parse_u64(rest.substr(0, slash));
    if (!id) return error_response(404, "not found", ka);

    if (slash == std::string_view::npos) {
      if (m == "GET") return get_item(req, store, *id);
      if (m == "PUT") return replace_item(req, store, *id);
      if (m == "PATCH") return patch_item(req, store, *id);
      if (m == "DELETE") return delete_item(req, store, *id);
      return not_allowed();
    }
    if (rest.substr(slash) == "/increment") {
      if (m == "POST") return increment_item(req, store, *id);
      return not_allowed();
    }
  }

  return error_response(404, "not found", ka);
}

}  // namespace

http::Response handle(const http::Request& req, Store& store) {
  try {
    return route(req, store);
  } catch (...) {
    return error_response(500, "internal error", req.keep_alive);
  }
}

}  // namespace api
