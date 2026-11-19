#pragma once

// Vtable storage, RTTI meta, object-bind slots (optional intermediate ADL).

#include "iface_trait.hpp"

#include <cstddef>
#include <new>
#include <utility>

ASF_BEGIN_NAMESPACE
namespace detail {

// Static inline slot arrays: function results assigned at load time; linker
// COMDAT-merges identical (Final,Trait,...) specializations. Redirect only takes
// &cover data (address), so cover content may still be zero-init during init.
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
struct vtable;

#if ASF_HAS_DYN_RTTI
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
const vtable_meta* vtable_meta_addr();

// Per-key cast entry stored in vtable_meta::walk (defined after
// dyn_intermediate.hpp): resolve the target identity through the ADL
// object-vt link function (the same injection channel construction uses),
// gate the resolved node on the target's static DAG level, then test the
// anchor node's ancestor closure for the resolved index.
template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As>
vt_ptr rtti_cast_vt(std::size_t anchor, const std::type_info* to_id,
                    std::uint32_t to_level) noexcept;

// Physical table slots for Trait under Final (leaf or cover).
template <bool MaskNonConst, template <class> class Final,
          template <class> class Cover, typename Storage, typename As>
vt_ptr vtable_cover_slots() {
  return vtable<MaskNonConst, Final, Cover, Storage, As>::slots();
}

// I-th logical node table (nodes: reachable_t<Final>): physical vt of
// convert_cover(Final, node_i); prefix-shared traits repeat the cover's vt.
template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As, std::size_t I>
vt_ptr rtti_table_at() {
  constexpr auto cover = convert_cover_info<Final<void>,
                                            tlist_at<reachable_t<Final<void>>,
                                                     I>>();
  static_assert(cover != std::meta::info{},
                "reachable trait has no cover table under Final");
  constexpr auto cover_tmpl = std::meta::template_of(cover);
  constexpr auto slots_helper = std::meta::substitute(
      ^^vtable_cover_slots,
      {std::meta::reflect_constant(MaskNonConst), ^^Final, cover_tmpl,
       ^^Storage, ^^As});
  return [:slots_helper:]();
}

// Per-key resolve: target identity -> the node's physical table tagged with
// its node index; nullptr when the type is outside the Final's tree. This
// is the only step that compares typeids. Nodes are unique by type
// (reachable_t is deduped), so typeid alone keys a node.
template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As, std::size_t... I>
vt_ptr rtti_node_table_vt_impl(const std::type_info* to_id,
                               std::index_sequence<I...>) noexcept {
  vt_ptr r = nullptr;
  (void)((r == nullptr &&
                  to_id == &typeid(tlist_at<reachable_t<Final<void>>, I>)
              ? (r = tag_vt(rtti_table_at<MaskNonConst, Final, Storage, As,
                                          I>(),
                            I),
                 true)
              : false) ||
         ...);
  return r;
}

template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As>
vt_ptr rtti_node_table_vt(const std::type_info* to_id) noexcept {
  return rtti_node_table_vt_impl<MaskNonConst, Final, Storage, As>(
      to_id, std::make_index_sequence<reachable_t<Final<void>>::size>{});
}

// Ancestor-closure cast: node K's searchable set is reachable_t of its own
// trait (self first, DFS over live direct bases, deduped) — the same
// live-ancestor closure the static upcast rules use. Members are remapped
// into the Final node numbering; each is reachable under Final by
// transitivity. Compile-time only: the closure unrolls into the walk
// dispatch code; no ids[] / links[] arrays are materialized.
template <template <class> class Final, typename T>
consteval std::size_t rtti_anc_index() {
  constexpr std::size_t k = tlist_index_of<reachable_t<Final<void>>, T>;
  static_assert(k != static_cast<std::size_t>(-1),
                "live ancestor not reachable under Final");
  return k;
}

template <template <class> class Final, typename Closure>
struct rtti_anc_seq;
template <template <class> class Final, typename... Ts>
struct rtti_anc_seq<Final, tlist<Ts...>> {
  using type = std::index_sequence<rtti_anc_index<Final, Ts>()...>;
};

// Node -> static DAG level dispatch (per-Final constants; only the
// runtime to_level varies). The level gate is a cast-layer consistency
// check applied to the resolved node; the link-function resolve itself
// is typeid-only.
template <template <class> class Final, std::size_t... I>
bool rtti_node_level_is(std::size_t node, std::uint32_t level,
                        std::index_sequence<I...>) noexcept {
  bool r = false;
  (void)((node == I &&
          (r = trait_level_v<tlist_at<reachable_t<Final<void>>, I>> ==
                   level,
           true)) ||
         ...);
  return r;
}

// Membership of a resolved node index in node K's ancestor closure (self
// included; a trait owns exactly one node). Pure integer compares; the
// resolve that produced the index lives in rtti_node_table_vt.
template <std::size_t... A>
bool rtti_closure_contains(std::index_sequence<A...>, int target) noexcept {
  return ((static_cast<int>(A) == target) || ...);
}

template <template <class> class Final, std::size_t K>
int rtti_walk_from(int target) noexcept {
  using node_t = tlist_at<reachable_t<Final<void>>, K>;
  using seq = typename rtti_anc_seq<Final, reachable_t<node_t>>::type;
  return rtti_closure_contains(seq{}, target) ? target : -1;
}

// Anchor node tag -> membership dispatch over the Final's nodes.
template <template <class> class Final, std::size_t... I>
int rtti_walk_dispatch_impl(std::size_t node, int target,
                            std::index_sequence<I...>) noexcept {
  int r = -1;
  (void)((node == I &&
          (r = rtti_walk_from<Final, I>(target), true)) ||
         ...);
  return r;
}

template <template <class> class Final>
int rtti_walk_dispatch(std::size_t node, int target) noexcept {
  return rtti_walk_dispatch_impl<Final>(
      node, target,
      std::make_index_sequence<reachable_t<Final<void>>::size>{});
}

template <template <class> class Final, std::size_t... I>
const std::type_info& rtti_node_typeid_impl(std::size_t node,
                                            std::index_sequence<I...>) {
  const std::type_info* r = &typeid(void);
  (void)((node == I &&
          (r = &typeid(tlist_at<reachable_t<Final<void>>, I>), true)) ||
         ...);
  return *r;
}

// Per-Final node -> trait typeid dispatch.
template <template <class> class Final>
const std::type_info& rtti_node_typeid_dispatch(std::size_t node) {
  return rtti_node_typeid_impl<Final>(
      node, std::make_index_sequence<reachable_t<Final<void>>::size>{});
}

// One meta block per physical vt key. The walk entry resolves and casts
// through the key's link channel (rtti_cast_vt), so the block carries no
// tables[] array — node tables are referenced from the resolve code.
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
struct vtable_meta_holder {
  static constexpr std::size_t node_count = reachable_t<Final<void>>::size;
  // The max node index must fit the vt tag field (no runtime escape).
  static_assert(node_count <= vt_tag_capacity,
                "RTTI node count exceeds the vt tag bit capacity: the "
                "Final's reachable trait tree is too wide for the pointer "
                "tag field (raise ASF_DYN_VT_TAG_HI_BITS)");
  // cache_deref VT: Storage is Ptr, logical type stays the pointee (As).
  using meta_type =
      std::conditional_t<storage_derefs_to_as<Storage, As>(),
                         std::remove_cvref_t<As>, std::remove_cvref_t<Storage>>;
  static inline const vtable_meta instance{
      typeid(meta_type), node_count,
      &rtti_cast_vt<MaskNonConst, Final, Storage, As>,
      &rtti_node_typeid_dispatch<Final>};
};

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
const vtable_meta* vtable_meta_addr() {
  return &vtable_meta_holder<MaskNonConst, Final, Trait, Storage,
                             As>::instance;
}

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
const vtable_meta* vtable_meta_slot() {
  return vtable_meta_addr<MaskNonConst, Final, Trait, Storage, As>();
}
#endif

// Size of Storage after methods when Trait has op_construct (allocation only).
// Teardown is always vt[-1] (~Storage / Src), not a method slot.
// Construct overloads live in method slots; rproxy selects rvalue overload.
#define ASF_VT_CONSTRUCT_TAIL() , storage_size_slot<Storage>()
#define ASF_VT_DESTROY() vt_fixed_destroy_slot<MaskNonConst, Storage>()

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As,
          typename ISeq,
          bool HasConstruct = trait_has_dyn_construct<Trait>()>
struct vtable_data;

#if ASF_HAS_DYN_RTTI
// [-2]=meta [-1]=destroy
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As,
          std::size_t... I>
struct vtable_data<MaskNonConst, Final, Trait, Storage, As,
                   std::index_sequence<I...>, true> {
  static inline vt_fn const data[] = {
      as_vt_fn(vtable_meta_slot<MaskNonConst, Final, Trait, Storage, As>),
      ASF_VT_DESTROY(),
      make_flat_slot<MaskNonConst, Final, Trait, Storage, As, I>()...
          ASF_VT_CONSTRUCT_TAIL()};
  static inline vt_ptr const ptr = data + 2;
};
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As,
          std::size_t... I>
struct vtable_data<MaskNonConst, Final, Trait, Storage, As,
                   std::index_sequence<I...>, false> {
  static inline vt_fn const data[] = {
      as_vt_fn(vtable_meta_slot<MaskNonConst, Final, Trait, Storage, As>),
      ASF_VT_DESTROY(),
      make_flat_slot<MaskNonConst, Final, Trait, Storage, As, I>()...};
  static inline vt_ptr const ptr = data + 2;
};
#else
// [-1]=destroy
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As,
          std::size_t... I>
struct vtable_data<MaskNonConst, Final, Trait, Storage, As,
                   std::index_sequence<I...>, true> {
  static inline vt_fn const data[] = {
      ASF_VT_DESTROY(),
      make_flat_slot<MaskNonConst, Final, Trait, Storage, As, I>()...
          ASF_VT_CONSTRUCT_TAIL()};
  static inline vt_ptr const ptr = data + 1;
};
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As,
          std::size_t... I>
struct vtable_data<MaskNonConst, Final, Trait, Storage, As,
                   std::index_sequence<I...>, false> {
  static inline vt_fn const data[] = {
      ASF_VT_DESTROY(),
      make_flat_slot<MaskNonConst, Final, Trait, Storage, As, I>()...};
  static inline vt_ptr const ptr = data + 1;
};
#endif

#undef ASF_VT_CONSTRUCT_TAIL
#undef ASF_VT_DESTROY

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As,
          bool HasConstruct = trait_has_dyn_construct<Trait>()>
struct vtable_empty;

#if ASF_HAS_DYN_RTTI
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
struct vtable_empty<MaskNonConst, Final, Trait, Storage, As, true> {
  static inline vt_fn const data[] = {
      as_vt_fn(vtable_meta_slot<MaskNonConst, Final, Trait, Storage, As>),
      vt_fixed_destroy_slot<MaskNonConst, Storage>(),
      storage_size_slot<Storage>()};
  static inline vt_ptr const ptr = data + 2;
};
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
struct vtable_empty<MaskNonConst, Final, Trait, Storage, As, false> {
  static inline vt_fn const data[] = {
      as_vt_fn(vtable_meta_slot<MaskNonConst, Final, Trait, Storage, As>),
      vt_fixed_destroy_slot<MaskNonConst, Storage>()};
  static inline vt_ptr const ptr = data + 2;
};
#else
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
struct vtable_empty<MaskNonConst, Final, Trait, Storage, As, true> {
  static inline vt_fn const data[] = {
      vt_fixed_destroy_slot<MaskNonConst, Storage>(),
      storage_size_slot<Storage>()};
  static inline vt_ptr const ptr = data + 1;
};
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
struct vtable_empty<MaskNonConst, Final, Trait, Storage, As, false> {
  static inline vt_fn const data[] = {
      vt_fixed_destroy_slot<MaskNonConst, Storage>()};
  static inline vt_ptr const ptr = data + 1;
};
#endif

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
struct vtable {
  static vt_ptr slots() {
    constexpr auto nf = flat_slot_count<Trait>();
    if constexpr (nf == 0)
      return vtable_empty<MaskNonConst, Final, Trait, Storage, As>::ptr;
    else
      return vtable_data<MaskNonConst, Final, Trait, Storage, As,
                        std::make_index_sequence<nf>>::ptr;
  }
};

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
vt_ptr redirect_cover() {
  // Table state only: the plain cover vt. The node tag is the caller's
  // construction anchor, not a property of this table — upcast_vt carries
  // the source handle's tag over the Redirect.
  return vtable<MaskNonConst, Final, Trait, Storage, As>::slots();
}

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
vt_fn redirect_slot_ptr() {
  return as_vt_fn(redirect_cover<MaskNonConst, Final, Trait, Storage, As>);
}

// Object-bind sugar: Final == Trait.
template <template <class> class Trait, typename Storage, typename As>
using mut_vtable = vtable<false, Trait, Trait, Storage, As>;

template <template <class> class Trait, typename Storage, typename As>
using c_vtable = vtable<true, Trait, Trait, Storage, As>;

template <template <class> class Final, template <class> class Trait,
          typename Storage, typename As>
using mut_cover_vtable = vtable<false, Final, Trait, Storage, As>;

template <template <class> class Final, template <class> class Trait,
          typename Storage, typename As>
using c_cover_vtable = vtable<true, Final, Trait, Storage, As>;

// Object-bind vt selection. With ASF_HAS_DYN_INTERMEDIATE: final injects ADL
// friend (same-TU STMP first wins); non-final only calls. Multi-TU: ODR/COMDAT.
// Without: caller must require Trait<As> final; just emit that primary table.
#if ASF_HAS_DYN_INTERMEDIATE
#include "dyn_intermediate.hpp"
#else
template <bool MaskNonConst, template <class> class Trait, typename Storage,
          typename As>
vt_ptr dyn_object_slots() {
  assert_no_final_trait_parent<Trait, As>();
  assert_single_inherit<Trait>();
  return vtable<MaskNonConst, Trait, Trait, Storage, As>::slots();
}
#endif  // ASF_HAS_DYN_INTERMEDIATE

#if ASF_HAS_DYN_RTTI
// Per-key cast entry: resolve the target identity to its node's tagged
// table through the ADL object-vt link function (INTERMEDIATE builds: the
// same injection channel that serves construction, so the type tree and
// the tables live in one link artifact per key) or, without the ADL
// channel, the plain per-key resolve. The resolve is typeid-only; the
// resolved node is then gated on the target's static DAG level (the
// cast-layer consistency check) and tested against the anchor node's
// live-ancestor closure; a hit keeps the anchor tag, a miss returns
// nullptr.
template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As>
vt_ptr rtti_cast_vt(std::size_t anchor, const std::type_info* to_id,
                    std::uint32_t to_level) noexcept {
#if ASF_HAS_DYN_INTERMEDIATE
  vt_ptr r = adl_dyn_object_vt(dyn_vt_tag<MaskNonConst, Final, Storage, As>{},
                               to_id);
#else
  vt_ptr r = rtti_node_table_vt<MaskNonConst, Final, Storage, As>(to_id);
#endif
  if (r == nullptr)
    return nullptr;
  const int target = static_cast<int>(vt_node_tag(r));
  if (!rtti_node_level_is<Final>(
          static_cast<std::size_t>(target), to_level,
          std::make_index_sequence<reachable_t<Final<void>>::size>{}))
    return nullptr;
  if (rtti_walk_dispatch<Final>(anchor, target) < 0)
    return nullptr;
  return tag_vt(untag_vt(r), anchor);
}
#endif

}  // namespace detail
ASF_END_NAMESPACE
