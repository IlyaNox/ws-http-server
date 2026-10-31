# Многопоточный HTTP-сервер: work-stealing пул

## Команда и роли

| Участник | GitHub | Зона ответственности (основная) |
|----------|--------|----------------------------------|
| Гришин Илья | https://github.com/IlyaNox | сеть (сокеты, accept, keep-alive), HTTP-парсер, graceful shutdown |
| Фишман Артем | https://github.com/artemfishman7 | work-stealing пул, хранилище, тесты и нагрузочные замеры |

## Сборка и запуск

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/server --port 8080 --threads 8
```

Санитайзеры:

```bash
cmake -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" && cmake --build build-tsan -j
cmake -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g" && cmake --build build-asan -j
```

## Статус

- [x] v0.0: репозиторий, CMake, роли
- [ ] v0.1: `/health`, keep-alive
- [ ] v0.2: 8 ручек, unit-тесты
- [ ] v0.3: work-stealing пул, TSan чистый
- [ ] v1.0: замеры, отчёт, защита
