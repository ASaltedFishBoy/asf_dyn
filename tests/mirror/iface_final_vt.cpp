#include "asf_check.hpp"
#include "cast.hpp"
#include "mirror_bind.hpp"

#include <algorithm>
#include <print>
#include <string>
#include <utility>
#include <vector>

/* DAG: [2:1],[3:1],[4:2,3],[5:4],[6:4],[7:5,6],[8:7],[9:7],[10:8,9]
         I1
        /  \
      I2    I3
        \  /
         I4
        /  \
      I5    I6
        \  /
         I7
        /  \
      I8    I9
        \  /
        I10 (Trait<Person> final bind) */
template <typename T = void>
struct I1;
template <>
struct [[=asf_ann::mirror]]
I1<void> {
  int m1(auto& self);
};

template <typename T = void>
struct I2;
template <>
struct [[=asf_ann::mirror]]
I2<void> : I1<void> {
  int m2(auto& self);
};

template <typename T = void>
struct I3;
template <>
struct [[=asf_ann::mirror]]
I3<void> : I1<void> {
  int m3(auto& self);
};

template <typename T = void>
struct I4;
template <>
struct [[=asf_ann::mirror]]
I4<void> : I2<void>, I3<void> {
  int m4(auto& self);
};

template <typename T = void>
struct I5;
template <>
struct [[=asf_ann::mirror]]
I5<void> : I4<void> {
  int m5(auto& self);
};

template <typename T = void>
struct I6;
template <>
struct [[=asf_ann::mirror]]
I6<void> : I4<void> {
  int m6(auto& self);
};

template <typename T = void>
struct I7;
template <>
struct [[=asf_ann::mirror]]
I7<void> : I5<void>, I6<void> {
  int m7(auto& self);
};

template <typename T = void>
struct I8;
template <>
struct [[=asf_ann::mirror]]
I8<void> : I7<void> {
  int m8(auto& self);
};

template <typename T = void>
struct I9;
template <>
struct [[=asf_ann::mirror]]
I9<void> : I7<void> {
  int m9(auto& self);
};

template <typename T = void>
struct I10;
template <>
struct [[=asf_ann::mirror]]
I10<void> : I8<void>, I9<void> {
  int m10(auto& self);
};

struct Person {
  std::string name;
  int hits = 0;
};

// Return mixes Src.name with method id; bump hits so calls are observable.
constexpr int src_tag(Person const& p, int id) {
  return static_cast<int>(p.name.size()) * 100 + id;
}

template <>
struct I1<Person> {
  static int m1(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 1;
  }
};
template <>
struct I2<Person> {
  static int m2(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 2;
  }
};
template <>
struct I3<Person> {
  static int m3(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 3;
  }
};
template <>
struct I4<Person> {
  static int m4(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 4;
  }
};
template <>
struct I5<Person> {
  static int m5(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 5;
  }
};
template <>
struct I6<Person> {
  static int m6(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 6;
  }
};
template <>
struct I7<Person> {
  static int m7(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 7;
  }
};
template <>
struct I8<Person> {
  static int m8(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 8;
  }
};
template <>
struct I9<Person> {
  static int m9(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 9;
  }
};
template <>
struct I10<Person> final {
  static int m10(asf::mirror<Person> self) {
    ++self.hits;
    return static_cast<int>(self.name.size()) * 100 + 10;
  }
};

namespace {

using Storage = Person;
using As = Person;

template <template <class> class Final, template <class> class Trait>
asf::detail::vt_ptr table_of() {
  return asf::detail::vtable<false, Final, Trait, Storage, As>::slots();
}

template <template <class> class Final>
consteval std::size_t layout_table_count() {
  return asf::detail::vtable_instance_count<Final>();
}

template <template <class> class Final, std::size_t I>
asf::detail::vt_ptr table_at() {
  constexpr auto trait = [] {
    auto out = asf::detail::vtable_traits_vec<Final<void>>();
    return out[I];
  }();
  constexpr auto helper = std::meta::substitute(
      ^^table_of, {^^Final, std::meta::template_of(trait)});
  return [:helper:]();
}

template <template <class> class Final, std::size_t... I>
std::vector<asf::detail::vt_ptr> collect_impl(std::index_sequence<I...>) {
  return {table_at<Final, I>()...};
}

template <template <class> class Final>
std::vector<asf::detail::vt_ptr> collect_final_tables() {
  return collect_impl<Final>(
      std::make_index_sequence<layout_table_count<Final>()>{});
}

// Call To's own method via get; require return derived from Src.
template <template <class> class To, template <class> class From>
bool invoke_own(asf::dyn<From>& from, Person const& src) {
  if constexpr (!asf::detail::is_trait_base_of<To, From>())
    return true;
  auto h = from.template as<To>();
  if constexpr (std::same_as<To<void>, I1<void>>)
    return h->m1() == src_tag(src, 1);
  else if constexpr (std::same_as<To<void>, I2<void>>)
    return h->m2() == src_tag(src, 2);
  else if constexpr (std::same_as<To<void>, I3<void>>)
    return h->m3() == src_tag(src, 3);
  else if constexpr (std::same_as<To<void>, I4<void>>)
    return h->m4() == src_tag(src, 4);
  else if constexpr (std::same_as<To<void>, I5<void>>)
    return h->m5() == src_tag(src, 5);
  else if constexpr (std::same_as<To<void>, I6<void>>)
    return h->m6() == src_tag(src, 6);
  else if constexpr (std::same_as<To<void>, I7<void>>)
    return h->m7() == src_tag(src, 7);
  else if constexpr (std::same_as<To<void>, I8<void>>)
    return h->m8() == src_tag(src, 8);
  else if constexpr (std::same_as<To<void>, I9<void>>)
    return h->m9() == src_tag(src, 9);
  else if constexpr (std::same_as<To<void>, I10<void>>)
    return h->m10() == src_tag(src, 10);
  else
    return true;
}

template <template <class> class From>
bool upcasts_from(asf::dyn<From>& from, Person const& src, std::size_t& n) {
  bool ok = true;
  auto one = [&]<template <class> class To>() {
    if constexpr (asf::detail::is_trait_base_of<To, From>()) {
      ok = invoke_own<To>(from, src) && ok;
      ++n;
    }
  };
  one.template operator()<I1>();
  one.template operator()<I2>();
  one.template operator()<I3>();
  one.template operator()<I4>();
  one.template operator()<I5>();
  one.template operator()<I6>();
  one.template operator()<I7>();
  one.template operator()<I8>();
  one.template operator()<I9>();
  one.template operator()<I10>();
  return ok;
}

std::size_t all_upcast_channels(asf::dyn<I10>& root, Person const& src,
                                bool& ok) {
  std::size_t n = 0;
  auto from = [&]<template <class> class From>() {
    if constexpr (!asf::detail::is_trait_base_of<From, I10>())
      return;
    if constexpr (std::same_as<From<void>, I10<void>>) {
      ok = upcasts_from(root, src, n) && ok;
    } else if constexpr (asf::detail::can_dyn_upcast<From, I10>()) {
      auto h = asf::trait_cast<From>(root);
      ok = upcasts_from(h, src, n) && ok;
    }
  };
  from.template operator()<I1>();
  from.template operator()<I2>();
  from.template operator()<I3>();
  from.template operator()<I4>();
  from.template operator()<I5>();
  from.template operator()<I6>();
  from.template operator()<I7>();
  from.template operator()<I8>();
  from.template operator()<I9>();
  from.template operator()<I10>();
  return n;
}

std::size_t unique_count(std::vector<asf::detail::vt_ptr> a) {
  std::sort(a.begin(), a.end());
  a.erase(std::unique(a.begin(), a.end()), a.end());
  return a.size();
}

}  // namespace

static_assert(asf::detail::is_trait_final<I10, Person>());
static_assert(!asf::detail::is_trait_final<I9, Person>());
static_assert(asf::detail::can_dyn_upcast<I4, I10>());
static_assert(asf::detail::can_dyn_upcast<I1, I8>());
static_assert(asf::detail::can_dyn_upcast<I7, I9>());
// Cross-diamond: I8鈫扞9 is not an upcast (siblings).
static_assert(!asf::detail::can_dyn_upcast<I9, I8>());
static_assert(!asf::detail::can_dyn_upcast<I8, I9>());

int main() {
#if !ASF_HAS_DYN_MULTI_VT
  std::println("iface_final_vt needs ASF_HAS_DYN_MULTI_VT");
  return 0;
#else
  Person person{asf_test::no_sso("Ada")};
  auto root = asf_test::to_dyn_m<I10>(person);

  // Leaf �?every ancestor via get (same vt); each return depends on Src.name.
  if (root.as<I1>()->m1() != src_tag(person, 1) ||
      root.as<I2>()->m2() != src_tag(person, 2) ||
      root.as<I3>()->m3() != src_tag(person, 3) ||
      root.as<I4>()->m4() != src_tag(person, 4) ||
      root.as<I5>()->m5() != src_tag(person, 5) ||
      root.as<I6>()->m6() != src_tag(person, 6) ||
      root.as<I7>()->m7() != src_tag(person, 7) ||
      root.as<I8>()->m8() != src_tag(person, 8) ||
      root.as<I9>()->m9() != src_tag(person, 9) ||
      root.as<I10>()->m10() != src_tag(person, 10))
    return 1;
  if (person.hits != 10)
    return 1;

  bool ok = true;
  auto channels = all_upcast_channels(root, person, ok);
  if (!ok)
    return 1;

  auto tables = collect_final_tables<I10>();
  auto n_runtime = unique_count(tables);
  constexpr auto layout_n = layout_table_count<I10>();
  constexpr auto section_n = asf::detail::sections_only<I10>().size();

  if (n_runtime != layout_n)
    return 1;

  // Multi-hop get (same Layout vt); to_dyn only when storing another flat.
  if (root.as<I8>().as<I5>().as<I2>().as<I1>()->m1() != src_tag(person, 1))
    return 1;
  if (asf::get_proxy<I1>(root)->m1() != src_tag(person, 1))
    return 1;
  {
    auto d9 = asf::trait_cast<I9>(root);
    if (d9.as<I6>().as<I3>().as<I1>()->m1() != src_tag(person, 1))
      return 1;
  }
  if (root.as<I8>().as<I6>()->m6() != src_tag(person, 6))
    return 1;
  if (root.as<I9>().as<I5>()->m5() != src_tag(person, 5))
    return 1;

  // hits advanced only through Src-backed calls above + channel sweep.
  if (person.hits < 10 + static_cast<int>(channels))
    return 1;

  std::println(
      "iface_final_vt ok: dag=I1..I10 sections={} layout_tables={} "
      "runtime_vtables={} upcast_channels={} hits={} name={}",
      section_n, layout_n, n_runtime, channels, person.hits, person.name);
  return 0;
#endif
}
