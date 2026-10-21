// Compile-fail: a static method is iface-only, never a dyn slot.
#include "cast.hpp"

template <typename T = void>
struct Mixed;
template <>
struct Mixed<void> {
  void greet(auto& self);
  static int who(auto const& self) {
    if constexpr (requires { self.x; })
      return self.x;
    else
      return 0;
  }
};

struct P {
  int x{};
};
template <>
struct Mixed<P> final {};

int main() {
  P p{1};
  auto d = asf::to_dyn<Mixed>(&p);
  return d->who();  // ERROR: static is not a dyn slot
}
