#include "multi_final_odr_traits.hpp"
#include "test_bind.hpp"

// This CU only ever object-binds I3 (final).
// INTERMEDIATE ON: injects I1 -> I3 primary. OFF: I1 is an upcast of I3.

asf::detail::vt_ptr odr_tu_i3_leaf(Person& p) {
  return asf::detail::vt_of(asf_test::to_dyn<I3>(p));
}

asf::detail::vt_ptr odr_tu_i3_i1(Person& p) {
  auto leaf = asf_test::to_dyn<I3>(p);
#if ASF_HAS_DYN_INTERMEDIATE
  (void)leaf;
  return asf::detail::vt_of(asf_test::to_dyn<I1>(p));
#else
  return asf::detail::vt_of(asf::trait_cast<I1>(leaf));
#endif
}
