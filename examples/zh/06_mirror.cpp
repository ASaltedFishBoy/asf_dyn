// mirror：与对象同一字段约定，不同的内存形态。
//   Person              一个对象（name、age… 就地存放）
//   mirror<Person>      指向这些成员的引用束
//   mirror<Person, Src> 同一束，外加指回 Src 的指针
#include "interface.hpp"
#include "cast.hpp"

#include <memory>
#include <print>
#include <string>
#include <type_traits>

struct Person {
  std::string name;
  int age{};
  ~Person() { std::println("~Person {}", name); }
};

// 继承：Employee 是 Person，故可隐式转成 Person 视口。
struct Employee : Person {
  std::string dept;
};

// 无关类型：经转换运算符映射成 mirror<Person>。
struct Alias {
  std::string title;
  int years{};
  operator asf::mirror<Person>() { return asf::mirror<Person>{title, years}; }
};

// 带 [[=asf_ann::mirror]] 时，Trait 的 self 是 mirror 束，不是 Person&。
// 方法仍只按字段约定用 self.name。
template <typename T = void>
struct Named;

template <>
struct [[=asf_ann::mirror]]
Named<void> {
  auto who(auto const& self) -> std::string const& { return self.name; }
};

// 绑定 mirror<Person, Src> 时使用的 view-T 特化。
inline int named_person_hits = 0;
template <>
struct Named<Person> {
  static auto who(asf::c_mirror<Person> const& self) -> std::string const& {
    ++named_person_hits;
    return self.name;
  }
};

// Named 与 Bundle 允许 mirror。destroy 恒为 vt[-1]（~Src），不是 Trait 方法。
// op_construct 不跟 Trait 的 mirror 标记走，self 是对象。方法自己标
// [[=asf_ann::mirror]] 则不合法。
template <typename T = void>
struct Bundle;

template <>
struct [[=asf_ann::mirror]]
Bundle<void> : Named<void> {
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

template <>
struct Bundle<Person> final {};

int main() {
  Person person{"Ada", 36};

  // 裸 mirror：引用进 person，没有额外源指针。
  asf::mirror<Person> view{person};
  static_assert(std::is_same_v<decltype(view.name), std::string&>);
  static_assert(std::is_same_v<decltype(view.age), int&>);
  view.name = "Bea";
  view.age = 1;
  std::println("object sees = {} {}", person.name, person.age);

  // 同一约定，两种布局：都暴露 .name。
  auto show = [](auto const& self) { return self.name; };
  std::println("object name = {}", show(person));
  std::println("mirror name = {}", show(view));

  // 带 Src 的 mirror：同样的引用，外加 Src* 以便回到对象。
  auto sourced = asf::to_mirror(person);
  sourced.age = 2;
  asf::src_of(sourced).name = "Cara";
  std::println("via src_of = {} {}", person.name, person.age);

  // 按字段写。operator= 重绑视图（指针语义）；
  // assign_mirror 透过当前绑定写入。
  Person other{"Dst", 0};
  auto into = asf::to_mirror(other);
  asf::assign_mirror(into, person);
  std::println("assigned other = {} {}", other.name, other.age);

  // 丢掉 Src 指针；字段引用仍在。
  asf::mirror<Person> bare = asf::to_pure_mirror(sourced);
  std::println("pure name = {}", bare.name);

  // Trait + mirror：self 是引用束，仍然按 .name 读取。
  auto iface = asf::to_iface<Named>(sourced);
  std::println("iface who = {}", iface->who());
  std::println("Named<Person> hits = {}", named_person_hits);

  // 继承：隐式转成 Person 视口。
  Employee emp{{"Fay", 41}, "R&D"};
  asf::mirror<Person> from_emp{emp};
  std::println("from Employee = {} {}", from_emp.name, from_emp.age);

  // view 与 Src 一致：destroy 仍经 vt[-1] 跑 ~Person，不是 mirror。
  {
    auto* heap = new Person{"Eve", 40};
    asf::mirror<Person, Person> owned{*heap};
    asf::dyn<Bundle> d{owned};
    std::string who = d.as<Named>()->who();
    std::println("owned who = {}", who);
    std::println("Named<Person> hits = {}", named_person_hits);
    d.destroy();  // 经 vt[-1] ~Person
    std::println("after destroy engaged = {}", static_cast<bool>(d));
  }

  // 无关 Src 映射到 Person：destroy 仍是 ~Alias（Src）；普通 dyn 即可。
  {
    Alias alias{"Gil", 42};
    asf::mirror<Person, Alias> mapped{alias};
    static_assert(
        std::constructible_from<asf::dyn<Bundle>, decltype(mapped)&>);
    auto d = asf::to_dyn<Bundle>(mapped);
    auto named = d.as<Named>();
    std::string who = named->who();
    std::println("mapped who = {}", who);
    std::println("Named<Person> hits = {}", named_person_hits);
  }

  // const 对象 => const 引用，mutable 成员除外。
  struct Sample {
    int val{};
    mutable int cache{};
  };
  const Sample sample{7, 0};
  asf::mirror<const Sample> cview{sample};
  cview.cache = 9;
  std::println("mutable cache = {}", sample.cache);
  static_assert(std::is_same_v<decltype(cview.val), int const&>);
  static_assert(std::is_same_v<decltype(cview.cache), int&>);

#if 0  // --- 错误 / 不合法 ---
  // 经 mirror<const T> 写非 mutable 字段。
  cview.val = 2;
  // to_pure_mirror 丢掉 Src。
  asf::src_of(bare);
  // into = sourced 把 into 重绑到 person，不写 other。
  // 字段写入是 asf::assign_mirror。
  // 无源 mirror -> iface：绑定不要求整布局 [[=mirror]]；只允许调 [[=mirror]] 方法。
  // op_construct 自己标 [[=mirror]]：建虚表时不合法。
#endif
}
