#include "asf_check.hpp"
#include "cast.hpp"

#include <print>
#include <string>
#include <type_traits>

// ASF_HAS_DYN_SINGLE_INHERIT: live-edge SI only; truncate/truncated edges ok.
// Detection helpers are always checked; dyn bind / upcast when the flag is on.

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
struct Linear;
template <>
struct Linear<void> : A<void> {
  std::string lin(auto& self);
};

// Live MI: two non-truncate bases — forbidden under SINGLE_INHERIT.
template <typename T = void>
struct LiveMI;
template <>
struct LiveMI<void> : A<void>, B<void> {
  std::string mi(auto& self);
};

// truncate on secondary base-specifier: one live base (A).
template <typename T = void>
struct TruncEdge;
template <>
struct TruncEdge<void> : A<void>, [[=asf_ann::truncate]] B<void> {
  std::string leaf(auto& self);
};

// truncated on B: inheriting B is a cut edge; one live base (A).
template <typename T = void>
struct BTrunc;
template <>
struct [[=asf_ann::truncated]] BTrunc<void> {
  std::string b(auto& self);
};

template <typename T = void>
struct TruncPassive;
template <>
struct TruncPassive<void> : A<void>, BTrunc<void> {
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
struct Linear<Obj> final {
  static std::string lin(Obj& self) { return self.name + "/lin"; }
};
template <>
struct LiveMI<Obj> final {
  static std::string mi(Obj& self) { return self.name + "/mi"; }
};
template <>
struct TruncEdge<Obj> final {
  static std::string leaf(Obj& self) { return self.name + "/leaf"; }
};
template <>
struct BTrunc<Obj> {
  static std::string b(Obj& self) { return self.name + "/b"; }
};
template <>
struct TruncPassive<Obj> final {
  static std::string leaf(Obj& self) { return self.name + "/leaf"; }
};

// --- compile-time: live-edge MI detection (independent of the flag) ---
static_assert(!asf::detail::trait_has_multi_inherit<Linear>());
static_assert(asf::detail::trait_has_multi_inherit<LiveMI>());
static_assert(!asf::detail::trait_has_multi_inherit<TruncEdge>());
static_assert(!asf::detail::trait_has_multi_inherit<TruncPassive>());
static_assert(asf::detail::live_dyn_base_count<TruncEdge<void>>() == 1);
static_assert(asf::detail::live_dyn_base_count<LiveMI<void>>() == 2);

#if ASF_HAS_DYN_SINGLE_INHERIT
static_assert(asf::dyn_single_inherit_v);
static_assert(!asf::dyn_multi_vt_v);

static_assert(asf::detail::can_dyn_upcast<A, Linear>());
static_assert(asf::detail::can_dyn_upcast<A, TruncEdge>());
static_assert(!asf::detail::can_dyn_upcast<B, TruncEdge>());
static_assert(asf::detail::can_dyn_upcast<A, TruncPassive>());
static_assert(!asf::detail::can_dyn_upcast<BTrunc, TruncPassive>());
static_assert(std::is_convertible_v<asf::dyn<Linear>, asf::dyn<A>>);
static_assert(std::is_convertible_v<asf::dyn<TruncEdge>, asf::dyn<A>>);
static_assert(!std::is_convertible_v<asf::dyn<TruncEdge>, asf::dyn<B>>);
#endif

int main() {
#if !ASF_HAS_DYN_SINGLE_INHERIT
  std::println("iface_single_inherit: detection ok (flag off; SI dyn path skipped)");
  return 0;
#else
  Obj o{asf_test::no_sso("T")};
  auto tag = [&](char const* m) { return o.name + "/" + m; };

  auto lin = asf::to_dyn<Linear>(&o);
  asf::dyn<A> from_lin = lin;
  if (from_lin->a() != tag("a") || lin->lin() != tag("lin"))
    return 1;

  auto edge = asf::to_dyn<TruncEdge>(&o);
  asf::dyn<A> from_edge = edge;
  if (from_edge->a() != tag("a") || edge->leaf() != tag("leaf"))
    return 1;
  // same-table view still sees truncated branch methods
  if (edge.as<B>()->b() != tag("b"))
    return 1;

  auto pass = asf::to_dyn<TruncPassive>(&o);
  asf::dyn<A> from_pass = pass;
  if (from_pass->a() != tag("a"))
    return 1;

  std::println("iface_single_inherit ok (SINGLE_INHERIT + truncate edges)");
  return 0;
#endif
}
