#include "unit/test_framework.h"

#include <exception>
#include <iostream>

int main() {
  size_t passed = 0;
  const size_t total = fx::test::Registry().size();
  for (const auto& tc : fx::test::Registry()) {
    const int before = fx::test::Failures();
    try {
      tc.fn();
    } catch (const std::exception& e) {
      ++fx::test::Failures();
      std::cerr << "  FAIL " << tc.name << " threw: " << e.what() << "\n";
    } catch (...) {
      ++fx::test::Failures();
      std::cerr << "  FAIL " << tc.name << " threw unknown exception\n";
    }
    if (fx::test::Failures() == before) ++passed;
    std::cout << (fx::test::Failures() == before ? "[ ok ] " : "[FAIL] ") << tc.name << "\n";
  }
  std::cout << "\n" << passed << "/" << total << " tests passed, " << fx::test::Failures()
            << " assertions failed\n";
  return fx::test::Failures() == 0 ? 0 : 1;
}
