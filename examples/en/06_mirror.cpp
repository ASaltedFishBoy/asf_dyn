// mirror: same field convention as the object, different memory.
//   Person              one object (name, age, ... stored in place)
//   mirror<Person>      a bundle of references to those members
//   mirror<Person, Src> that bundle plus a pointer back to Src
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

// Inheritance: Employee is a Person, so it converts into the Person view.
struct Employee : Person {
  std::string dept;
};

// Unrelated type: maps to mirror<Person> through a conversion operator.
struct Alias {
  std::string title;
  int years{};
  operator asf::mirror<Person>() { return asf::mirror<Person>{title, years}; }
};

// With [[=asf_ann::mirror]], Trait self is the mirror bundle, not Person&.
// The method still only uses the field convention (self.name).
template <typename T = void>
struct Named;

template <>
struct [[=asf_ann::mirror]]
Named<void> {
  auto who(auto const& self) -> std::string const& { return self.name; }
};

// View-T specialization used when binding mirror<Person, Src>.
inline int named_person_hits = 0;
template <>
struct Named<Person> {
  static auto who(asf::c_mirror<Person> const& self) -> std::string const& {
    ++named_person_hits;
    return self.name;
  }
};

// Named and Bundle allow mirror. Destroy is always vt[-1] (~Src), not a Trait
// method. op_construct does not follow the Trait mirror tag: self is the
// object. Marking op_construct [[=asf_ann::mirror]] is ill-formed.
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

  // Bare mirror: references into person, no extra source pointer.
  asf::mirror<Person> view{person};
  static_assert(std::is_same_v<decltype(view.name), std::string&>);
  static_assert(std::is_same_v<decltype(view.age), int&>);
  view.name = "Bea";
  view.age = 1;
  std::println("object sees = {} {}", person.name, person.age);

  // Same convention, two layouts: both expose .name.
  auto show = [](auto const& self) { return self.name; };
  std::println("object name = {}", show(person));
  std::println("mirror name = {}", show(view));

  // Sourced mirror: same refs, plus Src* so you can reach the object.
  auto sourced = asf::to_mirror(person);
  sourced.age = 2;
  asf::src_of(sourced).name = "Cara";
  std::println("via src_of = {} {}", person.name, person.age);

  // Field-wise write. operator= rebinds the view (pointer semantics);
  // assign_mirror writes through the current binding.
  Person other{"Dst", 0};
  auto into = asf::to_mirror(other);
  asf::assign_mirror(into, person);
  std::println("assigned other = {} {}", other.name, other.age);

  // Drop the Src pointer; the field refs remain.
  asf::mirror<Person> bare = asf::to_pure_mirror(sourced);
  std::println("pure name = {}", bare.name);

  // Trait + mirror: self is the ref bundle, still read as .name.
  auto iface = asf::to_iface<Named>(sourced);
  std::println("iface who = {}", iface->who());
  std::println("Named<Person> hits = {}", named_person_hits);

  // Inheritance: implicit conversion into the Person view.
  Employee emp{{"Fay", 41}, "R&D"};
  asf::mirror<Person> from_emp{emp};
  std::println("from Employee = {} {}", from_emp.name, from_emp.age);

  // Matching view/Src: destroy still ~Person via vt[-1], not a mirror.
  {
    auto* heap = new Person{"Eve", 40};
    asf::mirror<Person, Person> owned{*heap};
    asf::dyn<Bundle> d{owned};
    std::string who = d.as<Named>()->who();
    std::println("owned who = {}", who);
    std::println("Named<Person> hits = {}", named_person_hits);
    d.destroy();  // ~Person via vt[-1]
    std::println("after destroy engaged = {}", static_cast<bool>(d));
  }

  // Unrelated Src mapped to Person: destroy is still ~Alias (Src); ordinary dyn.
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

  // const object => const refs, except mutable members.
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

#if 0  // --- wrong / ill-formed ---
  // Non-mutable field through mirror<const T>.
  cview.val = 2;
  // to_pure_mirror drops Src.
  asf::src_of(bare);
  // into = sourced rebinds into onto person; it does not write other.
  // Field write is asf::assign_mirror.
  // Unsourced mirror -> iface: bind ok without [[=mirror]]; the call needs it.
  // Marking op_construct itself [[=mirror]] is ill-formed when building a vtable.
#endif
}
