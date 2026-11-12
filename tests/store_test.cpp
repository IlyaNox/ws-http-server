#include "store/store.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <thread>
#include <vector>

// ---------- Базовое поведение ----------

TEST(Store, CreateAssignsIncreasingIdsAndVersionOne) {
  Store s;
  Item a = s.create("a", 1);
  Item b = s.create("b", 2);
  EXPECT_EQ(a.version, 1u);
  EXPECT_EQ(b.version, 1u);
  EXPECT_LT(a.id, b.id);
}

TEST(Store, GetExistingAndMissing) {
  Store s;
  Item a = s.create("a", 5);
  auto got = s.get(a.id);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->name, "a");
  EXPECT_EQ(got->value, 5);
  EXPECT_FALSE(s.get(9999).has_value());
}

TEST(Store, ReplaceBumpsVersion) {
  Store s;
  Item a = s.create("a", 1);
  auto r = s.replace(a.id, "b", 2, std::nullopt);
  ASSERT_EQ(r.status, Status::Ok);
  EXPECT_EQ(r.item->name, "b");
  EXPECT_EQ(r.item->value, 2);
  EXPECT_EQ(r.item->version, 2u);
}

TEST(Store, ReplaceWithMatchingVersionSucceeds) {
  Store s;
  Item a = s.create("a", 1);
  auto r = s.replace(a.id, "b", 2, 1);
  EXPECT_EQ(r.status, Status::Ok);
}

TEST(Store, ReplaceConflictOnWrongVersion) {
  Store s;
  Item a = s.create("a", 1);
  auto r = s.replace(a.id, "b", 2, 42);
  EXPECT_EQ(r.status, Status::Conflict);
  EXPECT_FALSE(r.item.has_value());
  EXPECT_EQ(s.get(a.id)->name, "a");  // объект не изменился
  EXPECT_EQ(s.get(a.id)->version, 1u);
}

TEST(Store, ReplaceMissingIsNotFound) {
  Store s;
  EXPECT_EQ(s.replace(1, "x", 0, std::nullopt).status, Status::NotFound);
}

TEST(Store, PatchChangesOnlyGivenFields) {
  Store s;
  Item a = s.create("a", 1);
  auto r = s.patch(a.id, std::nullopt, 10, std::nullopt);
  ASSERT_EQ(r.status, Status::Ok);
  EXPECT_EQ(r.item->name, "a");
  EXPECT_EQ(r.item->value, 10);
  EXPECT_EQ(r.item->version, 2u);

  r = s.patch(a.id, "z", std::nullopt, std::nullopt);
  ASSERT_EQ(r.status, Status::Ok);
  EXPECT_EQ(r.item->name, "z");
  EXPECT_EQ(r.item->value, 10);
  EXPECT_EQ(r.item->version, 3u);
}

TEST(Store, PatchConflictOnWrongVersion) {
  Store s;
  Item a = s.create("a", 1);
  EXPECT_EQ(s.patch(a.id, "z", std::nullopt, 7).status, Status::Conflict);
  EXPECT_EQ(s.get(a.id)->name, "a");
}

TEST(Store, PatchMissingIsNotFound) {
  Store s;
  EXPECT_EQ(s.patch(1, "z", std::nullopt, std::nullopt).status, Status::NotFound);
}

TEST(Store, RemoveWorksOnce) {
  Store s;
  Item a = s.create("a", 1);
  EXPECT_TRUE(s.remove(a.id));
  EXPECT_FALSE(s.remove(a.id));
  EXPECT_FALSE(s.get(a.id).has_value());
}

TEST(Store, IdIsNotReusedAfterRemove) {
  Store s;
  Item a = s.create("a", 1);
  s.remove(a.id);
  Item b = s.create("b", 1);
  EXPECT_GT(b.id, a.id);
}

TEST(Store, IncrementAddsDeltaAndBumpsVersion) {
  Store s;
  Item a = s.create("a", 10);
  auto r = s.increment(a.id, 5);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->value, 15);
  EXPECT_EQ(r->version, 2u);
  r = s.increment(a.id, -20);
  EXPECT_EQ(r->value, -5);
}

TEST(Store, IncrementMissingReturnsNullopt) {
  Store s;
  EXPECT_FALSE(s.increment(1, 1).has_value());
}

TEST(Store, ListPagination) {
  Store s;
  for (int i = 0; i < 10; ++i) s.create("n" + std::to_string(i), i);
  EXPECT_EQ(s.list(0, 3).size(), 3u);
  EXPECT_EQ(s.list(8, 100).size(), 2u);
  EXPECT_TRUE(s.list(50, 10).empty());
  EXPECT_TRUE(s.list(0, 0).empty());
  auto page = s.list(2, 2);
  ASSERT_EQ(page.size(), 2u);
  EXPECT_LT(page[0].id, page[1].id);
}

// ---------- Многопоточные тесты ----------

TEST(StoreConcurrent, IncrementIsExact) {
  Store s;
  Item a = s.create("counter", 0);
  constexpr int kThreads = 8, kIters = 10000;
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t)
    ts.emplace_back([&] {
      for (int i = 0; i < kIters; ++i) s.increment(a.id, 1);
    });
  for (auto& t : ts) t.join();
  EXPECT_EQ(s.get(a.id)->value, kThreads * kIters);
  EXPECT_EQ(s.get(a.id)->version, 1u + kThreads * kIters);
}

TEST(StoreConcurrent, CreateGivesUniqueIds) {
  Store s;
  constexpr int kThreads = 8, kIters = 500;
  std::vector<std::vector<std::uint64_t>> ids(kThreads);
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t)
    ts.emplace_back([&, t] {
      for (int i = 0; i < kIters; ++i) ids[t].push_back(s.create("x", i).id);
    });
  for (auto& t : ts) t.join();
  std::set<std::uint64_t> all;
  for (auto& v : ids) all.insert(v.begin(), v.end());
  EXPECT_EQ(all.size(), static_cast<size_t>(kThreads * kIters));
  EXPECT_EQ(s.list(0, 100000).size(), static_cast<size_t>(kThreads * kIters));
}

// Оптимистичная блокировка: из N потоков, ждущих версию 1, выигрывает ровно один.
TEST(StoreConcurrent, OptimisticLockOnlyOneWinner) {
  Store s;
  Item a = s.create("a", 0);
  constexpr int kThreads = 8;
  std::atomic<int> ok{0}, conflict{0}, ready{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t)
    ts.emplace_back([&, t] {
      ++ready;
      while (ready.load() < kThreads) {
      }  // стартуем одновременно
      auto r = s.replace(a.id, "t" + std::to_string(t), t, 1);
      if (r.status == Status::Ok)
        ++ok;
      else if (r.status == Status::Conflict)
        ++conflict;
    });
  for (auto& t : ts) t.join();
  EXPECT_EQ(ok.load(), 1);
  EXPECT_EQ(conflict.load(), kThreads - 1);
  EXPECT_EQ(s.get(a.id)->version, 2u);
}

// Параллельные GET, increment и DELETE одних и тех же объектов не должны падать.
TEST(StoreConcurrent, GetIncrementRemoveDoNotCrash) {
  Store s;
  constexpr int kItems = 500;
  std::vector<std::uint64_t> ids;
  for (int i = 0; i < kItems; ++i) ids.push_back(s.create("x", 0).id);

  std::vector<std::thread> ts;
  for (int t = 0; t < 3; ++t)
    ts.emplace_back([&] {
      for (int round = 0; round < 50; ++round)
        for (auto id : ids) s.get(id);
    });
  for (int t = 0; t < 2; ++t)
    ts.emplace_back([&] {
      for (int round = 0; round < 50; ++round)
        for (auto id : ids) s.increment(id, 1);
    });
  ts.emplace_back([&] {
    for (auto id : ids) s.remove(id);
  });
  for (auto& t : ts) t.join();

  for (auto id : ids) EXPECT_FALSE(s.get(id).has_value());
}

// list() во время параллельных create() всегда возвращает упорядоченные по id данные.
TEST(StoreConcurrent, ListWhileCreatingIsSorted) {
  Store s;
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    for (int i = 0; i < 5000; ++i) s.create("x", i);
    stop = true;
  });
  std::thread reader([&] {
    while (!stop.load()) {
      auto v = s.list(0, 100);
      for (size_t i = 1; i < v.size(); ++i) EXPECT_LT(v[i - 1].id, v[i].id);
    }
  });
  writer.join();
  reader.join();
}