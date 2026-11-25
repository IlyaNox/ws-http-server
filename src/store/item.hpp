#pragma once
#include <cstdint>
#include <string>

struct Item {
  std::uint64_t id = 0;
  std::string name;
  std::int64_t value = 0;
  std::uint64_t version = 1;
};