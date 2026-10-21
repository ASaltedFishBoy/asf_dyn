// Compile-fail: same wrong impl as cref_wrong_impl_dyn, through iface.
// EXCLUDE_FROM_ALL (expect fail).

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
  asf::iface<Named, Person> i{p};
  (void)i->who();
}
