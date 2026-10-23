// Compile-fail: unsourced mirror iface can only call [[=mirror]] methods.
#include "cast.hpp"

struct P {
  int x{};
};

template <typename T = void>
struct Mixed;
template <>
struct Mixed<void> {
  int plain(auto const& self);
  [[=asf_ann::mirror]] int mir(auto const& self);
};
template <>
struct Mixed<P> final {
  static int plain(P const& self) { return self.x; }
  static int mir(asf::c_mirror<P> self) { return self.x; }
};

int main() {
  P p{1};
  auto m = asf::to_pure_mirror(p);  // mirror<P>, no Src
  auto i = asf::to_iface<Mixed>(m); // bind ok
  (void)i->mir();                   // ok: [[=mirror]]
  return i->plain();                // ERROR: unsourced + non-mirror
}
