// Compile-fail (dual-pass): const-self impl wrote the mutable self view.
// Declaration is auto const&; general pass: impl wrote T& (needs const T&);
// mirror pass: impl wrote mirror<T> (needs c_mirror<T>).
// EXCLUDE_FROM_ALL (expect fail on both passes).

#include "test_bind.hpp"

#include <string>

template <typename T = void>
struct Named;

template <>
struct ASF_TEST_TRAIT_MIRROR
Named<void> {
  auto who(auto const& self) -> std::string const&;
};

struct Person {
  std::string name;
};

template <>
struct Named<Person> final {
  static auto who(ASF_TEST_M_CREF(Person) self) -> std::string const& {
    return self.name;
  }
};

int main() {
  Person p;
  asf::dyn<Named> d{&p};
  (void)d->who();
}
