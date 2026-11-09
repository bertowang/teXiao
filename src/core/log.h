#pragma once
#include <functional>
#include <string>

#include "fx/types.h"

namespace fx {
namespace log {

using Sink = std::function<void(LogLevel, const std::string&)>;

void SetSink(Sink sink);
void Emit(LogLevel level, const std::string& text);

inline void Debug(const std::string& s) { Emit(LogLevel::Debug, s); }
inline void Info(const std::string& s) { Emit(LogLevel::Info, s); }
inline void Warn(const std::string& s) { Emit(LogLevel::Warn, s); }
inline void Error(const std::string& s) { Emit(LogLevel::Error, s); }

}  // namespace log
}  // namespace fx
