#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "store/item.hpp"

enum class Status { Ok, NotFound, Conflict };

struct StoreResult {
  Status status;
  std::optional<Item> item;  // заполнен только при Ok
};

// Потокобезопасное хранилище в памяти.
// Все методы возвращают копии Item, поэтому висячих ссылок после remove() не бывает.
class Store {
 public:
  Item create(std::string name, std::int64_t value);
  std::optional<Item> get(std::uint64_t id) const;
  std::vector<Item> list(std::size_t offset, std::size_t limit) const;

  StoreResult replace(std::uint64_t id, std::string name, std::int64_t value,
                      std::optional<std::uint64_t> expected_version);
  StoreResult patch(std::uint64_t id, std::optional<std::string> name,
                    std::optional<std::int64_t> value,
                    std::optional<std::uint64_t> expected_version);

  bool remove(std::uint64_t id);
  std::optional<Item> increment(std::uint64_t id, std::int64_t delta);

 private:
  mutable std::shared_mutex mutex_;
  std::map<std::uint64_t, Item> items_;
  std::atomic<std::uint64_t> next_id_{1};
};