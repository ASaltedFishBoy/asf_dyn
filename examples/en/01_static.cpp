// Static binding: iface / to_iface (concrete T kept in the type).
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

// A second Trait with Greeter as its interface base.
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

  // to_iface: object -> iface<Greeter, Person> (Self is Person&)
  auto i = asf::to_iface<Greeter>(person);
  i->greet();
  std::println("who = {}", i->who());

  asf::iface<Greeter, Person> direct{person};
  direct->greet();

  // Derived object binds as the Person surface
  Employee emp{{"Carol"}, 7};
  asf::iface<Greeter, Person> from_emp{emp};
  from_emp->greet();

  // iface converts either way along the Trait chain: upcast
  // Welcomer -> Greeter, and back down (statically safe: T is kept).
  asf::iface<Welcomer, Person> wel{emp};
  std::println("banner = {}", wel->banner());
  asf::iface<Greeter, Person> up = wel;         // upcast
  up->greet();
  asf::iface<Welcomer, Person> back = up;       // downcast back
  std::println("back banner = {}", back->banner());

  // const observation: Self is const Person&
  const Person eve{"Eve"};
  auto ci = asf::to_iface<Greeter>(eve);
  std::println("c_iface who = {}", ci->who());

#if 0  // --- wrong / ill-formed ---
  // to_dyn does not take a naked object; pass &obj.
  asf::to_dyn<Greeter>(person);

  // iface keeps T; cannot downcast iface<Greeter, Person> to Employee.
  asf::iface<Greeter, Employee> bad = from_emp;

  // greet takes Person&; a const iface cannot call it.
  ci->greet();
#endif
}
