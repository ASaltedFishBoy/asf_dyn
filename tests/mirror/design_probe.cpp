#include "asf_check.hpp"
#include "asf.hpp"
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
  operator asf_dyn::mirror<Person>() { return asf_dyn::mirror<Person>{name}; }
  operator asf_dyn::mirror<const Person>() const {
    return asf_dyn::mirror<const Person>{name};
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
  operator asf_dyn::mirror<Person, Person>() const {
    return asf_dyn::mirror<Person, Person>{*p};
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
  static void greet(asf_dyn::mirror<Person> self) {
    std::println("person {}", self.name);
  }
};

template <>
struct Interface<Token> final {
  static void greet(asf_dyn::mirror<Token, Token> self) {
    std::println("token {}", self.name);
  }
};

// Dyn bind leaves for other storage types used as As (default body / who).
template <>
struct Interface<Alias> final {};
template <>
struct Interface<Employee> final {};

using PersonIface = asf_dyn::iface<Interface, Person>;
using ConstPersonIface = asf_dyn::iface<Interface, const Person>;
using TokenIface = asf_dyn::iface<Interface, Token>;

template <class D>
concept has_greet = requires(D& d) { d->greet(); };
template <class D>
concept has_who = requires(D& d) { d->who(); };
static_assert(std::constructible_from<asf_dyn::mirror<Person>, Person&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person>, const Person&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person>, const Employee&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person>, const Alias&>);
static_assert(std::constructible_from<asf_dyn::mirror<const Person>, Person&>);
static_assert(std::constructible_from<asf_dyn::mirror<const Person>, const Person&>);
static_assert(std::constructible_from<asf_dyn::mirror<const Person>,
                                      asf_dyn::mirror<Person>&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person>,
                                       asf_dyn::mirror<const Person>&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person>,
                                       asf_dyn::mirror<const Person, Person>&>);
static_assert(std::constructible_from<asf_dyn::mirror<Person>, Alias&>);
static_assert(std::constructible_from<asf_dyn::mirror<Person>, Employee&>);
static_assert(std::constructible_from<asf_dyn::mirror<Person>, PersonRef&>);
static_assert(std::constructible_from<asf_dyn::mirror<Person, Alias>, Alias&>);
static_assert(std::constructible_from<asf_dyn::mirror<Person, Employee>, Employee&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person, Alias>,
                                       asf_dyn::mirror<Person> const&, Alias&>);
static_assert(std::constructible_from<asf_dyn::mirror<Person, Person>, Person&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person, NoName>, NoName&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person>, NoName&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person, IntName>, IntName&>);
static_assert(!std::constructible_from<asf_dyn::mirror<Person, Renamed>, Renamed&>);
static_assert(std::constructible_from<asf_dyn::mirror<Person>, PersonHandle&>);

static_assert(std::constructible_from<PersonIface, Person&>);
static_assert(std::constructible_from<PersonIface, Alias&>);
static_assert(std::constructible_from<PersonIface, Employee&>);
static_assert(std::constructible_from<PersonIface, PersonHandle&>);
static_assert(std::constructible_from<PersonIface, asf_dyn::mirror<Person>>);
static_assert(std::constructible_from<PersonIface, asf_dyn::mirror<Person, Alias>>);
static_assert(!std::constructible_from<PersonIface, NoName&>);
static_assert(!std::constructible_from<PersonIface, Renamed&>);

static_assert(!std::constructible_from<asf_dyn::mirror<Token>, TokenAlias&>);
static_assert(
    !std::constructible_from<asf_dyn::mirror<Token, TokenAlias>, TokenAlias&>);
static_assert(std::constructible_from<TokenIface, asf_dyn::mirror<Token, Token>>);
static_assert(!std::constructible_from<TokenIface, Token&>);
static_assert(!std::constructible_from<TokenIface, TokenAlias&>);
static_assert(!std::constructible_from<TokenIface, asf_dyn::mirror<Token>>);
static_assert(
    !std::constructible_from<TokenIface, asf_dyn::mirror<Token, TokenAlias>>);

static_assert(std::is_same_v<decltype(asf_dyn::mirror<const Person>::name),
                             const std::string&>);
static_assert(std::constructible_from<asf_dyn::mirror<const Person>, Employee&>);
static_assert(std::constructible_from<asf_dyn::mirror<const Person>, const Employee&>);
static_assert(std::constructible_from<asf_dyn::mirror<const Person, Person>,
                                      const Person&>);
static_assert(std::constructible_from<asf_dyn::mirror<const Person, Employee>,
                                      const Employee&>);
static_assert(!std::constructible_from<asf_dyn::dyn<Interface>, Person&>);
static_assert(!std::constructible_from<asf_dyn::dyn<Interface>, const Person*>);
static_assert(std::constructible_from<asf_dyn::c_dyn<Interface>, Person*>);
static_assert(std::constructible_from<asf_dyn::c_dyn<Interface>, const Person*>);
static_assert(std::constructible_from<asf_dyn::c_dyn<Interface>, const Employee*>);
static_assert(std::constructible_from<asf_dyn::c_dyn<Interface>,
                                      asf_dyn::dyn<Interface>&>);
static_assert(std::constructible_from<ConstPersonIface, const Person&>);
static_assert(std::constructible_from<ConstPersonIface, Person&>);
static_assert(std::constructible_from<ConstPersonIface, asf_dyn::mirror<Person>>);
static_assert(std::constructible_from<ConstPersonIface, asf_dyn::mirror<const Person>>);
static_assert(has_greet<asf_dyn::dyn<Interface>>);
static_assert(!has_greet<asf_dyn::c_dyn<Interface>>);
static_assert(has_who<asf_dyn::c_dyn<Interface>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_iface<Interface>(std::declval<Person&>())),
    asf_dyn::iface<Interface, Person>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_iface<Interface>(std::declval<const Person&>())),
    asf_dyn::iface<Interface, const Person>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_iface<Interface>(
        std::declval<asf_dyn::mirror<Person, Alias>&>())),
    asf_dyn::iface<Interface, asf_dyn::mirror<Person, Alias>>>);
static_assert(std::same_as<
    decltype(std::declval<asf_dyn::iface<Interface, asf_dyn::mirror<Person, Alias>>&>()
                 .get()),
    asf_dyn::mirror<Person, Alias>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_mirror<Person>(std::declval<Employee&>())),
    asf_dyn::mirror<Person, Employee>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_mirror(std::declval<Person&>())),
    asf_dyn::mirror<Person, Person>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_mirror<Person>(std::declval<Alias&>())),
    asf_dyn::mirror<Person, Alias>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_mirror<Person>(std::declval<Person&>())),
    asf_dyn::mirror<Person, Person>>);
static_assert(std::same_as<
    decltype(asf_dyn::src_of(std::declval<asf_dyn::mirror<Person, Person>&>())),
    Person&>);
static_assert(std::same_as<
    decltype(asf_dyn::src_of(std::declval<asf_dyn::mirror<Person, Person> const&>())),
    Person&>);
static_assert(std::same_as<
    decltype(asf_dyn::src_of(std::declval<asf_dyn::mirror<const Person, Person>&>())),
    const Person&>);
static_assert(std::same_as<
    decltype(asf_dyn::src_of(std::declval<asf_dyn::rmirror<Person, Person>&>())),
    Person&&>);
static_assert(std::same_as<
    decltype(asf_dyn::src_of(std::declval<asf_dyn::c_rmirror<Person, Person>&>())),
    const Person&&>);
static_assert(std::same_as<
    decltype(asf_dyn::to_pure_mirror(
        std::declval<asf_dyn::mirror<Person, Alias> const&>())),
    asf_dyn::mirror<Person>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_pure_mirror(
        std::declval<asf_dyn::mirror<const Person, Person> const&>())),
    asf_dyn::mirror<const Person>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_pure_mirror<Person>(std::declval<Employee&>())),
    asf_dyn::mirror<Person>>);
static_assert(std::same_as<
    decltype(asf_dyn::to_pure_mirror(std::declval<Employee&>())),
    asf_dyn::mirror<Employee>>);

static_assert(std::constructible_from<asf_dyn::dyn<Interface>, Person*>);
static_assert(std::constructible_from<asf_dyn::dyn<Interface>, Alias*>);
static_assert(!std::constructible_from<asf_dyn::dyn<Interface>, Token*>);
static_assert(!std::constructible_from<asf_dyn::dyn<Interface>, Token&>);
static_assert(std::constructible_from<asf_dyn::dyn<Interface>,
                                      asf_dyn::mirror<Token, Token>&>);
// Destroy is always vt[-1]; mismatch mirror may still bind dyn.
static_assert(std::constructible_from<asf_dyn::dyn<Interface>,
                                      asf_dyn::mirror<Person, Alias>&>);
// Heap Ptr still not from stack mirror&.
static_assert(!std::constructible_from<asf_dyn::unique_dyn<Interface>,
                                       asf_dyn::mirror<Person, Alias>&>);
static_assert(!std::constructible_from<asf_dyn::shared_dyn<Interface>,
                                       asf_dyn::mirror<Person, Alias>&>);
// Match mirror -> dyn.
static_assert(std::constructible_from<asf_dyn::dyn<Interface>,
                                      asf_dyn::mirror<Token, Token>&>);
template <class H>
concept design_has_destroy = requires(H& h) { h.destroy(); };
static_assert(design_has_destroy<asf_dyn::dyn<Interface>>);

int main() {
  using asf_dyn::dyn;
  using asf_dyn::iface;
  using asf_dyn::mirror;

  Person person{asf_test::no_sso("Ada")};
  Alias alias{"Bob", 42};
  Employee employee{{"Carol"}, 7};
  PersonHandle handle{&person};

  auto as_alias = asf_dyn::to_mirror<Person>(alias);
  std::println("mirror<Person, Alias> = {}", as_alias.name);
  auto as_emp = asf_dyn::to_mirror<Person>(employee);
  std::println("mirror<Person, Employee> = {}", as_emp.name);
  mirror<Person> raw_handle{handle};
  std::println("mirror from handle = {}", raw_handle.name);

  auto a = asf_dyn::to_iface<Interface>(as_alias);
  a->greet();
  a->eat(std::string{"via-iface"});
  iface<Interface, Person> b{employee};
  b->greet();
  iface<Interface, Person> c{handle};
  c->greet();

  auto alias_m = asf_dyn::to_mirror<Person>(alias);
  auto d1 = asf_dyn::to_dyn<Interface>(alias_m);
  static_assert(std::same_as<decltype(d1), dyn<Interface>>);
  d1->greet();
  std::println("dyn alias who = {}", d1->who());
  d1->eat(std::string{"via-dyn"});
  auto emp_m = asf_dyn::to_mirror<Person>(employee);
  auto d2 = asf_dyn::to_dyn<Interface>(emp_m);
  d2->greet();
  asf_dyn::to_dyn<Interface>(alias_m)->greet();
  asf_dyn::to_dyn<Interface>(emp_m)->greet();

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
  auto from_calias = asf_dyn::to_mirror<const Person>(calias);
  std::println("to_mirror const Alias = {}", from_calias.name);
  asf_dyn::mirror<const Person> bare_c = asf_dyn::to_pure_mirror(from_calias);
  std::println("to_pure_mirror drop Src = {}", bare_c.name);
  mirror<const Person> cv{cperson};
  mirror<const Person> from_mut{person};
  mirror<const Person> lifted{mirror<Person>{person}};
  std::println("mirror<const Person> = {}", cv.name);
  std::println("T -> c T = {}", from_mut.name);
  std::println("mirror<T> -> mirror<c T> = {}", lifted.name);

  auto ci = asf_dyn::to_iface<Interface>(cperson);
  std::println("const iface who = {}", ci->who());
  auto ci_from_t = asf_dyn::to_iface<Interface>(person);
  std::println("const iface from T who = {}", ci_from_t->who());

  auto _m_cperson = asf_dyn::to_mirror(cperson);
  asf_dyn::c_dyn<Interface> cd1{_m_cperson};
  auto _m_person = asf_dyn::to_mirror(person);
  asf_dyn::c_dyn<Interface> cd2{_m_person};
  auto cd3 = asf_dyn::c_trait_cast<Interface>(d2);
  static_assert(std::same_as<decltype(cd3), asf_dyn::c_dyn<Interface>>);
  std::println("c_dyn const who = {}", cd1->who());
  std::println("c_dyn T -> c who = {}", cd2->who());
  std::println("c_dyn from dyn who = {}", cd3->who());

  std::println("ok rtti={}", asf_dyn::dyn_rtti_v);
}
