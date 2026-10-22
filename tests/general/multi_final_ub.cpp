#include "asf_check.hpp"
#include "cast.hpp"
#include "test_bind.hpp"

#include <cstdint>
#include <print>
#include <string>

// I2 / I3 both final over shared base I1.
// Section-prefix upcast keeps the leaf vt (two final addrs).
// ASF_HAS_DYN_INTERMEDIATE ON: to_dyn<I1> is ADL get-vt; same-TU first final
// wins (cross-TU ODR/COMDAT pick is internal UB).
// OFF: I1 is not pointer-bindable; reach I1 only by upcast from a final.

template <typename T = void>
struct I1;
template <>
struct ASF_TEST_TRAIT_MIRROR
I1<void> {
  void m1(auto& self) { (void)self; }
};

template <typename T = void>
struct I2;
template <>
struct ASF_TEST_TRAIT_MIRROR
I2<void> : I1<void> {
  void m2(auto& self) { (void)self; }
};

template <typename T = void>
struct I3;
template <>
struct ASF_TEST_TRAIT_MIRROR
I3<void> : I1<void> {
  void m3(auto& self) { (void)self; }
};

struct Person {
  std::string name;
};

template <>
struct I1<Person> {
  static void m1(ASF_TEST_M(Person) self) {
    std::println("I1<Person>::m1 {}", self.name);
  }
};

template <>
struct I2<Person> final {
  static void m2(ASF_TEST_M(Person) self) {
    std::println("I2<Person>::m2 {}", self.name);
  }
};

template <>
struct I3<Person> final {
  static void m3(ASF_TEST_M(Person) self) {
    std::println("I3<Person>::m3 {}", self.name);
  }
};

static_assert(asf::detail::is_trait_final<I2, Person>());
static_assert(asf::detail::is_trait_final<I3, Person>());
static_assert(!asf::detail::is_trait_final<I1, Person>());
#if ASF_HAS_DYN_INTERMEDIATE
static_assert(std::constructible_from<asf::dyn<I1>, Person*>);
#else
static_assert(!std::constructible_from<asf::dyn<I1>, Person*>);
#endif
static_assert(std::constructible_from<asf::dyn<I2>, Person*>);
static_assert(std::constructible_from<asf::dyn<I3>, Person*>);
static_assert(!std::constructible_from<asf::dyn<I1>, Person&>);
static_assert(std::is_convertible_v<asf::dyn<I2>, asf::dyn<I1>>);
static_assert(std::is_convertible_v<asf::dyn<I3>, asf::dyn<I1>>);
static_assert(asf::detail::is_section_prefix_of<I1, I2>());
static_assert(asf::detail::is_section_prefix_of<I1, I3>());

int main() {
  Person person{asf_test::no_sso("Ada")};

  auto d2 = asf_test::to_dyn<I2>(person);
  auto d3 = asf_test::to_dyn<I3>(person);
  auto d1_from_i2 = asf::trait_cast<I1>(d2);
  auto d1_from_i3 = asf::trait_cast<I1>(d3);
  asf::dyn<I1> d1_imp = d2;

  auto* vt2 = asf::detail::vt_of(d2);
  auto* vt3 = asf::detail::vt_of(d3);
  auto* vt1_from2 = asf::detail::vt_of(d1_from_i2);
  auto* vt1_from3 = asf::detail::vt_of(d1_from_i3);

  std::println("vt I2 (Final=I2)           {:#x}",
               reinterpret_cast<std::uintptr_t>(vt2));
  std::println("vt I3 (Final=I3)           {:#x}",
               reinterpret_cast<std::uintptr_t>(vt3));
  std::println("vt I1 from I2 (same vt?)   {:#x} eq_I2={}",
               reinterpret_cast<std::uintptr_t>(vt1_from2), vt1_from2 == vt2);
  std::println("vt I1 from I3 (same vt?)   {:#x} eq_I3={}",
               reinterpret_cast<std::uintptr_t>(vt1_from3), vt1_from3 == vt3);
  std::println("vt I1 implicit I2          {:#x} eq_I2={}",
               reinterpret_cast<std::uintptr_t>(asf::detail::vt_of(d1_imp)),
               asf::detail::vt_of(d1_imp) == vt2);

  d2->m2();
  d3->m3();
  d1_from_i2->m1();
  d1_from_i3->m1();
  d1_imp->m1();

#if ASF_HAS_DYN_INTERMEDIATE
  auto d1 = asf_test::to_dyn<I1>(person);
  auto* vt1 = asf::detail::vt_of(d1);
  std::println("vt I1 direct (ADL)         {:#x} eq_I2={} (STM first final)",
               reinterpret_cast<std::uintptr_t>(vt1), vt1 == vt2);
  d1->m1();
  bool ok = vt2 != vt3 && vt1_from2 == vt2 && vt1_from3 == vt3 &&
            asf::detail::vt_of(d1_imp) == vt2 && vt1 == vt2;
  std::println("multi_final_ub ok: two finals + ADL I1==I2? {} rtti={}", ok,
               asf::dyn_rtti_v);
#else
  bool ok = vt2 != vt3 && vt1_from2 == vt2 && vt1_from3 == vt3 &&
            asf::detail::vt_of(d1_imp) == vt2;
  std::println("multi_final_off ok: upcast I1 keeps leaf vt? {} rtti={}", ok,
               asf::dyn_rtti_v);
#endif
  return ok ? 0 : 1;
}
