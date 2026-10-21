// Trait 的 op_* 方法生成 proxy 成员运算符，句柄在左侧。
// dyn 槽要求 self 是第一个参数；不在首位则编译错误。
// 反向(字面量在左)仅 iface *: Trait 上再写 self 靠后的重载并标 no_dyn。裸 iface 没有 Trait 运算符。
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
  // iface 反向：4 + (*h)。static 表示仅 iface（self 不在首位）。
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

  int n = int(*ad);  // 显式 op_convert -> operator int()
  std::println("convert to int = {}", n);

  Person assigned{"Zoe"};
  asf::dyn<Ops> av{&assigned};
  *av = 7;  // 具体 RHS 的 op_assign(int)
  std::println("after assign name = {}", assigned.name);

  // 句柄重绑定（指针语义），不是经 Trait 的值赋值
  asf::dyn<Ops> other;
  other = av;
  std::println("rebound size = {}", int(*other));

#if 0  // --- 错误 / 不合法 ---
  // 不要在 Trait 上声明成员 operator+ — 请写 op_add。
  // dyn 槽的 self 不在第一个参数上是编译错误。仅 iface 时标 [[=no_dyn]]。
  struct BadOps {
    int operator+(auto const& self, int x);
    int op_sub(int x, auto const& self);
    void op_assign(auto& self, auto const& other);
  };
  // dyn 不支持反向
  4 + ai;
  4 + (*ad);
#endif
}
