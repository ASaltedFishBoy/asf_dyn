// 多继承：iface/dyn 上行、as<>、Redirect cover、dyn_cast 下行。
#include "cast.hpp"

#include <print>
#include <string>

//   Readable
  //    /      \.
// Writable  Printable
//    \.     /
//     Document  （Person 的 final 叶）

template <typename T = void>
struct Readable;
template <>
struct Readable<void> {
  auto title(auto const& self) -> std::string const&;
};

template <typename T = void>
struct Writable;
template <>
struct Writable<void> : Readable<void> {
  void rename(auto& self, std::string n);
};

template <typename T = void>
struct Printable;
template <>
struct Printable<void> : Readable<void> {
  void print(auto const& self);
};

template <typename T = void>
struct Document;
template <>
struct Document<void> : Writable<void>, Printable<void> {
  auto tag(auto const& self) -> std::string;
};

struct Person {
  std::string name;
};

template <>
struct Readable<Person> {
  static auto title(Person const& self) -> std::string const& {
    return self.name;
  }
};
template <>
struct Writable<Person> {
  static void rename(Person& self, std::string n) { self.name = std::move(n); }
};
template <>
struct Printable<Person> {
  static void print(Person const& self) {
    std::println("doc: {}", self.name);
  }
};
template <>
struct Document<Person> final {
  static auto tag(Person const& self) -> std::string {
    return self.name + "/doc";
  }
};

int main() {
  Person person{"Ada"};

  // --- as<>：dyn / iface 都是零开销（iface 为 Self 重包） ---
  auto doc_i = asf::to_iface<Document>(person);
  std::println("tag = {}", doc_i->tag());
  doc_i.as<Writable>()->rename("Bob");
  doc_i.as<Printable>()->print();
  std::println("via Readable = {}", doc_i.as<Readable>()->title());

  // 隐式 iface 上行（派生 Trait → 基 Trait）
  asf::iface<Readable, Person> as_readable = doc_i;
  std::println("upcast title = {}", as_readable->title());

  // iface 也能下行 Trait（Self 已知）
  asf::iface<Document, Person> back_i = as_readable;
  std::println("iface down tag = {}", back_i->tag());

  // --- dyn：叶绑定，再上行 / as<> ---
  auto doc = asf::to_dyn<Document>(&person);
  std::println("dyn tag = {}", doc->tag());

  asf::dyn<Readable> up = doc;  // 隐式上行
  std::println("dyn up title = {}", up->title());

  // 同一物理虚表：as<> 同表零开销视口，不 convert
  doc.as<Writable>()->rename("Carol");
  doc.as<Printable>()->print();
  std::println("as Readable = {}", doc.as<Readable>()->title());

#if ASF_HAS_DYN_MULTI_VT
  // 非 section 前缀基可走 Redirect 到 cover 虚表（可存为 dyn<Printable>）
  auto printable = asf::trait_cast<Printable>(doc);
  printable->print();
#endif

#if ASF_HAS_DYN_RTTI
  // dyn_cast：只保证创建节点及其上方。链 1-2-3-4 绑在 3：3-1-2 受保证，3-1-4 不一定。
  asf::dyn<Readable> base = doc;
  if (auto back = asf::dyn_cast<Document>(base))
    std::println("dyn_cast back = {}", back->tag());
  if (auto cross = asf::dyn_cast<Writable>(base)) {
    cross->rename("Dale");
    // dyn 表面只有本 section 方法；基槽经 as<>
    std::println("dyn_cast cross title = {}", cross.as<Readable>()->title());
  }
#else
  // 无 RTTI：仅在确知物理叶虚表时用 unsafe_downcast
  auto leaf_again = asf::unsafe_downcast<Document>(up);
  std::println("unsafe_downcast tag = {}", leaf_again->tag());
#endif

#if 0  // --- 错误 / 不合法或 UB ---
  // 没有隐式 dyn 下行。
  asf::dyn<Document> down = up;
  // 基类方法名不在派生 dyn 的 section 表面上。
  doc->title();
  // Redirect cover 之后 unsafe_downcast：虚表不是叶则 UB。
  // unsafe_downcast 不换表（有无 RTTI 都一样）；换表是 dyn_cast。
#endif
}