// Redundant direct bases (edge domination) under complex multiple
// inheritance: a direct base that is an ancestor of another direct base
// warns and its edge is treated as truncate, while the base stays reachable
// through the dominating live path.
//
//   A <- B <- C        X <- Y        Z        D <- A
//   Wide   : A, B, C, X, Y, Z        (A,B,X dominated; live C,Y,Z)
//   Diamond: A, B, D                 (A dominated via B and D; live B,D)
//   Cut    : A, [truncate]B, C       (truncated B cannot dominate; A cut via C)
//   Solo   : A, [truncate]B          (truncated B cannot dominate; A stays live)

#include "asf_check.hpp"
#include "cast.hpp"

namespace iface_dominant_base_detail {

template <typename T = void>
struct A;
template <>
struct A<void> {
  int a(auto& self);
};
template <typename T = void>
struct B;
template <>
struct B<void> : A<void> {
  int b(auto& self);
};
template <typename T = void>
struct C;
template <>
struct C<void> : B<void> {
  int c(auto& self);
};
template <typename T = void>
struct X;
template <>
struct X<void> {
  int x(auto& self);
};
template <typename T = void>
struct Y;
template <>
struct Y<void> : X<void> {
  int y(auto& self);
};
template <typename T = void>
struct Z;
template <>
struct Z<void> {
  int z(auto& self);
};
template <typename T = void>
struct D;
template <>
struct D<void> : A<void> {
  int d(auto& self);
};

template <typename T = void>
struct Wide;
template <>
struct Wide<void> final : A<void>, B<void>, C<void>, X<void>, Y<void>,
                          Z<void> {};

template <typename T = void>
struct Diamond;
template <>
struct Diamond<void> final : A<void>, B<void>, D<void> {};

template <typename T = void>
struct Cut;
template <>
struct Cut<void> final : A<void>, [[=asf_ann::truncate]] B<void>, C<void> {
};

template <typename T = void>
struct Solo;
template <>
struct Solo<void> final : A<void>, [[=asf_ann::truncate]] B<void> {};

struct Obj {
  std::string payload{asf_test::no_sso("dom")};
};

// Impl specializations are not final: a final ancestor forbids binding the
// derived Trait as dyn (the leaf is Wide/Diamond/Cut/Solo via Trait<void>).
template <>
struct A<Obj> {
  static int a(Obj&) { return 3; }
};
template <>
struct B<Obj> {
  static int b(Obj&) { return 7; }
};
template <>
struct C<Obj> {
  static int c(Obj&) { return 11; }
};
template <>
struct X<Obj> {
  static int x(Obj&) { return 13; }
};
template <>
struct Y<Obj> {
  static int y(Obj&) { return 17; }
};
template <>
struct Z<Obj> {
  static int z(Obj&) { return 19; }
};
template <>
struct D<Obj> {
  static int d(Obj&) { return 23; }
};

// --- Domination structure (Wide: A<-B<-C chain, X<-Y pair, Z free) ---
static_assert(asf::detail::direct_base_dominated<Wide<void>, 0>());   // A
static_assert(asf::detail::direct_base_dominated<Wide<void>, 1>());   // B
static_assert(!asf::detail::direct_base_dominated<Wide<void>, 2>());  // C
static_assert(asf::detail::direct_base_dominated<Wide<void>, 3>());   // X
static_assert(!asf::detail::direct_base_dominated<Wide<void>, 4>());  // Y
static_assert(!asf::detail::direct_base_dominated<Wide<void>, 5>());  // Z
static_assert(asf::detail::live_dyn_base_count<Wide<void>>() == 3,
              "A, B, X dominated; live bases are C, Y, Z");

// Live (not just DAG) reachability after the cuts.
template <typename T, typename S>
inline constexpr bool live_base_of_v =
    asf::detail::tlist_has<asf::detail::reachable_t<T>, S>::value;

// Every dominated base still reaches the layout through a live path.
static_assert(live_base_of_v<Wide<void>, A<void>>);
static_assert(live_base_of_v<Wide<void>, B<void>>);
static_assert(live_base_of_v<Wide<void>, C<void>>);
static_assert(live_base_of_v<Wide<void>, X<void>>);
static_assert(live_base_of_v<Wide<void>, Y<void>>);
static_assert(live_base_of_v<Wide<void>, Z<void>>);

// --- Diamond: A dominated by both B and D ---
static_assert(asf::detail::direct_base_dominated<Diamond<void>, 0>());
static_assert(!asf::detail::direct_base_dominated<Diamond<void>, 1>());
static_assert(!asf::detail::direct_base_dominated<Diamond<void>, 2>());
static_assert(asf::detail::live_dyn_base_count<Diamond<void>>() == 2);
static_assert(live_base_of_v<Diamond<void>, A<void>>);
static_assert(live_base_of_v<Diamond<void>, B<void>>);
static_assert(live_base_of_v<Diamond<void>, D<void>>);

// --- Cut: truncated B cannot dominate; A is still cut via C ---
static_assert(asf::detail::direct_base_dominated<Cut<void>, 0>());
static_assert(!asf::detail::direct_base_dominated<Cut<void>, 1>());
static_assert(!asf::detail::direct_base_dominated<Cut<void>, 2>());
static_assert(asf::detail::live_dyn_base_count<Cut<void>>() == 1);
// The Cut->B edge is dead, but B and A still reach live through C.
static_assert(live_base_of_v<Cut<void>, C<void>>);
static_assert(live_base_of_v<Cut<void>, B<void>>);
static_assert(live_base_of_v<Cut<void>, A<void>>);

// --- Solo: the only path into B is truncated, so A is NOT dominated ---
static_assert(!asf::detail::direct_base_dominated<Solo<void>, 0>());
static_assert(!asf::detail::direct_base_dominated<Solo<void>, 1>());
static_assert(asf::detail::live_dyn_base_count<Solo<void>>() == 1);
static_assert(live_base_of_v<Solo<void>, A<void>>);
static_assert(!live_base_of_v<Solo<void>, B<void>>,
              "B is reachable only through the truncated edge: not live");

void TestWide_Dyn() {
  Obj o{};
  asf::dyn<Wide> d{&o};
  ASF_CHECK_EQ(d.as<C>()->c(), 11);
  ASF_CHECK_EQ(d.as<C>().as<B>()->b(), 7);
  ASF_CHECK_EQ(d.as<C>().as<A>()->a(), 3);
  ASF_CHECK_EQ(d.as<Y>()->y(), 17);
  ASF_CHECK_EQ(d.as<Y>().as<X>()->x(), 13);
  ASF_CHECK_EQ(d.as<Z>()->z(), 19);
}

void TestDiamond_Iface() {
  Obj o{};
  asf::iface<Diamond, Obj> i{o};
  ASF_CHECK_EQ(i.as<B>()->b(), 7);
  ASF_CHECK_EQ(i.as<D>()->d(), 23);
  ASF_CHECK_EQ(i.as<A>()->a(), 3);
}

void TestCut_Iface() {
  Obj o{};
  asf::iface<Cut, Obj> i{o};
  ASF_CHECK_EQ(i.as<C>()->c(), 11);
  // iface converts along the full Trait DAG (truncate is dyn/MULTI_VT only).
  ASF_CHECK_EQ(i.as<B>()->b(), 7);
  ASF_CHECK_EQ(i.as<A>()->a(), 3);
}

void TestSolo_Iface() {
  Obj o{};
  asf::iface<Solo, Obj> i{o};
  ASF_CHECK_EQ(i.as<A>()->a(), 3);
  ASF_CHECK_EQ(i.as<B>()->b(), 7);
}

}  // namespace iface_dominant_base_detail

int main() {
  std::fprintf(stdout, "general/iface_dominant_base\n");
  ASF_RUN(iface_dominant_base_detail::TestWide_Dyn);
  ASF_RUN(iface_dominant_base_detail::TestDiamond_Iface);
  ASF_RUN(iface_dominant_base_detail::TestCut_Iface);
  ASF_RUN(iface_dominant_base_detail::TestSolo_Iface);
  return asf_test::g_failures ? 1 : 0;
}
