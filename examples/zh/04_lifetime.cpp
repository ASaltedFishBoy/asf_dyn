// 生命周期：destroy 走 vt[-1]（~Src）、construct、*dyn 浅拷贝 vs 深克隆、拥有型 Ptr
#include "cast.hpp"

#include <memory>
#include <print>
#include <string>

template <typename T = void>
struct Droppable;

template <>
struct Droppable<void> {
  auto who(auto const& self) -> std::string const& { return self.name; }
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

template <typename T = void>
struct OwnBase;

template <>
struct OwnBase<void> {
  auto who(auto const& self) -> std::string const& { return self.name; }
};

template <typename T = void>
struct OwnLeaf;

template <>
struct OwnLeaf<void> : OwnBase<void> {
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

struct Person {
  std::string name;
  ~Person() { std::println("~Person {}", name); }
};

template <>
struct Droppable<Person> final {};

template <>
struct OwnLeaf<Person> final {};

int main() {
  // --- destroy：恒为 vt[-1]（~Src）---
  {
    auto* heap = new Person{"Yan"};
    asf::dyn<Droppable> hd{heap};
    std::println("before destroy who = {}", hd->who());
    hd.destroy();  // 经 vt[-1] 跑 ~Person；句柄清空（不释放分配）
    std::println("after destroy engaged = {}", static_cast<bool>(hd));
    ::operator delete(heap);  // destroy() 不释放分配，须自行归还
  }

  // --- *dyn 代理：浅拷贝 vs construct() 深克隆 ---
  {
    Person src{"Ada"};
    asf::dyn<Droppable> d{&src};
    auto view = *d;
    auto alias = view;                 // 浅：同一观察
    auto clone = asf::construct(view); // 深：拥有拷贝（默认 Ptr）

    src.name = "Zed";
    std::println("alias sees = {}", alias->who());  // Zed
    std::println("clone keeps = {}", clone->who());                 // Ada

    // rproxy 选择移动构造重载（非 const 观察）
    Person mv{"MoveMe"};
    asf::dyn<Droppable> md{&mv};
    auto moved = asf::construct(md.rproxy());
    std::println("construct(rproxy) who = {}", moved->who());
  }

  // --- unique_ptr 所有权：末位所有者跑 ~Src，再释放存储 ---
  {
    Person drop_src{"Uptr"};
    asf::dyn<Droppable> obs{&drop_src};
    auto owned = asf::construct<std::unique_ptr>(*obs);
    std::println("unique_dyn who = {}", owned->who());
    // 作用域结束：经 vt[-1] ~Person，再 ::operator delete
  }

  // --- shared_ptr / weak_ptr ---
  {
    Person sp_src{"Sptr"};
    asf::dyn<Droppable> obs{&sp_src};
    auto a = asf::construct<std::shared_ptr>(*obs);
    auto b = a;
    a.reset();
    std::println("shared remaining = {}", b->who());

    asf::weak_dyn<Droppable> weak{b};
    if (auto locked = weak.lock())
      std::println("weak lock = {}", locked->who());
    b.reset();
    std::println("weak expired = {}", weak.expired());
  }

  // --- sub_dyn：只有本 section 方法；没有表原点前缀 ---
  {
    Person own_src{"Sub"};
    asf::dyn<OwnLeaf> leaf{&own_src};
    asf::sub_dyn<OwnBase> sub{leaf};
    std::println("sub_dyn who = {}", sub->who());
  }

#if 0  // --- 错误 / 不合法 ---
  // 把 destroy 放进删除器再交接：见 examples/*/07_pointer_trait.cpp。
  // 从 *c_dyn construct 恒为深拷贝，即使 asf::construct(cd.rproxy())。
  // weak_dyn 没有方法面；先 lock()。
  asf::weak_dyn<Droppable> w;
  w->who();
#endif
}
