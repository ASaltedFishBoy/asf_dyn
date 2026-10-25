#pragma once

// dyn / c_dyn handles, dyn_view / c_dyn_view, dyn_handle_base, *dyn fn_proxy.
// dyn<Trait, Ptr=void*>: Ptr is the complete erased pointer type;
// vt stays on the handle. Default void* = non-owning observation pointer.

#include "iface.hpp"
#include "ref.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <memory>
#include <meta>
#include <new>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

ASF_BEGIN_NAMESPACE

namespace detail {
template <class Derived, template <class> class Trait, bool Const,
          bool SliceCall, bool SoftUptrDestroy, class Ptr>
class dyn_ptr_core;
template <class Derived, template <class> class Trait, bool Const,
          bool SliceCall, bool SoftUptrDestroy, class Ptr>
vt_ptr vt_of(dyn_ptr_core<Derived, Trait, Const, SliceCall, SoftUptrDestroy,
                          Ptr> const& h) noexcept;
}  // namespace detail

// Advanced: raw vtable pointer of a dyn / c_dyn handle. Not a member �?// keep the public handle surface free of vt plumbing.
template <class Handle>
detail::vt_ptr vtable_of(Handle const& h) noexcept;

// Dispose kind of an erased Ptr. One marker, three states:
//   observer            -- no ownership (default when kind is absent)
//   vt_destroy          -- release runs vt[-1] destroy, then the pointer drops
//                          storage
//   deleter             -- release does not run destroy; constructing the Ptr
//                          is P(raw, d) where d destroys then frees
// A missing trait, or a missing / unrecognized kind, is observer.
// pointer_trait hooks: the kind tag; optionally release(Handle&) to replace
// the default vt_destroy behavior (the library passes *this); and optionally
// an Ops-shaped `ops` type for cache-style Ptr lifetime/bool slots.
// A deleter Ptr is built as P(raw, d); d destroys then frees.
struct pointer_vt_destroy_tag {
  explicit pointer_vt_destroy_tag() = default;
};
struct pointer_deleter_tag {
  explicit pointer_deleter_tag() = default;
};
inline constexpr pointer_vt_destroy_tag pointer_vt_destroy{};
inline constexpr pointer_deleter_tag pointer_deleter{};

enum class pointer_kind { observer, vt_destroy, deleter };

template <class P>
struct pointer_trait {};

// Defaults live on the iface_proxy.hpp forward decls (included via iface.hpp).
template <template <class> class Trait, class Ptr>
class dyn;
template <template <class> class Trait, class Ptr>
class c_dyn;
template <template <class> class Trait, class Ptr>
class sub_dyn;
template <template <class> class Trait, class Ptr>
class c_sub_dyn;

// Same-vt base view returned by dyn::as / c_dyn::as (observer only).
template <template <class> class Layout, template <class> class View>
class dyn_view;
template <template <class> class Layout, template <class> class View>
class c_dyn_view;

namespace detail {
template <class T, class D>
struct dyn_typed_delete;
template <class Derived, template <class> class Trait, bool Const,
          bool SliceCall, bool SoftUptrDestroy, class Ptr>
class dyn_ptr_core;
template <template <template <class> class, class> class Handle,
          template <class> class Trait, bool Const, class Ptr>
class dyn_handle_base;
template <class P>
void ptr_reset(P&);
template <class P>
void ptr_reset(P&, void*);
}

// unique_ptr Deleter for non-default Alloc: sized block via allocator_traits.
// Handle runs vt[-1] destroy first. (Not ordinary ::delete.)
template <class Alloc = std::allocator<std::byte>>
struct alloc_delete {
  using allocator_type = Alloc;

  Alloc alloc{};
  std::size_t size = 0;

  alloc_delete() noexcept = default;
  explicit alloc_delete(std::size_t n) noexcept : size(n) {}
  alloc_delete(Alloc a, std::size_t n) noexcept
      : alloc(std::move(a)), size(n) {}

  // Null p is the caller's job (unique_ptr already skips empty).
  void operator()(void* p) const noexcept {
    using ByteAlloc =
        typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
    ByteAlloc ba(alloc);
    std::allocator_traits<ByteAlloc>::deallocate(
        ba, static_cast<std::byte*>(p), size);
  }
};

namespace detail {
struct new_delete;  // fwd: the default Free below (defined with the Ptr types)
struct dyn_shared_storage;  // fwd: named in dyn_adopt_delete_impl's assert
}

// Dest deleter for a deleter-kind Ptr: vt[-1] destroy, then Free(p).
// Default source storage deleter is not forwarded; Free defaults to
// detail::new_delete (the void-safe stand-in for std::default_delete<void>:
// ::operator delete). dyn_default_delete names the default-Free form.
template <class Free = detail::new_delete>
struct dyn_adopt_delete;

using dyn_default_delete = dyn_adopt_delete<>;

// cache_ptr creation is disabled (see cast.hpp). The buffer travels with the
// object.
// Not relocatable by pointer steal; dyn move relocates via op_construct.
// Ownership Ptrs are std::unique_ptr / shared_ptr / weak_ptr (no asf aliases).
// unique_ptr recognition is by deleter: detail::new_delete (default
// std::default_delete<void>), alloc_delete (non-default Alloc), or
// dyn_adopt_delete (deleter-kind: destroy lives in the Ptr;
// dyn_default_delete aliases the default-Free form).

// How one of copy / move / destroy is defined.
//   op_trivial            memcpy, or no payload destructor
//   op_trivial_relocate   move only: memcpy bytes (source vt is cleared by dyn)
//   op_unsupported        special member is deleted
//   op_unset              not chosen yet
//   op_from_trait         internal: cache_dyn uses Trait op_construct / vt[-1]
//                         destroy; no type and no function pointer in the Ptr
//   anything else         a callable for that slot, carried by the type and
//                         invoked in place (no per-object state).
//   op_copy_of<T> / op_move_of<T> / op_destroy_of<T>  T's own special member
//                    copy (void*, void const*), move (void*, void*), destroy (void*)
// VT method/construct slots come from the Trait alone. Ops only chooses whether
// the Ptr-level shallow path uses a slot (or leaves it unsupported).
// Ops for a Ptr come from pointer_trait<P>::ops when present. The type need
// not be asf::pointer_ops; dyn reads slot aliases and each slot's call
// operator. If absent, dyn uses that Ptr's native type interface.
struct op_trivial {};
struct op_trivial_relocate {};
struct op_unsupported {};
struct op_unset {};
struct op_from_trait {
  explicit op_from_trait() = default;
};

// Proxy (Trait-erased) shallow slots: same as op_from_trait; named for
// proxy_wrap / smart-pointer cache Specs where copy/move go through the VT.
using op_proxy_copy = op_from_trait;
using op_proxy_move = op_from_trait;

namespace detail {
// Modified proxy: an erased typed op (natural specialization or user
// override) carried into the erased world with its identity. Internal
// erasure detail: users never write this. Mutually exclusive with
// op_trivial; op_from_trait is the fallback when neither applies. Empty
// marker (the inner op is usually stateless), so it costs nothing.
template <class Op>
struct op_modified {
  using op_type = Op;
};
}  // namespace detail

template <class T>
struct op_copy_of {
  void operator()(void* dst, void const* src) const {
    std::construct_at(static_cast<T*>(dst), *static_cast<T const*>(src));
  }
};
template <class T>
struct op_move_of {
  void operator()(void* dst, void* src) const {
    auto* s = static_cast<T*>(src);
    std::construct_at(static_cast<T*>(dst), std::move(*s));
    s->~T();
  }
};
template <class T>
struct op_destroy_of {
  void operator()(void* p) const { static_cast<T*>(p)->~T(); }
};

// Assign into an already-live destination object: no construct, no destroy
// for that step. The typed relocation tier between stored fns and the
// op_construct + destroy deep path.
template <class T>
struct op_assign_copy_of {
  void operator()(void* dst, void const* src) const {
    *static_cast<T*>(dst) = *static_cast<T const*>(src);
  }
};
template <class T>
struct op_assign_move_of {
  void operator()(void* dst, void* src) const {
    *static_cast<T*>(dst) = std::move(*static_cast<T*>(src));
  }
};

// Cache / handle emptiness: dyn is non-empty iff its vt is live. Invoked with
// the dyn handle (op_modified unwraps to this). See asf::vtable_of.
struct op_bool_vt {
  bool operator()(auto const& h) const noexcept {
    return vtable_of(h) != nullptr;
  }
};

// Convenience pack. Any type with the same slot aliases is an Ops; dyn never
// requires this exact template �?it looks at destroy/copy/move(/assign/bool)
// types and each slot's call operator (or tag).
template <class Destroy = op_unset, class Copy = op_unset,
          class Move = op_unset, class CopyAssign = op_unset,
          class MoveAssign = op_unset, class Bool = op_unset>
struct pointer_ops {
  static_assert(!std::is_same_v<Destroy, op_unsupported>,
                "pointer_ops destroy cannot be op_unsupported");
  using destroy_type = Destroy;
  using copy_type = Copy;
  using move_type = Move;
  // Assign tiers: assign into an already-live destination. op_unset means no
  // assign op (fall back to construct).
  using copy_assign_type = CopyAssign;
  using move_assign_type = MoveAssign;
  // Emptiness query for dyn::operator bool. op_unset -> native Ptr bool.
  using bool_type = Bool;
};

// Structural Ops: slot aliases, not a prescribed pack type. Assign slots are
// optional (missing -> op_unset). Destroy must not be op_unsupported.
template <class Ops>
concept pointer_ops_shaped = requires {
  typename Ops::destroy_type;
  typename Ops::copy_type;
  typename Ops::move_type;
};

template <class Ops>
inline constexpr bool is_pointer_ops_v = false;
template <class Ops>
  requires(pointer_ops_shaped<Ops>)
inline constexpr bool is_pointer_ops_v<Ops> =
    !std::is_same_v<typename Ops::destroy_type, op_unsupported>;

template <class Ops>
  requires(is_pointer_ops_v<Ops>)
using ops_destroy_t = typename Ops::destroy_type;
template <class Ops>
  requires(is_pointer_ops_v<Ops>)
using ops_copy_t = typename Ops::copy_type;
template <class Ops>
  requires(is_pointer_ops_v<Ops>)
using ops_move_t = typename Ops::move_type;

namespace detail {
template <class Ops, class = void>
struct ops_copy_assign_or {
  using type = op_unset;
};
template <class Ops>
struct ops_copy_assign_or<Ops, std::void_t<typename Ops::copy_assign_type>> {
  using type = typename Ops::copy_assign_type;
};
template <class Ops, class = void>
struct ops_move_assign_or {
  using type = op_unset;
};
template <class Ops>
struct ops_move_assign_or<Ops, std::void_t<typename Ops::move_assign_type>> {
  using type = typename Ops::move_assign_type;
};
template <class Ops, class = void>
struct ops_bool_or {
  using type = op_unset;
};
template <class Ops>
struct ops_bool_or<Ops, std::void_t<typename Ops::bool_type>> {
  using type = typename Ops::bool_type;
};
}  // namespace detail

template <class Ops>
  requires(is_pointer_ops_v<Ops>)
using ops_copy_assign_t = typename detail::ops_copy_assign_or<Ops>::type;
template <class Ops>
  requires(is_pointer_ops_v<Ops>)
using ops_move_assign_t = typename detail::ops_move_assign_or<Ops>::type;
template <class Ops>
  requires(is_pointer_ops_v<Ops>)
using ops_bool_t = typename detail::ops_bool_or<Ops>::type;

template <class Ops>
  requires(is_pointer_ops_v<Ops>)
inline constexpr bool pointer_ops_all_unset_v =
    std::is_same_v<ops_destroy_t<Ops>, op_unset> &&
    std::is_same_v<ops_copy_t<Ops>, op_unset> &&
    std::is_same_v<ops_move_t<Ops>, op_unset>;

// pointer_trait<P>::ops when present: any Ops-shaped export.
template <class P>
inline constexpr bool ptr_declares_ops_v = false;
template <class P>
  requires requires { typename pointer_trait<P>::ops; }
inline constexpr bool ptr_declares_ops_v<P> =
    is_pointer_ops_v<typename pointer_trait<P>::ops>;

template <class P>
  requires(ptr_declares_ops_v<P>)
using ptr_ops_t = typename pointer_trait<P>::ops;

namespace detail {
// Cache emptiness always rides as modified op_bool_vt (handle; vtable_of).
using cache_bool_op = op_modified<op_bool_vt>;

// Erased ops for cache_dyn: behavior comes from Trait, not from a stored type.
using pointer_ops_erased =
    pointer_ops<op_from_trait, op_from_trait, op_from_trait, op_unset,
                op_unset, cache_bool_op>;
}

// cache_op_* tags below; ops_with_cache_bool follows them.

// Ops packs for smart-pointer-in-cache.
// unique: no copy; move is byte-relocate. Destroy via VT.
// shared: copy via Trait; move is byte-relocate. Destroy via VT.
// Bool: modified vt check (dyn handle in; no is_cache_ptr specialization).
using unique_pointer_ops =
    pointer_ops<op_from_trait, op_unsupported, op_trivial_relocate, op_unset,
                op_unset, detail::cache_bool_op>;
using shared_pointer_ops =
    pointer_ops<op_from_trait, op_proxy_copy, op_trivial_relocate, op_unset,
                op_unset, detail::cache_bool_op>;

template <class T>
inline constexpr bool cache_op_trivial_v = std::is_same_v<T, op_trivial>;
template <class T>
inline constexpr bool cache_op_trivial_relocate_v =
    std::is_same_v<T, op_trivial_relocate>;
template <class T>
inline constexpr bool cache_op_unsupported_v =
    std::is_same_v<T, op_unsupported>;
template <class T>
inline constexpr bool cache_op_unset_v = std::is_same_v<T, op_unset>;
template <class T>
inline constexpr bool cache_op_from_trait_v = std::is_same_v<T, op_from_trait>;

template <class T>
inline constexpr bool cache_op_modified_v = false;
template <class Op>
inline constexpr bool cache_op_modified_v<detail::op_modified<Op>> = true;

namespace detail {
// cache_ptr export: unset bool -> modified op_bool_vt (vtable_of on the dyn).
template <class Ops>
struct ops_with_cache_bool {
  using destroy_type = ops_destroy_t<Ops>;
  using copy_type = ops_copy_t<Ops>;
  using move_type = ops_move_t<Ops>;
  using copy_assign_type = ops_copy_assign_t<Ops>;
  using move_assign_type = ops_move_assign_t<Ops>;
  using bool_type =
      std::conditional_t<cache_op_unset_v<ops_bool_t<Ops>>, cache_bool_op,
                         ops_bool_t<Ops>>;
};
template <class Ops>
using ops_with_cache_bool_t = ops_with_cache_bool<Ops>;
}  // namespace detail

// Unwrap: a slot's callable op. Fixed typed ops are themselves; modified
// proxies unwrap (recursively, so a re-erased modified slot collapses).
// Every capability query on a custom op goes through this unwrap.
template <class Op, bool = cache_op_modified_v<Op>>
struct cache_slot_op {
  using type = Op;
};
template <class Op>
struct cache_slot_op<Op, true> : cache_slot_op<typename Op::op_type> {};
template <class Op>
using cache_slot_op_t = typename cache_slot_op<Op>::type;

namespace detail {
// Unified typed-op invocation, one structure for every dyn handle
// flavor: an op may overload on the participating public handles
// (deduced) in addition to the erased payload pointers. Wherever a
// participating handle is fully formed, the handle form is tried first;
// the void* form is the baseline every op must provide. Construct
// receives the source handle only (its destination is raw storage, not
// yet a handle); assign receives both. The final dtor has no live handle
// either (op_no_handle is passed), so destroy always stays
// void*-invocable. Assign alone may omit every form: destroy + construct
// substitutes for it, so a dead assign tier simply falls back.
//
// Only the hookup differs per Ptr flavor, if at all: cache Ptrs reach
// all five ops here; other Ptr flavors own their teardown inside the
// smart pointer (deleter) or the vt, so destroy is the only op this
// could ever reach for them -- and there is no handle to hook in.
struct op_no_handle {};

template <class F, class H>
void dyn_op_invoke_destroy(H& h, void* p) {
  if constexpr (!std::same_as<std::remove_cvref_t<H>, op_no_handle> &&
                std::invocable<F, H&>)
    F{}(h);
  else
    F{}(p);
}
template <class F, class SrcH>
void dyn_op_invoke_copy(void* dst, SrcH const& sh, void const* sp) {
  if constexpr (std::invocable<F, void*, SrcH const&>)
    F{}(dst, sh);
  else
    F{}(dst, sp);
}
template <class F, class SrcH>
void dyn_op_invoke_move(void* dst, SrcH& sh, void* sp) {
  if constexpr (std::invocable<F, void*, SrcH&>)
    F{}(dst, sh);
  else
    F{}(dst, sp);
}
template <class F, class DstH, class SrcH>
void dyn_op_invoke_copy_assign(DstH& dh, SrcH const& sh, void* dp,
                               void const* sp) {
  if constexpr (std::invocable<F, DstH&, SrcH const&>)
    F{}(dh, sh);
  else
    F{}(dp, sp);
}
template <class F, class DstH, class SrcH>
void dyn_op_invoke_move_assign(DstH& dh, SrcH& sh, void* dp, void* sp) {
  if constexpr (std::invocable<F, DstH&, SrcH&>)
    F{}(dh, sh);
  else
    F{}(dp, sp);
}
}  // namespace detail

template <class T>
inline constexpr bool cache_op_fixed_v =
    !cache_op_trivial_v<T> && !cache_op_trivial_relocate_v<T> &&
    !cache_op_unsupported_v<T> && !cache_op_unset_v<T> &&
    !cache_op_from_trait_v<T>;

// Rank, strongest first: trivial, the type's own operation, stored, none.
// A requested op may replace T's with a weaker implementation of the same
// operation, or the user may explicitly strip it with op_unsupported.
// It cannot become stronger than what T can do.
template <class T>
consteval int cache_copy_natural_rank() {
  if constexpr (std::is_trivially_copy_constructible_v<T>)
    return 3;
  else if constexpr (std::is_copy_constructible_v<T>)
    return 2;
  else
    return 0;
}
template <class T>
consteval int cache_move_natural_rank() {
  if constexpr (std::is_trivially_move_constructible_v<T>)
    return 3;
  else if constexpr (std::is_move_constructible_v<T>)
    return 2;
  else
    return 0;
}
template <class T>
consteval int cache_destroy_natural_rank() {
  if constexpr (std::is_trivially_destructible_v<T>)
    return 3;
  else if constexpr (std::is_destructible_v<T>)
    return 2;
  else
    return 0;
}

template <class Op, class T>
consteval int cache_copy_request_rank() {
  if constexpr (cache_op_unsupported_v<Op>)
    return 0;
  else if constexpr (cache_op_from_trait_v<Op>)
    return 2;
  else if constexpr (cache_op_trivial_v<Op>)
    return 3;
  else if constexpr (std::invocable<cache_slot_op_t<Op>, void*, void const*>)
    return 2;
  else
    return 4;
}
template <class Op, class T>
consteval int cache_move_request_rank() {
  if constexpr (cache_op_unsupported_v<Op>)
    return 0;
  else if constexpr (cache_op_from_trait_v<Op>)
    return 2;
  // Byte relocate is an alternate move impl (unique_ptr-shaped), not a
  // stronger-than-trivial demand: same rank as typed move.
  else if constexpr (cache_op_trivial_relocate_v<Op>)
    return 2;
  else if constexpr (cache_op_trivial_v<Op>)
    return 3;
  else if constexpr (std::invocable<cache_slot_op_t<Op>, void*, void*>)
    return 2;
  else
    return 4;
}
template <class Op, class T>
consteval int cache_destroy_request_rank() {
  if constexpr (cache_op_unsupported_v<Op>)
    return 0;
  else if constexpr (cache_op_from_trait_v<Op>)
    return 2;
  else if constexpr (cache_op_trivial_v<Op>)
    return 3;
  else if constexpr (std::invocable<cache_slot_op_t<Op>, void*>)
    return 2;
  else
    return 4;
}

consteval bool cache_op_can_weaken(int request, int natural) {
  if (request == 0)
    return true;
  return request <= natural;
}

template <class T, class Ops>
consteval bool pointer_ops_can_weaken() {
  return cache_op_can_weaken(cache_copy_request_rank<ops_copy_t<Ops>, T>(),
                             cache_copy_natural_rank<T>()) &&
         cache_op_can_weaken(cache_move_request_rank<ops_move_t<Ops>, T>(),
                             cache_move_natural_rank<T>()) &&
         cache_op_can_weaken(
             cache_destroy_request_rank<ops_destroy_t<Ops>, T>(),
             cache_destroy_natural_rank<T>());
}

// Standalone cache_ptr (not yet a cache_dyn): match T operation by operation.
template <class T>
using cache_copy_of =
    std::conditional_t<std::is_trivially_copy_constructible_v<T>, op_trivial,
                       std::conditional_t<std::is_copy_constructible_v<T>,
                                          op_copy_of<T>, op_unsupported>>;
template <class T>
using cache_move_of =
    std::conditional_t<std::is_trivially_move_constructible_v<T>, op_trivial,
                       std::conditional_t<std::is_move_constructible_v<T>,
                                          op_move_of<T>, op_unsupported>>;
template <class T>
using cache_destroy_of =
    std::conditional_t<std::is_trivially_destructible_v<T>, op_trivial,
                       op_destroy_of<T>>;

// Assign tier defaults: trivial when the type is trivially assignable, the
// type's own operator= when assignable, else unset (no assign op).
template <class T>
using cache_assign_copy_of =
    std::conditional_t<std::is_trivially_copy_assignable_v<T>, op_trivial,
                       std::conditional_t<std::is_copy_assignable_v<T>,
                                          op_assign_copy_of<T>, op_unset>>;
template <class T>
using cache_assign_move_of =
    std::conditional_t<std::is_trivially_move_assignable_v<T>, op_trivial,
                       std::conditional_t<std::is_move_assignable_v<T>,
                                          op_assign_move_of<T>, op_unset>>;

template <class T>
using pointer_ops_of =
    pointer_ops<cache_destroy_of<T>, cache_copy_of<T>, cache_move_of<T>,
                cache_assign_copy_of<T>, cache_assign_move_of<T>,
                detail::cache_bool_op>;

template <class T, class Ops>
struct resolve_pointer_ops {
  static_assert(is_pointer_ops_v<Ops>,
                "Ops must expose destroy_type / copy_type / move_type");
  static_assert(std::is_destructible_v<T>,
                "pointer_ops requires a destructible type");
  static_assert(pointer_ops_can_weaken<T, Ops>(),
                "pointer_ops may only weaken T's copy, move, and destroy");
  using type = Ops;
};
template <class T, class Ops>
  requires(pointer_ops_all_unset_v<Ops>)
struct resolve_pointer_ops<T, Ops> {
  static_assert(std::is_destructible_v<T>,
                "pointer_ops requires a destructible type");
  using type = pointer_ops_of<T>;
};
template <class T, class Ops>
using resolve_pointer_ops_t = typename resolve_pointer_ops<T, Ops>::type;

template <class Op>
consteval bool cache_op_can_wrap_copy() {
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
    else if constexpr (cache_op_trivial_v<Op>)
    return true;
  else
    return std::invocable<cache_slot_op_t<Op>, void*, void const*>;
}
template <class Op>
consteval bool cache_op_can_wrap_move() {
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
  else if constexpr (cache_op_trivial_v<Op> ||
                     cache_op_trivial_relocate_v<Op>)
    return true;
  else
    return std::invocable<cache_slot_op_t<Op>, void*, void*>;
}
template <class Op>
consteval bool cache_op_can_wrap_destroy() {
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
    else if constexpr (cache_op_trivial_v<Op>)
    return true;
  else
    return std::invocable<cache_slot_op_t<Op>, void*>;
}
template <class Op>
consteval bool cache_op_can_wrap_copy_assign() {
  return cache_op_can_wrap_copy<Op>();
}
template <class Op>
consteval bool cache_op_can_wrap_move_assign() {
  return cache_op_can_wrap_move<Op>();
}

// PreferOpFromTrait: when true (cache_dyn enter), Trait chain can deep-construct
// -> op_from_trait; else keep T's shallow op (trivial / typed). T is erased
// after enter, so that choice must happen here �?call sites only see Trait.
// false: typed erase without a Trait (trivial stays; wrap-able rides along).
// Copy/move may stay stripped. Destroy is never stripped.
template <class SrcOp, bool PreferOpFromTrait>
using pointer_ops_erase_copy = std::conditional_t<
    cache_op_unsupported_v<SrcOp>, op_unsupported,
    std::conditional_t<
        PreferOpFromTrait, op_from_trait,
        std::conditional_t<
            cache_op_trivial_v<SrcOp>, op_trivial,
            std::conditional_t<cache_op_can_wrap_copy<SrcOp>(),
                               detail::op_modified<SrcOp>, op_unsupported>>>>;
template <class SrcOp, bool PreferOpFromTrait>
using pointer_ops_erase_move = std::conditional_t<
    cache_op_unsupported_v<SrcOp>, op_unsupported,
    // Explicit byte-relocate stays at the Ptr level; Trait construct slots
    // remain on the VT either way (pointer_ops only chooses whether to use them).
    std::conditional_t<
        cache_op_trivial_relocate_v<SrcOp>, op_trivial_relocate,
        std::conditional_t<
            PreferOpFromTrait, op_from_trait,
            std::conditional_t<
                cache_op_trivial_v<SrcOp>, op_trivial,
                std::conditional_t<cache_op_can_wrap_move<SrcOp>(),
                                   detail::op_modified<SrcOp>,
                                   op_unsupported>>>>>;
template <class SrcOp, bool PreferOpFromTrait>
using pointer_ops_erase_destroy = std::conditional_t<
    PreferOpFromTrait, op_from_trait,
    std::conditional_t<cache_op_trivial_v<SrcOp>, op_trivial,
                       detail::op_modified<SrcOp>>>;

// Assign slots erase independently of the Trait: relocation does not consult
// the op_assign catalog. from_trait degrades to unset (no assign op); typed
// assigner ops become modified proxies (empty marker keeping the op).
template <class SrcOp>
using pointer_ops_erase_assign = std::conditional_t<
    cache_op_unset_v<SrcOp> || cache_op_from_trait_v<SrcOp>, op_unset,
    std::conditional_t<
        cache_op_trivial_v<SrcOp> || cache_op_trivial_relocate_v<SrcOp>, SrcOp,
        detail::op_modified<SrcOp>>>;

// Bool slot: unset stays unset; already-modified / cache_bool_op keep; else
// wrap the callable as op_modified (handle form can take the dyn).
template <class SrcOp>
using pointer_ops_erase_bool = std::conditional_t<
    cache_op_unset_v<SrcOp>, op_unset,
    std::conditional_t<cache_op_modified_v<SrcOp>, SrcOp,
                       detail::op_modified<SrcOp>>>;

// Into cache_dyn: PreferOpFromTrait only when the Trait chain can deep-construct
// (else keep T's shallow ops �?T is gone after erase). Src vs T weaken is
// separate and always required.
template <template <class> class Trait, class Ops>
using pointer_ops_erase_for_trait = pointer_ops<
    pointer_ops_erase_destroy<ops_destroy_t<Ops>, true>,
    pointer_ops_erase_copy<ops_copy_t<Ops>,
                         detail::chain_has_dyn_copy_construct<Trait>()>,
    pointer_ops_erase_move<ops_move_t<Ops>,
                         detail::chain_has_dyn_move_construct<Trait>()>,
    pointer_ops_erase_assign<ops_copy_assign_t<Ops>>,
    pointer_ops_erase_assign<ops_move_assign_t<Ops>>,
    pointer_ops_erase_bool<ops_bool_t<Ops>>>;

template <template <class> class Trait, class T>
using pointer_ops_from_trait_or_t = pointer_ops_erase_for_trait<Trait, pointer_ops_of<T>>;

template <template <class> class Trait>
using pointer_ops_from_trait = pointer_ops<
    op_from_trait,
    std::conditional_t<detail::chain_has_dyn_copy_construct<Trait>(),
                       op_from_trait, op_unsupported>,
    std::conditional_t<detail::chain_has_dyn_move_construct<Trait>(),
                       op_from_trait, op_unsupported>,
    op_unset, op_unset, detail::cache_bool_op>;

// Source ops must weaken T. Erasure may still consult Trait only to choose
// op_from_trait vs keeping T's shallow slot (T will not be available later).
template <template <class> class Trait, class T, class Ops>
struct resolve_pointer_dyn_ops {
  static_assert(is_pointer_ops_v<Ops>,
                "Ops must expose destroy_type / copy_type / move_type");
  static_assert(std::is_destructible_v<T>,
                "pointer_ops requires a destructible type");
  static_assert(pointer_ops_can_weaken<T, Ops>(),
                "pointer_ops may only weaken T's copy, move, and destroy");
  using type = pointer_ops_erase_for_trait<Trait, Ops>;
};
template <template <class> class Trait, class T, class Ops>
  requires(pointer_ops_all_unset_v<Ops>)
struct resolve_pointer_dyn_ops<Trait, T, Ops> {
  static_assert(std::is_destructible_v<T>,
                "pointer_ops requires a destructible type");
  using type = pointer_ops_from_trait_or_t<Trait, T>;
};
template <template <class> class Trait, class T, class Ops>
using resolve_pointer_dyn_ops_t =
    typename resolve_pointer_dyn_ops<Trait, T, Ops>::type;

template <class Op>
consteval bool cache_op_is_erased_slot() {
  return cache_op_from_trait_v<Op> || cache_op_trivial_v<Op> ||
         cache_op_trivial_relocate_v<Op> || cache_op_unsupported_v<Op>;
}

// Erased form: no payload type left in the ops (from_trait / stored / trivial).
template <class Ops>
inline constexpr bool pointer_ops_is_erased_v =
    cache_op_is_erased_slot<ops_copy_t<Ops>>() &&
    cache_op_is_erased_slot<ops_move_t<Ops>>() &&
    cache_op_is_erased_slot<ops_destroy_t<Ops>>() &&
    !(cache_op_unsupported_v<ops_copy_t<Ops>> &&
      cache_op_unsupported_v<ops_move_t<Ops>> &&
      cache_op_unsupported_v<ops_destroy_t<Ops>>);

template <class Op>
consteval bool cache_copy_live() {
  // op_from_trait: Ptr itself does not copy; dyn uses Trait op_construct.
  // A modified slot unwraps to its inner op for the capability check.
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
    else if constexpr (cache_op_trivial_v<Op>)
    return true;
  else
    return std::invocable<cache_slot_op_t<Op>, void*, void const*>;
}
template <class Op>
consteval bool cache_move_live() {
  // op_from_trait: Ptr itself does not move; dyn relocates via Trait.
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
  else if constexpr (cache_op_trivial_v<Op> ||
                     cache_op_trivial_relocate_v<Op>)
    return true;
  else
    return std::invocable<cache_slot_op_t<Op>, void*, void*>;
}
template <class Op>
consteval bool cache_destroy_live() {
  // op_from_trait: hold clears only; dyn runs vt[-1] destroy.
  if constexpr (cache_op_unsupported_v<Op>)
    return false;
  else
    return true;
}

// Assign tier: assign into an already-live destination. from_trait is not an
// assign op (relocation does not consult the op_assign catalog).
template <class Op>
consteval bool cache_copy_assign_live_v() {
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
    else if constexpr (cache_op_trivial_v<Op>)
    return true;
  else
    return std::invocable<cache_slot_op_t<Op>, void*, void const*>;
}
template <class Op>
consteval bool cache_move_assign_live_v() {
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
  else if constexpr (cache_op_trivial_v<Op> ||
                     cache_op_trivial_relocate_v<Op>)
    return true;
  else
    return std::invocable<cache_slot_op_t<Op>, void*, void*>;
}

// Erase typed ops without a Trait: each usable slot becomes op_modified
// (or stays trivial). Used for cache_ptr -> erased cache_ptr conversion.
template <class Ops>
using pointer_ops_erase_typed = pointer_ops<
    pointer_ops_erase_destroy<ops_destroy_t<Ops>, false>,
    pointer_ops_erase_copy<ops_copy_t<Ops>, false>,
    pointer_ops_erase_move<ops_move_t<Ops>, false>, op_unset, op_unset,
    pointer_ops_erase_bool<ops_bool_t<Ops>>>;

template <class Op>
consteval bool cache_bool_live() {
  if constexpr (cache_op_unset_v<Op> || cache_op_unsupported_v<Op>)
    return false;
  else
    return true;
}

template <std::size_t Size, std::size_t Align = alignof(std::max_align_t)>
class cache_buf {
  static_assert(Size > 0, "cache_buf Size must be > 0");
  static_assert(Align > 0 && (Align & (Align - 1)) == 0,
                "cache_buf Align must be a power of two");

  template <std::size_t, std::size_t>
  friend class cache_buf;

  // Raw traveling buffer with no occupancy state: the ptr level does not
  // support an empty check. A dyn knows emptiness from its vt
  // (vt_ == nullptr); standalone users track liveness themselves.
  alignas(Align) std::byte buf_[Size]{};

 protected:
  // Copy the source buffer's bytes. Cross-geometry adoption only ever
  // grows (the destination is at least as large and as aligned), so
  // copying the source's S1 bytes always stays in bounds.
  template <std::size_t S1, std::size_t A1>
  void adopt_bytes(cache_buf<S1, A1> const& o) {
    static_assert(S1 <= Size && A1 <= Align,
                  "cache_buf adoption may only grow size and align");
    std::memcpy(buf_, o.buf_, S1);
  }

  // Trivial relocate: memcpy bytes. Dyn clears the source vt so destroy
  // does not run on the vacated buffer.
  template <std::size_t S1, std::size_t A1>
  void adopt_relocate_bytes(cache_buf<S1, A1>& o) {
    static_assert(S1 <= Size && A1 <= Align,
                  "cache_buf adoption may only grow size and align");
    std::memcpy(buf_, o.buf_, S1);
  }

 public:
  static constexpr std::size_t cache_capacity = Size;
  static constexpr std::size_t cache_alignment = Align;

  cache_buf() noexcept = default;
  cache_buf(std::nullptr_t) noexcept {}
  cache_buf(cache_buf const&) = default;
  cache_buf& operator=(cache_buf const&) = default;
  cache_buf(cache_buf&&) = default;
  cache_buf& operator=(cache_buf&&) = default;

  void* get() const noexcept { return const_cast<std::byte*>(buf_); }

  void reset(void* p = nullptr) noexcept { (void)p; }

  // Engage own buf for placement; nbytes/align must fit Size/Align.
  // Runtime-checked entry point for objects whose size/align is only
  // known at runtime (erased construction of an external object).
  void* emplace(std::size_t nbytes, std::size_t align) {
    assert(nbytes <= Size && "cache_buf capacity too small for Storage");
    assert(align <= Align && "cache_buf alignment too small for Storage");
    (void)nbytes;
    (void)align;
    return static_cast<void*>(buf_);
  }

  // Engage buf without a fit check: fit is guaranteed at compile time
  // (same-geometry payload transfer, grow-only cache -> cache adoption).
  void* occupy() noexcept { return static_cast<void*>(buf_); }
};

// Middle layer: copy, move, and destroy follow Ops.
template <std::size_t Size, std::size_t Align, class Ops>
class cache_hold : public cache_buf<Size, Align> {
  template <std::size_t, std::size_t, class>
  friend class cache_hold;

  static_assert(is_pointer_ops_v<Ops>,
                "Ops must expose destroy_type / copy_type / move_type");
  using buf = cache_buf<Size, Align>;
  using Copy = ops_copy_t<Ops>;
  using Move = ops_move_t<Ops>;
  using Destroy = ops_destroy_t<Ops>;
  using CopyAssign = ops_copy_assign_t<Ops>;
  using MoveAssign = ops_move_assign_t<Ops>;

  // --- Payload primitives, handle-threaded ---------------------------
  // Typed ops are invoked through the unified dispatchers: wherever a
  // participating public handle is fully formed it is offered to the op
  // (any dyn handle flavor, deduced); otherwise the erased void* form
  // runs. Construct receives the source handle only (its destination is
  // raw storage); assign receives both.

  // Assign tier: assign into the live destination (no construct/destroy).
  template <std::size_t S1, std::size_t A1, class DstH, class SrcH>
  void copy_assign_to(cache_hold<S1, A1, Ops> const& o, DstH& dh,
                      SrcH const& sh) {
    if constexpr (cache_op_trivial_v<CopyAssign>) {
      this->adopt_bytes(static_cast<cache_buf<S1, A1> const&>(o));
    } else {
      detail::dyn_op_invoke_copy_assign<cache_slot_op_t<CopyAssign>>(
          dh, sh, this->get(), o.get());
    }
  }
  template <std::size_t S1, std::size_t A1, class DstH, class SrcH>
  void move_assign_to(cache_hold<S1, A1, Ops>& o, DstH& dh, SrcH& sh) {
    if constexpr (cache_op_trivial_v<MoveAssign>) {
      this->adopt_bytes(static_cast<cache_buf<S1, A1>&&>(o));
    } else if constexpr (cache_op_trivial_relocate_v<MoveAssign>) {
      this->adopt_relocate_bytes(static_cast<cache_buf<S1, A1>&>(o));
    } else {
      detail::dyn_op_invoke_move_assign<cache_slot_op_t<MoveAssign>>(
          dh, sh, this->get(), o.get());
    }
  }

  // Construct a payload copy/move into empty storage. Callers guarantee a
  // live source (a dyn gates on its vt; the ptr level has no empty check).
  template <std::size_t S1, std::size_t A1, class SrcH>
  void copy_payload(cache_hold<S1, A1, Ops> const& o, SrcH const& sh) {
    if constexpr (cache_op_trivial_v<Copy>) {
      this->adopt_bytes(static_cast<cache_buf<S1, A1> const&>(o));
    } else {
      void* dst = this->occupy();
      detail::dyn_op_invoke_copy<cache_slot_op_t<Copy>>(dst, sh, o.get());
    }
  }
  template <std::size_t S1, std::size_t A1, class SrcH>
  void move_payload(cache_hold<S1, A1, Ops>& o, SrcH& sh) {
    if constexpr (cache_op_trivial_v<Move>) {
      this->adopt_bytes(static_cast<cache_buf<S1, A1>&&>(o));
    } else if constexpr (cache_op_trivial_relocate_v<Move>) {
      this->adopt_relocate_bytes(static_cast<cache_buf<S1, A1>&>(o));
    } else {
      void* dst = this->occupy();
      detail::dyn_op_invoke_move<cache_slot_op_t<Move>>(dst, sh, o.get());
    }
  }

 protected:
  // Destroy: the op is offered the live handle where one exists (reset,
  // assign-replace). Handle-less paths (the final dtor) pass
  // detail::op_no_handle and the op's void* form runs. Callers guarantee a
  // live payload (a dyn gates on its vt).
  template <class H>
  void destroy_payload(H& h) {
    if constexpr (cache_op_from_trait_v<Destroy>) {
      // vt[-1] destroy already ran (or will run) through the dyn handle.
    } else if constexpr (cache_op_fixed_v<Destroy>) {
      detail::dyn_op_invoke_destroy<cache_slot_op_t<Destroy>>(h,
                                                             this->get());
    }
  }
  void destroy_payload() {
    detail::op_no_handle h;
    destroy_payload(h);
  }

 public:
  cache_hold() noexcept = default;
  cache_hold(std::nullptr_t) noexcept : buf(nullptr) {}

  // No copy/move special members: payload transfer goes through the named
  // methods below so the participating handles can be threaded into the
  // typed ops, which special members cannot do.
  cache_hold(cache_hold const&) = delete;
  cache_hold& operator=(cache_hold const&) = delete;
  cache_hold(cache_hold&&) = delete;
  cache_hold& operator=(cache_hold&&) = delete;

  // Whole-payload transfers with the participating handles threaded in.
  template <std::size_t S1, std::size_t A1, class SrcH>
  void copy_from(cache_hold<S1, A1, Ops> const& o, SrcH const& sh) {
    copy_payload(o, sh);
  }
  template <std::size_t S1, std::size_t A1, class SrcH>
  void move_from(cache_hold<S1, A1, Ops>& o, SrcH& sh) {
    move_payload(o, sh);
  }
  template <std::size_t S1, std::size_t A1, class DstH, class SrcH>
  void assign_copy_from(cache_hold<S1, A1, Ops> const& o, DstH& dh,
                        SrcH const& sh) {
    // Assign tier: caller guarantees both payloads live.
    if constexpr (cache_copy_assign_live_v<CopyAssign>()) {
      copy_assign_to(o, dh, sh);
    } else {
      destroy_payload(dh);
      copy_payload(o, sh);
    }
  }
  template <std::size_t S1, std::size_t A1, class DstH, class SrcH>
  void assign_move_from(cache_hold<S1, A1, Ops>& o, DstH& dh, SrcH& sh) {
    if constexpr (cache_move_assign_live_v<MoveAssign>()) {
      move_assign_to(o, dh, sh);
    } else {
      destroy_payload(dh);
      move_payload(o, sh);
    }
  }

  ~cache_hold()
    requires(!cache_destroy_live<Destroy>())
  = delete;
  ~cache_hold()
    requires(cache_destroy_live<Destroy>()) {
    destroy_payload();
  }


};

// Erased pointer over a traveling buffer.
template <std::size_t Size, std::size_t Align = alignof(std::max_align_t),
          class Ops = pointer_ops<>>
class cache_ptr : private cache_hold<Size, Align, Ops> {
  using hold = cache_hold<Size, Align, Ops>;

  template <std::size_t, std::size_t, class>
  friend class cache_ptr;
  template <class Derived, template <class> class Trait, bool Const,
            bool SliceCall, bool SoftUptrDestroy, class Ptr>
  friend class detail::dyn_ptr_core;
  template <template <template <class> class, class> class Handle,
            template <class> class Trait, bool Const, class Ptr>
  friend class detail::dyn_handle_base;
  template <template <class> class Trait, class Ptr>
  friend class sub_dyn;
  template <template <class> class Trait, class Ptr>
  friend class c_sub_dyn;
  template <class P>
  friend void detail::ptr_reset(P&);
  template <class P>
  friend void detail::ptr_reset(P&, void*);

  using hold::emplace;
  using hold::occupy;

  hold const& as_hold() const noexcept { return *this; }
  hold& as_hold() noexcept { return *this; }

  // Destroy through the ops: a live handle participates where one exists.
  void destroy_held() { this->destroy_payload(); }
  template <class H>
  void destroy_held(H& h) {
    this->destroy_payload(h);
  }

  // dyn_ptr_core entry points: the dyn handle participates in the ops.
  template <class SrcH>
  void copy_from(cache_ptr const& o, SrcH const& sh) {
    hold::copy_from(o.as_hold(), sh);
  }
  template <class SrcH>
  void move_from(cache_ptr& o, SrcH& sh) {
    hold::move_from(o.as_hold(), sh);
  }
  template <class DstH, class SrcH>
  void assign_copy_from(cache_ptr const& o, DstH& dh, SrcH const& sh) {
    if (this != &o)
      hold::assign_copy_from(o.as_hold(), dh, sh);
  }
  template <class DstH, class SrcH>
  void assign_move_from(cache_ptr& o, DstH& dh, SrcH& sh) {
    if (this != &o)
      hold::assign_move_from(o.as_hold(), dh, sh);
  }

  void reset(void* p = nullptr) noexcept { hold::reset(p); }

 public:
  using element_type = void;
  using cache_ptr_tag = void;
  using hold::cache_capacity;
  using hold::cache_alignment;

  static constexpr std::size_t size = Size;
  static constexpr std::size_t align = Align;

  cache_ptr() noexcept = default;
  cache_ptr(std::nullptr_t) noexcept : hold(nullptr) {}

  // Standalone transfer: this cache_ptr itself is the handle the ops see
  // (there is no dyn handle above it).
  cache_ptr(cache_ptr const& o)
    requires(cache_copy_live<ops_copy_t<Ops>>())
      : hold() {
    hold::copy_from(o.as_hold(), o);
  }
  cache_ptr(cache_ptr const&)
    requires(!cache_copy_live<ops_copy_t<Ops>>())
  = delete;
  cache_ptr& operator=(cache_ptr const& o)
    requires(cache_copy_live<ops_copy_t<Ops>>()) {
    assign_copy_from(o, *this, o);
    return *this;
  }
  cache_ptr& operator=(cache_ptr const&)
    requires(!cache_copy_live<ops_copy_t<Ops>>())
  = delete;
  cache_ptr(cache_ptr&& o)
    requires(cache_move_live<ops_move_t<Ops>>())
      : hold() {
    hold::move_from(o.as_hold(), o);
  }
  cache_ptr(cache_ptr&&)
    requires(!cache_move_live<ops_move_t<Ops>>())
  = delete;
  cache_ptr& operator=(cache_ptr&& o)
    requires(cache_move_live<ops_move_t<Ops>>()) {
    assign_move_from(o, *this, o);
    return *this;
  }
  cache_ptr& operator=(cache_ptr&&)
    requires(!cache_move_live<ops_move_t<Ops>>())
  = delete;
  ~cache_ptr() = default;

  // Grow-only cache -> cache with the same ops: the payload lived in the
  // smaller-or-equal source buffer, so it provably fits; no runtime check.
  // from_trait slots transfer at the dyn level (vt construct) instead.
  template <std::size_t S1, std::size_t A1>
    requires((S1 != Size || A1 != Align) && S1 <= Size && A1 <= Align &&
             cache_copy_live<ops_copy_t<Ops>>() &&
             !cache_op_from_trait_v<ops_copy_t<Ops>>)
  cache_ptr(cache_ptr<S1, A1, Ops> const& o) : hold() {
    hold::copy_from(o.as_hold(), o);
  }
  template <std::size_t S1, std::size_t A1>
    requires((S1 != Size || A1 != Align) && S1 <= Size && A1 <= Align &&
             cache_move_live<ops_move_t<Ops>>() &&
             !cache_op_from_trait_v<ops_move_t<Ops>>)
  cache_ptr(cache_ptr<S1, A1, Ops>&& o) : hold() {
    hold::move_from(o.as_hold(), o);
  }

  // Typed ops -> erased: each supported slot becomes stored (or stays
  // trivial). Grow-only as above: the destination geometry must be at
  // least the source's, so the payload provably fits without emplace.
  template <std::size_t S1, std::size_t A1, class FromOps>
    requires(!std::is_same_v<Ops, FromOps> && S1 <= Size && A1 <= Align &&
             std::is_same_v<Ops, pointer_ops_erase_typed<FromOps>>)
  cache_ptr(cache_ptr<S1, A1, FromOps> const& o) : hold() {
    using Src = ops_copy_t<FromOps>;
    if constexpr (cache_op_trivial_v<Src>) {
      this->adopt_bytes(static_cast<cache_buf<S1, A1> const&>(o.as_hold()));
    } else {
      void* dst = this->occupy();
      detail::dyn_op_invoke_copy<Src>(dst, o, o.get());
    }
  }

  template <std::size_t S1, std::size_t A1, class FromOps>
    requires(!std::is_same_v<Ops, FromOps> && S1 <= Size && A1 <= Align &&
             std::is_same_v<Ops, pointer_ops_erase_typed<FromOps>>)
  cache_ptr(cache_ptr<S1, A1, FromOps>&& o) : hold() {
    using Src = ops_move_t<FromOps>;
    if constexpr (cache_op_trivial_v<Src>) {
      this->adopt_bytes(static_cast<cache_buf<S1, A1>&&>(o.as_hold()));
    } else if constexpr (cache_op_trivial_relocate_v<Src>) {
      this->adopt_relocate_bytes(static_cast<cache_buf<S1, A1>&>(o.as_hold()));
    } else {
      void* dst = this->occupy();
      detail::dyn_op_invoke_move<Src>(dst, o, o.get());
    }
  }

  void* get() const noexcept { return hold::get(); }
};

template <class T, class Ops = pointer_ops<>>
using cache_ptr_for = cache_ptr<sizeof(T), alignof(T), Ops>;

// cache_ptr Specs sized for unique_ptr<T> / shared_ptr<T> with proxy ops.
template <class T>
using unique_cache_ptr = cache_ptr_for<std::unique_ptr<T>, unique_pointer_ops>;
template <class T>
using shared_cache_ptr = cache_ptr_for<std::shared_ptr<T>, shared_pointer_ops>;

// proxy_wrap<Ptr>: holds a dereferenceable Ptr for to_dyn into cache_dyn.
// Cache geometry/ops come from cache_ptr Spec:
//   to_dyn<Trait, cache_ptr<Size,Align,Ops>>(proxy_wrap{p})
//   to_dyn<Trait>(proxy_wrap{p})  // Spec = cache_ptr<sizeof(Ptr), alignof(Ptr)>
// Not a dyn storage Ptr -- result is still cache_ptr holding the Ptr value;
// VT is keyed on Ptr (methods dereference to the pointee).
template <class Ptr>
class proxy_wrap {
  static_assert(
      requires(Ptr& p) {
        typename std::pointer_traits<Ptr>::element_type;
        { *p } -> std::convertible_to<
            typename std::pointer_traits<Ptr>::element_type&>;
      },
      "proxy_wrap requires a dereferenceable Ptr with pointer_traits");

 public:
  using pointer_type = Ptr;
  using element_type = typename std::pointer_traits<Ptr>::element_type;
  using proxy_wrap_tag = void;
  using default_cache_spec =
      cache_ptr<sizeof(Ptr), alignof(Ptr), pointer_ops<>>;

  proxy_wrap() = default;
  proxy_wrap(Ptr p) noexcept(std::is_nothrow_move_constructible_v<Ptr>)
      : ptr_(std::move(p)) {}

  Ptr& pointer() & noexcept { return ptr_; }
  Ptr const& pointer() const& noexcept { return ptr_; }
  Ptr&& pointer() && noexcept { return std::move(ptr_); }

 private:
  Ptr ptr_{};
};

template <class Ptr>
proxy_wrap(Ptr) -> proxy_wrap<Ptr>;

namespace detail {

template <class T>
inline constexpr bool is_proxy_wrap_v = false;
template <class Ptr>
inline constexpr bool is_proxy_wrap_v<proxy_wrap<Ptr>> = true;

template <class P>
using ptr_element_t = typename std::pointer_traits<
    std::remove_cvref_t<P>>::element_type;

// void*-safe stand-in for std::default_delete<void> (::operator delete).
// Not a user-facing deleter: unique_dyn's default Deleter is default_delete<void>,
// and writing that default is the same type as omitting Deleter.
struct new_delete {
  void operator()(void* p) const noexcept { ::operator delete(p); }
};

template <class Deleter>
struct unique_deleter {
  using type = Deleter;
};
template <>
struct unique_deleter<std::default_delete<void>> {
  using type = new_delete;
};
template <class Deleter>
using unique_deleter_t = typename unique_deleter<Deleter>::type;

using pointer_destroy_fn = vt_destroy_fn;

template <class P>
consteval pointer_kind pointer_kind_of() {
  if constexpr (requires { typename pointer_trait<P>::kind; }) {
    using K = typename pointer_trait<P>::kind;
    if constexpr (std::is_same_v<K, pointer_vt_destroy_tag>)
      return pointer_kind::vt_destroy;
    else if constexpr (std::is_same_v<K, pointer_deleter_tag>)
      return pointer_kind::deleter;
    else
      return pointer_kind::observer;
  } else {
    return pointer_kind::observer;
  }
}

template <class P>
inline constexpr pointer_kind pointer_kind_v = pointer_kind_of<P>();

template <class P>
inline constexpr bool pointer_vt_destroy_v =
    pointer_kind_v<P> == pointer_kind::vt_destroy;

template <class P>
inline constexpr bool pointer_has_deleter_v = pointer_kind_v<P> == pointer_kind::deleter;

template <class P>
inline constexpr bool pointer_observer_v = pointer_kind_v<P> == pointer_kind::observer;

template <class P, class Handle>
consteval bool pointer_trait_can_release() {
  return requires(Handle& h) { pointer_trait<P>::release(h); };
}

// Uniform reset for Ptr = void* (observation) and smart-pointer Ptrs.
// (ptr_get lives in ref.hpp and already handles void* / .get().)
// Clear vs rebind are separate: shared_ptr::reset(void*) is ill-formed.
template <class P>
void ptr_reset(P& p) {
  if constexpr (std::is_pointer_v<P>)
    p = nullptr;
  else
    p.reset();
}

template <class P>
void ptr_reset(P& p, void* v) {
  if constexpr (std::is_pointer_v<P>)
    p = v;
  else
    p.reset(v);
}

// True when Alloc is a std::allocator specialization (cv/rebind ignored).
template <class Alloc>
inline constexpr bool is_std_allocator_v = false;
template <class T>
inline constexpr bool is_std_allocator_v<std::allocator<T>> = true;

template <class D>
inline constexpr bool is_alloc_delete_v = false;
template <class Alloc>
inline constexpr bool is_alloc_delete_v<alloc_delete<Alloc>> = true;

template <class D>
inline constexpr bool is_std_default_delete_v = false;
template <class T>
inline constexpr bool is_std_default_delete_v<std::default_delete<T>> = true;

template <class D>
inline constexpr bool is_default_storage_delete_v =
    std::is_same_v<D, new_delete> || is_std_default_delete_v<D>;

// Construct sugar: template-template Ptr (+ optional Alloc/deleter) -> complete
// erased pointer type. Empty unique_ptr Args => new_delete (void*-safe).
template <template <class...> class Ptr, class... PtrArgs>
struct erased_ptr_type {
  using type = Ptr<void, PtrArgs...>;
};
template <>
struct erased_ptr_type<std::unique_ptr> {
  using type = std::unique_ptr<void, new_delete>;
};

template <template <class...> class Ptr, class... PtrArgs>
using erased_ptr_t = typename erased_ptr_type<Ptr, PtrArgs...>::type;

// Library unique_ptr by deleter: alloc_delete or new_delete (default Alloc).
template <class P>
inline constexpr bool is_default_uptr_ptr_v = false;
template <class Alloc>
inline constexpr bool is_default_uptr_ptr_v<
    std::unique_ptr<void, alloc_delete<Alloc>>> = true;
template <>
inline constexpr bool is_default_uptr_ptr_v<
    std::unique_ptr<void, new_delete>> = true;

template <class P>
inline constexpr bool is_default_uptr_v = is_default_uptr_ptr_v<P>;

template <class P>
inline constexpr bool is_new_delete_unique_ptr_v = false;
template <>
inline constexpr bool is_new_delete_unique_ptr_v<
    std::unique_ptr<void, new_delete>> = true;

template <class P>
inline constexpr bool is_new_delete_unique_v = is_new_delete_unique_ptr_v<P>;

template <class P>
inline constexpr bool is_alloc_delete_unique_ptr_v = false;
template <class Alloc>
inline constexpr bool is_alloc_delete_unique_ptr_v<
    std::unique_ptr<void, alloc_delete<Alloc>>> = true;

template <class P>
inline constexpr bool is_sptr_ptr_v = false;
template <>
inline constexpr bool is_sptr_ptr_v<std::shared_ptr<void>> = true;

template <class P>
inline constexpr bool is_sptr_v = is_sptr_ptr_v<P>;

template <class P>
inline constexpr bool is_wptr_ptr_v = false;
template <>
inline constexpr bool is_wptr_ptr_v<std::weak_ptr<void>> = true;

template <class P>
inline constexpr bool is_wptr_v = is_wptr_ptr_v<P>;

// Same-Trait handle rebind: DstP is built from SrcP. Deleter-kind dest from
// vt_destroy uses the handoff below, not DstP(src).
template <class DstP, class SrcP, class From>
concept dyn_ptr_rebind_from =
    !std::same_as<DstP, SrcP> && std::constructible_from<DstP, From> &&
    !(pointer_has_deleter_v<DstP> && pointer_vt_destroy_v<SrcP>);

// construct sugar: no extra Ptr args. Alloc is an optional first function
// argument (detected by *dyn / proxy position), not a template argument.
template <template <class...> class Ptr, class... PtrArgs>
struct unique_ptr_dyn_args {
  using type = std::tuple<PtrArgs...>;
};
template <>
struct unique_ptr_dyn_args<std::unique_ptr> {
  using type = std::tuple<>;
};

// Erased pointer type P -> dyn / sub_dyn (identity: Ptr is already complete).
template <template <class> class Trait, class P>
struct dyn_for_erased_ptr {
  using type = dyn<Trait, P>;
};
template <template <class> class Trait, class P>
using dyn_for_erased_ptr_t = typename dyn_for_erased_ptr<Trait, P>::type;

template <template <class> class Trait, class P>
struct sub_dyn_for_erased_ptr {
  using type = sub_dyn<Trait, P>;
};
template <template <class> class Trait, class P>
using sub_dyn_for_erased_ptr_t = typename sub_dyn_for_erased_ptr<Trait, P>::type;

// Observation Ptr: bare void* (non-owning).
template <class P>
inline constexpr bool is_obs_ptr_v = std::is_same_v<P, void*>;

template <class P>
concept is_cache_ptr_ptr =
    requires {
      typename P::cache_ptr_tag;
      { P::cache_capacity } -> std::convertible_to<std::size_t>;
      { P::cache_alignment } -> std::convertible_to<std::size_t>;
    };

template <class P>
inline constexpr bool is_cache_ptr_v = is_cache_ptr_ptr<P>;

// All three ops trivial: may be adopted, then erased into cache_dyn.
// Erased (op_from_trait) cache_ptr is the cache_dyn form. Any other cache_ptr
// enters dyn only by storing a src in place, or by moving one cache_dyn into
// another.
template <class P>
inline constexpr bool cache_ptr_all_trivial_v = false;
template <std::size_t Size, std::size_t Align, class Ops>
inline constexpr bool
    cache_ptr_all_trivial_v<cache_ptr<Size, Align, Ops>> =
        cache_op_trivial_v<ops_copy_t<Ops>> &&
        cache_op_trivial_v<ops_move_t<Ops>> &&
        cache_op_trivial_v<ops_destroy_t<Ops>>;

// Ops-driven queries. Undeclared / non-Ops-shaped Ptr -> native type interface.
template <class P>
inline constexpr bool cache_ptr_erased_ops_v = [] {
  if constexpr (ptr_declares_ops_v<P>)
    return pointer_ops_is_erased_v<ptr_ops_t<P>>;
  else
    return false;
}();

template <class P>
inline constexpr bool cache_ptr_copy_live_v = [] {
  if constexpr (ptr_declares_ops_v<P>)
    return cache_copy_live<ops_copy_t<ptr_ops_t<P>>>();
  else
    return false;
}();
template <class P>
inline constexpr bool cache_ptr_move_live_v = [] {
  if constexpr (ptr_declares_ops_v<P>)
    return cache_move_live<ops_move_t<ptr_ops_t<P>>>();
  else
    return false;
}();
template <class P>
inline constexpr bool cache_ptr_copy_via_trait_v = [] {
  if constexpr (ptr_declares_ops_v<P>)
    return cache_op_from_trait_v<ops_copy_t<ptr_ops_t<P>>>;
  else
    return false;
}();
template <class P>
inline constexpr bool cache_ptr_move_via_trait_v = [] {
  if constexpr (ptr_declares_ops_v<P>)
    return cache_op_from_trait_v<ops_move_t<ptr_ops_t<P>>>;
  else
    return false;
}();
template <class P>
inline constexpr bool cache_ptr_destroy_via_trait_v = [] {
  if constexpr (ptr_declares_ops_v<P>)
    return cache_op_from_trait_v<ops_destroy_t<ptr_ops_t<P>>>;
  else
    return false;
}();

// Grow-only cache -> cache geometry: the destination must be at least as
// large and as aligned as the source (a shrink is a compile error). The
// payload then provably fits, so the transfer needs no runtime check.
template <class DstP, class SrcP>
consteval bool dyn_cache_grow() {
  if constexpr (is_cache_ptr_v<DstP> && is_cache_ptr_v<SrcP>)
    return DstP::cache_capacity >= SrcP::cache_capacity &&
           DstP::cache_alignment >= SrcP::cache_alignment;
  else
    return false;
}

// Declared ops supply copy/move/destroy without Trait (no op_from_trait).
template <class P>
inline constexpr bool cache_ptr_self_lifetime_v = [] {
  if constexpr (!ptr_declares_ops_v<P>)
    return false;
  else
    return cache_copy_live<ops_copy_t<ptr_ops_t<P>>>() &&
           cache_move_live<ops_move_t<ptr_ops_t<P>>>() &&
           cache_destroy_live<ops_destroy_t<ptr_ops_t<P>>>() &&
           !cache_op_from_trait_v<ops_copy_t<ptr_ops_t<P>>> &&
           !cache_op_from_trait_v<ops_move_t<ptr_ops_t<P>>> &&
           !cache_op_from_trait_v<ops_destroy_t<ptr_ops_t<P>>>;
}();

// Owning / non-observer Ptr kinds (not bare T* observation bind).
template <class P>
inline constexpr bool is_heap_ptr_v =
    is_default_uptr_v<P> || is_sptr_v<P> || is_wptr_v<P> || is_cache_ptr_v<P>;

// vt_destroy comes from pointer_trait, including cache_ptr whose destroy op
// is trivial, fixed, or op_from_trait.
template <class P>
inline constexpr bool is_local_owning_ptr_v = pointer_vt_destroy_v<P>;

template <class P>
inline constexpr bool vt_destroys_on_dispose_v = is_local_owning_ptr_v<P>;

}  // namespace detail

// Standard pointers themselves, not only the erased void forms.
// unique_ptr: default_delete / new_delete / alloc_delete are vt_destroy.
// unique_ptr with dyn_adopt_delete (incl. the dyn_default_delete alias) or
// dyn_typed_delete is deleter-kind. Any other deleter stays observer until
// the user specializes that unique_ptr.
template <class T, class U>
struct pointer_trait<std::unique_ptr<T, std::default_delete<U>>> {
  using kind = pointer_vt_destroy_tag;
};
template <class T>
struct pointer_trait<std::unique_ptr<T, detail::new_delete>> {
  using kind = pointer_vt_destroy_tag;
};
template <class T, class Alloc>
struct pointer_trait<std::unique_ptr<T, alloc_delete<Alloc>>> {
  using kind = pointer_vt_destroy_tag;
};
template <class T, class Free>
struct pointer_trait<std::unique_ptr<T, dyn_adopt_delete<Free>>> {
  using kind = pointer_deleter_tag;
};
// Adopted custom deleters are deleter-kind: the wrapper runs D (which carries
// the teardown), not vt[-1].
template <class T, class T2, class D>
struct pointer_trait<std::unique_ptr<T, detail::dyn_typed_delete<T2, D>>> {
  using kind = pointer_deleter_tag;
};
template <class T>
struct pointer_trait<std::shared_ptr<T>> {
  using kind = pointer_deleter_tag;
};
// cache_ptr's kind follows pointer_ops's destroy:
//   op_unset    no kind (observer)
//   otherwise   vt_destroy
template <class Destroy>
struct cache_pointer_kind_alias {
  using type = pointer_vt_destroy_tag;
};
template <>
struct cache_pointer_kind_alias<op_unset> {
  using type = void;
};

template <std::size_t Size, std::size_t Align, class Ops>
struct pointer_trait<cache_ptr<Size, Align, Ops>> {
  using kind =
      typename cache_pointer_kind_alias<ops_destroy_t<Ops>>::type;
  using ops = detail::ops_with_cache_bool_t<Ops>;
};

namespace detail {

// Deleter-kind Ptr: destroy lives in the pointer, not the handle.
template <class P>
inline constexpr bool pointer_stores_vt_destroy_v = pointer_has_deleter_v<P>;

// Downcast checks the Trait chain only when pointer_ops does not itself
// supply copy, move, and destroy. Other Ptrs are unconstrained.
template <class P>
inline constexpr bool ptr_downcast_chain_constrained_v =
    is_cache_ptr_v<P> && !cache_ptr_self_lifetime_v<P>;

// View As and source agree (cv ignored). Mismatch may still use dyn:
// destroy is always vt[-1] (~Src) and does not follow Trait [[=mirror]].
template <typename T, typename Src>
inline constexpr bool mirror_view_is_src_v =
    std::same_as<std::remove_cv_t<T>, std::remove_cv_t<Src>>;

// Storage is the sourced mirror when the bind must keep that object alive:
// identity_mirror Self, or view T != Src (field subset / alias).
// When view matches Src, observe the object instead and rebuild the mirror
// per call -- safe if the caller only had a temporary mirror (to_dyn_m).
template <template <class> class Trait, typename As, typename MT,
          typename Src>
consteval bool sourced_mirror_binds_storage() {
  return impl_self_is_identity_mirror<Trait, std::remove_cv_t<As>>() ||
         !mirror_view_is_src_v<MT, Src>;
}

// Bind sourced mirror. Mirror sections use As=T, non-mirror sections As=Src
// (make_flat_slot). op_construct uses Src; destroy is always vt[-1] (~Src).
template <bool Mask, template <class> class Trait, typename As, typename MT,
          typename Src>
void* dyn_bind_mirror_ptr(mirror<MT, Src> const& v) {
  if constexpr (sourced_mirror_binds_storage<Trait, As, MT, Src>())
    return const_cast<void*>(static_cast<const void*>(std::addressof(v)));
  else {
    auto&& s = src_of(v);
    return const_cast<void*>(static_cast<const void*>(std::addressof(s)));
  }
}

template <bool Mask, template <class> class Trait, typename As, typename MT,
          typename Src>
vt_ptr dyn_bind_mirror_slots() {
  if constexpr (sourced_mirror_binds_storage<Trait, As, MT, Src>())
    return dyn_object_slots<Mask, Trait, mirror<MT, Src>, As>();
  else
    return dyn_object_slots<Mask, Trait, Src, As>();
}

// Alloc from complete Ptr: allocator_type / Deleter::allocator_type / default.
template <class P>
concept dyn_ptr_has_allocator = requires { typename P::allocator_type; };

template <class P>
concept dyn_ptr_has_deleter_allocator =
    requires { typename P::deleter_type::allocator_type; };

template <class P>
struct dyn_ptr_allocator {
  using type = std::allocator<std::byte>;
  static constexpr bool from_ptr = false;
};
template <class P>
  requires dyn_ptr_has_allocator<P>
struct dyn_ptr_allocator<P> {
  using type = typename P::allocator_type;
  static constexpr bool from_ptr = true;
};
template <class P>
  requires(!dyn_ptr_has_allocator<P> && dyn_ptr_has_deleter_allocator<P>)
struct dyn_ptr_allocator<P> {
  using type = typename P::deleter_type::allocator_type;
  static constexpr bool from_ptr = true;
};
template <class P>
using dyn_ptr_allocator_t = typename dyn_ptr_allocator<P>::type;

// Alloc from Out's complete Ptr type.
template <class Out>
struct dyn_handle_allocator;

template <template <template <class> class, class> class Handle,
          template <class> class Trait, class Ptr>
struct dyn_handle_allocator<Handle<Trait, Ptr>> {
  using type = dyn_ptr_allocator_t<Ptr>;
};

template <class Out>
using dyn_handle_allocator_t = typename dyn_handle_allocator<Out>::type;

template <class Alloc>
void* dyn_allocate_bytes(Alloc alloc, std::size_t nbytes) {
  using ByteAlloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
  ByteAlloc ba(std::move(alloc));
  return std::allocator_traits<ByteAlloc>::allocate(ba, nbytes);
}

// Pointer-like args for to_dyn / to_c_dyn: T* / unique_ptr / shared_ptr / weak_ptr.
// Ptr family + vt(As) deduced; source must become that Ptr (no bare object bind).
template <class P>
struct dyn_ptr_bind;

template <class T>
  requires(!std::is_void_v<std::remove_cv_t<T>>)
struct dyn_ptr_bind<T*> {
  using element_cv = T;
  using element = std::remove_cv_t<T>;
  static constexpr bool is_raw = true;
  static constexpr bool is_sptr = false;
  static constexpr bool is_wptr = false;
  static constexpr bool is_uptr = false;

  static void* adopt(T* p) noexcept {
    return const_cast<void*>(static_cast<const void*>(p));
  }
};

// adopt dispatch: kinds that arm the deleter from vt[-1] take (p, vt);
// everyone else takes p only.
template <class Bind, class P>
decltype(auto) adopt_ptr(P&& p, vt_ptr vt) {
  if constexpr (requires { Bind::adopt(std::forward<P>(p), vt); })
    return Bind::adopt(std::forward<P>(p), vt);
  else
    return Bind::adopt(std::forward<P>(p));
}

template <class T>
  requires(!std::is_void_v<std::remove_cv_t<T>>)
struct dyn_ptr_bind<std::unique_ptr<T>> {
  using element_cv = T;
  using element = std::remove_cv_t<T>;
  static constexpr bool is_raw = false;
  static constexpr bool is_sptr = false;
  static constexpr bool is_wptr = false;
  static constexpr bool is_uptr = true;

  // Default Alloc: do not use alloc_delete; void*-safe new_delete stand-in.
  static std::unique_ptr<void, new_delete> adopt(std::unique_ptr<T> p) noexcept {
    return std::unique_ptr<void, new_delete>{p.release()};
  }
};

template <class T, class U>
  requires(!std::is_void_v<std::remove_cv_t<T>> &&
           is_std_default_delete_v<std::default_delete<U>>)
struct dyn_ptr_bind<std::unique_ptr<T, std::default_delete<U>>>
    : dyn_ptr_bind<std::unique_ptr<T>> {};

template <class T, class Alloc>
  requires(!std::is_void_v<std::remove_cv_t<T>>)
struct dyn_ptr_bind<std::unique_ptr<T, alloc_delete<Alloc>>> {
  using element_cv = T;
  using element = std::remove_cv_t<T>;
  static constexpr bool is_raw = false;
  static constexpr bool is_sptr = false;
  static constexpr bool is_wptr = false;
  static constexpr bool is_uptr = true;

  using void_alloc = typename std::allocator_traits<
      Alloc>::template rebind_alloc<void>;

  static std::unique_ptr<void, alloc_delete<void_alloc>> adopt(
      std::unique_ptr<T, alloc_delete<Alloc>> p) noexcept {
    alloc_delete<Alloc> del = p.get_deleter();
    const std::size_t n =
        del.size != 0 ? del.size : sizeof(std::remove_cv_t<T>);
    return std::unique_ptr<void, alloc_delete<void_alloc>>{
        p.release(),
        alloc_delete<void_alloc>{void_alloc{del.alloc}, n}};
  }
};

// EBO: Free is a base, so an empty Free costs nothing; no layout split.
template <class Free>
struct dyn_adopt_delete_impl : private Free {
  static_assert(!std::is_same_v<std::remove_cvref_t<Free>, dyn_shared_storage>);
  static_assert(!std::is_final_v<Free>,
                "dyn_adopt_delete Free must not be final (inherited for EBO)");

  void (*destroy)(void*) = nullptr;

  dyn_adopt_delete_impl() noexcept = default;
  dyn_adopt_delete_impl(void (*fn)(void*), Free f = {}) noexcept
      : Free(std::move(f)), destroy(fn) {}

  // Null p / null destroy: caller's job. Empty unique_ptr never calls this;
  // a live Ptr with no destroy armed is a bug (crash).
  void operator()(void* p) const noexcept {
    destroy(p);
    this->Free::operator()(p);
  }
};

}  // namespace detail

// Custom src deleter passed through as dest d. Free defaults to
// detail::new_delete (the dyn_default_delete alias). Empty Free is a base
// (EBO), so the default form is a single pointer.
template <class Free>
struct dyn_adopt_delete : private detail::dyn_adopt_delete_impl<Free> {
 private:
  using impl = detail::dyn_adopt_delete_impl<Free>;

 public:
  using impl::impl;
  using impl::operator();
  using impl::destroy;
};

namespace detail {

// Any other unique_ptr<T, D> (custom deleter): adopted DELETER-KIND, not by
// name but by principle (pointer_trait below). A T-typed wrapper calls
// D(static_cast<T*>(p)). No vt destroy is attached: D was written for T and
// is expected to carry the full teardown (~T + free) itself, so wiring vt[-1]
// in would double-destroy.
template <class T, class D>
struct dyn_typed_delete {
  [[no_unique_address]] D del{};

  void operator()(void* p) const { del(static_cast<T*>(p)); }
};

template <class T, class D>
  requires(!std::is_void_v<std::remove_cv_t<T>> &&
           !is_default_storage_delete_v<D> && !is_alloc_delete_v<D> &&
           !std::is_same_v<D, dyn_default_delete> &&
           std::is_move_constructible_v<D> && std::invocable<D const&, T*>)
struct dyn_ptr_bind<std::unique_ptr<T, D>> {
  using element_cv = T;
  using element = std::remove_cv_t<T>;
  static constexpr bool is_raw = false;
  static constexpr bool is_sptr = false;
  static constexpr bool is_wptr = false;
  static constexpr bool is_uptr = true;

  static std::unique_ptr<void, dyn_typed_delete<T, D>>
  adopt(std::unique_ptr<T, D> p) noexcept {
    return std::unique_ptr<void, dyn_typed_delete<T, D>>{
        p.release(), {std::move(p.get_deleter())}};
  }
};

// unique_ptr with any dyn_adopt_delete<Free>: adopted deleter-kind
// directly. The deleter's destroy slot is (re)wired to vt[-1] (~T) so the
// full teardown always runs before Free(p); an unwired source deleter
// would otherwise leak ~T. The user's Free instance moves along.
template <class T, class Free>
  requires(!std::is_void_v<std::remove_cv_t<T>>)
struct dyn_ptr_bind<std::unique_ptr<T, dyn_adopt_delete<Free>>> {
  using element_cv = T;
  using element = std::remove_cv_t<T>;
  static constexpr bool is_raw = false;
  static constexpr bool is_sptr = false;
  static constexpr bool is_wptr = false;
  static constexpr bool is_uptr = true;

  static std::unique_ptr<void, dyn_adopt_delete<Free>> adopt(
      std::unique_ptr<T, dyn_adopt_delete<Free>> p, vt_ptr vt) noexcept {
    auto del = std::move(p.get_deleter());
    del.destroy = dyn_vt_destroy(vt);
    return std::unique_ptr<void, dyn_adopt_delete<Free>>{p.release(),
                                                       std::move(del)};
  }
};

template <class T>
  requires(!std::is_void_v<std::remove_cv_t<T>>)
struct dyn_ptr_bind<std::shared_ptr<T>> {
  using element_cv = T;
  using element = std::remove_cv_t<T>;
  static constexpr bool is_raw = false;
  static constexpr bool is_sptr = true;
  static constexpr bool is_wptr = false;
  static constexpr bool is_uptr = false;

  static std::shared_ptr<void> adopt(std::shared_ptr<T> p) noexcept {
    return std::shared_ptr<void>{std::move(p)};
  }
};

template <class T>
  requires(!std::is_void_v<std::remove_cv_t<T>>)
struct dyn_ptr_bind<std::weak_ptr<T>> {
  using element_cv = T;
  using element = std::remove_cv_t<T>;
  static constexpr bool is_raw = false;
  static constexpr bool is_sptr = false;
  static constexpr bool is_wptr = true;
  static constexpr bool is_uptr = false;

  static std::weak_ptr<void> adopt(std::weak_ptr<T> p) noexcept {
    return std::weak_ptr<void>{std::move(p)};
  }
};

template <class P>
concept dyn_bindable_ptr = requires { typename dyn_ptr_bind<P>::element; };

template <class P>
using dyn_ptr_element_t = typename dyn_ptr_bind<P>::element;

template <class P>
using dyn_ptr_element_cv_t = typename dyn_ptr_bind<P>::element_cv;

// Managed T for library sptr. Layout (aligned payload after header):
//   [destroy|align | Storage...]
// destroy low bits = log2(align) from vt size slot. allocate_shared owns
// the block; ~dyn_shared_storage runs object destroy.
struct dyn_shared_storage {
  vt_destroy_fn destroy = nullptr;

  static constexpr std::size_t payload_offset_for(std::size_t align) noexcept {
    return (sizeof(dyn_shared_storage) + align - 1) / align * align;
  }

  std::size_t payload_align() const noexcept {
    return std::size_t{1} << align_log_from_destroy(destroy);
  }

  std::size_t payload_offset() const noexcept {
    return payload_offset_for(payload_align());
  }

  void* payload() noexcept {
    return reinterpret_cast<std::byte*>(this) + payload_offset();
  }

  static dyn_shared_storage* from_payload(void* payload,
                                          std::size_t align) noexcept {
    return reinterpret_cast<dyn_shared_storage*>(
        static_cast<std::byte*>(payload) - payload_offset_for(align));
  }

  // Always call. Pre-placement arming uses vt_destroy_nop (not null); a
  // null untagged destroy is a bug (crash).
  ~dyn_shared_storage() { untag_destroy_fn(destroy)(payload()); }
};

// allocate_shared proxy: n is always 1 (one object). extra packs Storage
// size (low) | log2(align) (high), same as the vt size slot, and is the
// tail after that object. Real alloc is rebound to byte; its n is that
// total byte count. deallocate uses the same count.
template <class T, class Alloc>
struct dyn_sptr_proxy_alloc {
  using value_type = T;

  Alloc alloc{};
  std::size_t extra = 0;

  dyn_sptr_proxy_alloc() = default;
  dyn_sptr_proxy_alloc(Alloc a, std::size_t packed) noexcept
      : alloc(std::move(a)), extra(packed) {}

  template <class U>
  dyn_sptr_proxy_alloc(dyn_sptr_proxy_alloc<U, Alloc> const& o) noexcept
      : alloc(o.alloc), extra(o.extra) {}

  std::size_t extra_size() const noexcept {
    return extra & dyn_size_value_mask;
  }
  std::size_t extra_align() const noexcept {
    return std::size_t{1} << (extra >> dyn_size_value_bits);
  }

  std::size_t extra_bytes(std::size_t n) const noexcept {
    const std::size_t a = extra_align();
    return (n * sizeof(T) + a - 1) / a * a + extra_size();
  }

  T* allocate(std::size_t n) {
    using ByteAlloc =
        typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
    ByteAlloc ba(alloc);
    return reinterpret_cast<T*>(
        std::allocator_traits<ByteAlloc>::allocate(ba, extra_bytes(n)));
  }

  void deallocate(T* p, std::size_t n) noexcept {
    using ByteAlloc =
        typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
    ByteAlloc ba(alloc);
    std::allocator_traits<ByteAlloc>::deallocate(
        ba, reinterpret_cast<std::byte*>(p), extra_bytes(n));
  }

  template <class U>
  friend bool operator==(dyn_sptr_proxy_alloc const& a,
                         dyn_sptr_proxy_alloc<U, Alloc> const& b) noexcept {
    if (a.extra != b.extra)
      return false;
    if constexpr (std::allocator_traits<Alloc>::is_always_equal::value)
      return true;
    else
      return a.alloc == b.alloc;
  }
};

// Deep construct: allocate_shared so control + header + Storage are one
// block. ~dyn_shared_storage runs object destroy. Passing a deleter on a
// shallow handoff is a different path.
template <template <class> class Trait, class Alloc = std::allocator<std::byte>>
std::shared_ptr<void> make_dyn_sptr(std::size_t size, vt_ptr vt,
                                    Alloc alloc = {})
  requires(trait_has_dyn_construct<Trait>()) {
  const std::size_t alog = dyn_storage_align_log<Trait>(vt);
  dyn_sptr_proxy_alloc<dyn_shared_storage, Alloc> pa(
      std::move(alloc), size | (alog << dyn_size_value_bits));
  // Align tag in destroy low bits. Arm vt_destroy_nop first so a throwing
  // construct does not run vt[-1] on uninitialized payload (~ always calls).
  auto p = std::allocate_shared<dyn_shared_storage>(
      pa, tag_destroy_align(vt_destroy_nop, alog));
  void* payload = p->payload();
  return std::shared_ptr<void>(std::move(p), payload);
}

// Deleter-kind dest from a vt_destroy source: release(), then P(raw, d)
// with d destroying and freeing. If dest has deleter_type constructible from
// the destroy fn, that type is d (it already frees). Else default src deleter
// is not forwarded: d is dyn_default_delete (::operator delete). Any other
// src deleter type is passed through.
template <class P>
concept pointer_has_storage_deleter = requires(P& p) {
  p.release();
  p.get_deleter();
};

template <class P>
  requires(pointer_has_storage_deleter<P>)
using pointer_storage_deleter_t =
    std::remove_cvref_t<decltype(std::declval<P&>().get_deleter())>;

template <class DstP>
consteval bool dyn_ptr_uses_typed_deleter() {
  if constexpr (!requires { typename DstP::deleter_type; })
    return false;
  else
    return std::constructible_from<typename DstP::deleter_type,
                                   pointer_destroy_fn> &&
           std::constructible_from<DstP, void*, typename DstP::deleter_type>;
}

template <class DstP, class SrcP>
consteval bool dyn_ptr_proxy_handoff_ok() {
  if constexpr (!pointer_has_deleter_v<DstP> ||
                !pointer_vt_destroy_v<SrcP> ||
                !pointer_has_storage_deleter<SrcP>)
    return false;
  else if constexpr (dyn_ptr_uses_typed_deleter<DstP>())
    return true;
  else if constexpr (is_default_storage_delete_v<
                         pointer_storage_deleter_t<SrcP>>)
    return std::constructible_from<DstP, void*, dyn_default_delete>;
  else
    return std::constructible_from<
        DstP, void*, dyn_adopt_delete<pointer_storage_deleter_t<SrcP>>>;
}

template <class DstP, class SrcP, class From>
consteval bool dyn_ptr_deleter_handoff() {
  if constexpr (std::same_as<DstP, SrcP> || !pointer_has_deleter_v<DstP>)
    return false;
  else if constexpr (dyn_ptr_proxy_handoff_ok<DstP, SrcP>())
    return !std::is_lvalue_reference_v<From>;
  else
    return false;
}

// Handle conversion: dest Ptr from this value category of SrcP.
// Same Ptr: copy/move (or cache via vt). Deleter-kind dest from
// vt_destroy: handoff. Else DstP(src).
// explicitness of the handle ctor is is_convertible SrcP->DstP.
template <class DstP, class SrcP, class From>
concept dyn_handle_ptr_from =
    (std::same_as<DstP, SrcP> &&
     (std::constructible_from<DstP, From> ||
      (is_cache_ptr_v<DstP> &&
       ((std::is_lvalue_reference_v<From> &&
         cache_ptr_copy_via_trait_v<DstP>) ||
        (std::is_rvalue_reference_v<From> &&
         cache_ptr_move_via_trait_v<DstP>))))) ||
    dyn_ptr_deleter_handoff<DstP, SrcP, From>() ||
    // Grow-only erased cache -> cache (vt construct at the handle level).
    (dyn_cache_grow<DstP, SrcP>() &&
     ((std::is_lvalue_reference_v<From> && cache_ptr_copy_via_trait_v<DstP> &&
       cache_ptr_copy_via_trait_v<SrcP>) ||
      (std::is_rvalue_reference_v<From> &&
       cache_ptr_move_via_trait_v<DstP> &&
       cache_ptr_move_via_trait_v<SrcP>))) ||
    (!std::same_as<DstP, SrcP> && !pointer_observer_v<SrcP> &&
     std::constructible_from<DstP, From> &&
     !dyn_ptr_proxy_handoff_ok<DstP, SrcP>());

template <class DstP>
DstP dyn_ptr_deleter_from_raw(void* raw, vt_ptr vt, auto free) {
  if (raw == nullptr)
    return {};
  vt_destroy_fn fn = nullptr;
  if (vt != nullptr)
    fn = dyn_vt_destroy(vt);
  using Free = std::remove_cvref_t<decltype(free)>;
  if constexpr (dyn_ptr_uses_typed_deleter<DstP>()) {
    (void)free;
    return DstP(raw, typename DstP::deleter_type{fn});
  } else if constexpr (std::is_same_v<Free, dyn_default_delete>) {
    return DstP(raw, dyn_default_delete{fn});
  } else {
    return DstP(raw, dyn_adopt_delete<Free>{fn, std::move(free)});
  }
}

template <class DstP, class SrcP>
DstP dyn_ptr_to_deleter_copy(SrcP const& src, vt_ptr vt) {
  static_assert(std::constructible_from<DstP, SrcP const&>,
                "Ptr cannot be copied onto this deleter dest");
  (void)vt;
  return DstP(src);
}

template <class DstP, class SrcP>
DstP dyn_ptr_to_deleter(SrcP src, vt_ptr vt) {
  if constexpr (dyn_ptr_proxy_handoff_ok<DstP, SrcP>()) {
    using Del = pointer_storage_deleter_t<SrcP>;
    if constexpr (is_default_storage_delete_v<Del>) {
      void* raw = src.release();
      return dyn_ptr_deleter_from_raw<DstP>(raw, vt, dyn_default_delete{});
    } else {
      Del del = std::move(src.get_deleter());
      void* raw = src.release();
      return dyn_ptr_deleter_from_raw<DstP>(raw, vt, std::move(del));
    }
  } else {
    static_assert(std::constructible_from<DstP, SrcP>,
                  "Ptr cannot be moved onto this deleter dest");
    return DstP(std::move(src));
  }
}

template <template <class> class Trait, class SrcP>
consteval bool dyn_ptr_to_sptr_ok() {
  return dyn_handle_ptr_from<std::shared_ptr<void>, SrcP, SrcP&&>;
}
template <template <class> class Trait, class SrcP>
consteval bool dyn_ptr_to_sptr_copy_ok() {
  return dyn_handle_ptr_from<std::shared_ptr<void>, SrcP, SrcP const&>;
}

// Shared Ptr+vt core for dyn / c_dyn / sub_*.
// SliceCall (sub_*): vt is adjusted to the section start at creation
// (sub_slice_offset is consteval); else the full dyn table.
// SoftUptrDestroy: empty default-uptr OK on sub. sub_dyn never
// reads the table-origin prefix (destroy / RTTI): a section
// cannot know if it sits at origin (this Trait may be a sibling).
// vt_destroy Ptr on sub_dyn only if pointer_trait::release accepts *this.
template <class Derived, template <class> class Trait, bool Const,
          bool SliceCall, bool SoftUptrDestroy, class Ptr>
vt_ptr vt_of(dyn_ptr_core<Derived, Trait, Const, SliceCall, SoftUptrDestroy,
                          Ptr> const& h) noexcept;
template <class Handle>
void* storage_addr_of(Handle const& h) noexcept;

// GCC: constrained members are hard errors inside requires { h.fn() }.
// Specialize the facet away so the member does not exist.
template <class Core, bool Enable>
struct dyn_destroy_facet {};
template <class Core>
struct dyn_destroy_facet<Core, true> {
  void destroy() {
    auto& s = static_cast<Core&>(*this);
    // Emptiness follows operator bool (cache: vt; else: ptr).
    if (s)
      dyn_vt_destroy(s.vt_)(ptr_get(s.obj_));
    s.vt_ = nullptr;
    ptr_reset(s.obj_);
  }
};

template <class Core, template <class> class Trait, bool Enable>
struct dyn_typeid_facet {};
#if ASF_HAS_DYN_RTTI
template <class Core, template <class> class Trait>
struct dyn_typeid_facet<Core, Trait, true> {
  const std::type_info& typeid_of() const noexcept {
    auto const& s = static_cast<Core const&>(*this);
    if (!s)
      return typeid(void);
    return meta_of<Trait>(s.vt_)->typeid_of();
  }
};
#endif

template <class Handle, template <class> class Trait, bool Enable>
struct dyn_size_facet {};
template <class Handle, template <class> class Trait>
struct dyn_size_facet<Handle, Trait, true> {
  std::size_t size() const noexcept {
    auto const& s = static_cast<Handle const&>(*this);
    return s ? dyn_storage_size<Trait>(s.vt_) : 0;
  }
  std::size_t align() const noexcept {
    auto const& s = static_cast<Handle const&>(*this);
    return s ? dyn_storage_align<Trait>(s.vt_) : 0;
  }
};

template <class Derived, template <class> class Trait, bool Const,
          bool SliceCall, bool SoftUptrDestroy, class Ptr>
class dyn_ptr_core
    : public dyn_destroy_facet<
          dyn_ptr_core<Derived, Trait, Const, SliceCall, SoftUptrDestroy, Ptr>,
          !Const && !is_wptr_v<Ptr> && !is_sptr_v<Ptr> && !SliceCall>,
      public dyn_typeid_facet<
          dyn_ptr_core<Derived, Trait, Const, SliceCall, SoftUptrDestroy, Ptr>,
          Trait, !SliceCall> {
  template <class D, template <class> class Tr, bool C, bool Sc, bool Soft,
            class P>
  friend vt_ptr vt_of(dyn_ptr_core<D, Tr, C, Sc, Soft, P> const&) noexcept;
  template <class H>
  friend void* storage_addr_of(H const&) noexcept;
  friend dyn_destroy_facet<dyn_ptr_core, true>;
  friend dyn_destroy_facet<dyn_ptr_core, false>;
  friend dyn_typeid_facet<dyn_ptr_core, Trait, true>;
  friend dyn_typeid_facet<dyn_ptr_core, Trait, false>;

 protected:
  using ptr_type = Ptr;

  static consteval bool slice_proxy_ok() {
    return !SliceCall || !vt_destroys_on_dispose_v<Ptr> ||
           pointer_trait_can_release<Ptr, Derived>();
  }

  // vt first (offset 0): matches common proxy layouts and keeps emptiness /
  // dispatch next to the hot loads. Payload follows.
  vt_ptr vt_{};
  // sub_dyn's vt is adjusted to the section start at creation
  // (sub_slice_offset is consteval); sub never reads the negative prefix
  // (destroy goes through pointer_trait::release, no meta), so no runtime
  // slice offset is carried.
  ptr_type obj_{};

  dyn_ptr_core() noexcept = default;
  // Erased, all-trivial, or self-lifetime cache_ptr may be adopted. Any other
  // cache_ptr is filled in place from a src, or relocated from another
  // cache_dyn. Args are (vt, obj) matching member order: publish vt last
  // only at call sites that fill payload first then set vt_.
  dyn_ptr_core(vt_ptr vt, ptr_type obj) noexcept : vt_(vt),
                                                   obj_(std::move(obj)) {
    static_assert(!is_cache_ptr_v<Ptr> || cache_ptr_all_trivial_v<Ptr> ||
                      cache_ptr_erased_ops_v<Ptr> ||
                      cache_ptr_self_lifetime_v<Ptr>,
                  "cache_ptr enters dyn only as erased / all-trivial / "
                  "self-lifetime ops, by storing a src, or from another "
                  "cache_dyn");
    (void)SoftUptrDestroy;
  }
  dyn_ptr_core(vt_ptr vt, void* ptr) noexcept
    requires(!is_sptr_v<Ptr> && !is_wptr_v<Ptr> && !is_cache_ptr_v<Ptr> &&
             !pointer_has_deleter_v<Ptr>)
      : vt_(vt), obj_(ptr) {}

  // Move-handoff: unpublish source first, then take payload, then publish.
  // Avoids swap and a window where source still looks live after its bits
  // have been relocated.
  void take_shallow_moved_from(dyn_ptr_core& o) noexcept {
    vt_ptr v = std::exchange(o.vt_, nullptr);
    obj_ = std::move(o.obj_);
    vt_ = v;
  }

  pointer_destroy_fn destroy_fn() const noexcept {
    if (*this)
      return dyn_vt_destroy(vt_);
    return nullptr;
  }

  void dispose_owned() {
    if constexpr (vt_destroys_on_dispose_v<Ptr>) {
      if constexpr (pointer_trait_can_release<Ptr, Derived>()) {
        pointer_trait<Ptr>::release(static_cast<Derived&>(*this));
      } else {
        static_assert(!SliceCall,
                      "sub_dyn vt_destroy Ptr needs "
                      "pointer_trait::release that accepts this handle");
        if (*this)
          dyn_vt_destroy(vt_)(ptr_get(obj_));
        ptr_reset(obj_);
      }
      vt_ = nullptr;
    } else {
      vt_ = nullptr;
      ptr_reset(obj_);
    }
  }

  // Cache shallow paths (assign / prepare_assign / dtor via dispose) may
  // trigger this deep teardown; library soft-skips empty (*this).
  void end_cache_payload()
    requires(is_cache_ptr_v<Ptr>) {
    if (!*this)
      return;
    if constexpr (pointer_trait_can_release<Ptr, Derived>()) {
      pointer_trait<Ptr>::release(static_cast<Derived&>(*this));
    } else if constexpr (cache_ptr_destroy_via_trait_v<Ptr>) {
      static_assert(!SliceCall,
                    "sub_dyn vt_destroy Ptr needs "
                    "pointer_trait::release that accepts this handle");
      dyn_vt_destroy(vt_)(ptr_get(obj_));
    } else {
      obj_.destroy_held(static_cast<Derived&>(*this));
    }
    vt_ = nullptr;
  }

  // Same-geometry payload transfer: fit is guaranteed at compile time, so
  // no runtime check (occupy, not emplace). Empty source is a shallow no-op.
  void cache_take_copied_from(dyn_ptr_core const& o)
    requires(is_cache_ptr_v<Ptr> && cache_ptr_copy_via_trait_v<Ptr>) {
    if (!o)
      return;
    if constexpr (SliceCall) {
      // sub: no table head. Construct via the section's own op_construct.
      void* dst = obj_.occupy();
      detail::dyn_construct_of_own_slice<Trait, false>(o.vt_)(
          ptr_get(o.obj_), dst);
      vt_ = o.vt_;
    } else {
      void* dst = obj_.occupy();
      dyn_construct_of<Trait, false>(o.vt_)(ptr_get(o.obj_), dst);
      vt_ = o.vt_;
    }
  }

  void cache_take_moved_from(dyn_ptr_core& o)
    requires(is_cache_ptr_v<Ptr>) {
    if (!o)
      return;
    if constexpr (cache_ptr_move_via_trait_v<Ptr>) {
      if constexpr (SliceCall) {
        void* dst = obj_.occupy();
        detail::dyn_construct_of_own_slice<Trait, true>(o.vt_)(
            ptr_get(o.obj_), dst);
        vt_ = o.vt_;
      } else {
        void* dst = obj_.occupy();
        dyn_construct_of<Trait, true>(o.vt_)(ptr_get(o.obj_), dst);
        vt_ = o.vt_;
      }
      o.dispose_owned();
    } else {
      vt_ptr v = std::exchange(o.vt_, nullptr);
      obj_.move_from(o.obj_, static_cast<Derived&>(o));
      vt_ = v;
    }
  }

  // Naked storage address for vt slots / observer rebind. No emptiness
  // gate (unlike raw()). cache: always &buf; unique_ptr: .get() bits.
  void* storage_addr() const noexcept
    requires(!is_wptr_v<Ptr>) {
    if constexpr (is_cache_ptr_v<Ptr>)
      return obj_.get();
    else
      return ptr_get(obj_);
  }

 public:
  dyn_ptr_core(dyn_ptr_core const&)
    requires(!is_cache_ptr_v<Ptr>)
  = default;
  // cache with its own copy op: thread the source handle into the ops.
  dyn_ptr_core(dyn_ptr_core const& o)
    requires(is_cache_ptr_v<Ptr> && cache_ptr_copy_live_v<Ptr>)
      : dyn_ptr_core() {
    if (o) {
      obj_.copy_from(o.obj_, static_cast<Derived const&>(o));
      vt_ = o.vt_;
    }
  }
  dyn_ptr_core(dyn_ptr_core const& o)
    requires(is_cache_ptr_v<Ptr> && cache_ptr_copy_via_trait_v<Ptr>)
      : dyn_ptr_core() {
    cache_take_copied_from(o);
  }
  dyn_ptr_core(dyn_ptr_core const&)
    requires(is_cache_ptr_v<Ptr> && !cache_ptr_copy_live_v<Ptr> &&
             !cache_ptr_copy_via_trait_v<Ptr>)
  = delete;

  dyn_ptr_core& operator=(dyn_ptr_core const&)
    requires(!is_cache_ptr_v<Ptr>)
  = default;
  dyn_ptr_core& operator=(dyn_ptr_core const& o)
    requires(is_cache_ptr_v<Ptr> && slice_proxy_ok() &&
             (cache_ptr_copy_live_v<Ptr> || cache_ptr_copy_via_trait_v<Ptr>)) {
    if (this != &o) {
      // Assign tier: live destination + a live assign op -> assign in place
      // (skip the destroy + construct round trip entirely).
      if constexpr (cache_copy_assign_live_v<
                    ops_copy_assign_t<ptr_ops_t<Ptr>>>()) {
        if (*this && o) {
          obj_.assign_copy_from(o.obj_, static_cast<Derived&>(*this),
                                static_cast<Derived const&>(o));
          vt_ = o.vt_;
          return *this;
        }
      }
      end_cache_payload();
      if constexpr (cache_ptr_copy_via_trait_v<Ptr>) {
        cache_take_copied_from(o);
      } else if (o) {
        obj_.copy_from(o.obj_, static_cast<Derived const&>(o));
        vt_ = o.vt_;
      }
    }
    return *this;
  }
  dyn_ptr_core& operator=(dyn_ptr_core const&)
    requires(is_cache_ptr_v<Ptr> && !cache_ptr_copy_live_v<Ptr> &&
             !cache_ptr_copy_via_trait_v<Ptr>)
  = delete;

  // Move construction frees nothing (a pure handoff), so it defaults for
  // every non-cache Ptr. Move assignment defaults for non-vt_destroy Ptrs
  // (observer, deleter kinds): the smart pointer's own move-assign runs the
  // full teardown. Both keep those handles trivially assignable (vector
  // relocate = plain memberwise stores, same class as proxy_view).
  dyn_ptr_core(dyn_ptr_core&&) noexcept
    requires(!is_cache_ptr_v<Ptr>)
  = default;
  dyn_ptr_core& operator=(dyn_ptr_core&&) noexcept
    requires(!vt_destroys_on_dispose_v<Ptr>)
  = default;

  dyn_ptr_core(dyn_ptr_core&& o) noexcept
    requires(is_cache_ptr_v<Ptr> && cache_ptr_move_live_v<Ptr> &&
             !cache_ptr_move_via_trait_v<Ptr>)
      : dyn_ptr_core() {
    if (o) {
      vt_ptr v = std::exchange(o.vt_, nullptr);
      obj_.move_from(o.obj_, static_cast<Derived&>(o));
      vt_ = v;
    }
  }

  dyn_ptr_core(dyn_ptr_core&& o) noexcept
    requires(is_cache_ptr_v<Ptr> && cache_ptr_move_via_trait_v<Ptr> &&
             slice_proxy_ok())
      : dyn_ptr_core() {
    cache_take_moved_from(o);
  }

  dyn_ptr_core(dyn_ptr_core&&)
    requires(is_cache_ptr_v<Ptr> && !cache_ptr_move_live_v<Ptr> &&
             !cache_ptr_move_via_trait_v<Ptr>)
  = delete;

  dyn_ptr_core& operator=(dyn_ptr_core&& o)
    requires(!is_cache_ptr_v<Ptr> && vt_destroys_on_dispose_v<Ptr> &&
             slice_proxy_ok()) {
    if (this != &o) {
      // vt[-1] must run before the pointer frees the storage.
      // Dtor/assign are shallow: dispose_owned soft-skips empty.
      dispose_owned();
      take_shallow_moved_from(o);
    }
    return *this;
  }
  dyn_ptr_core& operator=(dyn_ptr_core&& o)
    requires(is_cache_ptr_v<Ptr> && slice_proxy_ok() &&
             (cache_ptr_move_live_v<Ptr> || cache_ptr_move_via_trait_v<Ptr>)) {
    if (this != &o) {
      if constexpr (cache_move_assign_live_v<
                    ops_move_assign_t<ptr_ops_t<Ptr>>>()) {
        if (*this && o) {
          // Source must stay live for handle-form assign ops; unpublish after.
          obj_.assign_move_from(o.obj_, static_cast<Derived&>(*this),
                                static_cast<Derived&>(o));
          vt_ = std::exchange(o.vt_, nullptr);
          return *this;
        }
      }
      end_cache_payload();
      cache_take_moved_from(o);
    }
    return *this;
  }
  dyn_ptr_core& operator=(dyn_ptr_core&&)
    requires(is_cache_ptr_v<Ptr> && !cache_ptr_move_live_v<Ptr> &&
             !cache_ptr_move_via_trait_v<Ptr>)
  = delete;
  // vt_destroy default reads vt[-1]. sub_dyn: only if release(*this) works.
  ~dyn_ptr_core()
    requires(slice_proxy_ok()) {
    if constexpr (vt_destroys_on_dispose_v<Ptr>)
      dispose_owned();
  }

  void reset()
    requires(slice_proxy_ok()) {
    if constexpr (vt_destroys_on_dispose_v<Ptr>)
      dispose_owned();
    else {
      vt_ = nullptr;
      ptr_reset(obj_);
    }
  }

  Derived& operator=(std::nullptr_t) noexcept
    requires(slice_proxy_ok()) {
    reset();
    return *this;
  }

  std::conditional_t<Const, const void*, void*> raw() const noexcept
    requires(!is_wptr_v<Ptr>) {
    // Ops bool slot (e.g. cache): buffer always addressable; gate on !*this.
    if constexpr (ptr_declares_ops_v<Ptr>) {
      if constexpr (cache_bool_live<ops_bool_t<ptr_ops_t<Ptr>>>()) {
        if (!*this)
          return nullptr;
      }
    }
    if constexpr (Const)
      return static_cast<const void*>(ptr_get(obj_));
    else
      return ptr_get(obj_);
  }

  ptr_type& get() noexcept { return obj_; }
  ptr_type const& get() const noexcept { return obj_; }

  bool expired() const noexcept
    requires(is_wptr_v<Ptr>) {
    return obj_.expired();
  }

  explicit operator bool() const noexcept {
    if constexpr (ptr_declares_ops_v<Ptr>) {
      if constexpr (cache_bool_live<ops_bool_t<ptr_ops_t<Ptr>>>()) {
        using Bool = cache_slot_op_t<ops_bool_t<ptr_ops_t<Ptr>>>;
        return Bool{}(static_cast<Derived const&>(*this));
      }
    }
    if constexpr (is_wptr_v<Ptr>)
      return !obj_.expired();
    else
      return ptr_get(obj_) != nullptr;
  }

  friend bool operator==(Derived const& d, std::nullptr_t) noexcept {
    return !d;
  }
  friend bool operator==(Derived const& a, Derived const& b) noexcept
    requires(!is_wptr_v<Ptr>) {
    return a.raw() == b.raw();
  }

 protected:
  template <std::meta::info, std::meta::info, std::meta::info>
  friend struct ::asf::call_fn;
  template <class, std::meta::info, std::meta::info, std::meta::info>
  friend struct ::asf::bound_call_fn;
  template <class, std::meta::info, std::meta::info, std::meta::info, class,
            class...>
  friend struct ::asf::bound_call_exact;
  template <class, std::size_t>
  friend struct op_slot;

  template <std::meta::info Method, std::meta::info = std::meta::info{},
            std::meta::info = std::meta::info{}>
  decltype(auto) call(auto&&... args)
    requires(!Const && !is_wptr_v<Ptr>) {
    if constexpr (SliceCall) {
      return handle_call_own_slice<Trait, Method, false>(
          ptr_get(obj_), vt_, std::forward<decltype(args)>(args)...);
    } else if constexpr (is_sptr_v<Ptr>) {
      // Shared ownership: no AllowReset / reset(raw) (would break control block).
      return handle_call<Trait, Method, false, false>(
          ptr_get(obj_), vt_, nullptr, nullptr,
          std::forward<decltype(args)>(args)...);
    } else {
      void* raw_p = ptr_get(obj_);
      struct sync_obj {
        ptr_type& obj;
        void*& raw_p;
        ~sync_obj() { ptr_reset(obj, raw_p); }
      } sync{obj_, raw_p};
      return handle_call<Trait, Method, false, true>(
          raw_p, vt_, &raw_p, &vt_, std::forward<decltype(args)>(args)...);
    }
  }

  template <std::meta::info Method, std::meta::info = std::meta::info{},
            std::meta::info = std::meta::info{}>
  decltype(auto) call(auto&&... args) const
    requires(Const && !is_wptr_v<Ptr>) {
    if constexpr (SliceCall)
      return handle_call_own_slice<Trait, Method, true>(
          ptr_get(obj_), vt_, std::forward<decltype(args)>(args)...);
    else
      return handle_call<Trait, Method, true, false>(
          storage_addr(), vt_, nullptr, nullptr,
          std::forward<decltype(args)>(args)...);
  }

  template <fixed_string Name, class... Args>
  decltype(auto) call(Args&&... args)
    requires(!Const && !is_wptr_v<Ptr>) {
    constexpr auto method =
        find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<Args>...>(
            Name.view());
    static_assert(method != std::meta::info{},
                  "Interface has no overload matching these arguments");
    if constexpr (SliceCall) {
      return handle_call_own_slice<Trait, method, false>(
          ptr_get(obj_), vt_, std::forward<Args>(args)...);
    } else if constexpr (is_sptr_v<Ptr>) {
      return handle_call<Trait, method, false, false>(
          ptr_get(obj_), vt_, nullptr, nullptr, std::forward<Args>(args)...);
    } else {
      void* raw_p = ptr_get(obj_);
      struct sync_obj {
        ptr_type& obj;
        void*& raw_p;
        ~sync_obj() { ptr_reset(obj, raw_p); }
      } sync{obj_, raw_p};
      return handle_call<Trait, method, false, true>(
          raw_p, vt_, &raw_p, &vt_, std::forward<Args>(args)...);
    }
  }

  template <fixed_string Name, class... Args>
  decltype(auto) call(Args&&... args) const
    requires(Const && !is_wptr_v<Ptr>) {
    constexpr auto method =
        find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<Args>...>(
            Name.view());
    static_assert(method != std::meta::info{},
                  "Interface has no overload matching these arguments");
    if constexpr (SliceCall)
      return handle_call_own_slice<Trait, method, true>(
          ptr_get(obj_), vt_, std::forward<Args>(args)...);
    else
      return handle_call<Trait, method, true, false>(
          storage_addr(), vt_, nullptr, nullptr, std::forward<Args>(args)...);
  }

};

template <class Derived, template <class> class Trait, bool Const,
          bool SliceCall, bool SoftUptrDestroy, class Ptr>
vt_ptr vt_of(dyn_ptr_core<Derived, Trait, Const, SliceCall, SoftUptrDestroy,
                          Ptr> const& h) noexcept {
  return h.vt_;
}

}  // namespace detail

template <class Handle>
detail::vt_ptr vtable_of(Handle const& h) noexcept {
  return detail::vt_of(h);
}

namespace detail {

// Naked storage address; no raw() emptiness gate. construct / vt hot paths.
template <class Handle>
void* storage_addr_of(Handle const& h) noexcept {
  return h.storage_addr();
}

// Same-vt view on Layout's table. Mutable and const are separate types
// (Layout is a template template, so it cannot carry const). Public handles
// dyn_view / c_dyn_view follow this detail block.
template <template <class> class Layout, template <class> class View,
          bool ConstOnly>
struct dyn_view_fn_box {
  struct data;
  consteval {
    if constexpr (ConstOnly)
      std::meta::define_aggregate(
          ^^data,
          dyn_like_fn_specs<View, true>(^^c_dyn_view<Layout, View>));
    else
      std::meta::define_aggregate(
          ^^data,
          dyn_like_fn_specs<View, false>(^^dyn_view<Layout, View>));
  }
};

template <template <class> class Layout, template <class> class View, bool Const,
          class Derived>
class dyn_view_body {
  using fns_type = typename dyn_view_fn_box<Layout, View, Const>::data;
  vt_ptr vt_{};
  void* ptr_{};

  template <template <class> class, template <class> class>
  friend class dyn_view;
  template <template <class> class, template <class> class>
  friend class c_dyn_view;

 protected:
  dyn_view_body(vt_ptr vt, void* ptr) noexcept
      : vt_(vt), ptr_(ptr) {}

 public:
  using proxy_t = fn_proxy<Derived, fns_type>;
  using rproxy_t = fn_rproxy<Derived, fns_type>;
  using c_proxy_t = std::conditional_t<
      Const, proxy_t,
      fn_proxy<c_dyn_view<Layout, View>,
               typename dyn_view_fn_box<Layout, View, true>::data>>;

  template <template <class> class To>
  using as_type =
      std::conditional_t<Const, c_dyn_view<Layout, To>,
                         dyn_view<Layout, To>>;

 private:
  template <std::meta::info, std::meta::info, std::meta::info>
  friend struct ::asf::call_fn;
  template <class, std::meta::info, std::meta::info, std::meta::info>
  friend struct ::asf::bound_call_fn;
  template <class, std::meta::info, std::meta::info, std::meta::info, class,
            class...>
  friend struct ::asf::bound_call_exact;
  template <class, std::size_t>
  friend struct op_slot;

  // Impl/SrcImpl unused: dyn slots resolve via invoke_wired at vt bind time.
  template <std::meta::info Method, std::meta::info = std::meta::info{},
            std::meta::info = std::meta::info{}>
  decltype(auto) call(auto&&... args) {
    return handle_call_in<Layout, View, Method, Const>(
        ptr_, vt_, std::forward<decltype(args)>(args)...);
  }

  template <fixed_string Name, class... Args>
  decltype(auto) call(Args&&... args) {
    constexpr auto method =
        find_method_for_args_on<^^View<void>, std::remove_cvref_t<Args>...>(
            Name.view());
    static_assert(method != std::meta::info{},
                  "View has no overload matching these arguments");
    return call<method>(std::forward<Args>(args)...);
  }

 public:
  // Continue the same-vt view from the current View (still no convert).
  template <template <class> class To>
    requires(is_trait_base_of<To, View>())
  as_type<To> as() const {
    return as_type<To>(vt_, ptr_);
  }

  proxy_t proxy() const {
    return proxy_t{const_cast<Derived*>(static_cast<Derived const*>(this))};
  }
  rproxy_t rproxy() const {
    return rproxy_t{const_cast<Derived*>(static_cast<Derived const*>(this))};
  }
  c_proxy_t c_proxy() const {
    if constexpr (Const)
      return proxy();
    else
      return c_dyn_view<Layout, View>(vt_, ptr_).proxy();
  }

  template <template <class> class To>
    requires(is_trait_base_of<To, View>())
  auto proxy() const {
    if constexpr (std::same_as<as_type<To>, Derived>)
      return proxy_t{const_cast<Derived*>(static_cast<Derived const*>(this))};
    else
      return as<To>().proxy();
  }

  template <template <class> class To>
    requires(is_trait_base_of<To, View>())
  auto rproxy() const {
    if constexpr (std::same_as<as_type<To>, Derived>)
      return rproxy_t{const_cast<Derived*>(static_cast<Derived const*>(this))};
    else
      return as<To>().rproxy();
  }

  template <template <class> class To>
    requires(is_trait_base_of<To, View>())
  auto c_proxy() const {
    if constexpr (std::same_as<as_type<To>, Derived>)
      return c_proxy();
    else
      return as<To>().c_proxy();
  }

  proxy_t operator->() const { return proxy(); }
  proxy_t operator*() const { return proxy(); }
};

struct dyn_view_ops {
  template <class H>
  explicit dyn_view_ops(H*) noexcept {}
};

template <template <class> class Layout, template <class> class View>
struct host_op_mixins<dyn_view<Layout, View>> {
  using type = dyn_view_ops;
};
template <template <class> class Layout, template <class> class View>
struct host_op_mixins<c_dyn_view<Layout, View>> {
  using type = dyn_view_ops;
};

}  // namespace detail

// Same-vt views (observer only; no get/ptr/bool, no convert).
// Obtained only via dyn::as / c_dyn::as / dyn_view::as �?the (ptr, vt)
// ctor is private (friends: dyn_handle_base, dyn_view_body).
template <template <class> class Layout, template <class> class View>
class dyn_view
    : public detail::dyn_view_body<Layout, View, false,
                                   dyn_view<Layout, View>> {
  using base =
      detail::dyn_view_body<Layout, View, false, dyn_view<Layout, View>>;

  template <template <template <class> class, class> class,
            template <class> class, bool, class>
  friend class detail::dyn_handle_base;
  template <template <class> class, template <class> class, bool, class>
  friend class detail::dyn_view_body;

  dyn_view(detail::vt_ptr vt, void* ptr) noexcept : base(vt, ptr) {}
};

template <template <class> class Layout, template <class> class View>
class c_dyn_view
    : public detail::dyn_view_body<Layout, View, true,
                                   c_dyn_view<Layout, View>> {
  using base =
      detail::dyn_view_body<Layout, View, true, c_dyn_view<Layout, View>>;

  template <template <template <class> class, class> class,
            template <class> class, bool, class>
  friend class detail::dyn_handle_base;
  template <template <class> class, template <class> class, bool, class>
  friend class detail::dyn_view_body;

  c_dyn_view(detail::vt_ptr vt, void* ptr) noexcept : base(vt, ptr) {}
};

namespace detail {

template <template <class> class Trait, bool ConstOnly>
struct sub_dyn_fn_box {
  struct data;
  consteval {
    if constexpr (ConstOnly)
      std::meta::define_aggregate(
          ^^data, dyn_like_fn_specs<Trait, true>(^^c_sub_dyn<Trait> const));
    else
      std::meta::define_aggregate(
          ^^data, dyn_like_fn_specs<Trait, false>(^^sub_dyn<Trait>));
  }
};

// Const-unified sub_* body + views (slice / proxy / lock). Public shells:
// sub_dyn / c_sub_dyn. SoftUptrDestroy kept true (SliceCall path).
template <template <class> class Trait, bool Const, class Ptr, class Derived>
class sub_dyn_body
    : public dyn_ptr_core<Derived, Trait, Const, true, true, Ptr> {
  using base = dyn_ptr_core<Derived, Trait, Const, true, true, Ptr>;

 protected:
  using typename base::ptr_type;
  using vt_ptr = detail::vt_ptr;
  using base::base;
  using base::obj_;
  using base::storage_addr;
  using base::vt_;

  template <template <class> class, bool, class, class>
  friend class sub_dyn_body;
  template <template <class> class T, class P>
  friend class ::asf::sub_dyn;
  template <template <class> class T, class P>
  friend class ::asf::c_sub_dyn;

  // Void* observation flavor of this Const (proxy host).
  using Obs = std::conditional_t<Const, c_sub_dyn<Trait>, sub_dyn<Trait>>;
  using Locked =
      std::conditional_t<Const, c_sub_dyn<Trait, std::shared_ptr<void>>,
                         sub_dyn<Trait, std::shared_ptr<void>>>;

 public:
  using base::raw;
  using base::reset;

  using proxy_t =
      fn_proxy<Obs, typename sub_dyn_fn_box<Trait, Const>::data>;

  sub_dyn_body() noexcept = default;
  sub_dyn_body(std::nullptr_t) noexcept : sub_dyn_body() {}
  sub_dyn_body(sub_dyn_body const&) = default;
  sub_dyn_body(sub_dyn_body&&) = default;
  sub_dyn_body& operator=(sub_dyn_body const&) = default;
  sub_dyn_body& operator=(sub_dyn_body&&) = default;

  // Slice from mut dyn (both Const flavors).
  template <template <class> class Final>
    requires(can_sub_slice<Final, Trait>())
  explicit sub_dyn_body(dyn<Final, Ptr> const& handle) noexcept
      : base(handle.vt_ + sub_slice_offset<Final, Trait>(), handle.obj_) {}

  template <template <class> class Final>
    requires(can_sub_slice<Final, Trait>())
  explicit sub_dyn_body(dyn<Final, Ptr>&& handle) noexcept : base() {
    // Sequence: unpublish source, then take payload (arg order is unspecified).
    vt_ptr v = std::exchange(handle.vt_, nullptr);
    this->obj_ = std::move(handle.obj_);
    this->vt_ = v + sub_slice_offset<Final, Trait>();
  }

  // Slice from c_dyn: const surface only.
  template <template <class> class Final>
    requires(Const && can_sub_slice<Final, Trait>())
  explicit sub_dyn_body(c_dyn<Final, Ptr> const& handle) noexcept
      : base(handle.vt_ + sub_slice_offset<Final, Trait>(), handle.obj_) {}

  template <template <class> class Final>
    requires(Const && can_sub_slice<Final, Trait>())
  explicit sub_dyn_body(c_dyn<Final, Ptr>&& handle) noexcept : base() {
    vt_ptr v = std::exchange(handle.vt_, nullptr);
    this->obj_ = std::move(handle.obj_);
    this->vt_ = v + sub_slice_offset<Final, Trait>();
  }

  proxy_t operator->()
    requires(!Const && !is_wptr_v<Ptr>) {
    auto obs = observe(this->storage_addr(), this->vt_);
    return proxy_t{&obs};
  }
  proxy_t operator*()
    requires(!Const && !is_wptr_v<Ptr>) {
    auto obs = observe(this->storage_addr(), this->vt_);
    return proxy_t{&obs};
  }

  proxy_t operator->() const
    requires(Const && !is_wptr_v<Ptr>) {
    auto obs = observe(
        const_cast<void*>(static_cast<const void*>(this->storage_addr())),
        this->vt_);
    return proxy_t{&obs};
  }
  proxy_t operator*() const
    requires(Const && !is_wptr_v<Ptr>) {
    auto obs = observe(
        const_cast<void*>(static_cast<const void*>(this->storage_addr())),
        this->vt_);
    return proxy_t{&obs};
  }

  Locked lock() const
    requires(is_wptr_v<Ptr>) {
    auto p = this->obj_.lock();
    if (!p)
      return {};
    return Locked::from_ptr(std::move(p), this->vt_);
  }

 protected:
  // Internal observation factories (proxy/lock use only; not public API).
  // Always Obs (void* Ptr); host Ptr may be unique_ptr / raw.
  static Obs observe(void* p, vt_ptr vt) noexcept
    requires(!is_sptr_v<Ptr> && !is_wptr_v<Ptr>) {
    return Obs{vt, p};
  }

  static Derived from_ptr(ptr_type p, vt_ptr vt) noexcept {
    return Derived{vt, std::move(p)};
  }
};

}  // namespace detail

// Isolated upcast slice of a leaf/final dyn's table onto Trait's own section.
// Independent of ASF_HAS_DYN_MULTI_VT. Same Ptr as dyn: & copies Ptr, && moves
// Ptr (construct via sub_dyn{dyn} / to_sub_dyn - not dyn members). No to /
// to_dyn / secondary sub slice (no secondary upcast).
// No negative prefix (destroy / RTTI meta): a section cannot
// know if it sits at table origin (this Trait may be a sibling even with
// no bases). vt_destroy Ptr only if pointer_trait::release accepts *this.
template <template <class> class Trait, class Ptr>
class sub_dyn
    : public detail::sub_dyn_body<Trait, false, Ptr, sub_dyn<Trait, Ptr>> {
  using base = detail::sub_dyn_body<Trait, false, Ptr, sub_dyn<Trait, Ptr>>;
  using typename base::ptr_type;
  using typename base::vt_ptr;

  template <template <class> class T, class P>
  friend class c_sub_dyn;
  template <template <class> class T, bool C, class P, class D>
  friend class detail::sub_dyn_body;

 public:
  using base::base;
  using base::proxy_t;
};

template <template <class> class Trait, class Ptr>
class c_sub_dyn
    : public detail::sub_dyn_body<Trait, true, Ptr, c_sub_dyn<Trait, Ptr>> {
  using base = detail::sub_dyn_body<Trait, true, Ptr, c_sub_dyn<Trait, Ptr>>;
  using typename base::ptr_type;
  using typename base::vt_ptr;

  template <template <class> class T, bool C, class P, class D>
  friend class detail::sub_dyn_body;

 public:
  using base::base;
  using base::proxy_t;

  // Mut sub -> const sub (same Ptr, or Ptr rebind).
  explicit c_sub_dyn(sub_dyn<Trait, Ptr> const& w) noexcept
      : base(w.vt_, w.obj_) {}

  explicit c_sub_dyn(sub_dyn<Trait, Ptr>&& w) noexcept : base() {
    this->vt_ = std::exchange(w.vt_, nullptr);
    this->obj_ = std::move(w.obj_);
  }

  template <class SrcP>
    requires(detail::dyn_ptr_rebind_from<Ptr, SrcP, SrcP const&>)
  explicit(!std::is_convertible_v<SrcP const&, Ptr>)
  c_sub_dyn(sub_dyn<Trait, SrcP> const& w)
      noexcept(std::is_nothrow_constructible_v<Ptr, SrcP const&>)
      : base(w.vt_, ptr_type(w.obj_)) {}

  template <class SrcP>
    requires(detail::dyn_ptr_rebind_from<Ptr, SrcP, SrcP &&>)
  explicit(!std::is_convertible_v<SrcP &&, Ptr>)
  c_sub_dyn(sub_dyn<Trait, SrcP>&& w)
      noexcept(std::is_nothrow_constructible_v<Ptr, SrcP &&>)
      : base() {
    vt_ptr v = w.vt_;
    this->obj_ = ptr_type(std::move(w.obj_));
    this->vt_ = v;
    if constexpr (!std::is_constructible_v<Ptr, SrcP const&>) {
      w.vt_ = nullptr;
    }
  }
};

namespace detail {

template <template <class> class Trait>
struct sub_dyn_ops {
  template <class H>
  explicit sub_dyn_ops(H*) noexcept {}
};

template <template <class> class Trait, class Ptr>
struct host_op_mixins<sub_dyn<Trait, Ptr>> {
  using type = sub_dyn_ops<Trait>;
};
template <template <class> class Trait, class Ptr>
struct host_op_mixins<c_sub_dyn<Trait, Ptr>> {
  using type = sub_dyn_ops<Trait>;
};
// Impl facets (parameterized). Public surface is distinct sealed types below
// (defined after the facets) -- not transparent aliases.
template <template <class> class Trait, bool Const>
struct dyn_obs_proxy_facet;
// SrcP = host erased pointer type (void*, unique_ptr<void, Del>, ...).
template <template <class> class Trait, bool Const, class SrcP = void*,
          bool Rvalue = false>
struct dyn_proxy_facet;

template <template <class> class Trait> struct dyn_obs_proxy;
template <template <class> class Trait> struct dyn_c_obs_proxy;

template <template <class> class Trait, class SrcP = void*>
struct dyn_proxy;
template <template <class> class Trait, class SrcP = void*>
struct dyn_c_proxy;
template <template <class> class Trait, class SrcP = void*>
struct dyn_rproxy;
template <template <class> class Trait, class SrcP = void*>
struct dyn_c_rproxy;
struct dyn_factory_access;

struct cache_owned_inplace_t {};
inline constexpr cache_owned_inplace_t cache_owned_inplace{};

// Src construct via vt op_construct; definition follows dyn_factory_access.
// AsRvalue selects the rvalue-self slot; alloc is not an erased argument.
template <template <class> class Trait, class Out, bool AsRvalue,
          class Alloc = std::allocator<std::byte>, class... Extra>
Out construct_erased_to(void* ptr, vt_ptr vt, Alloc alloc, Extra&&... extra)
  requires(trait_has_dyn_construct<Trait>());

// cache_ptr free-construct: capacity check only; empty vt if it does not fit.
// Live source is the caller's job (same as construct_erased_to). Ops are the
// Trait deduction (chain slot -> op_from_trait, else unsupported for copy/move;
// destroy is always op_from_trait). Definition follows dyn_factory_access.
template <template <class> class Trait, std::size_t Size, std::size_t Align,
          bool AsRvalue, class... Extra>
dyn<Trait, cache_ptr<Size, Align, pointer_ops_from_trait<Trait>>>
try_construct_cache(void* ptr, vt_ptr vt, Extra&&... extra)
  requires(trait_has_dyn_construct<Trait>());

// dyn / c_dyn surface on dyn_ptr_core.
// Handle = public handle type (template template). Const=true: c_* .
// RTTI: meta.find is switch-style id->one of meta.tables[0..x).
// Casts rebind Trait only - same complete Ptr.
template <template <template <class> class, class> class Handle,
          template <class> class Trait, bool Const, class Ptr>
class dyn_handle_base
    : public dyn_ptr_core<Handle<Trait, Ptr>, Trait, Const, false,
                          false, Ptr>,
      public dyn_size_facet<dyn_handle_base<Handle, Trait, Const, Ptr>, Trait,
                            trait_has_dyn_construct<Trait>()> {
  friend dyn_size_facet<dyn_handle_base, Trait, true>;
  friend dyn_size_facet<dyn_handle_base, Trait, false>;
  template <class H>
  friend void* storage_addr_of(H const&) noexcept;

 protected:
  using core =
      dyn_ptr_core<Handle<Trait, Ptr>, Trait, Const, false, false, Ptr>;
  using typename core::ptr_type;
  using core::core;
  using core::obj_;
  using core::storage_addr;
  using core::vt_;

  template <template <template <class> class, class> class H,
            template <class> class T, bool C, class P>
  friend class dyn_handle_base;

  template <template <class> class T, class P>
  friend class ::asf::dyn;
  template <template <class> class T, class P>
  friend class ::asf::c_dyn;
  template <template <class> class T, class P>
  friend class ::asf::sub_dyn;
  template <template <class> class T, class P>
  friend class ::asf::c_sub_dyn;
  template <template <class> class T, bool C, class P, class D>
  friend class sub_dyn_body;
  template <template <class> class Tr, std::size_t Sz, std::size_t Al,
            bool AsRvalue, class... Extra>
  friend dyn<Tr, cache_ptr<Sz, Al, pointer_ops_from_trait<Tr>>>
  try_construct_cache(void*, vt_ptr, Extra&&...);
  template <template <class> class Tr, bool C>
  friend struct dyn_obs_proxy_facet;
  template <template <class> class Tr, bool C, class SP, bool R>
  friend struct dyn_proxy_facet;
  friend struct dyn_factory_access;

  using self = Handle<Trait, Ptr>;

  auto proxy_host() const {
    if constexpr (Const)
      return static_cast<self const*>(this);
    else
      return const_cast<self*>(static_cast<self const*>(this));
  }

  template <template <class> class To>
  static Handle<To, Ptr> make(vt_ptr vt, ptr_type obj) {
    return Handle<To, Ptr>(vt, std::move(obj));
  }

  template <template <class> class To>
  Handle<To, Ptr> clone_retype() const {
    return make<To>(vt_, obj_);
  }
  template <template <class> class To>
  Handle<To, Ptr> move_retype() {
    vt_ptr v = std::exchange(vt_, nullptr);
    return make<To>(v, std::move(obj_));
  }
  template <template <class> class To>
  Handle<To, Ptr> clone_retype_vt(vt_ptr vt) const {
    return make<To>(vt, obj_);
  }
  template <template <class> class To>
  Handle<To, Ptr> move_retype_vt(vt_ptr vt) {
    vt_ = nullptr;
    return make<To>(vt, std::move(obj_));
  }

  template <template <class> class To>
  static Handle<To, Ptr> make(vt_ptr vt, void* ptr)
    requires(!is_obs_ptr_v<Ptr>) {
    return make<To>(vt, ptr_type{ptr});
  }

  // Section-prefix: same physical vt. Else MULTI_VT Redirect slot -> cover.
  template <template <class> class To>
  static vt_ptr upcast_vt(vt_ptr vt) noexcept {
    if constexpr (is_section_prefix_of<To, Trait>()) {
      return vt;
    } else {
      constexpr auto idx = find_redirect_slot_v<Trait<void>, To<void>>;
      static_assert(idx != static_cast<std::size_t>(-1),
                    "no shared redirect path to target interface");
      return from_vt_fn<vt_ptr()>(vt[idx])();
    }
  }

  static self from_storage(void* ptr, vt_ptr vt)
    requires(!is_heap_ptr_v<Ptr>) {
    return self(vt, ptr);
  }

  static self from_owned_storage(void* ptr, vt_ptr vt)
    requires(!is_heap_ptr_v<Ptr>) {
    return self(vt, ptr);
  }

  // std::unique_ptr + new_delete (default Alloc): ::operator new block.
  static self from_owned_storage(void* ptr, vt_ptr vt)
    requires(is_new_delete_unique_v<Ptr> && !Const) {
    return self(vt, ptr_type{ptr});
  }

  // std::unique_ptr + alloc_delete (non-default Alloc): sized deallocate.
  template <class Alloc>
  static self from_owned_storage(void* ptr, vt_ptr vt,
                                 std::size_t nbytes, Alloc alloc)
    requires(is_alloc_delete_unique_ptr_v<ptr_type> && !Const) {
    using Deleter = typename ptr_type::deleter_type;
    return self(vt, ptr_type{ptr, Deleter{std::move(alloc), nbytes}});
  }

  // cache_ptr: place Storage into the embedded buffer.
  template <class DoConstruct>
  static self from_cache_owned(vt_ptr vt, std::size_t size,
                               std::size_t align, DoConstruct&& do_construct)
    requires(is_cache_ptr_v<Ptr> && !Const) {
    self out;
    out.cache_emplace_owned(vt, size, align,
                            std::forward<DoConstruct>(do_construct));
    return out;
  }

  // Erased construct: size/align come from the source vt (runtime).
  template <class DoConstruct>
  void cache_emplace_owned(vt_ptr vt, std::size_t size,
                           std::size_t align, DoConstruct&& do_construct)
    requires(is_cache_ptr_v<Ptr> && !Const) {
    void* dst = obj_.emplace(size, align);
    std::forward<DoConstruct>(do_construct)(dst);
    vt_ = vt;
  }

  // Typed adopt: Spec fit already static_assert'd; no runtime size/align.
  template <class DoConstruct>
  void cache_occupy_owned(vt_ptr vt, DoConstruct&& do_construct)
    requires(is_cache_ptr_v<Ptr> && !Const) {
    void* dst = obj_.occupy();
    std::forward<DoConstruct>(do_construct)(dst);
    vt_ = vt;
  }

  static constexpr bool cache_owned = is_cache_ptr_v<Ptr>;
  static constexpr bool new_delete_unique_owned =
      is_new_delete_unique_v<Ptr>;

  static self from_sptr(std::shared_ptr<void> p, vt_ptr vt)
    requires(is_sptr_v<Ptr>) {
    return self(vt, ptr_type{std::move(p)});
  }

 public:
  using core::raw;
  using core::reset;

  // Split ownership from the vtable. Callable on an lvalue; the Ptr is moved
  // out here. Ptr comes first. The dyn<void*> keeps this same vt and observes
  // the object; it does not own it. *this is left empty. cache_ptr and
  // weak_ptr stay intact (their address is not separable this way).
  std::pair<Ptr, dyn<Trait, void*>> release()
    requires(!is_cache_ptr_v<Ptr> && !is_wptr_v<Ptr>) {
    void* raw = ptr_get(obj_);
    vt_ptr vt = std::exchange(vt_, nullptr);
    Ptr owned = std::move(obj_);
    if constexpr (std::is_pointer_v<Ptr>)
      obj_ = nullptr;
    return {std::move(owned), dyn<Trait, void*>::from_storage(raw, vt)};
  }

  using proxy_t =
      std::conditional_t<Const, dyn_c_proxy<Trait, ptr_type>,
                         dyn_proxy<Trait, ptr_type>>;
  using rproxy_t =
      std::conditional_t<Const, dyn_c_rproxy<Trait, ptr_type>,
                         dyn_rproxy<Trait, ptr_type>>;
  using c_proxy_t = dyn_c_proxy<Trait, ptr_type>;

  dyn_handle_base() noexcept = default;
  dyn_handle_base(std::nullptr_t) noexcept : dyn_handle_base() {}
  dyn_handle_base(dyn_handle_base const&) = default;
  dyn_handle_base(dyn_handle_base&&) = default;
  dyn_handle_base& operator=(dyn_handle_base const&) = default;
  dyn_handle_base& operator=(dyn_handle_base&&) = default;
  dyn_handle_base& operator=(std::nullptr_t) noexcept {
    this->reset();
    return *this;
  }

  // Convert copies or moves/wraps the Ptr, then rebinds vt.
  // cache op_from_trait applies the deep vt op as that cache slot.
  // The converting constructor is the upcast + Ptr convert; not member as().
  template <template <template <class> class, class> class H,
            template <class> class From, bool FromConst>
  void take_upcast_copy(
      dyn_handle_base<H, From, FromConst, Ptr> const& o) {
    using Src = dyn_handle_base<H, From, FromConst, Ptr>;
    if constexpr (is_cache_ptr_v<Ptr> && cache_ptr_copy_via_trait_v<Ptr>) {
      // Shallow convert may deep-construct; library soft-skips empty source.
      if (!o)
        return;
      auto* new_vt = Src::template upcast_vt<Trait>(o.vt_);
      // Same geometry: fit is guaranteed at compile time (occupy).
      void* dst = obj_.occupy();
      dyn_construct_of<From, false>(o.vt_)(ptr_get(o.obj_), dst);
      vt_ = new_vt;
    } else {
      obj_ = o.obj_;
      vt_ = o ? Src::template upcast_vt<Trait>(o.vt_) : nullptr;
    }
  }

  template <template <template <class> class, class> class H,
            template <class> class From, bool FromConst>
  void take_upcast_move(dyn_handle_base<H, From, FromConst, Ptr>&& o) {
    using Src = dyn_handle_base<H, From, FromConst, Ptr>;
    if constexpr (is_cache_ptr_v<Ptr> && cache_ptr_move_via_trait_v<Ptr>) {
      if (!o)
        return;
      auto* new_vt = Src::template upcast_vt<Trait>(o.vt_);
      void* dst = obj_.occupy();
      dyn_construct_of<From, true>(o.vt_)(ptr_get(o.obj_), dst);
      vt_ = new_vt;
      o.dispose_owned();
    } else if (!o) {
      return;
    } else {
      vt_ptr src_vt = std::exchange(o.vt_, nullptr);
      obj_ = std::move(o.obj_);
      vt_ = Src::template upcast_vt<Trait>(src_vt);
    }
  }

  template <template <template <class> class, class> class H,
            template <class> class From, bool FromConst, class SrcP>
  void take_convert_copy(
      dyn_handle_base<H, From, FromConst, SrcP> const& o) {
    using Src = dyn_handle_base<H, From, FromConst, SrcP>;
    if constexpr (std::same_as<Ptr, SrcP>) {
      take_upcast_copy(o);
    } else if constexpr (dyn_ptr_deleter_handoff<Ptr, SrcP, SrcP const&>()) {
      vt_ptr src_vt = o.vt_;
      obj_ = dyn_ptr_to_deleter_copy<Ptr>(o.obj_, src_vt);
      vt_ = *this ? Src::template upcast_vt<Trait>(src_vt) : nullptr;
    } else if constexpr (dyn_cache_grow<Ptr, SrcP>() &&
                         cache_ptr_copy_via_trait_v<Ptr> &&
                         cache_ptr_copy_via_trait_v<SrcP>) {
      // Grow-only erased cache -> cache: the payload lived in the
      // smaller-or-equal source buffer, so it provably fits (occupy).
      if (!o)
        return;
      auto* new_vt = Src::template upcast_vt<Trait>(o.vt_);
      void* dst = obj_.occupy();
      dyn_construct_of<From, false>(o.vt_)(ptr_get(o.obj_), dst);
      vt_ = new_vt;
    } else {
      obj_ = ptr_type(o.obj_);
      vt_ = o ? Src::template upcast_vt<Trait>(o.vt_) : nullptr;
    }
  }

  template <template <template <class> class, class> class H,
            template <class> class From, bool FromConst, class SrcP>
  void take_convert_move(
      dyn_handle_base<H, From, FromConst, SrcP>&& o) {
    using Src = dyn_handle_base<H, From, FromConst, SrcP>;
    if constexpr (std::same_as<Ptr, SrcP>) {
      take_upcast_move(std::move(o));
    } else if constexpr (dyn_ptr_deleter_handoff<Ptr, SrcP, SrcP&&>()) {
      vt_ptr src_vt = std::exchange(o.vt_, nullptr);
      auto next = dyn_ptr_to_deleter<Ptr>(std::move(o.obj_), src_vt);
      if constexpr (std::is_pointer_v<SrcP>)
        o.obj_ = nullptr;
      obj_ = std::move(next);
      vt_ = *this ? Src::template upcast_vt<Trait>(src_vt) : nullptr;
    } else if constexpr (dyn_cache_grow<Ptr, SrcP>() &&
                         cache_ptr_move_via_trait_v<Ptr> &&
                         cache_ptr_move_via_trait_v<SrcP>) {
      // Grow-only erased cache -> cache: provably fits (occupy).
      if (!o)
        return;
      auto* new_vt = Src::template upcast_vt<Trait>(o.vt_);
      void* dst = obj_.occupy();
      dyn_construct_of<From, true>(o.vt_)(ptr_get(o.obj_), dst);
      vt_ = new_vt;
      o.dispose_owned();
    } else if (!o) {
      return;
    } else {
      vt_ptr src_vt = o.vt_;
      if constexpr (!std::is_constructible_v<Ptr, SrcP const&>) {
        src_vt = std::exchange(o.vt_, nullptr);
      }
      obj_ = ptr_type(std::move(o.obj_));
      vt_ = Src::template upcast_vt<Trait>(src_vt);
    }
  }

  void prepare_assign_from() {
    if constexpr (is_cache_ptr_v<Ptr>)
      this->end_cache_payload();
    else if constexpr (vt_destroys_on_dispose_v<Ptr>)
      this->dispose_owned();
  }

  // Trait upcast (includes From == Trait) and Ptr convert together.
  // explicitness follows SrcP -> Ptr.
  template <template <class> class From, class SrcP>
    requires(can_dyn_upcast<Trait, From>() &&
             !std::same_as<self, Handle<From, SrcP>> &&
             dyn_handle_ptr_from<Ptr, SrcP, SrcP const&>)
  explicit(!std::is_convertible_v<SrcP const&, Ptr>)
  dyn_handle_base(Handle<From, SrcP> const& o)
      noexcept(std::is_nothrow_constructible_v<Ptr, SrcP const&>)
      : dyn_handle_base() {
    take_convert_copy(o);
  }

  template <template <class> class From, class SrcP>
    requires(can_dyn_upcast<Trait, From>() &&
             !std::same_as<self, Handle<From, SrcP>> &&
             dyn_handle_ptr_from<Ptr, SrcP, SrcP &&>)
  explicit(!std::is_convertible_v<SrcP &&, Ptr>)
  dyn_handle_base(Handle<From, SrcP>&& o)
      noexcept(std::is_nothrow_constructible_v<Ptr, SrcP &&>)
      : dyn_handle_base() {
    take_convert_move(std::move(o));
  }

  template <template <class> class From, class SrcP>
    requires(can_dyn_upcast<Trait, From>() &&
             !std::same_as<self, Handle<From, SrcP>> &&
             dyn_handle_ptr_from<Ptr, SrcP, SrcP const&>)
  self& operator=(Handle<From, SrcP> const& o) {
    prepare_assign_from();
    take_convert_copy(o);
    return static_cast<self&>(*this);
  }

  template <template <class> class From, class SrcP>
    requires(can_dyn_upcast<Trait, From>() &&
             !std::same_as<self, Handle<From, SrcP>> &&
             dyn_handle_ptr_from<Ptr, SrcP, SrcP &&>)
  self& operator=(Handle<From, SrcP>&& o) {
    prepare_assign_from();
    take_convert_move(std::move(o));
    return static_cast<self&>(*this);
  }

  Handle<Trait, std::shared_ptr<void>> lock() const
    requires(is_wptr_v<Ptr>) {
    auto p = obj_.lock();
    if (!p)
      return {};
    return Handle<Trait, std::shared_ptr<void>>::from_sptr(std::move(p), vt_);
  }

  proxy_t operator->() const
    requires(!is_wptr_v<Ptr>) {
    return proxy_t{proxy_host()};
  }
  proxy_t operator*() const
    requires(!is_wptr_v<Ptr>) {
    return proxy_t{proxy_host()};
  }

  // Same observation as operator*; named forms on dyn/c_dyn.
  // c_dyn: c_proxy() == proxy() (both dyn_c_proxy).
  proxy_t proxy() const
    requires(!is_wptr_v<Ptr>) {
    return operator*();
  }
  rproxy_t rproxy() const
    requires(!is_wptr_v<Ptr>) {
    return rproxy_t(operator*());
  }
  c_proxy_t c_proxy() const
    requires(!is_wptr_v<Ptr>) {
    return operator*();
  }

  // To == Trait: this handle's proxy / rproxy.
  // To is a base: that view's proxy / rproxy (same vt, To's methods).
  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>() && !is_wptr_v<Ptr>)
  auto proxy() const {
    if constexpr (std::same_as<Handle<To, Ptr>, self>)
      return operator*();
    else
      return as<To>().proxy();
  }
  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>() && !is_wptr_v<Ptr>)
  auto rproxy() const {
    if constexpr (std::same_as<Handle<To, Ptr>, self>)
      return rproxy_t(operator*());
    else
      return as<To>().rproxy();
  }
  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>() && !is_wptr_v<Ptr>)
  auto c_proxy() const {
    if constexpr (std::same_as<Handle<To, Ptr>, self>)
      return c_proxy();
    else
      return as<To>().c_proxy();
  }

  template <template <class> class Other>
    requires(!std::same_as<Handle<Trait, Ptr>,
                           Handle<Other, Ptr>> &&
             can_dyn_compare<Trait, Other>())
  friend bool operator==(Handle<Trait, Ptr> const& a,
                         Handle<Other, Ptr> const& b) noexcept {
    return a.raw() == b.raw();
  }

 private:
  template <class, template <class> class, convert_kind, special_op,
            std::size_t, bool, bool>
  friend struct special_op_slot;

  template <typename Key, special_op Op>
  decltype(auto) cast_special(auto&&... args) const {
    return handle_cast_special<Trait, Key, Op>(
        this->storage_addr(), this->vt_, std::forward<decltype(args)>(args)...);
  }

  template <typename To>
    requires(dyn_like_has_special_op<Trait, To, special_op::convert, Const>())
  To cast_to() const {
    return cast_special<To, special_op::convert>();
  }

  template <typename From>
    requires(!Const &&
             dyn_like_has_special_op<Trait, std::remove_cvref_t<From>,
                                     special_op::assign, false>())
  decltype(auto) cast_assign(From&& rhs) {
    using Key = std::remove_cvref_t<From>;
    return cast_special<Key, special_op::assign>(std::forward<From>(rhs));
  }

 public:
  // Same-vt view only. Does not convert and does not switch vt.
  // Upcast of the handle itself is the converting constructor.
  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>() && !Const)
  dyn_view<Trait, To> as() const {
    return dyn_view<Trait, To>(vt_, storage_addr());
  }

  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>() && Const)
  c_dyn_view<Trait, To> as() const {
    return c_dyn_view<Trait, To>(vt_, storage_addr());
  }

  // Retype Trait only (same ptr/vt). NOT static_cast: under interface MI a
  // handle may already sit on a Redirect cover table - keeping that vt while
  // naming a more-derived Trait is catastrophically wrong. Prefer
  // dyn_cast when RTTI is on.
  template <template <class> class To, template <class> class From, class P>
  friend dyn<To, P> unsafe_downcast(dyn<From, P> const&);
  template <template <class> class To, template <class> class From, class P>
  friend dyn<To, P> unsafe_downcast(dyn<From, P>&&);
  template <template <class> class To, template <class> class From, class P>
  friend c_dyn<To, P> unsafe_downcast(c_dyn<From, P> const&);
  template <template <class> class To, template <class> class From, class P>
  friend c_dyn<To, P> unsafe_downcast(c_dyn<From, P>&&);

#if ASF_HAS_DYN_RTTI
  template <template <class> class To, template <class> class From, class P>
  friend dyn<To, P> rtti_cast(dyn<From, P> const&);
  template <template <class> class To, template <class> class From, class P>
  friend dyn<To, P> rtti_cast(dyn<From, P>&&);
  template <template <class> class To, template <class> class From, class P>
  friend c_dyn<To, P> rtti_cast(c_dyn<From, P> const&);
  template <template <class> class To, template <class> class From, class P>
  friend c_dyn<To, P> rtti_cast(c_dyn<From, P>&&);

#endif
};

// Defined after dyn_factory_access (needs the complete type).
template <template <class> class To, template <class> class From, class Ptr>
  requires(can_dyn_downcast<To, From>() &&
           (!ptr_downcast_chain_constrained_v<Ptr> ||
            dyn_downcast_chain_ok<To, From>()))
dyn<To, Ptr> unsafe_downcast(dyn<From, Ptr> const& handle);
template <template <class> class To, template <class> class From, class Ptr>
  requires(can_dyn_downcast<To, From>() &&
           (!ptr_downcast_chain_constrained_v<Ptr> ||
            dyn_downcast_chain_ok<To, From>()))
dyn<To, Ptr> unsafe_downcast(dyn<From, Ptr>&& handle);
template <template <class> class To, template <class> class From, class Ptr>
  requires(can_dyn_downcast<To, From>() &&
           (!ptr_downcast_chain_constrained_v<Ptr> ||
            dyn_downcast_chain_ok<To, From>()))
c_dyn<To, Ptr> unsafe_downcast(c_dyn<From, Ptr> const& handle);
template <template <class> class To, template <class> class From, class Ptr>
  requires(can_dyn_downcast<To, From>() &&
           (!ptr_downcast_chain_constrained_v<Ptr> ||
            dyn_downcast_chain_ok<To, From>()))
c_dyn<To, Ptr> unsafe_downcast(c_dyn<From, Ptr>&& handle);

#if ASF_HAS_DYN_RTTI
template <template <class> class To, template <class> class From, class Ptr>
dyn<To, Ptr> rtti_cast(dyn<From, Ptr> const& handle);
template <template <class> class To, template <class> class From, class Ptr>
dyn<To, Ptr> rtti_cast(dyn<From, Ptr>&& handle);
template <template <class> class To, template <class> class From, class Ptr>
c_dyn<To, Ptr> rtti_cast(c_dyn<From, Ptr> const& handle);
template <template <class> class To, template <class> class From, class Ptr>
c_dyn<To, Ptr> rtti_cast(c_dyn<From, Ptr>&& handle);
#endif
template <template <class> class Trait, bool Const, class P>
auto bind_dyn_from_ptr(P&& p);

}  // namespace detail

template <template <class> class Trait, class Ptr>
class dyn
    : public detail::dyn_handle_base<dyn, Trait, false, Ptr> {
  using base =
      detail::dyn_handle_base<dyn, Trait, false, Ptr>;
  using ptr_type = typename base::ptr_type;
  using vt_ptr = detail::vt_ptr;
  static constexpr bool allows_mirror_source =
      detail::layout_allows_unsourced_mirror<Trait>();

  friend base;
  template <template <class> class T, class P>
  friend class dyn;
  template <template <class> class T, class P>
  friend class c_dyn;
  template <template <template <class> class, class> class H,
            template <class> class T, bool C, class P>
  friend class detail::dyn_handle_base;
  friend struct detail::dyn_factory_access;

 public:
  using base::base;
  using base::proxy_t;

 private:
  // In-place cache construction (detail::dyn_factory_access only).
  // Erased: size/align from vt. Typed: occupy (fit already static).
  template <class DoConstruct>
  dyn(detail::cache_owned_inplace_t, vt_ptr vt, std::size_t size,
      std::size_t align, DoConstruct&& do_construct)
    requires(detail::is_cache_ptr_v<Ptr>)
      : base() {
    this->cache_emplace_owned(vt, size, align,
                              std::forward<DoConstruct>(do_construct));
  }

  template <class DoConstruct>
  dyn(detail::cache_owned_inplace_t, vt_ptr vt, DoConstruct&& do_construct)
    requires(detail::is_cache_ptr_v<Ptr>)
      : base() {
    this->cache_occupy_owned(vt, std::forward<DoConstruct>(do_construct));
  }

 public:
  // Pointer bind (void*): T* becomes observation Ptr. Bare T& is not accepted.
  template <typename U>
    requires(detail::is_obs_ptr_v<Ptr> && !std::is_void_v<U> &&
             !std::is_const_v<U> &&
             !detail::impl_self_is_identity_mirror<Trait, U>() &&
             (dyn_intermediate_v || detail::is_trait_final<Trait, U>()))
  dyn(U* p)
      : base(detail::dyn_object_slots<false, Trait, U, U>(), p) {}

  // Sourced mirror (no [[=mirror]] required). Match always OK; mismatch OK
  // because destroy is always vt[-1] (~Src). Mixed layouts: mirror sections
  // -> T, non-mirror -> Src.
  template <typename T, typename Src>
    requires(!detail::is_heap_ptr_v<Ptr> &&
             is_sourced_mirror_v<mirror<T, Src>> && !std::is_const_v<T> &&
             (detail::mirror_view_is_src_v<T, Src> ||
              detail::sourced_dyn_view_mismatch_ok<Trait>()) &&
             (dyn_intermediate_v || detail::is_trait_final<Trait, T>()))
  dyn(mirror<T, Src> const& v)
      : base(detail::dyn_bind_mirror_slots<false, Trait, T, T, Src>(),
             detail::dyn_bind_mirror_ptr<false, Trait, T, T, Src>(v)) {}

  using base::operator=;
  dyn(const dyn&) = default;
  dyn(dyn&&) = default;
  dyn& operator=(const dyn&) = default;
  dyn& operator=(dyn&&) = default;
};

template <template <class> class Trait, class Ptr>
class c_dyn
    : public detail::dyn_handle_base<c_dyn, Trait, true, Ptr> {
  using base =
      detail::dyn_handle_base<c_dyn, Trait, true, Ptr>;
  using ptr_type = typename base::ptr_type;
  using vt_ptr = detail::vt_ptr;
  static constexpr bool allows_mirror_source =
      detail::layout_allows_unsourced_mirror<Trait>();

  friend base;
  template <template <class> class T, class P>
  friend class c_dyn;
  template <template <class> class T, class P>
  friend class dyn;
  template <template <template <class> class, class> class H,
            template <class> class T, bool C, class P>
  friend class detail::dyn_handle_base;

  template <typename U>
  using const_as = std::add_const_t<std::remove_cvref_t<U>>;

 public:
  using base::base;
  using base::proxy_t;

  // Pointer bind: U* becomes void* observation (const pointee OK for c_dyn).
  template <typename U>
    requires(detail::is_obs_ptr_v<Ptr> && !std::is_void_v<U> &&
             !detail::impl_self_is_identity_mirror<Trait, const_as<U>>() &&
             (dyn_intermediate_v ||
              detail::is_trait_final<Trait, const_as<U>>()))
  c_dyn(U* p)
      : base(detail::dyn_object_slots<true, Trait, U, const_as<U>>(),
             const_cast<void*>(static_cast<const void*>(p))) {}

  // Sourced mirror (no [[=mirror]] required). See dyn(mirror).
  template <typename T, typename Src>
    requires(!detail::is_heap_ptr_v<Ptr> &&
             is_sourced_mirror_v<mirror<T, Src>> &&
             (detail::mirror_view_is_src_v<T, Src> ||
              detail::sourced_dyn_view_mismatch_ok<Trait>()) &&
             (dyn_intermediate_v ||
              detail::is_trait_final<Trait, const_as<T>>()))
  c_dyn(mirror<T, Src> const& v)
      : base(detail::dyn_bind_mirror_slots<true, Trait, const_as<T>, T, Src>(),
             detail::dyn_bind_mirror_ptr<true, Trait, const_as<T>, T, Src>(
                 v)) {}


  using base::operator=;
  c_dyn(const c_dyn&) = default;
  c_dyn(c_dyn&&) = default;
  c_dyn& operator=(const c_dyn&) = default;
  c_dyn& operator=(c_dyn&&) = default;

  // dyn -> c_dyn: same checks as same-Handle convert (Trait upcast + Ptr).
  template <template <class> class From, class SrcP>
    requires(detail::can_dyn_upcast<Trait, From>() &&
             detail::dyn_handle_ptr_from<Ptr, SrcP, SrcP const&>)
  explicit(!std::is_convertible_v<SrcP const&, Ptr>)
  c_dyn(dyn<From, SrcP> const& o)
      noexcept(std::is_nothrow_constructible_v<Ptr, SrcP const&>)
      : base() {
    this->take_convert_copy(o);
  }

  template <template <class> class From, class SrcP>
    requires(detail::can_dyn_upcast<Trait, From>() &&
             detail::dyn_handle_ptr_from<Ptr, SrcP, SrcP &&>)
  explicit(!std::is_convertible_v<SrcP &&, Ptr>)
  c_dyn(dyn<From, SrcP>&& o)
      noexcept(std::is_nothrow_constructible_v<Ptr, SrcP &&>)
      : base() {
    this->take_convert_move(std::move(o));
  }

  template <template <class> class From, class SrcP>
    requires(detail::can_dyn_upcast<Trait, From>() &&
             detail::dyn_handle_ptr_from<Ptr, SrcP, SrcP const&>)
  c_dyn& operator=(dyn<From, SrcP> const& o) {
    this->prepare_assign_from();
    this->take_convert_copy(o);
    return *this;
  }

  template <template <class> class From, class SrcP>
    requires(detail::can_dyn_upcast<Trait, From>() &&
             detail::dyn_handle_ptr_from<Ptr, SrcP, SrcP &&>)
  c_dyn& operator=(dyn<From, SrcP>&& o) {
    this->prepare_assign_from();
    this->take_convert_move(std::move(o));
    return *this;
  }
};

template <template <class> class Trait,
          class Deleter = std::default_delete<void>>
using unique_dyn =
    dyn<Trait, std::unique_ptr<void, detail::unique_deleter_t<Deleter>>>;
template <template <class> class Trait>
using shared_dyn = dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_dyn = dyn<Trait, std::weak_ptr<void>>;

// Traveling-buffer dyn: Ptr = cache_ptr<Size,Align[,Ops]>.
// Storing a src erases ops: each slot the Trait chain can perform becomes
// op_from_trait (deep vt op used as that shallow cache slot); otherwise the
// cache keeps its own shallow op.
template <template <class> class Trait, std::size_t Size,
          std::size_t Align = alignof(std::max_align_t),
          class Ops = pointer_ops<>>
using cache_dyn = dyn<Trait, cache_ptr<Size, Align, Ops>>;


template <template <class> class Trait>
using shared_sub_dyn = sub_dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_sub_dyn = sub_dyn<Trait, std::weak_ptr<void>>;

template <template <class> class Trait>
using shared_c_dyn = c_dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_c_dyn = c_dyn<Trait, std::weak_ptr<void>>;
template <template <class> class Trait>
using shared_c_sub_dyn = c_sub_dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_c_sub_dyn = c_sub_dyn<Trait, std::weak_ptr<void>>;


namespace detail {

struct dyn_factory_access {
  // In-place cache dyn construction (private ctor on dyn).
  template <class Handle, class... Args>
  static Handle from_cache_inplace(Args&&... args) {
    return Handle(std::forward<Args>(args)...);
  }
  template <class Handle>
  static Handle from_storage(void* ptr, vt_ptr vt) {
    return Handle::from_storage(ptr, vt);
  }
  template <class Handle>
  static Handle from_sptr(std::shared_ptr<void> p, vt_ptr vt) {
    return Handle::from_sptr(std::move(p), vt);
  }
  template <class Handle, class Ptr>
  static Handle from_wptr_adopt(Ptr&& p, vt_ptr vt) {
    return Handle{vt, std::forward<Ptr>(p)};
  }
  template <class Handle, class Ptr>
  static Handle from_uptr_adopt(Ptr&& p, vt_ptr vt) {
    return Handle{vt, std::forward<Ptr>(p)};
  }
  template <class Handle>
  static Handle from_owned_storage(void* ptr, vt_ptr vt) {
    return Handle::from_owned_storage(ptr, vt);
  }
  template <class Handle, class Alloc>
  static Handle from_owned_storage(void* ptr, vt_ptr vt,
                                   std::size_t nbytes, Alloc alloc) {
    return Handle::from_owned_storage(ptr, vt, nbytes, std::move(alloc));
  }

  // Move the Ptr out without running vt destroy. Caller arms the deleter.
  template <class Handle>
  static auto steal(Handle& h) noexcept -> decltype(auto) {
    h.vt_ = nullptr;
    return std::move(h.obj_);
  }

  template <template <class> class To, class Handle>
  static vt_ptr upcasted_vt(Handle& h) noexcept {
    return Handle::template upcast_vt<To>(h.vt_);
  }


  template <template <class> class To,
            template <template <class> class, class> class H,
            template <class> class From, bool C, class Ptr>
  static auto clone_retype(dyn_handle_base<H, From, C, Ptr> const& h) {
    return h.template clone_retype<To>();
  }
  template <template <class> class To,
            template <template <class> class, class> class H,
            template <class> class From, bool C, class Ptr>
  static auto move_retype(dyn_handle_base<H, From, C, Ptr>& h) {
    return h.template move_retype<To>();
  }
  template <template <class> class To,
            template <template <class> class, class> class H,
            template <class> class From, bool C, class Ptr>
  static auto clone_retype_vt(dyn_handle_base<H, From, C, Ptr> const& h,
                              vt_ptr vt) {
    return h.template clone_retype_vt<To>(vt);
  }
  template <template <class> class To,
            template <template <class> class, class> class H,
            template <class> class From, bool C, class Ptr>
  static auto move_retype_vt(dyn_handle_base<H, From, C, Ptr>& h,
                             vt_ptr vt) {
    return h.template move_retype_vt<To>(vt);
  }

  template <class Handle>
  static vt_destroy_fn stored_destroy(Handle& h) noexcept {
    if constexpr (requires { h.obj_.get_deleter().destroy; })
      return h.obj_.get_deleter().destroy;
    else
      return nullptr;
  }

  template <template <class> class Trait, class Out, class Alloc, class Place>
  static Out place_erased(vt_ptr vt, std::size_t size, std::size_t align,
                          Alloc alloc, Place&& do_construct)
    requires(trait_has_dyn_construct<Trait>()) {
    if constexpr (Out::cache_owned) {
      (void)alloc;
      return Out::from_cache_owned(vt, size, align,
                                   std::forward<Place>(do_construct));
    } else if constexpr (requires {
                           Out::from_sptr(std::declval<std::shared_ptr<void>>(),
                                          vt);
                         }) {
      auto sp = make_dyn_sptr<Trait>(size, vt, alloc);
      do_construct(sp.get());
      dyn_shared_storage::from_payload(sp.get(), align)->destroy =
          tag_destroy_align(*dyn_vt_destroy(vt),
                            dyn_storage_align_log<Trait>(vt));
      return Out::from_sptr(std::move(sp), vt);
    } else if constexpr (requires {
                           Out::from_owned_storage((void*)nullptr, vt, size,
                                                   alloc);
                         }) {
      void* dst = dyn_allocate_bytes(alloc, size);
      do_construct(dst);
      return Out::from_owned_storage(dst, vt, size, std::move(alloc));
    } else if constexpr (requires { Out::new_delete_unique_owned; } &&
                         Out::new_delete_unique_owned) {
      void* dst = ::operator new(size);
      do_construct(dst);
      return Out::from_owned_storage(dst, vt);
    } else {
      void* dst = dyn_allocate_bytes(alloc, size);
      do_construct(dst);
      return Out::from_owned_storage(dst, vt);
    }
  }
};

template <template <class> class Trait, class Out, bool AsRvalue, class Alloc,
          class... Extra>
Out construct_erased_to(void* ptr, vt_ptr vt, Alloc alloc, Extra&&... extra)
  requires(trait_has_dyn_construct<Trait>()) {
  const std::size_t size = dyn_storage_size<Trait>(vt);
  const std::size_t align = dyn_storage_align<Trait>(vt);
  return dyn_factory_access::place_erased<Trait, Out>(
      vt, size, align, std::move(alloc), [&](void* dst) {
        dyn_construct_into<Trait, AsRvalue>(vt, ptr, dst,
                                            std::forward<Extra>(extra)...);
      });
}

template <template <class> class Trait, std::size_t Size, std::size_t Align,
          bool AsRvalue, class... Extra>
dyn<Trait, cache_ptr<Size, Align, pointer_ops_from_trait<Trait>>>
try_construct_cache(void* ptr, vt_ptr vt, Extra&&... extra)
  requires(trait_has_dyn_construct<Trait>()) {
  using Out =
      dyn<Trait, cache_ptr<Size, Align, pointer_ops_from_trait<Trait>>>;
  const std::size_t size = dyn_storage_size<Trait>(vt);
  const std::size_t align = dyn_storage_align<Trait>(vt);
  if (size > Size || align > Align)
    return {};

  auto do_construct = [&](void* dst) {
    dyn_construct_into<Trait, AsRvalue>(vt, ptr, dst,
                                        std::forward<Extra>(extra)...);
  };

  return dyn_factory_access::from_cache_inplace<Out>(cache_owned_inplace, vt,
                                                     size, align, do_construct);
}

// Retype Trait only: same ptr, same vt. const& copies Ptr; && moves Ptr.
template <template <class> class To, template <class> class From, class Ptr>
  requires(can_dyn_downcast<To, From>() &&
           (!ptr_downcast_chain_constrained_v<Ptr> ||
            dyn_downcast_chain_ok<To, From>()))
dyn<To, Ptr> unsafe_downcast(dyn<From, Ptr> const& handle) {
  using Src = dyn_handle_base<dyn, From, false, Ptr>;
  return dyn_factory_access::clone_retype<To>(static_cast<Src const&>(handle));
}

template <template <class> class To, template <class> class From, class Ptr>
  requires(can_dyn_downcast<To, From>() &&
           (!ptr_downcast_chain_constrained_v<Ptr> ||
            dyn_downcast_chain_ok<To, From>()))
dyn<To, Ptr> unsafe_downcast(dyn<From, Ptr>&& handle) {
  using Src = dyn_handle_base<dyn, From, false, Ptr>;
  return dyn_factory_access::move_retype<To>(static_cast<Src&>(handle));
}

template <template <class> class To, template <class> class From, class Ptr>
  requires(can_dyn_downcast<To, From>() &&
           (!ptr_downcast_chain_constrained_v<Ptr> ||
            dyn_downcast_chain_ok<To, From>()))
c_dyn<To, Ptr> unsafe_downcast(c_dyn<From, Ptr> const& handle) {
  using Src = dyn_handle_base<c_dyn, From, true, Ptr>;
  return dyn_factory_access::clone_retype<To>(static_cast<Src const&>(handle));
}

template <template <class> class To, template <class> class From, class Ptr>
  requires(can_dyn_downcast<To, From>() &&
           (!ptr_downcast_chain_constrained_v<Ptr> ||
            dyn_downcast_chain_ok<To, From>()))
c_dyn<To, Ptr> unsafe_downcast(c_dyn<From, Ptr>&& handle) {
  using Src = dyn_handle_base<c_dyn, From, true, Ptr>;
  return dyn_factory_access::move_retype<To>(static_cast<Src&>(handle));
}

#if ASF_HAS_DYN_RTTI
// Upcast: static upcast_vt (no meta). Else meta.find -> physical vt.
template <template <class> class To, template <class> class From, class Ptr>
dyn<To, Ptr> rtti_cast(dyn<From, Ptr> const& handle) {
  using Src = dyn_handle_base<dyn, From, false, Ptr>;
  auto const& b = static_cast<Src const&>(handle);
  if constexpr (can_dyn_upcast<To, From>()) {
    return dyn_factory_access::clone_retype_vt<To>(
        b, Src::template upcast_vt<To>(vtable_of(b)));
  } else if constexpr (ptr_downcast_chain_constrained_v<Ptr> &&
                       is_trait_base_of<From, To>() &&
                       !dyn_downcast_chain_ok<To, From>()) {
    return dyn<To, Ptr>{};
  } else {
    auto* new_vt = meta_of<From>(vtable_of(b))->find_vt(&trait_id<To>::id);
    if (!new_vt)
      return dyn<To, Ptr>{};
    return dyn_factory_access::clone_retype_vt<To>(b, new_vt);
  }
}

template <template <class> class To, template <class> class From, class Ptr>
dyn<To, Ptr> rtti_cast(dyn<From, Ptr>&& handle) {
  using Src = dyn_handle_base<dyn, From, false, Ptr>;
  auto& b = static_cast<Src&>(handle);
  if constexpr (can_dyn_upcast<To, From>()) {
    return dyn_factory_access::move_retype_vt<To>(
        b, Src::template upcast_vt<To>(vtable_of(b)));
  } else if constexpr (ptr_downcast_chain_constrained_v<Ptr> &&
                       is_trait_base_of<From, To>() &&
                       !dyn_downcast_chain_ok<To, From>()) {
    return dyn<To, Ptr>{};
  } else {
    auto* new_vt = meta_of<From>(vtable_of(b))->find_vt(&trait_id<To>::id);
    if (!new_vt)
      return dyn<To, Ptr>{};
    return dyn_factory_access::move_retype_vt<To>(b, new_vt);
  }
}

template <template <class> class To, template <class> class From, class Ptr>
c_dyn<To, Ptr> rtti_cast(c_dyn<From, Ptr> const& handle) {
  using Src = dyn_handle_base<c_dyn, From, true, Ptr>;
  auto const& b = static_cast<Src const&>(handle);
  if constexpr (can_dyn_upcast<To, From>()) {
    return dyn_factory_access::clone_retype_vt<To>(
        b, Src::template upcast_vt<To>(vtable_of(b)));
  } else if constexpr (ptr_downcast_chain_constrained_v<Ptr> &&
                       is_trait_base_of<From, To>() &&
                       !dyn_downcast_chain_ok<To, From>()) {
    return c_dyn<To, Ptr>{};
  } else {
    auto* new_vt = meta_of<From>(vtable_of(b))->find_vt(&trait_id<To>::id);
    if (!new_vt)
      return c_dyn<To, Ptr>{};
    return dyn_factory_access::clone_retype_vt<To>(b, new_vt);
  }
}

template <template <class> class To, template <class> class From, class Ptr>
c_dyn<To, Ptr> rtti_cast(c_dyn<From, Ptr>&& handle) {
  using Src = dyn_handle_base<c_dyn, From, true, Ptr>;
  auto& b = static_cast<Src&>(handle);
  if constexpr (can_dyn_upcast<To, From>()) {
    return dyn_factory_access::move_retype_vt<To>(
        b, Src::template upcast_vt<To>(vtable_of(b)));
  } else if constexpr (ptr_downcast_chain_constrained_v<Ptr> &&
                       is_trait_base_of<From, To>() &&
                       !dyn_downcast_chain_ok<To, From>()) {
    return c_dyn<To, Ptr>{};
  } else {
    auto* new_vt = meta_of<From>(vtable_of(b))->find_vt(&trait_id<To>::id);
    if (!new_vt)
      return c_dyn<To, Ptr>{};
    return dyn_factory_access::move_retype_vt<To>(b, new_vt);
  }
}
#endif

// Shallow handoff onto a deleter-kind Ptr. Not deep construct: the object
// stays in the allocation the source already owns.
template <template <class> class Trait, class Ptr, template <class> class From,
          class SrcP>
auto bridge_owned_dyn(dyn<From, SrcP>&& src) {
  static_assert(pointer_has_deleter_v<Ptr>,
                "bridge target must be a deleter Ptr");
  vt_ptr new_vt = dyn_factory_access::upcasted_vt<Trait>(src);
  vt_ptr src_vt = vt_of(src);
  auto stolen = dyn_factory_access::steal(src);
  Ptr out = dyn_ptr_to_deleter<Ptr>(std::move(stolen), src_vt);
  return dyn_factory_access::from_uptr_adopt<dyn<Trait, Ptr>>(std::move(out),
                                                              new_vt);
}

// to_dyn / to_c_dyn from T* | unique_ptr | shared_ptr | weak_ptr:
// Trait + pointee -> vt; Ptr family from the pointer type.
template <template <class> class Trait, bool Const, class P>
  requires(dyn_bindable_ptr<std::remove_cvref_t<P>>)
auto bind_dyn_from_ptr(P&& p) {
  using Raw = std::remove_cvref_t<P>;
  using Bind = dyn_ptr_bind<Raw>;
  using As = typename Bind::element;
  using AsCv = typename Bind::element_cv;
  if constexpr (!Const) {
    static_assert(!std::is_const_v<AsCv>,
                  "to_dyn from pointer requires non-const pointee "
                  "(use to_c_dyn for const)");
  }
  static_assert(!impl_self_is_identity_mirror<Trait, As>(),
                "pointer bind needs identity-mirror-free Trait");
  static_assert(dyn_intermediate_v || is_trait_final<Trait, As>(),
                "pointer bind requires Trait final for As "
                "(or ASF_HAS_DYN_INTERMEDIATE)");

  using SlotAs = std::conditional_t<Const, std::add_const_t<As>, As>;
  vt_ptr vt =
      dyn_object_slots<Const, Trait, SlotAs, SlotAs>();
  auto adopted = adopt_ptr<Bind>(std::forward<P>(p), vt);

  if constexpr (Bind::is_raw) {
    if constexpr (Const)
      return dyn_factory_access::from_storage<c_dyn<Trait>>(adopted, vt);
    else
      return dyn_factory_access::from_storage<dyn<Trait>>(adopted, vt);
  } else if constexpr (Bind::is_uptr) {
    using Adopted = decltype(adopted);
    using OutDel = typename Adopted::deleter_type;
    return dyn_factory_access::from_uptr_adopt<
        dyn<Trait, std::unique_ptr<void, OutDel>>>(std::move(adopted), vt);
  } else if constexpr (Bind::is_sptr) {
    if constexpr (Const)
      return dyn_factory_access::from_sptr<c_dyn<Trait, std::shared_ptr<void>>>(
          std::move(adopted), vt);
    else
      return dyn_factory_access::from_sptr<dyn<Trait, std::shared_ptr<void>>>(
          std::move(adopted), vt);
  } else {
    static_assert(Bind::is_wptr,
                  "pointer bind only supports a raw pointer, unique_ptr, "
                  "shared_ptr, or weak_ptr; This type does not match any branch");
    if constexpr (Const)
      return dyn_factory_access::from_wptr_adopt<c_dyn<Trait, std::weak_ptr<void>>>(
          std::move(adopted), vt);
    else
      return dyn_factory_access::from_wptr_adopt<dyn<Trait, std::weak_ptr<void>>>(
          std::move(adopted), vt);
  }
}

struct adopt_obs_t {
  explicit adopt_obs_t() = default;
};
inline constexpr adopt_obs_t adopt_obs{};

// One observation facet: Const selects dyn vs c_dyn (cref).
// Method surface (fn/ops) is shared; no per-alias copy.
template <template <class> class Trait, bool Const>
struct dyn_obs_proxy_facet
    : no_friend_ops,
      fn_proxy_impl<
          std::conditional_t<Const, c_dyn<Trait> const, dyn<Trait>>,
          std::conditional_t<Const, typename c_dyn_fn_box<Trait>::data,
                             typename dyn_fn_box<Trait>::data>,
          std::conditional_t<Const, c_dyn_ops_for<Trait, c_dyn<Trait> const>,
                             dyn_ops_for<Trait, dyn<Trait>>>> {
  static constexpr bool is_const = Const;

  using Obs = std::conditional_t<Const, c_dyn<Trait>, dyn<Trait>>;
  using Fns =
      std::conditional_t<Const, typename c_dyn_fn_box<Trait>::data,
                         typename dyn_fn_box<Trait>::data>;
  using Ops =
      std::conditional_t<Const, c_dyn_ops_for<Trait, c_dyn<Trait> const>,
                         dyn_ops_for<Trait, dyn<Trait>>>;
  using FnHost = std::conditional_t<Const, Obs const, Obs>;
  using base = fn_proxy_impl<FnHost, Fns, Ops>;
  using base::operator=;

  Obs buf_{};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
  // storage_addr(): naked storage addr, no raw()/emptiness gate. operator-> on an
  // empty handle is already on the caller.
  template <class Ptr>
    requires(!Const)
  explicit dyn_obs_proxy_facet(dyn<Trait, Ptr>* host)
      : no_friend_ops(), base(&buf_),
        buf_(Obs::from_storage(host->storage_addr(), vtable_of(*host))) {}

  template <class Ptr>
    requires(Const)
  explicit dyn_obs_proxy_facet(c_dyn<Trait, Ptr> const* host)
      : no_friend_ops(), base(&buf_),
        buf_(Obs::from_storage(host->storage_addr(), vtable_of(*host))) {}

  dyn_obs_proxy_facet(dyn_obs_proxy_facet const& o)
      : no_friend_ops(), base(&buf_), buf_(o.buf_) {}

  dyn_obs_proxy_facet(dyn_obs_proxy_facet&& o) noexcept
      : no_friend_ops(), base(&buf_), buf_(std::move(o.buf_)) {}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

 protected:
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
  explicit dyn_obs_proxy_facet(adopt_obs_t, Obs buf)
      : no_friend_ops(), base(&buf_), buf_(std::move(buf)) {}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
};

// Layer 2: Rvalue=false -> proxy_t (copy); Rvalue=true -> rproxy_t (move).
// SrcP = host erased pointer type (complete Ptr).
template <template <class> class Trait, bool Const, class SrcP>
struct dyn_proxy_facet<Trait, Const, SrcP, false>
    : dyn_obs_proxy_facet<Trait, Const> {
  using obs_base = dyn_obs_proxy_facet<Trait, Const>;
  using typename obs_base::Obs;
  using obs_base::operator=;
  using obs_base::buf_;
  using src_ptr = SrcP;

  static constexpr bool is_rvalue = false;
  using proxy_t = dyn_proxy_facet;
  using rproxy_t = dyn_proxy_facet<Trait, Const, SrcP, true>;

  template <class Ptr>
    requires(!Const && std::same_as<Ptr, SrcP>)
  explicit dyn_proxy_facet(dyn<Trait, Ptr>* host) : obs_base(host) {}

  template <class Ptr>
    requires(Const && std::same_as<Ptr, SrcP>)
  explicit dyn_proxy_facet(c_dyn<Trait, Ptr> const* host)
      : obs_base(host) {}

  dyn_proxy_facet(dyn_proxy_facet const& o) : obs_base(o) {}

  dyn_proxy_facet(dyn_proxy_facet&& o) noexcept
    requires(!Const)
      : obs_base(std::move(o)) {}

  dyn_proxy_facet(dyn_proxy_facet&&)
    requires(Const) = delete;

  explicit dyn_proxy_facet(rproxy_t const& rp)
      : obs_base(static_cast<obs_base const&>(rp)) {}

  explicit dyn_proxy_facet(rproxy_t&& rp) noexcept
      : obs_base(static_cast<obs_base&&>(rp)) {}

  explicit dyn_proxy_facet(adopt_obs_t, Obs buf)
      : obs_base(adopt_obs, std::move(buf)) {}

  friend struct dyn_proxy_facet<Trait, Const, SrcP, true>;
};

template <template <class> class Trait, bool Const, class SrcP>
struct dyn_proxy_facet<Trait, Const, SrcP, true>
    : dyn_obs_proxy_facet<Trait, Const> {
  using obs_base = dyn_obs_proxy_facet<Trait, Const>;
  using typename obs_base::Obs;
  using obs_base::operator=;
  using obs_base::buf_;
  using src_ptr = SrcP;

  static constexpr bool is_rvalue = true;
  using proxy_t = dyn_proxy_facet<Trait, Const, SrcP, false>;
  using rproxy_t = dyn_proxy_facet;

  dyn_proxy_facet(dyn_proxy_facet const& o) : obs_base(o) {}
  dyn_proxy_facet(dyn_proxy_facet&& o) noexcept : obs_base(std::move(o)) {}

  explicit dyn_proxy_facet(adopt_obs_t, Obs buf)
      : obs_base(adopt_obs, std::move(buf)) {}

  explicit dyn_proxy_facet(proxy_t const& p)
      : obs_base(static_cast<obs_base const&>(p)) {}

  explicit dyn_proxy_facet(proxy_t&& p) noexcept
      : obs_base(static_cast<obs_base&&>(p)) {}

  friend struct dyn_proxy_facet<Trait, Const, SrcP, false>;
};

template <template <class> class Trait, class SrcP>
struct dyn_rproxy : dyn_proxy_facet<Trait, false, SrcP, true> {
  using base = dyn_proxy_facet<Trait, false, SrcP, true>;
  using base::base;
  using base::operator=;
};
template <template <class> class Trait, class SrcP>
struct dyn_c_rproxy : dyn_proxy_facet<Trait, true, SrcP, true> {
  using base = dyn_proxy_facet<Trait, true, SrcP, true>;
  using base::base;
  using base::operator=;
};

template <template <class> class Trait, class SrcP>
struct dyn_proxy : dyn_proxy_facet<Trait, false, SrcP, false> {
  using base = dyn_proxy_facet<Trait, false, SrcP, false>;
  using base::base;
  using base::operator=;
  dyn_proxy(dyn_proxy const&) = default;
  dyn_proxy(dyn_proxy&&) = default;
};
template <template <class> class Trait, class SrcP>
struct dyn_c_proxy : dyn_proxy_facet<Trait, true, SrcP, false> {
  using base = dyn_proxy_facet<Trait, true, SrcP, false>;
  using typename base::Obs;
  using base::base;
  using base::operator=;
  dyn_c_proxy(dyn_c_proxy const&) = default;
  dyn_c_proxy(dyn_c_proxy&&) = delete;
  dyn_c_proxy(dyn_proxy<Trait, SrcP> const& p)
      : base(adopt_obs, Obs(p.buf_)) {}
  dyn_c_proxy(dyn_proxy<Trait, SrcP>&& p) noexcept
      : base(adopt_obs, Obs(p.buf_)) {}
};

template <template <class> class Trait>
struct dyn_obs_proxy : dyn_obs_proxy_facet<Trait, false> {
  using base = dyn_obs_proxy_facet<Trait, false>;
  using base::base;
  using base::operator=;
};
template <template <class> class Trait>
struct dyn_c_obs_proxy : dyn_obs_proxy_facet<Trait, true> {
  using base = dyn_obs_proxy_facet<Trait, true>;
  using base::base;
  using base::operator=;
};

template <class T>
inline constexpr bool is_dyn_proxy_facet_v = false;
template <template <class> class Trait, bool Const, class SrcP, bool Rvalue>
inline constexpr bool
    is_dyn_proxy_facet_v<dyn_proxy_facet<Trait, Const, SrcP, Rvalue>> = true;

template <class T>
inline constexpr bool is_dyn_rproxy_v = false;
template <template <class> class Trait, class SrcP>
inline constexpr bool is_dyn_rproxy_v<dyn_rproxy<Trait, SrcP>> = true;
template <template <class> class Trait, class SrcP>
inline constexpr bool is_dyn_rproxy_v<dyn_c_rproxy<Trait, SrcP>> = true;

template <class T>
inline constexpr bool is_dyn_lproxy_v = false;
template <template <class> class Trait, class SrcP>
inline constexpr bool is_dyn_lproxy_v<dyn_proxy<Trait, SrcP>> = true;
template <template <class> class Trait, class SrcP>
inline constexpr bool is_dyn_lproxy_v<dyn_c_proxy<Trait, SrcP>> = true;

template <template <class> class Trait, class SrcP>
dyn_rproxy(dyn_proxy<Trait, SrcP> const&) -> dyn_rproxy<Trait, SrcP>;
template <template <class> class Trait, class SrcP>
dyn_rproxy(dyn_proxy<Trait, SrcP>&&) -> dyn_rproxy<Trait, SrcP>;
template <template <class> class Trait, class SrcP>
dyn_proxy(dyn_rproxy<Trait, SrcP> const&) -> dyn_proxy<Trait, SrcP>;
template <template <class> class Trait, class SrcP>
dyn_proxy(dyn_rproxy<Trait, SrcP>&&) -> dyn_proxy<Trait, SrcP>;

template <template <class> class Trait, class SrcP>
dyn_c_rproxy(dyn_c_proxy<Trait, SrcP> const&) -> dyn_c_rproxy<Trait, SrcP>;
template <template <class> class Trait, class SrcP>
dyn_c_rproxy(dyn_c_proxy<Trait, SrcP>&&) -> dyn_c_rproxy<Trait, SrcP>;
template <template <class> class Trait, class SrcP>
dyn_c_proxy(dyn_c_rproxy<Trait, SrcP> const&) -> dyn_c_proxy<Trait, SrcP>;
template <template <class> class Trait, class SrcP>
dyn_c_proxy(dyn_c_rproxy<Trait, SrcP>&&) -> dyn_c_proxy<Trait, SrcP>;

template <template <class> class Trait, class SrcP>
dyn_c_proxy(dyn_proxy<Trait, SrcP> const&) -> dyn_c_proxy<Trait, SrcP>;
template <template <class> class Trait, class SrcP>
dyn_c_proxy(dyn_proxy<Trait, SrcP>&&) -> dyn_c_proxy<Trait, SrcP>;

}  // namespace detail

ASF_END_NAMESPACE
