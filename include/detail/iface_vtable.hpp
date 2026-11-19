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

// Cast-tree link of one node: parent = node of the tree parent trait (-1 at
// a base-less trait); level = static DAG depth of the node's trait (root
// trait = 0), consteval per trait and identical at the cast call site.
// Compile-time only: the tree unrolls into the walk dispatch code; no
// links[] array is materialized.
struct node_link {
  std::uint32_t parent;
  std::uint32_t level;
};

// Cast-tree link for node I: parent = tree parent trait's node (-1 at a
// base-less trait); level = static DAG depth of the node's trait.
template <template <class> class Final, std::size_t I>
consteval node_link rtti_node_link() {
  using node_t = tlist_at<reachable_t<Final<void>>, I>;
  using parent_t = tree_parent_t<node_t>;
  if constexpr (std::is_void_v<parent_t>) {
    return node_link{static_cast<std::uint32_t>(-1),
                     trait_level_resident<node_t>::value};
  } else {
    constexpr std::size_t p =
        tlist_index_of<reachable_t<Final<void>>, parent_t>;
    static_assert(p != static_cast<std::size_t>(-1),
                  "tree parent not reachable under Final");
    return node_link{static_cast<std::uint32_t>(p),
                     trait_level_resident<node_t>::value};
  }
}

// Tree walk from node K toward the roots: the parent chain is a constexpr
// unroll (levels and compared identities are constants); only to_id /
// to_level are runtime. Same check sequence as a links[] loop, as
// straight-line code.
template <template <class> class Final, std::size_t K>
int rtti_walk_from(const std::type_info* to_id,
                   std::uint32_t to_level) noexcept {
  constexpr node_link ln = rtti_node_link<Final, K>();
  if (ln.level < to_level)
    return -1;
  if (ln.level == to_level)
    return &typeid(tlist_at<reachable_t<Final<void>>, K>) == to_id
               ? static_cast<int>(K)
               : -1;
  if constexpr (ln.parent == static_cast<std::uint32_t>(-1)) {
    return -1;
  } else {
    return rtti_walk_from<Final, ln.parent>(to_id, to_level);
  }
}

template <template <class> class Final, std::size_t... I>
int rtti_walk_dispatch_impl(std::size_t node, const std::type_info* to_id,
                            std::uint32_t to_level,
                            std::index_sequence<I...>) noexcept {
  int r = -1;
  (void)((node == I &&
          (r = rtti_walk_from<Final, I>(to_id, to_level), true)) ||
         ...);
  return r;
}

// Per-Final walk dispatch: node tag -> hit node index (-1 on miss). One
// function per Final; meta blocks of all keys under Final point at it.
template <template <class> class Final>
int rtti_walk_dispatch(std::size_t node, const std::type_info* to_id,
                       std::uint32_t to_level) noexcept {
  return rtti_walk_dispatch_impl<Final>(
      node, to_id, to_level,
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

// { meta header, tables[X] }. Address of `meta` is the published
// vtable_meta*; tables() is this+1.
template <std::size_t X>
struct vtable_meta_inst {
  vtable_meta meta;
  vt_ptr tables[X];
};

template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As, class Seq>
struct vtable_meta_holder_for;

template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As, std::size_t... I>
struct vtable_meta_holder_for<MaskNonConst, Final, Storage, As,
                              std::index_sequence<I...>> {
  // The max node index must fit the vt tag field (no runtime escape).
  static_assert(sizeof...(I) <= vt_tag_capacity,
                "RTTI node count exceeds the vt tag bit capacity: the "
                "Final's reachable trait tree is too wide for the pointer "
                "tag field (raise ASF_DYN_VT_TAG_HI_BITS)");
  // cache_deref VT: Storage is Ptr, logical type stays the pointee (As).
  using meta_type =
      std::conditional_t<storage_derefs_to_as<Storage, As>(),
                         std::remove_cvref_t<As>, std::remove_cvref_t<Storage>>;
  static inline const vtable_meta_inst<sizeof...(I)> instance{
      vtable_meta{typeid(meta_type), sizeof...(I), &rtti_walk_dispatch<Final>,
                  &rtti_node_typeid_dispatch<Final>},
      {rtti_table_at<MaskNonConst, Final, Storage, As, I>()...}};
};

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
struct vtable_meta_holder
    : vtable_meta_holder_for<
          MaskNonConst, Final, Storage, As,
          std::make_index_sequence<reachable_t<Final<void>>::size>> {};

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
const vtable_meta* vtable_meta_addr() {
  return &vtable_meta_holder<MaskNonConst, Final, Trait, Storage,
                             As>::instance.meta;
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
  vt_ptr vt = vtable<MaskNonConst, Final, Trait, Storage, As>::slots();
#if ASF_HAS_DYN_RTTI
  // The thunk is instantiated per (Final, cover), so the node index is
  // static here: return the cover vt with its tag already merged.
  constexpr std::size_t k =
      tlist_index_of<reachable_t<Final<void>>, Trait<void>>;
  static_assert(k != static_cast<std::size_t>(-1),
                "redirect cover not reachable under Final");
  return tag_vt(vt, k);
#else
  return vt;
#endif
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

}  // namespace detail
ASF_END_NAMESPACE
