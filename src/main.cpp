// Неделя 1: "hello world" с разбором аргументов.
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

struct Config {
  int port = 8080;
  unsigned threads = std::thread::hardware_concurrency();
};

bool parse_args(int argc, char** argv, Config& cfg) {
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if ((arg == "--port" || arg == "--threads") && i + 1 < argc) {
      long v = std::strtol(argv[++i], nullptr, 10);
      if (v <= 0 || v > 65535) return false;
      if (arg == "--port")
        cfg.port = static_cast<int>(v);
      else
        cfg.threads = static_cast<unsigned>(v);
    } else {
      return false;
    }
  }
  if (cfg.threads == 0) cfg.threads = 1;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Config cfg;
  if (!parse_args(argc, argv, cfg)) {
    std::cerr << "usage: " << argv[0] << " --port <1-65535> --threads <N>\n";
    return 1;
  }
  std::cout << "ws-http-server (work-stealing pool): port=" << cfg.port
            << " threads=" << cfg.threads << "\n";
  return 0;
}
