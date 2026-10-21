// Compile-fail: same wrong impl as cref_wrong_impl_dyn, through iface.
// EXCLUDE_FROM_ALL (expect fail).

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
  asf::iface<Cell, Box> i{box};
  (void)i->read();
}
