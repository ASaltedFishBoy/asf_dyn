// 动态绑定：dyn / c_dyn / to_dyn（类型擦除的 Trait 句柄）
#include "interface.hpp"
#include "cast.hpp"

#include <print>
#include <string>

template <typename T = void>
struct Greeter;

template <>
struct Greeter<void> {
  void greet(auto& self);
  auto who(auto const& self) -> std::string const& { return self.name; }
};

struct Person {
  std::string name;
};

template <>
struct Greeter<Person> final {
  static void greet(Person& self) { std::println("hello {}", self.name); }
};

// 混合：greet 走虚表；who 为 static+mirror（仅 iface）
template <typename T = void>
struct Mixed;

template <>
struct Mixed<void> {
  void greet(auto& self);
  [[=asf_ann::mirror]] static auto who(auto const& self)
      -> std::string const& {
    return self.name;
  }
};

template <>
struct Mixed<Person> final {
  static void greet(Person& self) { std::println("mixed hello {}", self.name); }
};

int main() {
  Person person{"Ada"};
  const Person cperson{"Eve"};

  // 默认 Ptr = void*：非拥有观察（传地址）
  asf::dyn<Greeter> d{&person};
  d->greet();
  std::println("dyn who = {}", d->who());

  auto d2 = asf::to_dyn<Greeter>(&person);
  d2->greet();

  // 空句柄 / 空指针判定（指针语义）
  asf::dyn<Greeter> empty;
  std::println("empty = {}", static_cast<bool>(empty));
  std::println("d ok = {}", static_cast<bool>(d));

  // 已知具体类型时取回裸指针
  auto* raw = static_cast<Person*>(d.ptr());
  std::println("via ptr = {}", raw->name);

  // const 观察面
  asf::c_dyn<Greeter> cd{&cperson};
  std::println("c_dyn who = {}", cd->who());

  // 可变 dyn 挂到 c_dyn（同一观察的 const 视图）
  asf::c_dyn<Greeter> hooked{d};
  std::println("hooked who = {}", hooked->who());

  // const dyn（const 句柄）不是 c_dyn：const_handle 本身是 const，
  // 所指对象经 proxy 仍然可变——指针语义，如同 `Person* const`。
  // c_dyn 是另一套机制：const 视图（仅 const-self 方法、屏蔽虚表）。
  // 想分享一个不再重绑的可变观察用 const dyn；想真正只读观察用 c_dyn。

  const asf::dyn<Greeter> const_handle{&person};
  const_handle->greet();  // 可以：const 句柄上的可变调用
  std::println("const dyn who = {}", const_handle->who());

  // static who：仅 iface（static 永远不是 dyn 槽）
  asf::dyn<Mixed> md{&person};
  md->greet();
  asf::iface<Mixed, Person> mi{person};
  std::println("mixed who via iface = {}", mi->who());

#if 0  // --- 错误 / 不合法 ---
  // static who 不是 dyn 槽：dyn 上不可调。
  md->who();

  // 空 dyn 没有目标。
  empty->greet();
#endif
}
