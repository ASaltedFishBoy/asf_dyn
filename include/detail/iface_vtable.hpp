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

// One switch arm: trait_id match -> consteval tables index.
template <template <class> class Final, std::size_t I>
bool rtti_find_index_arm(const void* to_id, std::size_t* out) {
  using target_t = tlist_at<rtti_targets_t<Final<void>>, I>;
  constexpr auto target = std::meta::dealias(^^target_t);
  constexpr auto target_tmpl = std::meta::template_of(target);
  constexpr auto id_helper =
      std::meta::substitute(^^trait_id_addr, {target_tmpl});
  if (to_id != [:id_helper:]())
    return false;
  constexpr auto k = rtti_table_index_t<Final<void>, target_t>();
  static_assert(k != static_cast<std::size_t>(-1),
                "rtti target has no physical table under Final");
  *out = k;
  return true;
}

// Switch-style find: trait_id -> tables index, (std::size_t)-1 on miss.
template <template <class> class Final, std::size_t... I>
std::size_t rtti_find_index_pack(const void* to_id, std::index_sequence<I...>) {
  std::size_t out = static_cast<std::size_t>(-1);
  (void)(rtti_find_index_arm<Final, I>(to_id, &out) || ...);
  return out;
}

template <template <class> class Final>
std::size_t rtti_find_index(const void* to_id) {
  constexpr auto n = rtti_targets_t<Final<void>>::size;
  return rtti_find_index_pack<Final>(to_id, std::make_index_sequence<n>{});
}

// I-th distinct physical vt (vtable_traits_t<Final>)[I]).
template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As, std::size_t I>
vt_ptr rtti_table_at() {
  constexpr auto trait =
      std::meta::dealias(^^tlist_at<vtable_traits_t<Final<void>>, I>);
  constexpr auto trait_tmpl = std::meta::template_of(trait);
  constexpr auto slots_helper = std::meta::substitute(
      ^^vtable_cover_slots,
      {std::meta::reflect_constant(MaskNonConst), ^^Final, trait_tmpl,
       ^^Storage, ^^As});
  return [:slots_helper:]();
}

// { meta header, tables[X] suffix }. Address of `meta` is the published
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
  // proxy_wrap VT: Storage is Ptr, logical type stays the pointee (As).
  using meta_type =
      std::conditional_t<storage_derefs_to_as<Storage, As>(),
                         std::remove_cvref_t<As>, std::remove_cvref_t<Storage>>;
  static inline const vtable_meta_inst<sizeof...(I)> instance{
      vtable_meta{&rtti_find_index<Final>, typeid(meta_type)},
      {rtti_table_at<MaskNonConst, Final, Storage, As, I>()...}};
};

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
struct vtable_meta_holder
    : vtable_meta_holder_for<
          MaskNonConst, Final, Storage, As,
          std::make_index_sequence<vtable_traits_t<Final<void>>::size>> {};

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

}  // namespace detail
ASF_END_NAMESPACE
