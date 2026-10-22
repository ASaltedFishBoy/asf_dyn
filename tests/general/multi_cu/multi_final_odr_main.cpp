#include "asf_check.hpp"
#include "multi_final_odr_traits.hpp"
#include "test_bind.hpp"

#include <cstdint>
#include <cstring>
#include <print>
#include <string_view>

// Cross-TU probe:
// INTERMEDIATE ON: each TU injects adl_dyn_object_vt(I1) from its final.
// Same friend, different bodies: linker ODR/COMDAT keeps one (internal UB).
// INTERMEDIATE OFF: I1 is only an upcast of that TU's final (no inject).
//
//   multi_final_odr --i2-first
//   multi_final_odr --i3-first
//   multi_final_odr --report

static void dump(std::string_view label, asf::detail::vt_ptr vt) {
  std::println("  {} {:#x}", label, reinterpret_cast<std::uintptr_t>(vt));
}

static int report(Person& person) {
  auto* leaf2 = odr_tu_i2_leaf(person);
  auto* leaf3 = odr_tu_i3_leaf(person);
#if ASF_HAS_DYN_INTERMEDIATE
  auto* i1 = asf::detail::vt_of(asf_test::to_dyn<I1>(person));

  std::println("--- --report (I1 friend already linked) ---");
  dump("I2 leaf", leaf2);
  dump("I3 leaf", leaf3);
  dump("I1 ADL ", i1);
  std::println("  leaves distinct? {}", leaf2 != leaf3);
  std::println("  I1==I2? {}  I1==I3? {}", i1 == leaf2, i1 == leaf3);

  bool ok = leaf2 != leaf3 && (i1 == leaf2 || i1 == leaf3);
  std::println("ok (one linked final owns I1): {}", ok);
#else
  auto* i1_from2 = odr_tu_i2_i1(person);
  auto* i1_from3 = odr_tu_i3_i1(person);

  std::println("--- --report (INTERMEDIATE off; I1 is upcast) ---");
  dump("I2 leaf", leaf2);
  dump("I3 leaf", leaf3);
  dump("I1 upcast I2", i1_from2);
  dump("I1 upcast I3", i1_from3);
  std::println("  leaves distinct? {}", leaf2 != leaf3);
  std::println("  I1(I2)==I2? {}  I1(I3)==I3? {}", i1_from2 == leaf2,
               i1_from3 == leaf3);

  bool ok = leaf2 != leaf3 && i1_from2 == leaf2 && i1_from3 == leaf3;
  std::println("ok (each upcast keeps its leaf): {}", ok);
#endif
  return ok ? 0 : 1;
}

static int run_ordered(Person& person, bool i2_first) {
  asf::detail::vt_ptr leaf2 = nullptr;
  asf::detail::vt_ptr leaf3 = nullptr;
  asf::detail::vt_ptr i1_a = nullptr;
  asf::detail::vt_ptr i1_b = nullptr;

  if (i2_first) {
    leaf2 = odr_tu_i2_leaf(person);
    i1_a = odr_tu_i2_i1(person);
    leaf3 = odr_tu_i3_leaf(person);
    i1_b = odr_tu_i3_i1(person);
    std::println("--- --i2-first (call order; friend already linked) ---");
  } else {
    leaf3 = odr_tu_i3_leaf(person);
    i1_a = odr_tu_i3_i1(person);
    leaf2 = odr_tu_i2_leaf(person);
    i1_b = odr_tu_i2_i1(person);
    std::println("--- --i3-first (call order; friend already linked) ---");
  }

#if ASF_HAS_DYN_INTERMEDIATE
  auto* i1_m = asf::detail::vt_of(asf_test::to_dyn<I1>(person));
  dump("I2 leaf", leaf2);
  dump("I3 leaf", leaf3);
  dump("I1 after first CU", i1_a);
  dump("I1 after second CU", i1_b);
  dump("I1 from main", i1_m);

  bool same = i1_a == i1_b && i1_b == i1_m;
  bool sticks = i1_m == leaf2 || i1_m == leaf3;
  std::println("  all I1 views same? {}  sticks to a leaf? {} (I2={} I3={})",
               same, sticks, i1_m == leaf2, i1_m == leaf3);

  // Call order must not split I1; winner is whichever final's friend the
  // linker kept (observed: often I3 with this link order).
  bool ok = leaf2 != leaf3 && same && sticks;
#else
  dump("I2 leaf", leaf2);
  dump("I3 leaf", leaf3);
  dump("I1 after first CU", i1_a);
  dump("I1 after second CU", i1_b);
  asf::detail::vt_ptr first_leaf = i2_first ? leaf2 : leaf3;
  asf::detail::vt_ptr second_leaf = i2_first ? leaf3 : leaf2;
  bool ok = leaf2 != leaf3 && i1_a == first_leaf && i1_b == second_leaf;
  std::println("  each I1 upcast keeps its own leaf? {}", ok);
#endif
  std::println("ok: {}", ok);
  return ok ? 0 : 1;
}

int main(int argc, char** argv) {
  Person person{asf_test::no_sso("Ada")};

  if (argc == 2 && std::strcmp(argv[1], "--report") == 0)
    return report(person);
  if (argc == 2 && std::strcmp(argv[1], "--i2-first") == 0)
    return run_ordered(person, true);
  if (argc == 2 && std::strcmp(argv[1], "--i3-first") == 0)
    return run_ordered(person, false);

  std::println("usage: multi_final_odr --report | --i2-first | --i3-first");
  return 2;
}
