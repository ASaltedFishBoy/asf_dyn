// Compile-fail: explicit-only conversion to mirror<T> keeps the sourced
// mirror explicit too -- copy-initialization is ill-formed.
#include "asf.hpp"

struct B {
  int x;
};

struct ExplAlias {
  B b;
  explicit operator asf_dyn::mirror<B>() { return asf_dyn::mirror<B>{b}; }
};

int main() {
  ExplAlias a{{1}};
  asf_dyn::mirror<B, ExplAlias> m = a;  // ERROR: explicit ctor only
  (void)m;
  return 0;
}
