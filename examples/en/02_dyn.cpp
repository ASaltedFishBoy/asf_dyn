// Dynamic binding: dyn / c_dyn / to_dyn (type-erased Trait handle).
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

// Mixed: greet is dyn-dispatched; who is static+mirror (iface-only).
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

  // Default Ptr = void*: non-owning observation (pass address).
  asf::dyn<Greeter> d{&person};
  d->greet();
  std::println("dyn who = {}", d->who());

  auto d2 = asf::to_dyn<Greeter>(&person);
  d2->greet();

  // Empty / null check (pointer-like)
  asf::dyn<Greeter> empty;
  std::println("empty = {}", static_cast<bool>(empty));
  std::println("d ok = {}", static_cast<bool>(d));

  // Recover raw pointer when you know the concrete type
  auto* raw = static_cast<Person*>(d.raw());
  std::println("via ptr = {}", raw->name);

  // const observation surface
  asf::c_dyn<Greeter> cd{&cperson};
  std::println("c_dyn who = {}", cd->who());

  // Hook mutable dyn into c_dyn (const view of the same observation)
  asf::c_dyn<Greeter> hooked{d};
  std::println("hooked who = {}", hooked->who());

  // const dyn (a const HANDLE) is not c_dyn: `const_handle` is const, yet
  // the pointee stays mutable through the proxy -- pointer semantics, like
  // `Person* const`. c_dyn is the other mechanism: a const VIEW (only
  // const-self methods, masked vtable). Use a const dyn to share a mutable
  // observation you will not reseat; use c_dyn to actually const-observe.
  const asf::dyn<Greeter> const_handle{&person};
  const_handle->greet();  // OK: mutable call through a const handle
  std::println("const dyn who = {}", const_handle->who());

  // static who: iface-only (statics are never dyn slots)
  asf::dyn<Mixed> md{&person};
  md->greet();
  asf::iface<Mixed, Person> mi{person};
  std::println("mixed who via iface = {}", mi->who());

#if 0  // --- wrong / ill-formed ---
  // static who is not a dyn slot: ill-formed on dyn.
  md->who();

  // Empty dyn has no target.
  empty->greet();
#endif
}
