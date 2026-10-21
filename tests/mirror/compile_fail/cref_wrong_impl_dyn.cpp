// Compile-fail: const-self impl wrote mirror<T> (needs c_mirror).
// Declaration is auto const&; ptr is presented as const T&; mirror<T>
// does not accept that. EXCLUDE_FROM_ALL (expect fail).

#include "cast.hpp"

#include <string>

template <typename T = void>
struct Named;

template <>
struct [[=asf_ann::mirror]]
Named<void> {
  auto who(auto const& self) -> std::string const&;
};

struct Person {
  std::string name;
};

template <>
struct Named<Person> final {
  static auto who(asf::mirror<Person> const& self) -> std::string const& {
    return self.name;
  }
};

int main() {
  Person p;
  asf::dyn<Named> d{&p};
  (void)d->who();
}
