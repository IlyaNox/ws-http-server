#pragma once

#include "http/parser.hpp"
#include "http/response.hpp"
#include "store/store.hpp"

namespace api {

// Обрабатывает один HTTP-запрос и возвращает ответ.
// Потокобезопасна, если Store потокобезопасен. Исключения наружу не выбрасывает (-> 500).
http::Response handle(const http::Request& request, Store& store);

}  // namespace api
