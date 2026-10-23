#include "asf_check.hpp"
#include "cast.hpp"

#include <print>
#include <string>
#include <type_traits>

// truncate / truncated: prune storeable dyn upcast + MULTI_VT cover tables.
// as<> / iface convert are unchanged (same-object / no cover).

template <typename T = void>
struct A;
template <>
struct A<void> {
  std::string a(auto& self);
};

template <typename T = void>
struct B;
template <>
struct B<void> {
  std::string b(auto& self);
};

template <typename T = void>
struct C;
template <>
struct C<void> {
  std::string c(auto& self);
};

// Active truncate on Trait: dyn cannot upcast further from Mid.
template <typename T = void>
struct Mid;
template <>
struct [[=asf_ann::truncate]] Mid<void> : A<void> {
  std::string mid(auto& self);
};

// Separate ancestor (not final for Obj) so a derived leaf can still bind.
template <typename T = void>
struct MidCut;
template <>
struct [[=asf_ann::truncate]] MidCut<void> : A<void> {
  std::string mid(auto& self);
};

// Leaf via MidCut: can upcast to MidCut, not past it to A.
template <typename T = void>
struct LeafViaMid;
template <>
struct LeafViaMid<void> : MidCut<void>, B<void> {
  std::string leaf(auto& self);
};

// Active truncate on secondary base-specifier: no upcast into that branch.
template <typename T = void>
struct LeafEdge;
template <>
struct LeafEdge<void> : A<void>, [[=asf_ann::truncate]] B<void> {
  std::string leaf(auto& self);
};

// Passive truncated on B: inheriting B acts as truncate on the edge.
template <typename T = void>
struct BTrunc;
template <>
struct [[=asf_ann::truncated]] BTrunc<void> {
  std::string b(auto& self);
};

template <typename T = void>
struct LeafPassive;
template <>
struct LeafPassive<void> : A<void>, BTrunc<void> {
  std::string leaf(auto& self);
};

// Side branch exclusive of A: truncate edge drops Side and C only.
template <typename T = void>
struct Side;
template <>
struct Side<void> : C<void> {
  std::string side(auto& self);
};

template <typename T = void>
struct LeafSideCut;
template <>
struct LeafSideCut<void> : A<void>, [[=asf_ann::truncate]] Side<void> {
  std::string leaf(auto& self);
};

struct Obj {
  std::string name;
};

template <>
struct A<Obj> {
  static std::string a(Obj& self) { return self.name + "/a"; }
};
template <>
struct B<Obj> {
  static std::string b(Obj& self) { return self.name + "/b"; }
};
template <>
struct C<Obj> {
  static std::string c(Obj& self) { return self.name + "/c"; }
};
template <>
struct Mid<Obj> final {
  static std::string mid(Obj& self) { return self.name + "/mid"; }
};
template <>
struct MidCut<Obj> {
  static std::string mid(Obj& self) { return self.name + "/mid"; }
};
template <>
struct LeafViaMid<Obj> final {
  static std::string leaf(Obj& self) { return self.name + "/leaf"; }
};
template <>
struct LeafEdge<Obj> final {
  static std::string leaf(Obj& self) { return self.name + "/leaf"; }
};
template <>
struct BTrunc<Obj> {
  static std::string b(Obj& self) { return self.name + "/b"; }
};
template <>
struct LeafPassive<Obj> final {
  static std::string leaf(Obj& self) { return self.name + "/leaf"; }
};
template <>
struct Side<Obj> {
  static std::string side(Obj& self) { return self.name + "/side"; }
};
template <>
struct LeafSideCut<Obj> final {
  static std::string leaf(Obj& self) { return self.name + "/leaf"; }
};

// --- Mid: truncate on self ---
static_assert(asf::detail::can_dyn_upcast<Mid, Mid>());
static_assert(!asf::detail::can_dyn_upcast<A, Mid>());
static_assert(std::is_convertible_v<asf::dyn<Mid>, asf::dyn<Mid>>);
static_assert(!std::is_convertible_v<asf::dyn<Mid>, asf::dyn<A>>);
static_assert(asf::detail::vtable_instance_count<Mid>() == 1);

// --- LeafViaMid: ancestor MidCut is truncate ---
static_assert(asf::detail::can_dyn_upcast<LeafViaMid, LeafViaMid>());
static_assert(asf::detail::can_dyn_upcast<MidCut, LeafViaMid>());
static_assert(!asf::detail::can_dyn_upcast<A, LeafViaMid>());
#if ASF_HAS_DYN_MULTI_VT
static_assert(asf::detail::can_dyn_upcast<B, LeafViaMid>());
static_assert(asf::detail::vtable_instance_count<LeafViaMid>() == 2);  // leaf + B cover
#else
static_assert(!asf::detail::can_dyn_upcast<B, LeafViaMid>());
static_assert(asf::detail::vtable_instance_count<LeafViaMid>() == 1);
#endif

// --- LeafEdge: truncate on base-specifier to B ---
static_assert(asf::detail::can_dyn_upcast<A, LeafEdge>());
static_assert(!asf::detail::can_dyn_upcast<B, LeafEdge>());
static_assert(asf::detail::vtable_instance_count<LeafEdge>() == 1);

// --- LeafPassive: truncated on BTrunc ---
static_assert(asf::detail::can_dyn_upcast<A, LeafPassive>());
static_assert(!asf::detail::can_dyn_upcast<BTrunc, LeafPassive>());
static_assert(asf::detail::vtable_instance_count<LeafPassive>() == 1);

// --- LeafSideCut: truncate secondary Side (drops Side/C; A shared OK) ---
static_assert(asf::detail::can_dyn_upcast<A, LeafSideCut>());
static_assert(!asf::detail::can_dyn_upcast<Side, LeafSideCut>());
static_assert(!asf::detail::can_dyn_upcast<C, LeafSideCut>());
static_assert(asf::detail::vtable_instance_count<LeafSideCut>() == 1);

// iface still converts along the full Trait DAG (truncate is dyn/MULTI_VT).
static_assert(std::is_convertible_v<asf::iface<LeafEdge, Obj>,
                                    asf::iface<B, Obj>>);
static_assert(std::is_convertible_v<asf::iface<LeafPassive, Obj>,
                                    asf::iface<BTrunc, Obj>>);

int main() {
  Obj o{asf_test::no_sso("T")};
  auto tag = [&](char const* m) { return o.name + "/" + m; };
  auto mid = asf::to_dyn<Mid>(&o);
  if (mid->mid() != tag("mid") || mid.as<A>()->a() != tag("a"))
    return 1;

  auto leaf = asf::to_dyn<LeafViaMid>(&o);
  if (leaf->leaf() != tag("leaf") || leaf.as<A>()->a() != tag("a") ||
      leaf.as<B>()->b() != tag("b"))
    return 1;
  asf::dyn<MidCut> to_mid = leaf;
  if (to_mid->mid() != tag("mid"))
    return 1;

  auto edge = asf::to_dyn<LeafEdge>(&o);
  if (edge->leaf() != tag("leaf") || edge.as<B>()->b() != tag("b"))
    return 1;
  asf::dyn<A> to_a = edge;
  if (to_a->a() != tag("a"))
    return 1;

  auto pass = asf::to_dyn<LeafPassive>(&o);
  if (pass.as<BTrunc>()->b() != tag("b"))
    return 1;

  std::println("iface_truncate ok, mid_vt={} leaf_vt={} multi_vt={}",
               asf::detail::vtable_instance_count<Mid>(),
               asf::detail::vtable_instance_count<LeafViaMid>(),
               asf::dyn_multi_vt_v);
}
