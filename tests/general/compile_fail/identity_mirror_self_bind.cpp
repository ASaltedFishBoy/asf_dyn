// Compile-fail: a method whose Self is identity_mirror<T> (mirror<T, T>)
// needs Storage = identity_mirror<T>; a plain object bind is rejected.
#include "cast.hpp"

struct P {
  int x{};
};

template <typename T = void>
struct Ident;
template <>
struct Ident<void> {
  int get(asf::mirror<P, P> self);
};
template <>
struct Ident<P> final {};

int main() {
  P p{1};
  auto d = asf::to_dyn<Ident>(&p);  // ERROR: identity_mirror Self needs
  (void)d;                          // Storage = identity_mirror<P>
  return 0;
}
