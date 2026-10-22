// Compile-fail (dual-pass): same wrong impl as cref_wrong_impl_dyn, through
// iface. EXCLUDE_FROM_ALL (expect fail on both passes).

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
  asf::iface<Named, Person> i{p};
  (void)i->who();
}
