// Trait op_* methods become proxy members: the handle is the left operand.
// A dyn slot requires self first; otherwise compile error.
// Reverse (literal on the left) is iface * only: another Trait overload with
// self later, marked no_dyn. Bare iface has no Trait operators.
#include "cast.hpp"

#include <print>
#include <string>
#include <string_view>

template <typename T = void>
struct Ops;

template <>
struct Ops<void> {
  int op_add(auto const& self, int x) {
    if constexpr (requires { self.name.size(); })
      return static_cast<int>(self.name.size()) + x;
    else {
      (void)self;
      return x;
    }
  }
  // iface reverse: 4 + (*h). static = iface-only (self not first).
  static int op_add(int x, auto const& self) {
    if constexpr (requires { self.name.size(); })
      return x + static_cast<int>(self.name.size());
    else {
      (void)self;
      return x;
    }
  }
  bool op_eq(auto const& self, std::string_view name) {
    if constexpr (requires { self.name == name; })
      return self.name == name;
    else {
      (void)self;
      (void)name;
      return false;
    }
  }
  [[=asf_ann::expl]] int op_convert(auto const& self) {
    if constexpr (requires { self.name.size(); })
      return static_cast<int>(self.name.size());
    else {
      (void)self;
      return 0;
    }
  }
  void op_assign(auto& self, int n) {
    if constexpr (requires { self.name = std::to_string(n); })
      self.name = std::to_string(n);
    else
      (void)self, (void)n;
  }
};

struct Person {
  std::string name;
};

template <>
struct Ops<Person> final {};

int main() {
  Person person{"Ada"};  // size 3

  auto ai = asf::to_iface<Ops>(person);
  auto ad = asf::to_dyn<Ops>(&person);
  std::println("iface == name? {}", (*ai) == std::string_view{"Ada"});
  std::println("dyn   == name? {}", (*ad) == std::string_view{"Ada"});
  std::println("iface * + 4 = {}", (*ai) + 4);
  std::println("dyn   * + 3 = {}", (*ad) + 3);
  std::println("4 + *iface  = {}", 4 + (*ai));

  int n = int(*ad);  // explicit op_convert -> operator int()
  std::println("convert to int = {}", n);

  Person assigned{"Zoe"};
  asf::dyn<Ops> av{&assigned};
  *av = 7;  // concrete-RHS op_assign(int)
  std::println("after assign name = {}", assigned.name);

  // Handle rebind (pointer semantics), not Trait value assign
  asf::dyn<Ops> other;
  other = av;
  std::println("rebound size = {}", int(*other));

#if 0  // --- errors / illegal ---
  // Do not declare member operator+ on the Trait; write op_add.
  // A dyn slot with self not first is a compile error. Mark [[=no_dyn]] for iface.
  // Src/auto op_assign is not a dyn slot; only a concrete RHS (here int).
  struct BadOps {
    int operator+(auto const& self, int x);
    int op_sub(int x, auto const& self);
    void op_assign(auto& self, auto const& other);
  };
  // Bare iface has no Trait operators; dyn * has no reverse.
  4 + ai;
  4 + (*ad);
#endif
}
