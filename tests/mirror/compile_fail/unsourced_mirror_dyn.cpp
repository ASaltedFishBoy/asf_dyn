// Compile-fail: an unsourced mirror (no Src) does not bind to dyn.
#include "cast.hpp"

struct P {
  int x{};
};

template <typename T = void>
struct Greet;
template <>
struct [[=asf_ann::mirror]] Greet<void> {
  int get(auto const& self);
};
template <>
struct Greet<P> final {
  static int get(asf::c_mirror<P> self) { return self.x; }
};

int main() {
  P p{1};
  auto m = asf::to_pure_mirror(p);  // mirror<P>, no Src
  auto d = asf::to_dyn<Greet>(m);   // ERROR: unsourced mirror -> dyn
  (void)d;
  return 0;
}
