// Compile-fail: const-self impl wrote T& (needs const T&).
// Declaration is auto const&; ptr is presented as const T&; T& does not
// accept that. EXCLUDE_FROM_ALL (expect fail).

#include "cast.hpp"

template <typename T = void>
struct Cell;

template <>
struct Cell<void> {
  auto read(auto const& self) -> int;
};

struct Box {
  int num{};
};

template <>
struct Cell<Box> final {
  static auto read(Box& self) -> int { return self.num; }
};

int main() {
  Box box;
  asf::dyn<Cell> d{&box};
  (void)d->read();
}
