// Multi-inheritance: iface/dyn upcast, as<>, Redirect cover, dyn_cast downcast.
#include "cast.hpp"

#include <print>
#include <string>

//   Readable
//    /      \.
// Writable  Printable
//    \.     /
//     Document  (final leaf for Person)

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

  // --- as<> is zero-cost on iface and dyn (iface: Self rewrap) ---
  auto doc_i = asf::to_iface<Document>(person);
  std::println("tag = {}", doc_i->tag());
  doc_i.as<Writable>()->rename("Bob");
  doc_i.as<Printable>()->print();
  std::println("via Readable = {}", doc_i.as<Readable>()->title());

  // Implicit iface upcast (derived -> base Trait)
  asf::iface<Readable, Person> as_readable = doc_i;
  std::println("upcast title = {}", as_readable->title());

  // iface can also downcast Trait (Self is known)
  asf::iface<Document, Person> back_i = as_readable;
  std::println("iface down tag = {}", back_i->tag());

  // --- dyn: leaf bind, then upcast / as<> ---
  auto doc = asf::to_dyn<Document>(&person);
  std::println("dyn tag = {}", doc->tag());

  asf::dyn<Readable> up = doc;  // implicit upcast
  std::println("dyn up title = {}", up->title());

  // Same physical vt: as<> is a zero-cost same-table view (no convert)
  doc.as<Writable>()->rename("Carol");
  doc.as<Printable>()->print();
  std::println("as Readable = {}", doc.as<Readable>()->title());

#if ASF_HAS_DYN_MULTI_VT
  // Non-section-prefix base may Redirect to a cover vt (storable as dyn<Printable>)
  auto printable = asf::trait_cast<Printable>(doc);
  printable->print();
#endif

#if ASF_HAS_DYN_RTTI
  // dyn_cast: guaranteed only to the creation node and above it.
  // Chain 1-2-3-4 bound at 3: 3-1-2 is guaranteed; 3-1-4 may or may not succeed.
  asf::dyn<Readable> base = doc;
  if (auto back = asf::dyn_cast<Document>(base))
    std::println("dyn_cast back = {}", back->tag());
  if (auto cross = asf::dyn_cast<Writable>(base)) {
    cross->rename("Dale");
    // Own-section methods only on the dyn surface; base slots via as<>
    std::println("dyn_cast cross title = {}", cross.as<Readable>()->title());
  }
#else
  // Without RTTI: only unsafe_downcast when you know the physical leaf vt
  auto leaf_again = asf::unsafe_downcast<Document>(up);
  std::println("unsafe_downcast tag = {}", leaf_again->tag());
#endif

#if 0  // --- wrong / ill-formed or UB ---
  // No implicit dyn downcast.
  asf::dyn<Document> down = up;
  // Base method names are not on the derived dyn section surface.
  doc->title();
  // unsafe_downcast after Redirect cover: UB if the vt is not the leaf.
  // It never switches vt (RTTI or not); switching is dyn_cast.
#endif
}
