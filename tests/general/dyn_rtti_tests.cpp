// asf_dyn::dyn_cast (Trait RTTI). Requires ASF_HAS_DYN_RTTI=1.

#include "asf_check.hpp"
#include "test_bind.hpp"
#include "asf.hpp"

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
// Diamond: ID : IB1, IB2; IB1/IB2 : IA. The runtime cast scans the
// anchor's live-ancestor closure: from the ID leaf anchor all of
// {ID, IB1, IB2, IA} are reachable; from an IB1/IB2 anchor only that
// branch and IA are.
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
  asf_dyn::dyn<I1> base = leaf;
  auto back = asf_dyn::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(back));
  ASF_CHECK_STREQ(back->m3(), "m3");
}

void TestDynCast_Cross_Succeed() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf_dyn::dyn<I1> base = leaf;
  auto mid = asf_dyn::dyn_cast<I2>(base);
  ASF_CHECK(static_cast<bool>(mid));
  ASF_CHECK_STREQ(mid->m2(), "m2");
}

void TestDynCast_Fail() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf_dyn::dyn<I1> base = leaf;
  auto miss = asf_dyn::dyn_cast<Unrelated>(base);
  ASF_CHECK(!miss);
}

void TestTryCast() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf_dyn::dyn<I1> base = leaf;
  auto ok = asf_dyn::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(ok));
  auto bad = asf_dyn::dyn_cast<Unrelated>(base);
  ASF_CHECK(!bad);
}

void TestTypeid() {
  TypeTag a{17};
  auto p = asf_test::to_dyn<SomeMinimalTrait>(a);
  ASF_CHECK(asf_dyn::dyn_typeid(p) == typeid(TypeTag));
}

// Node tag: leaf bind -> root node, identity is the Final trait itself.
void TestNodeTypeid_Leaf() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  ASF_CHECK(asf_dyn::dyn_node_typeid(leaf) == typeid(I3<void>));
}

// Intermediate bind: the anchor is the construction Trait's own node, not
// the covering leaf — the runtime closure is the requested ancestry.
void TestNodeTypeid_IntermediateBind() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);  // same-TU final leaf injects first
  auto mid = asf_test::to_dyn<I1>(o);
  ASF_CHECK(asf_dyn::dyn_node_typeid(mid) == typeid(I1<void>));
}

// An intermediate bind sees only its own ancestry: the covering leaf and
// the chain nodes below the construction trait are definitive misses.
void TestDynCast_IntermediateMiss() {
  Obj o{};
  auto mid = asf_test::to_dyn<I1>(o);  // covering leaf I3
  auto self = asf_dyn::dyn_cast<I1>(mid);
  ASF_CHECK(static_cast<bool>(self));
  auto m2 = asf_dyn::dyn_cast<I2>(mid);
  ASF_CHECK(!m2);
  auto leaf = asf_dyn::dyn_cast<I3>(mid);
  ASF_CHECK(!leaf);
}

// Empty handle: both the static-upcast path and the meta-walk path return
// an empty handle (the dynamic_cast nullptr analogue).
void TestDynCast_Empty() {
  asf_dyn::dyn<I3> e;
  auto up = asf_dyn::dyn_cast<I1>(e);  // static upcast branch
  ASF_CHECK(!up);
  asf_dyn::dyn<I1> e1;
  auto down = asf_dyn::dyn_cast<I2>(e1);  // meta walk branch
  ASF_CHECK(!down);
}

// Section-prefix upcast keeps the same physical table: node tag unchanged.
void TestNodeTypeid_UpcastKeepsNode() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf_dyn::dyn<I1> base = leaf;
  ASF_CHECK(asf_dyn::dyn_node_typeid(base) == typeid(I3<void>));
}

// dyn_cast hit: the tree walk lands on the requested node (root, tag 0).
void TestNodeTypeid_CastHit() {
  Obj o{};
  auto leaf = asf_test::to_dyn<I3>(o);
  asf_dyn::dyn<I1> base = leaf;
  auto back = asf_dyn::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(back));
  ASF_CHECK(asf_dyn::dyn_node_typeid(back) == typeid(I3<void>));
}

void TestNodeTypeid_Empty() {
  asf_dyn::dyn<I3> e;
  ASF_CHECK(asf_dyn::dyn_node_typeid(e) == typeid(void));
}

#if ASF_HAS_DYN_MULTI_VT
// Closure scan from a prefix-upcast view (tag still the leaf node ID):
// ID's ancestors include IB1 (level 1), identity compared at the target
// level. The hit switches only the table; the anchor tag stays the leaf.
void TestTreeCast_BranchHit() {
  Obj o{};
  auto leaf = asf_test::to_dyn<ID>(o);
  asf_dyn::dyn<IA> base = leaf;
  auto b1 = asf_dyn::dyn_cast<IB1>(base);
  ASF_CHECK(static_cast<bool>(b1));
  ASF_CHECK_EQ(b1->mb1(), 102);
  ASF_CHECK(asf_dyn::dyn_node_typeid(b1) == typeid(ID<void>));
}

// The anchor is construction-fixed, so the walk from a post-hit view still
// starts at the leaf: casting back down to the leaf succeeds; hit/miss is
// history-independent.
void TestTreeCast_BackToLeafHit() {
  Obj o{};
  auto leaf = asf_test::to_dyn<ID>(o);
  asf_dyn::dyn<IA> base = leaf;
  auto b1 = asf_dyn::dyn_cast<IB1>(base);
  ASF_CHECK(static_cast<bool>(b1));
  auto id = asf_dyn::dyn_cast<ID>(b1);
  ASF_CHECK(static_cast<bool>(id));
  ASF_CHECK_EQ(id->md(), 104);
}

// Full-DAG runtime upcast: from the leaf anchor every live ancestor is
// reachable, including the other arm of the diamond (a miss under the old
// single-parent walk).
void TestTreeCast_LeafSideBranchHit() {
  Obj o{};
  auto leaf = asf_test::to_dyn<ID>(o);
  asf_dyn::dyn<IA> base = leaf;
  auto b2 = asf_dyn::dyn_cast<IB2>(base);
  ASF_CHECK(static_cast<bool>(b2));
  ASF_CHECK_EQ(b2->mb2(), 103);
  ASF_CHECK(asf_dyn::dyn_node_typeid(b2) == typeid(ID<void>));
}

// Round trip back to the anchor (2 -> 1 -> 2): the anchor is in its own
// closure, so an intermediate-anchored handle viewed as the common base
// casts back down to the construction trait — still without ever
// reaching the covering leaf.
void TestTreeCast_BackToAnchorHit() {
  Obj o{};
  auto mid = asf_test::to_dyn<IB1>(o);  // anchor IB1, covering leaf ID
  asf_dyn::dyn<IA> base = mid;
  auto b1 = asf_dyn::dyn_cast<IB1>(base);
  ASF_CHECK(static_cast<bool>(b1));
  ASF_CHECK_EQ(b1->mb1(), 102);
  ASF_CHECK(asf_dyn::dyn_node_typeid(b1) == typeid(IB1<void>));
  // Same round trip with a dyn_cast first step (hit keeps the anchor tag).
  auto a = asf_dyn::dyn_cast<IA>(mid);
  ASF_CHECK(static_cast<bool>(a));
  auto b1b = asf_dyn::dyn_cast<IB1>(a);
  ASF_CHECK(static_cast<bool>(b1b));
  ASF_CHECK_EQ(b1b->mb1(), 102);
  // The same common-base view cannot cross to the sibling arm (2 -> 1 ->
  // 3): the anchor is still IB1, and IB2 is not in its closure.
  auto b2 = asf_dyn::dyn_cast<IB2>(base);
  ASF_CHECK(!b2);
}

// Intermediate bind anchors at the construction trait: the closure is the
// trait's own ancestors, so a sibling branch (IB2) and the covering leaf
// below the construction (ID) are definitive misses.
void TestTreeCast_IntermediateScope() {
  Obj o{};
  auto mid = asf_test::to_dyn<IB1>(o);  // covering leaf ID injects
  ASF_CHECK(asf_dyn::dyn_node_typeid(mid) == typeid(IB1<void>));
  auto a = asf_dyn::dyn_cast<IA>(mid);
  ASF_CHECK(static_cast<bool>(a));
  ASF_CHECK_EQ(a->ma(), 101);
  auto b2 = asf_dyn::dyn_cast<IB2>(mid);
  ASF_CHECK(!b2);
  auto id = asf_dyn::dyn_cast<ID>(mid);
  ASF_CHECK(!id);
}

// Tree walk from a Redirect-upcast view (tag still the leaf node): ID's
// closure reaches the common base IA; identity compared at the target
// level.
void TestTreeCast_CommonBaseHit() {
  Obj o{};
  auto leaf = asf_test::to_dyn<ID>(o);
  asf_dyn::dyn<IB2> base = leaf;
  auto a = asf_dyn::dyn_cast<IA>(base);
  ASF_CHECK(static_cast<bool>(a));
  ASF_CHECK_EQ(a->ma(), 101);
}

// A Redirect upcast is only a view change: the anchor stays the
// construction leaf (the tag is construction identity, not table state).
// From the IB2 cover view every leaf ancestor is reachable — the sibling
// arm IB1 and the leaf ID itself included.
void TestTreeCast_RedirectViewKeepsAnchor() {
  Obj o{};
  auto leaf = asf_test::to_dyn<ID>(o);
  asf_dyn::dyn<IB2> base = leaf;  // Redirect to the IB2 cover; anchor stays ID
  ASF_CHECK(asf_dyn::dyn_node_typeid(base) == typeid(ID<void>));
  auto b1 = asf_dyn::dyn_cast<IB1>(base);
  ASF_CHECK(static_cast<bool>(b1));
  ASF_CHECK_EQ(b1->mb1(), 102);
  auto id = asf_dyn::dyn_cast<ID>(base);
  ASF_CHECK(static_cast<bool>(id));
  ASF_CHECK_EQ(id->md(), 104);
}

// Complete dyn_cast matrix for the diamond. The anchor is the
// construction trait; a hit requires To in the anchor's live-ancestor
// closure — the current view only selects the static surface, it never
// moves the anchor (upcasts carry the tag, walk hits keep it).
void TestTreeCast_FullMatrix() {
  Obj o{};

  // Anchor ID (leaf bind): self and every live ancestor reachable from
  // every view; only unrelated types miss.
  {
    auto h = asf_test::to_dyn<ID>(o);
    ASF_CHECK(asf_dyn::dyn_node_typeid(h) == typeid(ID<void>));
    asf_dyn::dyn<IB1> v1 = h;  // section-prefix view
    asf_dyn::dyn<IB2> v2 = h;  // Redirect cover view
    asf_dyn::dyn<IA> v0 = h;   // section-prefix view
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<ID>(h)));  // self
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<ID>(v1)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB2>(v1)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IA>(v1)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<ID>(v2)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB1>(v2)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IA>(v2)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<ID>(v0)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB1>(v0)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB2>(v0)));
    ASF_CHECK(!asf_dyn::dyn_cast<Unrelated>(v1));
    ASF_CHECK(!asf_dyn::dyn_cast<Unrelated>(v2));
    ASF_CHECK(!asf_dyn::dyn_cast<Unrelated>(v0));
    // Second-generation view: 4 -> 3(Redirect) -> 1 is the cover table
    // with the leaf tag; casts from it behave like any leaf-anchored view.
    asf_dyn::dyn<IA> v0via3 = v2;
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB1>(v0via3)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<ID>(v0via3)));
    // Multi-hop through walk hits: the hit keeps the leaf anchor, so the
    // chain continues from ID's closure — 4->1->3->4 and 4->1->2->3.
    auto h3 = asf_dyn::dyn_cast<IB2>(v0);
    ASF_CHECK(static_cast<bool>(h3));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<ID>(h3)));
    auto h2 = asf_dyn::dyn_cast<IB1>(v0);
    ASF_CHECK(static_cast<bool>(h2));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB2>(h2)));
  }

  // Anchor IB1 (intermediate bind): closure {IB1, IA} — sibling IB2 and
  // the covering leaf ID are definitive misses, from any view.
  {
    auto h = asf_test::to_dyn<IB1>(o);
    ASF_CHECK(asf_dyn::dyn_node_typeid(h) == typeid(IB1<void>));
    asf_dyn::dyn<IA> v0 = h;
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB1>(h)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IA>(h)));
    ASF_CHECK(!asf_dyn::dyn_cast<IB2>(h));
    ASF_CHECK(!asf_dyn::dyn_cast<ID>(h));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB1>(v0)));
    ASF_CHECK(!asf_dyn::dyn_cast<IB2>(v0));
    ASF_CHECK(!asf_dyn::dyn_cast<ID>(v0));
  }

  // Anchor IB2 (intermediate bind through a Redirect cover): closure
  // {IB2, IA} — sibling IB1 and the covering leaf ID miss from any view.
  {
    auto h = asf_test::to_dyn<IB2>(o);
    ASF_CHECK(asf_dyn::dyn_node_typeid(h) == typeid(IB2<void>));
    asf_dyn::dyn<IA> v0 = h;
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB2>(h)));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IA>(h)));
    ASF_CHECK(!asf_dyn::dyn_cast<IB1>(h));
    ASF_CHECK(!asf_dyn::dyn_cast<ID>(h));
    ASF_CHECK(static_cast<bool>(asf_dyn::dyn_cast<IB2>(v0)));
    ASF_CHECK(!asf_dyn::dyn_cast<IB1>(v0));
    ASF_CHECK(!asf_dyn::dyn_cast<ID>(v0));
  }
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
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_IntermediateMiss);
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Empty);
  ASF_RUN(dyn_rtti_tests_detail::TestNodeTypeid_UpcastKeepsNode);
  ASF_RUN(dyn_rtti_tests_detail::TestNodeTypeid_CastHit);
  ASF_RUN(dyn_rtti_tests_detail::TestNodeTypeid_Empty);
#if ASF_HAS_DYN_MULTI_VT
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_BranchHit);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_BackToLeafHit);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_LeafSideBranchHit);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_BackToAnchorHit);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_IntermediateScope);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_CommonBaseHit);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_RedirectViewKeepsAnchor);
  ASF_RUN(dyn_rtti_tests_detail::TestTreeCast_FullMatrix);
#endif
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_rtti_tests_detail::TestMirrorMethod_Ref);
#endif
  return asf_test::g_failures ? 1 : 0;
}
