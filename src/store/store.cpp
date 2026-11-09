#include "store/store.hpp"

#include <iterator>
#include <mutex>
#include <utility>

Item Store::create(std::string name, std::int64_t value) {
  const std::uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
  Item item{id, std::move(name), value, 1};
  std::unique_lock lock(mutex_);
  items_.emplace(id, item);
  return item;
}

std::optional<Item> Store::get(std::uint64_t id) const {
  std::shared_lock lock(mutex_);
  auto it = items_.find(id);
  if (it == items_.end()) return std::nullopt;
  return it->second;
}

std::vector<Item> Store::list(std::size_t offset, std::size_t limit) const {
  std::vector<Item> out;
  std::shared_lock lock(mutex_);
  if (offset >= items_.size()) return out;
  auto it = std::next(items_.begin(), static_cast<std::ptrdiff_t>(offset));
  for (; it != items_.end() && out.size() < limit; ++it) out.push_back(it->second);
  return out;
}

StoreResult Store::replace(std::uint64_t id, std::string name, std::int64_t value,
                           std::optional<std::uint64_t> expected_version) {
  std::unique_lock lock(mutex_);
  auto it = items_.find(id);
  if (it == items_.end()) return {Status::NotFound, std::nullopt};
  if (expected_version && *expected_version != it->second.version)
    return {Status::Conflict, std::nullopt};
  it->second.name = std::move(name);
  it->second.value = value;
  ++it->second.version;
  return {Status::Ok, it->second};
}

StoreResult Store::patch(std::uint64_t id, std::optional<std::string> name,
                         std::optional<std::int64_t> value,
                         std::optional<std::uint64_t> expected_version) {
  std::unique_lock lock(mutex_);
  auto it = items_.find(id);
  if (it == items_.end()) return {Status::NotFound, std::nullopt};
  if (expected_version && *expected_version != it->second.version)
    return {Status::Conflict, std::nullopt};
  if (name) it->second.name = std::move(*name);
  if (value) it->second.value = *value;
  ++it->second.version;
  return {Status::Ok, it->second};
}

bool Store::remove(std::uint64_t id) {
  std::unique_lock lock(mutex_);
  return items_.erase(id) > 0;
}

std::optional<Item> Store::increment(std::uint64_t id, std::int64_t delta) {
  std::unique_lock lock(mutex_);
  auto it = items_.find(id);
  if (it == items_.end()) return std::nullopt;
  it->second.value += delta;
  ++it->second.version;
  return it->second;
}