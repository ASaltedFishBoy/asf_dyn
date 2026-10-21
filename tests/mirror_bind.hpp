#pragma once

// Mirror-suite helpers: bind dyn/iface from a user mirror of an object.
// Matching-view dyn binds observe Src (the object). The sourced-mirror
// ctor takes the mirror by value. View-mismatch / identity_mirror still
// store the mirror object (see dyn_bind_mirror_*).

#include "cast.hpp"

namespace asf_test {

template <template <class> class Trait, class Obj>
auto to_dyn_m(Obj& obj) {
  auto m = asf::to_mirror(obj);
  return asf::to_dyn<Trait>(m);
}

template <template <class> class Trait, class Obj>
auto to_c_dyn_m(Obj& obj) {
  auto m = asf::to_mirror(obj);
  return asf::to_c_dyn<Trait>(m);
}

template <template <class> class Trait, class Obj>
auto to_iface_m(Obj& obj) {
  auto m = asf::to_mirror(obj);
  return asf::to_iface<Trait>(m);
}

}  // namespace asf_test
