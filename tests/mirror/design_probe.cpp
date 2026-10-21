#include "asf_check.hpp"
#include "cast.hpp"
#include "mirror_bind.hpp"

#include <concepts>
#include <memory>
#include <print>
#include <string>
#include <type_traits>

template <typename T = void>
struct Interface;

template <>
struct [[=asf_ann::mirror]]
Interface<void> {
  void greet(auto& self);
  auto who(auto const& self) -> std::string const& {
    return self.name;
  }
  void eat(auto& self, std::string&& s) {
    (void)self;
    std::println("eat {}", s);
  }
};

struct Person {
  std::string name;
};

struct Alias {
  std::string name;
  int extra;
  operator asf::mirror<Person>() { return asf::mirror<Person>{name}; }
  operator asf::mirror<const Person>() const {
    return asf::mirror<const Person>{name};
  }
};

struct Employee : Person {
  int extra;
};

struct NoName {
  int extra;
  std::string payload{asf_test::no_sso("noname")};
};

struct IntName {
  int name;
  std::string payload{asf_test::no_sso("intname")};
};

struct Renamed {
  std::string title;
};

struct PersonRef {
  Person* p;
  operator Person&() { return *p; }
};

struct PersonHandle {
  Person* p;
  operator asf::mirror<Person, Person>() const {
    return asf::mirror<Person, Person>{*p};
  }
};

struct Token {
  std::string name;
};

struct TokenAlias {
  std::string name;
  int extra;
};

template <>
struct Interface<Person> final {
  static void greet(asf::mirror<Person> self) {
    std::println("person {}", self.name);
  }
};

template <>
struct Interface<Token> final {
  static void greet(asf::mirror<Token, Token> self) {
    std::println("token {}", self.name);
  }
};

// Dyn bind leaves for other storage types used as As (default body / who).
template <>
struct Interface<Alias> final {};
template <>
struct Interface<Employee> final {};

using PersonIface = asf::iface<Interface, Person>;
using ConstPersonIface = asf::iface<Interface, const Person>;
using TokenIface = asf::iface<Interface, Token>;

template <class D>
concept has_greet = requires(D& d) { d->greet(); };
template <class D>
concept has_who = requires(D& d) { d->who(); };
static_assert(std::constructible_from<asf::mirror<Person>, Person&>);
static_assert(!std::constructible_from<asf::mirror<Person>, const Person&>);
static_assert(!std::constructible_from<asf::mirror<Person>, const Employee&>);
static_assert(!std::constructible_from<asf::mirror<Person>, const Alias&>);
static_assert(std::constructible_from<asf::mirror<const Person>, Person&>);
static_assert(std::constructible_from<asf::mirror<const Person>, const Person&>);
static_assert(std::constructible_from<asf::mirror<const Person>,
                                      asf::mirror<Person>&>);
static_assert(!std::constructible_from<asf::mirror<Person>,
                                       asf::mirror<const Person>&>);
static_assert(!std::constructible_from<asf::mirror<Person>,
                                       asf::mirror<const Person, Person>&>);
static_assert(std::constructible_from<asf::mirror<Person>, Alias&>);
static_assert(std::constructible_from<asf::mirror<Person>, Employee&>);
static_assert(std::constructible_from<asf::mirror<Person>, PersonRef&>);
static_assert(std::constructible_from<asf::mirror<Person, Alias>, Alias&>);
static_assert(std::constructible_from<asf::mirror<Person, Employee>, Employee&>);
static_assert(!std::constructible_from<asf::mirror<Person, Alias>,
                                       asf::mirror<Person> const&, Alias&>);
static_assert(std::constructible_from<asf::mirror<Person, Person>, Person&>);
static_assert(!std::constructible_from<asf::mirror<Person, NoName>, NoName&>);
static_assert(!std::constructible_from<asf::mirror<Person>, NoName&>);
static_assert(!std::constructible_from<asf::mirror<Person, IntName>, IntName&>);
static_assert(!std::constructible_from<asf::mirror<Person, Renamed>, Renamed&>);
static_assert(std::constructible_from<asf::mirror<Person>, PersonHandle&>);

static_assert(std::constructible_from<PersonIface, Person&>);
static_assert(std::constructible_from<PersonIface, Alias&>);
static_assert(std::constructible_from<PersonIface, Employee&>);
static_assert(std::constructible_from<PersonIface, PersonHandle&>);
static_assert(std::constructible_from<PersonIface, asf::mirror<Person>>);
static_assert(std::constructible_from<PersonIface, asf::mirror<Person, Alias>>);
static_assert(!std::constructible_from<PersonIface, NoName&>);
static_assert(!std::constructible_from<PersonIface, Renamed&>);

static_assert(!std::constructible_from<asf::mirror<Token>, TokenAlias&>);
static_assert(
    !std::constructible_from<asf::mirror<Token, TokenAlias>, TokenAlias&>);
static_assert(std::constructible_from<TokenIface, asf::mirror<Token, Token>>);
static_assert(!std::constructible_from<TokenIface, Token&>);
static_assert(!std::constructible_from<TokenIface, TokenAlias&>);
static_assert(!std::constructible_from<TokenIface, asf::mirror<Token>>);
static_assert(
    !std::constructible_from<TokenIface, asf::mirror<Token, TokenAlias>>);

static_assert(std::is_same_v<decltype(asf::mirror<const Person>::name),
                             const std::string&>);
static_assert(std::constructible_from<asf::mirror<const Person>, Employee&>);
static_assert(std::constructible_from<asf::mirror<const Person>, const Employee&>);
static_assert(std::constructible_from<asf::mirror<const Person, Person>,
                                      const Person&>);
static_assert(std::constructible_from<asf::mirror<const Person, Employee>,
                                      const Employee&>);
static_assert(!std::constructible_from<asf::dyn<Interface>, Person&>);
static_assert(!std::constructible_from<asf::dyn<Interface>, const Person*>);
static_assert(std::constructible_from<asf::c_dyn<Interface>, Person*>);
static_assert(std::constructible_from<asf::c_dyn<Interface>, const Person*>);
static_assert(std::constructible_from<asf::c_dyn<Interface>, const Employee*>);
static_assert(std::constructible_from<asf::c_dyn<Interface>,
                                      asf::dyn<Interface>&>);
static_assert(std::constructible_from<ConstPersonIface, const Person&>);
static_assert(std::constructible_from<ConstPersonIface, Person&>);
static_assert(std::constructible_from<ConstPersonIface, asf::mirror<Person>>);
static_assert(std::constructible_from<ConstPersonIface, asf::mirror<const Person>>);
static_assert(has_greet<asf::dyn<Interface>>);
static_assert(!has_greet<asf::c_dyn<Interface>>);
static_assert(has_who<asf::c_dyn<Interface>>);
static_assert(std::same_as<
    decltype(asf::to_iface<Interface>(std::declval<Person&>())),
    asf::iface<Interface, Person>>);
static_assert(std::same_as<
    decltype(asf::to_iface<Interface>(std::declval<const Person&>())),
    asf::iface<Interface, const Person>>);
static_assert(std::same_as<
    decltype(asf::to_iface<Interface>(
        std::declval<asf::mirror<Person, Alias>&>())),
    asf::iface<Interface, asf::mirror<Person, Alias>>>);
static_assert(std::same_as<
    decltype(std::declval<asf::iface<Interface, asf::mirror<Person, Alias>>&>()
                 .get()),
    asf::mirror<Person, Alias>>);
static_assert(std::same_as<
    decltype(asf::to_mirror<Person>(std::declval<Employee&>())),
    asf::mirror<Person, Employee>>);
static_assert(std::same_as<
    decltype(asf::to_mirror(std::declval<Person&>())),
    asf::mirror<Person, Person>>);
static_assert(std::same_as<
    decltype(asf::to_mirror<Person>(std::declval<Alias&>())),
    asf::mirror<Person, Alias>>);
static_assert(std::same_as<
    decltype(asf::to_mirror<Person>(std::declval<Person&>())),
    asf::mirror<Person, Person>>);
static_assert(std::same_as<
    decltype(asf::src_of(std::declval<asf::mirror<Person, Person>&>())),
    Person&>);
static_assert(std::same_as<
    decltype(asf::src_of(std::declval<asf::mirror<Person, Person> const&>())),
    Person&>);
static_assert(std::same_as<
    decltype(asf::src_of(std::declval<asf::mirror<const Person, Person>&>())),
    const Person&>);
static_assert(std::same_as<
    decltype(asf::src_of(std::declval<asf::rmirror<Person, Person>&>())),
    Person&&>);
static_assert(std::same_as<
    decltype(asf::src_of(std::declval<asf::c_rmirror<Person, Person>&>())),
    const Person&&>);
static_assert(std::same_as<
    decltype(asf::to_pure_mirror(
        std::declval<asf::mirror<Person, Alias> const&>())),
    asf::mirror<Person>>);
static_assert(std::same_as<
    decltype(asf::to_pure_mirror(
        std::declval<asf::mirror<const Person, Person> const&>())),
    asf::mirror<const Person>>);
static_assert(std::same_as<
    decltype(asf::to_pure_mirror<Person>(std::declval<Employee&>())),
    asf::mirror<Person>>);
static_assert(std::same_as<
    decltype(asf::to_pure_mirror(std::declval<Employee&>())),
    asf::mirror<Employee>>);

static_assert(std::constructible_from<asf::dyn<Interface>, Person*>);
static_assert(std::constructible_from<asf::dyn<Interface>, Alias*>);
static_assert(!std::constructible_from<asf::dyn<Interface>, Token*>);
static_assert(!std::constructible_from<asf::dyn<Interface>, Token&>);
static_assert(std::constructible_from<asf::dyn<Interface>,
                                      asf::mirror<Token, Token>&>);
// Destroy is always vt[-1]; mismatch mirror may still bind dyn.
static_assert(std::constructible_from<asf::dyn<Interface>,
                                      asf::mirror<Person, Alias>&>);
// Heap Ptr still not from stack mirror&.
static_assert(!std::constructible_from<asf::unique_dyn<Interface>,
                                       asf::mirror<Person, Alias>&>);
static_assert(!std::constructible_from<asf::shared_dyn<Interface>,
                                       asf::mirror<Person, Alias>&>);
// Match mirror -> dyn.
static_assert(std::constructible_from<asf::dyn<Interface>,
                                      asf::mirror<Token, Token>&>);
template <class H>
concept design_has_destroy = requires(H& h) { h.destroy(); };
static_assert(design_has_destroy<asf::dyn<Interface>>);

int main() {
  using asf::dyn;
  using asf::iface;
  using asf::mirror;

  Person person{asf_test::no_sso("Ada")};
  Alias alias{"Bob", 42};
  Employee employee{{"Carol"}, 7};
  PersonHandle handle{&person};

  auto as_alias = asf::to_mirror<Person>(alias);
  std::println("mirror<Person, Alias> = {}", as_alias.name);
  auto as_emp = asf::to_mirror<Person>(employee);
  std::println("mirror<Person, Employee> = {}", as_emp.name);
  mirror<Person> raw_handle{handle};
  std::println("mirror from handle = {}", raw_handle.name);

  auto a = asf::to_iface<Interface>(as_alias);
  a->greet();
  a->eat(std::string{"via-iface"});
  iface<Interface, Person> b{employee};
  b->greet();
  iface<Interface, Person> c{handle};
  c->greet();

  auto alias_m = asf::to_mirror<Person>(alias);
  auto d1 = asf::to_dyn<Interface>(alias_m);
  static_assert(std::same_as<decltype(d1), dyn<Interface>>);
  d1->greet();
  std::println("dyn alias who = {}", d1->who());
  d1->eat(std::string{"via-dyn"});
  auto emp_m = asf::to_mirror<Person>(employee);
  auto d2 = asf::to_dyn<Interface>(emp_m);
  d2->greet();
  asf::to_dyn<Interface>(alias_m)->greet();
  asf::to_dyn<Interface>(emp_m)->greet();

  Token token{"Tok"};
  mirror<Token, Token> sourced{token};
  iface<Interface, Token> strict{sourced};
  strict->greet();
  std::println("token who = {}", strict->who());

  dyn<Interface> dtok{sourced};
  dtok->greet();
  std::println("dyn token who = {}", dtok->who());

  const Person cperson{"Eve"};
  const Alias calias{asf_test::no_sso("Zed"), 1};
  auto from_calias = asf::to_mirror<const Person>(calias);
  std::println("to_mirror const Alias = {}", from_calias.name);
  asf::mirror<const Person> bare_c = asf::to_pure_mirror(from_calias);
  std::println("to_pure_mirror drop Src = {}", bare_c.name);
  mirror<const Person> cv{cperson};
  mirror<const Person> from_mut{person};
  mirror<const Person> lifted{mirror<Person>{person}};
  std::println("mirror<const Person> = {}", cv.name);
  std::println("T -> c T = {}", from_mut.name);
  std::println("mirror<T> -> mirror<c T> = {}", lifted.name);

  auto ci = asf::to_iface<Interface>(cperson);
  std::println("const iface who = {}", ci->who());
  auto ci_from_t = asf::to_iface<Interface>(person);
  std::println("const iface from T who = {}", ci_from_t->who());

  auto _m_cperson = asf::to_mirror(cperson);
  asf::c_dyn<Interface> cd1{_m_cperson};
  auto _m_person = asf::to_mirror(person);
  asf::c_dyn<Interface> cd2{_m_person};
  auto cd3 = asf::c_trait_cast<Interface>(d2);
  static_assert(std::same_as<decltype(cd3), asf::c_dyn<Interface>>);
  std::println("c_dyn const who = {}", cd1->who());
  std::println("c_dyn T -> c who = {}", cd2->who());
  std::println("c_dyn from dyn who = {}", cd3->who());

  std::println("ok rtti={}", asf::dyn_rtti_v);
}
