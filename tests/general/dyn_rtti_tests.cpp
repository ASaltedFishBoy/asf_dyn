// asf::dyn_cast (Trait RTTI). Requires ASF_HAS_DYN_RTTI=1.

#include "asf_check.hpp"
#include "test_bind.hpp"
#include "cast.hpp"

#include <typeinfo>

namespace dyn_rtti_tests_detail {

struct TypeTag {
  int n{};
  std::string payload{asf_test::no_sso("tag")};
};

template <typename T = void>
struct SomeMinimalTrait;

template <>
struct ASF_TEST_TRAIT_MIRROR
SomeMinimalTrait<void> {
  void mark(auto const& self) { (void)self; }
};

template <>
struct SomeMinimalTrait<TypeTag> final {};

template <typename T = void>
struct I1;
template <typename T = void>
struct I2;
template <typename T = void>
struct I3;
template <typename T = void>
struct Unrelated;

#if ASF_HAS_DYN_MULTI_VT
// Diamond: ID : IB1, IB2; IB1/IB2 : IA. The cast tree picks a single parent
// chain (largest level, first base on ties), so the walk from ID runs
// ID -> IB1 -> IA; IB2 is a same-level side branch off that chain.
// (MI Trait DAG + cover upcasts: MULTI_VT configs only.)
template <typename T = void>
struct IA;
template <typename T = void>
struct IB1;
template <typename T = void>
struct IB2;
template <typename T = void>
struct ID;
#endif

template <>
struct ASF_TEST_TRAIT_MIRROR
I1<void> {
  const char* m1(auto const& /*self*/) { return "m1"; }
};
template <>
struct ASF_TEST_TRAIT_MIRROR
I2<void> : I1<void> {
  const char* m2(auto const& /*self*/) { return "m2"; }
};
template <>
struct ASF_TEST_TRAIT_MIRROR
I3<void> : I2<void> {
  const char* m3(auto const& /*self*/) { return "m3"; }
};
template <>
struct ASF_TEST_TRAIT_MIRROR
Unrelated<void> {
  const char* mu(auto const& /*self*/) { return "mu"; }
};

#if ASF_HAS_DYN_MULTI_VT
template <>
struct ASF_TEST_TRAIT_MIRROR
IA<void> {
  int ma(auto const& /*self*/) { return 101; }
};
template <>
struct ASF_TEST_TRAIT_MIRROR
IB1<void> : IA<void> {
  int mb1(auto const& /*self*/) { return 102; }
};
template <>
struct ASF_TEST_TRAIT_MIRROR
IB2<void> : IA<void> {
  int mb2(auto const& /*self*/) { return 103; }
};
template <>
struct ASF_TEST_TRAIT_MIRROR
ID<void> : IB1<void>, IB2<void> {
  int md(auto const& /*self*/) { return 104; }
};
#endif

struct Obj {
  int tag{};
  std::string payload{asf_test::no_sso("obj")};
};

template <>
struct I3<Obj> final {};
template <>
struct Unrelated<Obj> final {};
#if ASF_HAS_DYN_MULTI_VT
template <>
struct ID<Obj> final {};
#endif

void TestDynCast_Up_Succeed() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf::dyn<I1> base = leaf;
  auto back = asf::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(back));
  ASF_CHECK_STREQ(back->m3(), "m3");
}

void TestDynCast_Cross_Succeed() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf::dyn<I1> base = leaf;
  auto mid = asf::dyn_cast<I2>(base);
  ASF_CHECK(static_cast<bool>(mid));
  ASF_CHECK_STREQ(mid->m2(), "m2");
}

void TestDynCast_Fail() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf::dyn<I1> base = leaf;
  auto miss = asf::dyn_cast<Unrelated>(base);
  ASF_CHECK(!miss);
}

void TestTryCast() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf::dyn<I1> base = leaf;
  auto ok = asf::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(ok));
  auto bad = asf::dyn_cast<Unrelated>(base);
  ASF_CHECK(!bad);
}

void TestTypeid() {
  TypeTag a{17};
  auto p = asf_test::to_dyn<SomeMinimalTrait>(a);
  ASF_CHECK(asf::dyn_typeid(p) == typeid(TypeTag));
}

// Node tag: leaf bind -> root node, identity is the Final trait itself.
void TestNodeTypeid_Leaf() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  ASF_CHECK(asf::dyn_node_typeid(leaf) == typeid(I3<void>));
}

// Intermediate bind: the ADL lookup attaches the covering leaf's table, so
// the node identity is the leaf, not the requested intermediate trait.
void TestNodeTypeid_IntermediateBind() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);  // same-TU final leaf injects first
  auto mid = asf_test::to_dyn<I1>(o);
  ASF_CHECK(asf::dyn_node_typeid(mid) == typeid(I3<void>));
}

// Section-prefix upcast keeps the same physical table: node tag unchanged.
void TestNodeTypeid_UpcastKeepsNode() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf::dyn<I1> base = leaf;
  ASF_CHECK(asf::dyn_node_typeid(base) == typeid(I3<void>));
}

// dyn_cast hit: the tree walk lands on the requested node (root, tag 0).
void TestNodeTypeid_CastHit() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf::dyn<I1> base = leaf;
  auto back = asf::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(back));
  ASF_CHECK(asf::dyn_node_typeid(back) == typeid(I3<void>));
}

void TestNodeTypeid_Empty() {
  asf::dyn<I3> e;
  ASF_CHECK(asf::dyn_node_typeid(e) == typeid(void));
}

#if ASF_HAS_DYN_MULTI_VT
// Tree walk from a prefix-upcast view (tag still the attached leaf ID):
// climbs ID -> IB1 (level 2 -> 1), identity compared at the target level.
// The hit switches only the table; the anchor tag stays the leaf.
void TestTreeCast_BranchHit() {
  Obj o{};
  auto leaf = asf_test::to_dyn<ID>(o);
  asf::dyn<IA> base = leaf;
  auto b1 = asf::dyn_cast<IB1>(base);
  ASF_CHECK(static_cast<bool>(b1));
  ASF_CHECK_EQ(b1->mb1(), 102);
  ASF_CHECK(asf::dyn_node_typeid(b1) == typeid(ID<void>));
}

// The anchor is construction-fixed, so the walk from a post-hit view still
// starts at the leaf: casting back down to the leaf succeeds; hit/miss is
// history-independent.
void TestTreeCast_BackToLeafHit() {
  Obj o{};
  auto leaf = asf_test::to_dyn<ID>(o);
  asf::dyn<IA> base = leaf;
  auto b1 = asf::dyn_cast<IB1>(base);
  ASF_CHECK(static_cast<bool>(b1));
  auto id = asf::dyn_cast<ID>(b1);
  ASF_CHECK(static_cast<bool>(id));
  ASF_CHECK_EQ(id->md(), 104);
}

// Tree walk from a Redirect-upcast view (tag is the IB2 cover node): the
// parent link of IB2 reaches the common base IA; level pruning skips the
// level-1 comparison and lands at level 0.
void TestTreeCast_CommonBaseHit() {
  Obj o{};
  auto leaf = asf_test::to_dyn<ID>(o);
  asf::dyn<IB2> base = leaf;
  auto a = asf::dyn_cast<IA>(base);
  ASF_CHECK(static_cast<bool>(a));
  ASF_CHECK_EQ(a->ma(), 101);
}

// Same-level side branch: IB1 is not on the parent chain of the IB2 node
// (level equal, identity differs -> miss), matching the single-parent tree.
void TestTreeCast_SideBranchMiss() {
  Obj o{};
  auto leaf = asf_test::to_dyn<ID>(o);
  asf::dyn<IB2> base = leaf;
  auto b1 = asf::dyn_cast<IB1>(base);
  ASF_CHECK(!b1);
}
#endif

#if ASF_TEST_VIA_MIRROR
// The recommended way to expose a mirror through dyn: a plain dyn method
// whose impl names the concrete mirror view (no feature macro needed).
struct MirrorBox {
  int num{};
  std::string payload{asf_test::no_sso("mirror")};
};

template <typename T = void>
struct MirrorReadable;

template <>
struct ASF_TEST_TRAIT_MIRROR
MirrorReadable<void> {
  int read(auto const& self);
};

template <>
struct MirrorReadable<MirrorBox> final {
  static int read(ASF_TEST_CM(MirrorBox) self) { return self.num; }
};

void TestMirrorMethod_Ref() {
  MirrorBox box{42};
  auto p = asf_test::to_dyn<MirrorReadable>(box);
  ASF_CHECK_EQ(p->read(), 42);
}
#endif


}  // namespace dyn_rtti_tests_detail

int main() {
  std::fprintf(stdout, ASF_TEST_SUITE_NAME("dyn_rtti_tests") "\n");
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Up_Succeed);
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Cross_Succeed);
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Fail);
  ASF_RUN(dyn_rtti_tests_detail::TestTryCast);
  ASF_RUN(dyn_rtti_tests_detail::TestTypeid);
  ASF_RUN(dyn_rtti_tests_detail::TestNodeTypeid_Leaf);
  ASF_RUN(dyn_rtti_tests_detail::TestNodeTypeid_IntermediateBind);
  ASF_RUN(dyn_rtti_tests_detail::TestNodeTypeid_UpcastKeepsNode);
  ASF_RUN(dyn_rtti_tests_detail::TestNodeTypeid_CastHit);
  ASF_RUN(dyn_rtti_tests_detail::TestNodeTypeid_Empty);
#if ASF_HAS_DYN_MULTI_VT
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_BranchHit);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_BackToLeafHit);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_CommonBaseHit);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_SideBranchMiss);
#endif
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_rtti_tests_detail::TestMirrorMethod_Ref);
#endif
  return asf_test::g_failures ? 1 : 0;
}
