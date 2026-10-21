#pragma once

#include "cast.hpp"

#include <string>

// Shared traits for the cross-TU multi-final experiment.
// INTERMEDIATE ON: TU-A binds I2, TU-B binds I3; both define the same
// adl_dyn_object_vt(I1) friend (linker ODR pick; internal UB).
// OFF: I1 is only an upcast of that TU's final (no inject).

template <typename T = void>
struct I1;
template <>
struct [[=asf_ann::mirror]]
I1<void> {
  void m1(auto& self) { (void)self; }
};

template <typename T = void>
struct I2;
template <>
struct [[=asf_ann::mirror]]
I2<void> : I1<void> {
  void m2(auto& self) { (void)self; }
};

template <typename T = void>
struct I3;
template <>
struct [[=asf_ann::mirror]]
I3<void> : I1<void> {
  void m3(auto& self) { (void)self; }
};

struct Person {
  std::string name;
};

template <>
struct I1<Person> {
  static void m1(asf::mirror<Person> self) { (void)self; }
};

template <>
struct I2<Person> final {
  static void m2(asf::mirror<Person> self) { (void)self; }
};

template <>
struct I3<Person> final {
  static void m3(asf::mirror<Person> self) { (void)self; }
};

// TU-A (I2 only)
asf::detail::vt_ptr odr_tu_i2_leaf(Person& p);
asf::detail::vt_ptr odr_tu_i2_i1(Person& p);

// TU-B (I3 only)
asf::detail::vt_ptr odr_tu_i3_leaf(Person& p);
asf::detail::vt_ptr odr_tu_i3_i1(Person& p);
