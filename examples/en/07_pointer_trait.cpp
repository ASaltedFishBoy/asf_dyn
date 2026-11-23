// Shallow handoff: a proxy-destroy unique_ptr can bridge onto
// unique_ptr<void, dyn_default_delete>. c_dyn may also hold a
// proxy-destroy Ptr directly.
#include "asf.hpp"

#include <memory>
#include <print>
#include <string>

template <typename T = void>
struct Named;

template <>
struct Named<void> {
  auto who(auto const& self) -> std::string const& { return self.name; }
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

struct Person {
  std::string name;
  ~Person() { std::println("~Person {}", name); }
};

template <>
struct Named<Person> final {};

template <class T>
using slot_uptr = std::unique_ptr<T, asf_dyn::dyn_default_delete>;

int main() {
  Person src{"Ada"};
  asf_dyn::dyn<Named> obs{&src};
  auto owned = asf_dyn::construct<std::unique_ptr>(*obs);
  std::println("owned who = {}", owned->who());

  // Put destroy in the pointer, then hand it to c_dyn.
  auto bridged = asf_dyn::to_dyn<Named, slot_uptr>(std::move(owned));
  asf_dyn::c_dyn<Named, slot_uptr<void>> cd{std::move(bridged)};
  std::println("c_dyn who = {}", cd->who());
}
