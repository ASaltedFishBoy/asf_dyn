#include "asf_check.hpp"
#include "cast.hpp"
#include "mirror_bind.hpp"

#include <print>
#include <string>
#include <type_traits>

// I2:I1  I3:I1  I4:I2  I5:I2,I3  I6:I3  I7:I4,I5  I8:I5,I6  I9:I7,I8
// Bases via inheritance; Trait<T> `final` marks dyn object-bind leaf for T.
template <typename T = void>
struct I1;
template <>
struct [[=asf_ann::mirror]]
I1<void> {
  std::string m1(auto& self);
};

template <typename T = void>
struct I2;
template <>
struct [[=asf_ann::mirror]]
I2<void> : I1<void> {
  std::string m2(auto& self);
};

template <typename T = void>
struct I3;
template <>
struct [[=asf_ann::mirror]]
I3<void> : I1<void> {
  std::string m3(auto& self);
};

template <typename T = void>
struct I4;
template <>
struct [[=asf_ann::mirror]]
I4<void> : I2<void> {
  std::string m4(auto& self);
};

template <typename T = void>
struct I5;
template <>
struct [[=asf_ann::mirror]]
I5<void> : I2<void>, I3<void> {
  std::string m5(auto& self);
};

template <typename T = void>
struct I6;
template <>
struct [[=asf_ann::mirror]]
I6<void> : I3<void> {
  std::string m6(auto& self);
};

template <typename T = void>
struct I7;
template <>
struct [[=asf_ann::mirror]]
I7<void> : I4<void>, I5<void> {
  std::string m7(auto& self);
};

template <typename T = void>
struct I8;
template <>
struct [[=asf_ann::mirror]]
I8<void> : I5<void>, I6<void> {
  std::string m8(auto& self);
};

template <typename T = void>
struct I9;
template <>
struct [[=asf_ann::mirror]]
I9<void> : I7<void>, I8<void> {
  std::string m9(auto& self);
};

// Same-name on sibling bases: each keeps its own slot; dyn from object only on final.
template <typename T = void>
struct SA;
template <>
struct [[=asf_ann::mirror]]
SA<void> {
  std::string foo(auto& self);
};

template <typename T = void>
struct SB;
template <>
struct [[=asf_ann::mirror]]
SB<void> {
  std::string foo(auto& self);
};

template <typename T = void>
struct SC;
template <>
struct [[=asf_ann::mirror]]
SC<void> : SA<void>, SB<void> {
  std::string bar(auto& self);
};

// Owned hierarchy for default-uptr implicit upcast (move-only).
template <typename T = void>
struct OwnI1;
template <>
struct [[=asf_ann::mirror]]
OwnI1<void> {
  std::string m1(auto& self);
};

template <typename T = void>
struct OwnI2;
template <>
struct [[=asf_ann::mirror]]
OwnI2<void> : OwnI1<void> {
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  {
    return self;
  }
  std::string m2(auto& self);
};

struct Person {
  std::string name;
};

template <>
struct I1<Person> {
  static std::string m1(asf::mirror<Person> self) { return self.name + "/m1"; }
};
template <>
struct I2<Person> {
  static std::string m2(asf::mirror<Person> self) { return self.name + "/m2"; }
};
template <>
struct I3<Person> {
  static std::string m3(asf::mirror<Person> self) { return self.name + "/m3"; }
};
template <>
struct I4<Person> {
  static std::string m4(asf::mirror<Person> self) { return self.name + "/m4"; }
};
template <>
struct I5<Person> {
  static std::string m5(asf::mirror<Person> self) { return self.name + "/m5"; }
};
template <>
struct I6<Person> {
  static std::string m6(asf::mirror<Person> self) { return self.name + "/m6"; }
};
template <>
struct I7<Person> {
  static std::string m7(asf::mirror<Person> self) { return self.name + "/m7"; }
};
// I8 must not be final when I9 is: assert_no_final_trait_parent.
template <>
struct I8<Person> {
  static std::string m8(asf::mirror<Person> self) { return self.name + "/m8"; }
};
template <>
struct I9<Person> final {
  static std::string m9(asf::mirror<Person> self) { return self.name + "/m9"; }
};
template <>
struct SA<Person> {
  static std::string foo(asf::mirror<Person> self) {
    return self.name + "/SA";
  }
};
template <>
struct SB<Person> {
  static std::string foo(asf::mirror<Person> self) {
    return self.name + "/SB";
  }
};
template <>
struct SC<Person> final {
  static std::string bar(asf::mirror<Person> self) {
    return self.name + "/bar";
  }
};

template <>
struct OwnI1<Person> {
  static std::string m1(asf::mirror<Person> self) { return self.name + "/m1"; }
};
template <>
struct OwnI2<Person> final {
  static std::string m2(asf::mirror<Person> self) { return self.name + "/m2"; }
};

static_assert(asf::detail::is_trait_final<I9, Person>());
static_assert(asf::detail::is_trait_final<SC, Person>());
static_assert(!asf::detail::is_trait_final<I8, Person>());

// I9 final leaf; I5/I8 intermediate only when ASF_HAS_DYN_INTERMEDIATE.
static_assert(std::constructible_from<asf::dyn<I9>, Person*>);
#if ASF_HAS_DYN_INTERMEDIATE
static_assert(std::constructible_from<asf::dyn<I5>, Person*>);
static_assert(std::constructible_from<asf::dyn<I8>, Person*>);
#else
static_assert(!std::constructible_from<asf::dyn<I5>, Person*>);
static_assert(!std::constructible_from<asf::dyn<I8>, Person*>);
#endif
static_assert(!std::constructible_from<asf::dyn<I9>, Person&>);
static_assert(std::constructible_from<asf::iface<I5, Person>, Person&>);
static_assert(std::constructible_from<asf::iface<I8, Person>, Person&>);

// dyn_index: own section only (base method names do not resolve on derived).
static_assert(asf::detail::dyn_index<I1>("m1") == 0);
static_assert(asf::detail::dyn_index<I2>("m1") ==
              static_cast<std::size_t>(-1));
static_assert(asf::detail::dyn_index<I2>("m2") !=
              static_cast<std::size_t>(-1));
static_assert(asf::detail::dyn_index<I9>("m1") ==
              static_cast<std::size_t>(-1));
static_assert(asf::detail::dyn_index<I9>("m9") !=
              static_cast<std::size_t>(-1));
static_assert(asf::detail::dyn_index<SC>("foo") ==
              static_cast<std::size_t>(-1));
static_assert(asf::detail::flat_slot_count<I2>() >= 2);
static_assert(asf::detail::is_section_prefix_of<I1, I2>());

static_assert(asf::detail::is_section_prefix_of<I1, I9>());
static_assert(asf::detail::is_section_prefix_of<I2, I9>());
static_assert(asf::detail::is_section_prefix_of<I4, I9>());
static_assert(asf::detail::is_section_prefix_of<I7, I9>());
static_assert(!asf::detail::is_section_prefix_of<I3, I9>());
static_assert(!asf::detail::is_section_prefix_of<I5, I9>());
static_assert(asf::detail::is_section_prefix_of<SA, SC>());
static_assert(!asf::detail::is_section_prefix_of<SB, SC>());

// dyn: Trait-base upcast only. iface: up/down when Self (mirror) converts.
static_assert(std::is_convertible_v<asf::dyn<I9>, asf::dyn<I1>>);
static_assert(!std::is_convertible_v<asf::dyn<I1>, asf::dyn<I9>>);
static_assert(!std::is_constructible_v<asf::shared_dyn<I1>, asf::dyn<I9>&&>);
static_assert(!std::is_convertible_v<asf::dyn<I9>, asf::shared_dyn<I1>>);
static_assert(std::is_convertible_v<asf::shared_dyn<I9>, asf::weak_dyn<I1>>);
static_assert(std::is_convertible_v<asf::unique_dyn<I9>, asf::shared_dyn<I1>>);
static_assert(!std::is_constructible_v<asf::shared_dyn<I1>,
                                       asf::unique_dyn<I9> const&>);
static_assert(std::same_as<decltype(asf::trait_cast<I1>(std::declval<asf::dyn<I9>&>())),
                           asf::dyn<I1>>);
static_assert(std::same_as<decltype(asf::trait_cast<I1>(std::declval<asf::dyn<I9> const&>())),
                           asf::dyn<I1>>);
static_assert(std::same_as<decltype(asf::trait_cast<I1>(std::declval<asf::dyn<I9>>())),
                           asf::dyn<I1>>);
static_assert(std::same_as<decltype(asf::c_trait_cast<I1>(std::declval<asf::dyn<I9>&>())),
                           asf::c_dyn<I1>>);
static_assert(std::same_as<decltype(asf::c_trait_cast<I1>(std::declval<asf::dyn<I9>>())),
                           asf::c_dyn<I1>>);
static_assert(std::same_as<decltype(asf::c_trait_cast<I1>(std::declval<asf::c_dyn<I9> const&>())),
                           asf::c_dyn<I1>>);
static_assert(std::is_convertible_v<asf::iface<I9, Person>,
                                    asf::iface<I1, Person>>);
static_assert(std::is_convertible_v<asf::iface<I1, Person>,
                                    asf::iface<I9, Person>>);
static_assert(std::same_as<decltype(asf::trait_cast<I1>(
                               std::declval<asf::iface<I9, Person>&>())),
                           asf::iface<I1, Person>>);
static_assert(std::same_as<decltype(asf::trait_cast<I9>(
                               std::declval<asf::iface<I1, Person>&>())),
                           asf::iface<I9, Person>>);
static_assert(std::same_as<decltype(asf::c_trait_cast<I1>(
                               std::declval<asf::iface<I9, Person>&>())),
                           asf::c_iface<I1, Person>>);
static_assert(!std::is_convertible_v<asf::sub_dyn<I5>, asf::sub_dyn<I1>>);
static_assert(std::constructible_from<asf::unique_dyn<OwnI1>,
                                      asf::unique_dyn<OwnI2>&&>);
static_assert(!std::constructible_from<asf::unique_dyn<OwnI1>,
                                       asf::unique_dyn<OwnI2>&>);
static_assert(!std::constructible_from<asf::unique_dyn<OwnI1>,
                                       asf::unique_dyn<OwnI2> const&>);

template struct asf::instantiate_dyn<I9, Person>;

int main() {
  Person person{asf_test::no_sso("Ada")};
  auto tag = [&](char const* m) { return person.name + "/" + m; };
  auto expect = [&](std::string const& got, char const* m) {
    return got == tag(m);
  };

  Person other{asf_test::no_sso("Zed")};
  auto d9 = asf_test::to_dyn_m<I9>(person);
  if (!expect(d9->m9(), "m9"))
    return 1;
  auto d1 = asf::trait_cast<I1>(d9);
  if (d9 != d1 || d9 == asf_test::to_dyn_m<I9>(other))
    return 1;
  // Implicit dyn upcast (same as to_dyn / as).
  asf::dyn<I1> d1_imp = d9;
  if (d1_imp != d1)
    return 1;
  auto take_i1 = [](asf::dyn<I1> d) { return static_cast<bool>(d); };
  if (!take_i1(d9))
    return 1;

  // dyn::as - same vt, all ancestors (no convert / no Redirect jump).
  if (!expect(d9.as<I1>()->m1(), "m1") || !expect(d9.as<I2>()->m2(), "m2") ||
      !expect(d9.as<I3>()->m3(), "m3") || !expect(d9.as<I4>()->m4(), "m4") ||
      !expect(d9.as<I5>()->m5(), "m5") || !expect(d9.as<I6>()->m6(), "m6") ||
      !expect(d9.as<I7>()->m7(), "m7") || !expect(d9.as<I8>()->m8(), "m8"))
    return 1;
  static_assert(std::is_invocable_v<decltype(asf::get_proxy<I1>), decltype(d9) &&>);
  static_assert(std::is_invocable_v<decltype(asf::get_proxy<I1>), decltype(d9) &>);
  static_assert(std::is_invocable_v<decltype(asf::get_rproxy<I1>), decltype(d9) &>);
  static_assert(std::is_invocable_v<decltype(asf::get_rproxy<I1>), decltype(d9) &&>);
  static_assert(std::is_invocable_v<decltype(asf::get_forward_proxy<I1>), decltype(d9) &>);
  static_assert(std::is_invocable_v<decltype(asf::get_forward_proxy<I1>), decltype(d9) &&>);
  static_assert(std::is_invocable_v<decltype(asf::get_rproxy<I1>), asf::c_dyn<I9> &>);
  static_assert(std::is_invocable_v<decltype(asf::get_rproxy<I1>), asf::c_dyn<I9> &&>);
  static_assert(std::is_invocable_v<decltype(asf::get_forward_proxy<I1>), asf::c_dyn<I9> &>);
  static_assert(std::is_invocable_v<decltype(asf::get_forward_proxy<I1>), asf::c_dyn<I9> &&>);

  // Continue the same-vt view (still no convert).
  if (!expect(d9.as<I8>().as<I5>().as<I2>().as<I1>()->m1(), "m1") ||
      !expect(d9.as<I8>().as<I6>().as<I3>()->m3(), "m3") ||
      !expect(asf::get_forward_proxy<I1>(d9.as<I7>())->m1(), "m1") ||
      !expect(asf::get_proxy<I3>(d9)->m3(), "m3") ||
      !expect(asf::get_proxy<I8>(d9)->m8(), "m8"))
    return 1;

#if ASF_HAS_DYN_MULTI_VT
  // as/to_dyn may Redirect when a different flat layout is required to store.
  auto d5 = asf::trait_cast<I5>(d9);
  asf::dyn<I5> d5_imp = d9;  // implicit non-prefix upcast
  if (d5_imp.ptr() != d5.ptr())
    return 1;
  if (!expect(d5.as<I2>()->m2(), "m2") || !expect(d5.as<I3>()->m3(), "m3") ||
      !expect(d5.as<I3>().as<I1>()->m1(), "m1") ||
      !expect(asf::get_proxy<I1>(d5)->m1(), "m1"))
    return 1;

  // I9鈫扞5 cover is I8, keyed by Final=I9 (not a bare I8 table).
  static_assert(asf::detail::convert_cover_of<I9, I5>() == ^^I8<void>);
  auto d3 = asf::trait_cast<I3>(d9);
  auto d5from9 = asf::trait_cast<I5>(d9);
  if (!expect(d3.as<I1>()->m1(), "m1") ||
      !expect(d5from9.as<I2>()->m2(), "m2"))
    return 1;
  if (asf::detail::mut_cover_vtable<I9, I8, Person, Person>::slots() !=
      asf::detail::redirect_cover<false, I9, I8, Person, Person>())
    return 1;
#if ASF_HAS_DYN_RTTI
  // dyn_cast: within physical DAG (back to I9 / cross to I5 from I1 handle).
  auto back = asf::dyn_cast<I9>(d1);
  auto cross = asf::dyn_cast<I5>(d1);
  auto miss = asf::dyn_cast<SC>(d1);
  if (!back || !cross || miss)
    return 1;
  if (!expect(back->m9(), "m9") || !expect(cross->m5(), "m5") ||
      !expect(asf::dyn_cast<I9>(d3)->m9(), "m9") ||
      !expect(asf::dyn_cast<I5>(d3)->m5(), "m5") ||
      !expect(asf::dyn_cast<I9>(d5from9)->m9(), "m9"))
    return 1;
#endif
#else
  // Without MULTI_VT, non-prefix bases are not storeable via as/to_dyn; only
  // section-prefix upcasts / unsafe_downcast on the same physical vt.
  if (!expect(asf::unsafe_downcast<I9>(d1)->m9(), "m9"))
    return 1;
#endif

  // sub_dyn: isolated section slice of leaf vt (no MULTI_VT; & copy / && move).
  {
    asf::sub_dyn<I5> w5{d9};
    if (!expect(w5->m5(), "m5"))
      return 1;
    auto w3 = asf::to_sub_dyn<I3>(d9);
    if (!expect(w3->m3(), "m3"))
      return 1;
    asf::sub_dyn<I1> w1{d9};
    if (!expect(w1->m1(), "m1"))
      return 1;
    asf::c_dyn<I9> cd9{d9};
    auto cw3 = asf::to_sub_dyn<I3>(cd9);  // c_dyn -> c_sub_dyn
    (void)cw3;
  }

  // iface::as - same-object base view (Self rewrap).
  auto i9 = asf_test::to_iface_m<I9>(person);
  if (!expect(i9->m9(), "m9") || !expect(i9.as<I1>()->m1(), "m1") ||
      !expect(i9.as<I3>()->m3(), "m3") || !expect(i9.as<I6>()->m6(), "m6") ||
      !expect(i9.as<I8>()->m8(), "m8") ||
      !expect(i9.as<I8>().as<I5>().as<I2>()->m2(), "m2") ||
      !expect(asf::get_proxy<I2>(i9)->m2(), "m2") ||
      !expect(asf::get_proxy<I7>(i9)->m7(), "m7") ||
      !expect(asf::get_forward_proxy<I1>(i9.as<I7>())->m1(), "m1"))
    return 1;
  asf::iface<I1, Person> i1_imp = i9;  // implicit iface upcast
  if (!expect(i1_imp->m1(), "m1"))
    return 1;
  asf::iface<I9, Person> i9_back = i1_imp;  // implicit iface downcast
  if (!expect(i9_back->m9(), "m9"))
    return 1;

  // Sourced iface: same-object / Trait-base compare via Src ptr.
  auto person_m = asf::to_mirror<Person>(person);
  auto other_m = asf::to_mirror<Person>(other);
  auto vi9 = asf::to_iface<I9>(person_m);
  auto vi1 = asf::trait_cast<I1>(vi9);
  if (vi9 != vi1 || vi9 == asf::to_iface<I9>(other_m))
    return 1;
  asf::iface<I1, asf::mirror<Person, Person>> vi1_imp = vi9;
  if (vi1_imp != vi1)
    return 1;
  asf::iface<I9, asf::mirror<Person, Person>> vi9_back = vi1_imp;  // sourced downcast
  if (vi9_back != vi9)
    return 1;
  if (&asf::src_of(vi9.get()) != &person)
    return 1;
  if (!expect(vi9.as<I4>()->m4(), "m4"))
    return 1;

  // default uptr: move-only implicit upcast
  {
    Person own_src{"Own"};
    auto _m_own_src = asf::to_mirror(own_src);
  asf::dyn<OwnI2> own_obs{_m_own_src};
    auto owned = asf::construct<std::unique_ptr>(*own_obs);
    asf::unique_dyn<OwnI1> base = std::move(owned);
    if (!base)
      return 1;
    if (base->m1() != "Own/m1")
      return 1;
  }

  auto sc = asf_test::to_dyn_m<SC>(person);
  if (!expect(sc->bar(), "bar") || !expect(sc.as<SA>()->foo(), "SA"))
    return 1;  // SA is primary / section-prefix of SC
#if ASF_HAS_DYN_MULTI_VT
  if (asf::trait_cast<SB>(sc)->foo() != tag("SB"))  // SB is cover - not get
    return 1;
#endif

  std::println("mirror/iface_inherit ok, vtables={} multi_vt={} rtti={}",
               asf::detail::vtable_instance_count<I9>(), asf::dyn_multi_vt_v,
               asf::dyn_rtti_v);
}
