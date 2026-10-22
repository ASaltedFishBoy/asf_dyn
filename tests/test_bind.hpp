#pragma once

// Unified bind layer for dual-pass suites (general + mirror).
// One source compiles twice: ASF_TEST_VIA_MIRROR=0 (default) binds
// pointers/objects directly; ASF_TEST_VIA_MIRROR=1 binds the same cases
// through asf::to_mirror (sourced-mirror observer). Owning and special-ptr
// binds (unique_ptr / cache_ptr / shared_ptr) do not depend on the pass and
// call asf::to_dyn directly in the shared source.

#include "cast.hpp"

#ifndef ASF_TEST_VIA_MIRROR
#define ASF_TEST_VIA_MIRROR 0
#endif

// Pass-conditional spellings. The mirror pass tags every Trait with
// [[=asf_ann::mirror]] (required for mirror sources) and writes impl self
// parameters as mirror views; the general pass keeps the plain-object
// forms. The tag is NOT semantically inert (it changes iface<Trait, T>
// object-bind storage), so each pass keeps its original semantics exactly.
#if ASF_TEST_VIA_MIRROR
#define ASF_TEST_TRAIT_MIRROR [[=asf_ann::mirror]]
#define ASF_TEST_M(T) asf::mirror<T>
#define ASF_TEST_CM(T) asf::c_mirror<T>
#define ASF_TEST_RM(T) asf::rmirror<T>
#define ASF_TEST_CRM(T) asf::c_rmirror<T>
#define ASF_TEST_COPYM(T) asf::mirror<T>
#define ASF_TEST_M_CREF(T) asf::mirror<T> const&
#else
#define ASF_TEST_TRAIT_MIRROR
#define ASF_TEST_M(T) T&
#define ASF_TEST_CM(T) const T&
#define ASF_TEST_RM(T) T&&
#define ASF_TEST_CRM(T) const T&&
#define ASF_TEST_COPYM(T) const T&
#define ASF_TEST_M_CREF(T) T&
#endif

// Suite banner: merged suites print the mirror/ prefix on the mirror pass.
#if ASF_TEST_VIA_MIRROR
#define ASF_TEST_SUITE_NAME(name) "mirror/" name
#else
#define ASF_TEST_SUITE_NAME(name) name
#endif

namespace asf_test {

#if ASF_TEST_VIA_MIRROR

template <template <class> class Trait, class Obj>
auto to_dyn(Obj& obj) {
  auto m = asf::to_mirror(obj);
  return asf::to_dyn<Trait>(m);
}

template <template <class> class Trait, class Obj>
auto to_c_dyn(Obj& obj) {
  auto m = asf::to_mirror(obj);
  return asf::to_c_dyn<Trait>(m);
}

template <template <class> class Trait, class Obj>
auto to_iface(Obj& obj) {
  auto m = asf::to_mirror(obj);
  return asf::to_iface<Trait>(m);
}

template <template <class> class Trait, class Obj>
auto to_c_iface(Obj& obj) {
  auto m = asf::to_mirror(obj);
  return asf::to_c_iface<Trait>(m);
}

#else

template <template <class> class Trait, class Obj>
auto to_dyn(Obj& obj) {
  return asf::to_dyn<Trait>(&obj);
}

template <template <class> class Trait, class Obj>
auto to_c_dyn(Obj& obj) {
  return asf::to_c_dyn<Trait>(&obj);
}

template <template <class> class Trait, class Obj>
auto to_iface(Obj& obj) {
  return asf::to_iface<Trait>(obj);
}

template <template <class> class Trait, class Obj>
auto to_c_iface(Obj& obj) {
  return asf::to_c_iface<Trait>(obj);
}

#endif

}  // namespace asf_test
