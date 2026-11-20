// Compile-fail: the Final's reachable trait tree (9 nodes) is wider than
// the vt node tag capacity (2^vt_tag_bits = 8; this TU is compiled with
// ASF_DYN_VT_TAG_HI_BITS=3), so the vtable_meta_holder static_assert
// fires. Requires ASF_HAS_DYN_RTTI=1.

#include "asf.hpp"

// The 9-node chain overflows exactly at capacity 8 (3-bit tag field). If
// the capacity differs (global width override) this guard fails instead,
// so the case always fails for the stated reason.
static_assert(asf::detail::vt_tag_capacity == 8,
              "this case assumes a 3-bit vt tag (capacity 8)");

template <typename T = void>
struct C1;
template <typename T = void>
struct C2;
template <typename T = void>
struct C3;
template <typename T = void>
struct C4;
template <typename T = void>
struct C5;
template <typename T = void>
struct C6;
template <typename T = void>
struct C7;
template <typename T = void>
struct C8;
template <typename T = void>
struct C9;

template <>
struct C1<void> {
  int c1(auto const&) { return 1; }
};
template <>
struct C2<void> : C1<void> {};
template <>
struct C3<void> : C2<void> {};
template <>
struct C4<void> : C3<void> {};
template <>
struct C5<void> : C4<void> {};
template <>
struct C6<void> : C5<void> {};
template <>
struct C7<void> : C6<void> {};
template <>
struct C8<void> : C7<void> {};
template <>
struct C9<void> : C8<void> {};

struct Obj {};

template <>
struct C9<Obj> final {};

int main() {
  Obj o{};
  auto d = asf::to_dyn<C9>(&o);  // ERROR: 9 nodes > tag capacity (8)
  (void)d;
  return 0;
}
