#pragma once
// 极简测试框架：不引入 Catch2 / GTest，保证 host 端零依赖即可跑回归。
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace fx {
namespace test {

struct TestCase {
  std::string name;
  void (*fn)();
};

inline std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

inline int& Failures() {
  static int failures = 0;
  return failures;
}

inline bool Register(const char* name, void (*fn)()) {
  Registry().push_back(TestCase{name, fn});
  return true;
}

inline void Check(bool ok, const char* expr, const char* file, int line) {
  if (!ok) {
    ++Failures();
    std::cerr << "  FAIL " << file << ":" << line << "  " << expr << "\n";
  }
}

inline void CheckNear(double a, double b, double eps, const char* expr, const char* file,
                      int line) {
  if (!(std::fabs(a - b) <= eps)) {
    ++Failures();
    std::cerr << "  FAIL " << file << ":" << line << "  " << expr << " (" << a << " vs " << b
              << ")\n";
  }
}

}  // namespace test
}  // namespace fx

#define FX_TEST(name)                                                          \
  static void name();                                                          \
  static bool name##_registered = ::fx::test::Register(#name, &name);           \
  static void name()

#define CHECK(expr) ::fx::test::Check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NEAR(a, b, eps) \
  ::fx::test::CheckNear((a), (b), (eps), #a " ~ " #b, __FILE__, __LINE__)
