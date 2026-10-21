// 静态绑定：iface / to_iface（类型参数中保留具体 T）
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

struct Employee : Person {
  int badge{};
};

template <>
struct Greeter<Person> final {
  static void greet(Person& self) { std::println("hello {}", self.name); }
};

template <typename T = void>
struct Welcomer;

template <>
struct Welcomer<void> : Greeter<void> {
  auto banner(auto const& self) -> std::string {
    return "welcome " + self.name;
  }
};

template <>
struct Welcomer<Person> final {};

int main() {
  Person person{"Ada"};

  // to_iface：对象 -> iface<Greeter, Person>（Self 是 Person&）
  auto i = asf::to_iface<Greeter>(person);
  i->greet();
  std::println("who = {}", i->who());

  asf::iface<Greeter, Person> direct{person};
  direct->greet();

  // 派生对象以 Person 表面绑定
  Employee emp{{"Carol"}, 7};
  asf::iface<Greeter, Person> from_emp{emp};
  from_emp->greet();

  // iface 沿 Trait 链双向转换：上行 Welcomer -> Greeter，再下行回来
  //（静态安全：具体类型 T 始终保留）。
  asf::iface<Welcomer, Person> wel{emp};
  std::println("banner = {}", wel->banner());
  asf::iface<Greeter, Person> up = wel;         // 上行
  up->greet();
  asf::iface<Welcomer, Person> back = up;       // 下行回来
  std::println("back banner = {}", back->banner());
// const 观察：Self 是 const Person&
  const Person eve{"Eve"};
  auto ci = asf::to_iface<Greeter>(eve);
  std::println("c_iface who = {}", ci->who());

#if 0  // --- 错误 / 不合法 ---
  // to_dyn 不能直接吃裸对象；观察请传 &obj。
  asf::to_dyn<Greeter>(person);

  // iface 保留 T，不能把 iface<Greeter, Person> 下行成 Employee。
  asf::iface<Greeter, Employee> bad = from_emp;

  // greet 需要 Person&；const iface 不能调用它。
  ci->greet();
#endif
}
