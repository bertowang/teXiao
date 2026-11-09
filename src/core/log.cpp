#include "core/log.h"

#include <mutex>

namespace fx {
namespace log {
namespace {
std::mutex g_mutex;
Sink g_sink;
}  // namespace

void SetSink(Sink sink) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_sink = std::move(sink);
}

void Emit(LogLevel level, const std::string& text) {
  Sink sink;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    sink = g_sink;
  }
  if (sink) sink(level, text);
}

}  // namespace log
}  // namespace fx
