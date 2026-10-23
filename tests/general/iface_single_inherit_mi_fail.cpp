#include "cast.hpp"

#include <string>

// Compile-fail under ASF_HAS_DYN_SINGLE_INHERIT=1: live MI at dyn generation.
// Built only by the iface_single_inherit_mi_fail custom target (expect fail).

template <typename T = void>
struct A;
template <>
struct A<void> {
  std::string a(auto& self);
};

template <typename T = void>
struct B;
template <>
struct B<void> {
  std::string b(auto& self);
};

template <typename T = void>
struct LiveMI;
template <>
struct LiveMI<void> : A<void>, B<void> {
  std::string mi(auto& self);
};

struct Obj {
  std::string name;
};

template <>
struct A<Obj> {
  static std::string a(Obj& self) { return self.name + "/a"; }
};
template <>
struct B<Obj> {
  static std::string b(Obj& self) { return self.name + "/b"; }
};
template <>
struct LiveMI<Obj> final {
  static std::string mi(Obj& self) { return self.name + "/mi"; }
};

int main() {
  Obj o{"T"};
  asf::dyn<LiveMI> d{&o};  // must static_assert: live multi-inherit
  (void)d;
  return 0;
}
