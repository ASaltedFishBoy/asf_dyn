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

// Dispose kind of an erased Ptr. One marker, three states:
//   observer            -- no ownership (default when kind is absent)
//   vt_destroy          -- release runs vt[-1] destroy, then the pointer drops
//                          storage
//   deleter             -- release does not run destroy; constructing the Ptr
//                          is P(raw, d) where d destroys then frees
// A missing trait, or a missing / unrecognized kind, is observer.
// pointer_trait has only those two hooks: the kind tag, and optionally
// release(Handle&) to replace the default vt_destroy behavior. The library
// passes the handle (*this); if that call is valid, it is used.
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

  void operator()(void* p) const noexcept {
    if (!p)
      return;
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
//   op_trivial       memcpy, or no payload destructor
//   op_unsupported   special member is deleted
//   op_stored        a function pointer kept in the object
//   op_unset         not chosen yet
//   op_from_trait    internal: cache_dyn uses Trait op_construct / vt[-1] destroy;
//                    no type and no function pointer are stored in the Ptr
//   otherwise        a callable for that slot:
//                    copy (void*, void const*), move (void*, void*), destroy (void*)
//   op_copy_of<T> / op_move_of<T> / op_destroy_of<T>  T's own special member
struct op_trivial {};
struct op_unsupported {};
struct op_stored {};
struct op_unset {};
struct op_from_trait {
  explicit op_from_trait() = default;
};

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

template <class Copy = op_unset, class Move = op_unset,
          class Destroy = op_unset>
struct cache_ops {
  static_assert(!std::is_same_v<Destroy, op_unsupported>,
                "cache_ops destroy cannot be op_unsupported");
  using copy_type = Copy;
  using move_type = Move;
  using destroy_type = Destroy;
};

namespace detail {
// Erased ops for cache_dyn: behavior comes from Trait, not from a stored type.
using cache_ops_erased =
    cache_ops<op_from_trait, op_from_trait, op_from_trait>;
}

template <class T>
inline constexpr bool cache_op_trivial_v = std::is_same_v<T, op_trivial>;
template <class T>
inline constexpr bool cache_op_unsupported_v =
    std::is_same_v<T, op_unsupported>;
template <class T>
inline constexpr bool cache_op_stored_v = std::is_same_v<T, op_stored>;
template <class T>
inline constexpr bool cache_op_unset_v = std::is_same_v<T, op_unset>;
template <class T>
inline constexpr bool cache_op_from_trait_v = std::is_same_v<T, op_from_trait>;
template <class T>
inline constexpr bool cache_op_fixed_v =
    !cache_op_trivial_v<T> && !cache_op_unsupported_v<T> &&
    !cache_op_stored_v<T> && !cache_op_unset_v<T> &&
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
  else if constexpr (cache_op_stored_v<Op>)
    return 1;
  else if constexpr (cache_op_from_trait_v<Op>)
    return 2;
  else if constexpr (cache_op_trivial_v<Op>)
    return 3;
  else if constexpr (std::invocable<Op, void*, void const*>)
    return 2;
  else
    return 4;
}
template <class Op, class T>
consteval int cache_move_request_rank() {
  if constexpr (cache_op_unsupported_v<Op>)
    return 0;
  else if constexpr (cache_op_stored_v<Op>)
    return 1;
  else if constexpr (cache_op_from_trait_v<Op>)
    return 2;
  else if constexpr (cache_op_trivial_v<Op>)
    return 3;
  else if constexpr (std::invocable<Op, void*, void*>)
    return 2;
  else
    return 4;
}
template <class Op, class T>
consteval int cache_destroy_request_rank() {
  if constexpr (cache_op_unsupported_v<Op>)
    return 0;
  else if constexpr (cache_op_stored_v<Op>)
    return 1;
  else if constexpr (cache_op_from_trait_v<Op>)
    return 2;
  else if constexpr (cache_op_trivial_v<Op>)
    return 3;
  else if constexpr (std::invocable<Op, void*>)
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
consteval bool cache_ops_can_weaken() {
  return cache_op_can_weaken(
             cache_copy_request_rank<typename Ops::copy_type, T>(),
             cache_copy_natural_rank<T>()) &&
         cache_op_can_weaken(
             cache_move_request_rank<typename Ops::move_type, T>(),
             cache_move_natural_rank<T>()) &&
         cache_op_can_weaken(
             cache_destroy_request_rank<typename Ops::destroy_type, T>(),
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
template <class T>
using cache_ops_of =
    cache_ops<cache_copy_of<T>, cache_move_of<T>, cache_destroy_of<T>>;

template <class T, class Ops>
struct resolve_cache_ops {
  static_assert(std::is_destructible_v<T>,
                "cache_ops requires a destructible type");
  static_assert(cache_ops_can_weaken<T, Ops>(),
                "cache_ops may only weaken T's copy, move, and destroy");
  using type = Ops;
};
template <class T>
struct resolve_cache_ops<T, cache_ops<op_unset, op_unset, op_unset>> {
  static_assert(std::is_destructible_v<T>,
                "cache_ops requires a destructible type");
  using type = cache_ops_of<T>;
};
template <class T, class Ops>
using resolve_cache_ops_t = typename resolve_cache_ops<T, Ops>::type;

template <class Op>
consteval bool cache_op_can_store_copy() {
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
  else if constexpr (cache_op_trivial_v<Op> || cache_op_stored_v<Op>)
    return true;
  else
    return std::invocable<Op, void*, void const*>;
}
template <class Op>
consteval bool cache_op_can_store_move() {
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
  else if constexpr (cache_op_trivial_v<Op> || cache_op_stored_v<Op>)
    return true;
  else
    return std::invocable<Op, void*, void*>;
}
template <class Op>
consteval bool cache_op_can_store_destroy() {
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
  else if constexpr (cache_op_trivial_v<Op> || cache_op_stored_v<Op>)
    return true;
  else
    return std::invocable<Op, void*>;
}

// Same rule on each slot: the Trait chain can do it -> op_from_trait;
// otherwise trivial stays trivial; otherwise a usable source op becomes
// op_stored. Copy/move may stay stripped. Destroy is never stripped.
template <class SrcOp, bool TraitCan>
using cache_dyn_erase_copy = std::conditional_t<
    cache_op_unsupported_v<SrcOp>, op_unsupported,
    std::conditional_t<
        TraitCan, op_from_trait,
        std::conditional_t<
            cache_op_trivial_v<SrcOp>, op_trivial,
            std::conditional_t<cache_op_can_store_copy<SrcOp>(), op_stored,
                               op_unsupported>>>>;
template <class SrcOp, bool TraitCan>
using cache_dyn_erase_move = std::conditional_t<
    cache_op_unsupported_v<SrcOp>, op_unsupported,
    std::conditional_t<
        TraitCan, op_from_trait,
        std::conditional_t<
            cache_op_trivial_v<SrcOp>, op_trivial,
            std::conditional_t<cache_op_can_store_move<SrcOp>(), op_stored,
                               op_unsupported>>>>;
template <class SrcOp, bool TraitCan>
using cache_dyn_erase_destroy = std::conditional_t<
    TraitCan, op_from_trait,
    std::conditional_t<cache_op_trivial_v<SrcOp>, op_trivial, op_stored>>;

template <template <class> class Trait, class Ops>
using cache_ops_erase_for_trait = cache_ops<
    cache_dyn_erase_copy<typename Ops::copy_type,
                         detail::chain_has_dyn_copy_construct<Trait>()>,
    cache_dyn_erase_move<typename Ops::move_type,
                         detail::chain_has_dyn_move_construct<Trait>()>,
    cache_dyn_erase_destroy<typename Ops::destroy_type, true>>;

template <template <class> class Trait, class T>
using cache_ops_from_trait_or_t = cache_ops_erase_for_trait<Trait, cache_ops_of<T>>;

template <template <class> class Trait>
using cache_ops_from_trait = cache_ops<
    std::conditional_t<detail::chain_has_dyn_copy_construct<Trait>(),
                       op_from_trait, op_unsupported>,
    std::conditional_t<detail::chain_has_dyn_move_construct<Trait>(),
                       op_from_trait, op_unsupported>,
    op_from_trait>;

// Source ops must weaken T. Erased result may use Trait and/or stored fns.
// If the Trait chain has no lifetime ops, the source must still supply them
// (stored) for each live operation it claims.
template <template <class> class Trait, class T, class Ops>
struct resolve_cache_dyn_ops {
  static_assert(std::is_destructible_v<T>,
                "cache_ops requires a destructible type");
  static_assert(cache_ops_can_weaken<T, Ops>(),
                "cache_ops may only weaken T's copy, move, and destroy");
  using type = cache_ops_erase_for_trait<Trait, Ops>;
};
template <template <class> class Trait, class T>
struct resolve_cache_dyn_ops<Trait, T, cache_ops<op_unset, op_unset, op_unset>> {
  static_assert(std::is_destructible_v<T>,
                "cache_ops requires a destructible type");
  using type = cache_ops_from_trait_or_t<Trait, T>;
};
template <template <class> class Trait, class T, class Ops>
using resolve_cache_dyn_ops_t =
    typename resolve_cache_dyn_ops<Trait, T, Ops>::type;

template <class Op>
consteval bool cache_op_is_erased_slot() {
  return cache_op_from_trait_v<Op> || cache_op_stored_v<Op> ||
         cache_op_trivial_v<Op> || cache_op_unsupported_v<Op>;
}

// Erased form: no payload type left in the ops (from_trait / stored / trivial).
template <class Ops>
inline constexpr bool cache_ops_is_erased_v =
    cache_op_is_erased_slot<typename Ops::copy_type>() &&
    cache_op_is_erased_slot<typename Ops::move_type>() &&
    cache_op_is_erased_slot<typename Ops::destroy_type>() &&
    !(cache_op_unsupported_v<typename Ops::copy_type> &&
      cache_op_unsupported_v<typename Ops::move_type> &&
      cache_op_unsupported_v<typename Ops::destroy_type>);

template <class Op>
consteval bool cache_copy_live() {
  // op_from_trait: Ptr itself does not copy; dyn uses Trait op_construct.
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
  else if constexpr (cache_op_trivial_v<Op> || cache_op_stored_v<Op>)
    return true;
  else
    return std::invocable<Op, void*, void const*>;
}
template <class Op>
consteval bool cache_move_live() {
  // op_from_trait: Ptr itself does not move; dyn relocates via Trait.
  if constexpr (cache_op_unsupported_v<Op> || cache_op_unset_v<Op> ||
                cache_op_from_trait_v<Op>)
    return false;
  else if constexpr (cache_op_trivial_v<Op> || cache_op_stored_v<Op>)
    return true;
  else
    return std::invocable<Op, void*, void*>;
}
template <class Op>
consteval bool cache_destroy_live() {
  // op_from_trait: hold clears only; dyn runs vt[-1] destroy.
  if constexpr (cache_op_unsupported_v<Op>)
    return false;
  else
    return true;
}

// Typed / fixed callable -> function pointer for an op_stored slot.
namespace detail {
template <class Op>
void cache_invoke_copy(void* d, void const* s) {
  Op{}(d, s);
}
template <class Op>
void cache_invoke_move(void* d, void* s) {
  Op{}(d, s);
}
template <class Op>
void cache_invoke_destroy(void* p) {
  Op{}(p);
}
}

// Erase typed ops without a Trait: each usable slot becomes stored (or stays
// trivial). Used for cache_ptr -> erased cache_ptr conversion.
template <class Ops>
using cache_ops_erase_typed = cache_ops<
    cache_dyn_erase_copy<typename Ops::copy_type, false>,
    cache_dyn_erase_move<typename Ops::move_type, false>,
    cache_dyn_erase_destroy<typename Ops::destroy_type, false>>;

template <std::size_t Size, std::size_t Align = alignof(std::max_align_t)>
class cache_buf {
  static_assert(Size > 0, "cache_buf Size must be > 0");
  static_assert(Align > 0 && (Align & (Align - 1)) == 0,
                "cache_buf Align must be a power of two");

  alignas(Align) std::byte buf_[Size]{};
  void* p_{};

 protected:
  void adopt_bytes(cache_buf const& o) {
    if (o.p_ == nullptr) {
      p_ = nullptr;
      return;
    }
    std::memcpy(buf_, o.buf_, Size);
    p_ = static_cast<void*>(buf_);
  }

  void adopt_bytes(cache_buf&& o) {
    adopt_bytes(static_cast<cache_buf const&>(o));
    o.p_ = nullptr;
  }

  void clear() noexcept { p_ = nullptr; }

 public:
  static constexpr std::size_t cache_capacity = Size;
  static constexpr std::size_t cache_alignment = Align;

  cache_buf() noexcept = default;
  cache_buf(std::nullptr_t) noexcept {}
  cache_buf(cache_buf const&) = default;
  cache_buf& operator=(cache_buf const&) = default;
  cache_buf(cache_buf&&) = default;
  cache_buf& operator=(cache_buf&&) = default;

  void* get() const noexcept { return p_; }

  void reset(void* p = nullptr) noexcept {
    (void)p;
    p_ = nullptr;
  }

  // Engage own buf for placement; nbytes/align must fit Size/Align.
  void* emplace(std::size_t nbytes, std::size_t align) {
    assert(nbytes <= Size && "cache_buf capacity too small for Storage");
    assert(align <= Align && "cache_buf alignment too small for Storage");
    (void)nbytes;
    (void)align;
    p_ = static_cast<void*>(buf_);
    return p_;
  }

  explicit operator bool() const noexcept { return p_ != nullptr; }

  friend bool operator==(cache_buf const& a, cache_buf const& b) noexcept {
    return a.p_ == b.p_;
  }
  friend bool operator==(cache_buf const& a, std::nullptr_t) noexcept {
    return a.p_ == nullptr;
  }
};

template <class Copy>
struct cache_copy_slot {};
template <>
struct cache_copy_slot<op_stored> {
  void (*copy)(void* dst, void const* src) = nullptr;
};

template <class Move>
struct cache_move_slot {};
template <>
struct cache_move_slot<op_stored> {
  void (*move)(void* dst, void* src) = nullptr;
};

template <class Destroy>
struct cache_destroy_slot {};
template <>
struct cache_destroy_slot<op_stored> {
  void (*destroy)(void*) = nullptr;
};

// Middle layer: copy, move, and destroy follow Ops.
template <std::size_t Size, std::size_t Align, class Ops>
class cache_hold : public cache_buf<Size, Align>,
                   private cache_copy_slot<typename Ops::copy_type>,
                   private cache_move_slot<typename Ops::move_type>,
                   private cache_destroy_slot<typename Ops::destroy_type> {
  template <std::size_t, std::size_t, class>
  friend class cache_hold;

  using buf = cache_buf<Size, Align>;
  using Copy = typename Ops::copy_type;
  using Move = typename Ops::move_type;
  using Destroy = typename Ops::destroy_type;

  void copy_payload(cache_hold const& o) {
    if (!o)
      return;
    if constexpr (cache_op_trivial_v<Copy>) {
      this->adopt_bytes(static_cast<buf const&>(o));
    } else {
      void* dst = this->emplace(Size, Align);
      if constexpr (cache_op_stored_v<Copy>)
        this->copy(dst, o.get());
      else
        Copy{}(dst, o.get());
    }
  }

  void move_payload(cache_hold& o) {
    if (!o)
      return;
    if constexpr (cache_op_trivial_v<Move>) {
      this->adopt_bytes(static_cast<buf&&>(o));
    } else {
      void* dst = this->emplace(Size, Align);
      if constexpr (cache_op_stored_v<Move>)
        this->move(dst, o.get());
      else
        Move{}(dst, o.get());
      o.clear();
    }
  }

 protected:
  void destroy_payload() {
    if (!*this)
      return;
    if constexpr (cache_op_from_trait_v<Destroy>) {
      // vt[-1] destroy already ran (or will run) through the dyn handle.
    } else if constexpr (cache_op_stored_v<Destroy>) {
      if (this->destroy)
        this->destroy(this->get());
    } else if constexpr (cache_op_fixed_v<Destroy>) {
      Destroy{}(this->get());
    }
    this->clear();
  }

 public:
  using ops_type = Ops;

  cache_hold() noexcept = default;
  cache_hold(std::nullptr_t) noexcept : buf(nullptr) {}

  cache_hold(cache_hold const&)
    requires(!cache_copy_live<Copy>())
  = delete;
  cache_hold(cache_hold const& o)
    requires(cache_copy_live<Copy>())
      : buf(),
        cache_copy_slot<Copy>(static_cast<cache_copy_slot<Copy> const&>(o)),
        cache_move_slot<Move>(static_cast<cache_move_slot<Move> const&>(o)),
        cache_destroy_slot<Destroy>(
            static_cast<cache_destroy_slot<Destroy> const&>(o)) {
    copy_payload(o);
  }

  cache_hold& operator=(cache_hold const&)
    requires(!cache_copy_live<Copy>())
  = delete;
  cache_hold& operator=(cache_hold const& o)
    requires(cache_copy_live<Copy>()) {
    if (this != &o) {
      static_cast<cache_copy_slot<Copy>&>(*this) = o;
      static_cast<cache_move_slot<Move>&>(*this) = o;
      static_cast<cache_destroy_slot<Destroy>&>(*this) = o;
      destroy_payload();
      copy_payload(o);
    }
    return *this;
  }

  cache_hold(cache_hold&&)
    requires(!cache_move_live<Move>())
  = delete;
  cache_hold(cache_hold&& o)
    requires(cache_move_live<Move>())
      : buf(),
        cache_copy_slot<Copy>(static_cast<cache_copy_slot<Copy> const&>(o)),
        cache_move_slot<Move>(static_cast<cache_move_slot<Move> const&>(o)),
        cache_destroy_slot<Destroy>(
            static_cast<cache_destroy_slot<Destroy> const&>(o)) {
    move_payload(o);
  }

  cache_hold& operator=(cache_hold&&)
    requires(!cache_move_live<Move>())
  = delete;
  cache_hold& operator=(cache_hold&& o)
    requires(cache_move_live<Move>()) {
    if (this != &o) {
      static_cast<cache_copy_slot<Copy>&>(*this) = o;
      static_cast<cache_move_slot<Move>&>(*this) = o;
      static_cast<cache_destroy_slot<Destroy>&>(*this) = o;
      destroy_payload();
      move_payload(o);
    }
    return *this;
  }

  ~cache_hold()
    requires(!cache_destroy_live<Destroy>())
  = delete;
  ~cache_hold()
    requires(cache_destroy_live<Destroy>()) {
    destroy_payload();
  }

  // Fill op_stored slots from a typed / fixed source ops definition.
  template <class SrcOps>
  void install_typed_ops() {
    if constexpr (cache_op_stored_v<Copy>) {
      using Src = typename SrcOps::copy_type;
      static_assert(cache_op_can_store_copy<Src>(),
                    "cannot store copy fn from source ops");
      this->copy = &detail::cache_invoke_copy<Src>;
    }
    if constexpr (cache_op_stored_v<Move>) {
      using Src = typename SrcOps::move_type;
      static_assert(cache_op_can_store_move<Src>(),
                    "cannot store move fn from source ops");
      this->move = &detail::cache_invoke_move<Src>;
    }
    if constexpr (cache_op_stored_v<Destroy>) {
      using Src = typename SrcOps::destroy_type;
      static_assert(cache_op_can_store_destroy<Src>(),
                    "cannot store destroy fn from source ops");
      this->destroy = &detail::cache_invoke_destroy<Src>;
    }
  }

  template <class SrcOps>
  void install_typed_ops_from(cache_hold<Size, Align, SrcOps> const& src) {
    if constexpr (cache_op_stored_v<Copy>) {
      if constexpr (cache_op_stored_v<typename SrcOps::copy_type>)
        this->copy = static_cast<cache_copy_slot<typename SrcOps::copy_type> const&>(
                         src)
                         .copy;
      else
        this->copy = &detail::cache_invoke_copy<typename SrcOps::copy_type>;
    }
    if constexpr (cache_op_stored_v<Move>) {
      if constexpr (cache_op_stored_v<typename SrcOps::move_type>)
        this->move = static_cast<cache_move_slot<typename SrcOps::move_type> const&>(
                         src)
                         .move;
      else
        this->move = &detail::cache_invoke_move<typename SrcOps::move_type>;
    }
    if constexpr (cache_op_stored_v<Destroy>) {
      if constexpr (cache_op_stored_v<typename SrcOps::destroy_type>)
        this->destroy =
            static_cast<cache_destroy_slot<typename SrcOps::destroy_type> const&>(
                src)
                .destroy;
      else
        this->destroy = &detail::cache_invoke_destroy<typename SrcOps::destroy_type>;
    }
  }
};

// Erased pointer over a traveling buffer.
template <std::size_t Size, std::size_t Align = alignof(std::max_align_t),
          class Ops = cache_ops<>>
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

  hold const& as_hold() const noexcept { return *this; }
  hold& as_hold() noexcept { return *this; }

  void abandon() noexcept { this->clear(); }
  void destroy_held() { this->destroy_payload(); }

  template <class SrcOps>
  void install_typed_ops() {
    hold::template install_typed_ops<SrcOps>();
  }

  void reset(void* p = nullptr) noexcept {
    if constexpr (cache_op_stored_v<typename Ops::destroy_type>)
      this->destroy_payload();
    hold::reset(p);
  }

 public:
  using element_type = void;
  using ops_type = Ops;
  using cache_ptr_tag = void;
  using hold::cache_capacity;
  using hold::cache_alignment;
  using hold::operator bool;

  static constexpr std::size_t size = Size;
  static constexpr std::size_t align = Align;

  cache_ptr() noexcept = default;
  cache_ptr(std::nullptr_t) noexcept : hold(nullptr) {}

  cache_ptr(cache_ptr const&) = default;
  cache_ptr& operator=(cache_ptr const&) = default;
  cache_ptr(cache_ptr&&) = default;
  cache_ptr& operator=(cache_ptr&&) = default;
  ~cache_ptr() = default;

  // Typed ops -> erased: each supported slot becomes stored (or stays trivial).
  // Payload is transferred by that slot, not by a raw byte copy.
  template <class FromOps>
    requires(!std::is_same_v<Ops, FromOps> &&
             std::is_same_v<Ops, cache_ops_erase_typed<FromOps>>)
  cache_ptr(cache_ptr<Size, Align, FromOps> const& o) : hold() {
    this->install_typed_ops_from(o.as_hold());
    if (!o)
      return;
    using Src = typename FromOps::copy_type;
    if constexpr (cache_op_trivial_v<Src>) {
      this->adopt_bytes(
          static_cast<cache_buf<Size, Align> const&>(o.as_hold()));
    } else {
      void* dst = this->emplace(Size, Align);
      if constexpr (cache_op_stored_v<typename Ops::copy_type>)
        this->copy(dst, o.get());
      else
        detail::cache_invoke_copy<Src>(dst, o.get());
    }
  }

  template <class FromOps>
    requires(!std::is_same_v<Ops, FromOps> &&
             std::is_same_v<Ops, cache_ops_erase_typed<FromOps>>)
  cache_ptr(cache_ptr<Size, Align, FromOps>&& o) : hold() {
    this->install_typed_ops_from(o.as_hold());
    if (!o)
      return;
    using Src = typename FromOps::move_type;
    if constexpr (cache_op_trivial_v<Src>) {
      this->adopt_bytes(static_cast<cache_buf<Size, Align>&&>(o.as_hold()));
    } else {
      void* dst = this->emplace(Size, Align);
      if constexpr (cache_op_stored_v<typename Ops::move_type>)
        this->move(dst, o.get());
      else
        detail::cache_invoke_move<Src>(dst, o.get());
      o.abandon();
    }
  }

  void* get() const noexcept { return hold::get(); }

  friend bool operator==(cache_ptr const& a, cache_ptr const& b) noexcept {
    return static_cast<hold const&>(a) == static_cast<hold const&>(b);
  }
  friend bool operator==(cache_ptr const& a, std::nullptr_t) noexcept {
    return static_cast<hold const&>(a) == nullptr;
  }
};

template <class T, class Ops = cache_ops<>>
using cache_ptr_for = cache_ptr<sizeof(T), alignof(T), Ops>;

namespace detail {

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
        cache_op_trivial_v<typename Ops::copy_type> &&
        cache_op_trivial_v<typename Ops::move_type> &&
        cache_op_trivial_v<typename Ops::destroy_type>;

template <class P>
inline constexpr bool cache_ptr_erased_ops_v = false;
template <std::size_t Size, std::size_t Align, class Ops>
inline constexpr bool
    cache_ptr_erased_ops_v<cache_ptr<Size, Align, Ops>> =
        cache_ops_is_erased_v<Ops>;

template <class P>
inline constexpr bool cache_ptr_copy_live_v = false;
template <class P>
inline constexpr bool cache_ptr_move_live_v = false;
template <class P>
inline constexpr bool cache_ptr_copy_via_trait_v = false;
template <class P>
inline constexpr bool cache_ptr_move_via_trait_v = false;
template <class P>
inline constexpr bool cache_ptr_destroy_via_trait_v = false;
template <std::size_t Size, std::size_t Align, class Ops>
inline constexpr bool
    cache_ptr_copy_live_v<cache_ptr<Size, Align, Ops>> =
        cache_copy_live<typename Ops::copy_type>();
template <std::size_t Size, std::size_t Align, class Ops>
inline constexpr bool
    cache_ptr_move_live_v<cache_ptr<Size, Align, Ops>> =
        cache_move_live<typename Ops::move_type>();
template <std::size_t Size, std::size_t Align, class Ops>
inline constexpr bool
    cache_ptr_copy_via_trait_v<cache_ptr<Size, Align, Ops>> =
        cache_op_from_trait_v<typename Ops::copy_type>;
template <std::size_t Size, std::size_t Align, class Ops>
inline constexpr bool
    cache_ptr_move_via_trait_v<cache_ptr<Size, Align, Ops>> =
        cache_op_from_trait_v<typename Ops::move_type>;
template <std::size_t Size, std::size_t Align, class Ops>
inline constexpr bool
    cache_ptr_destroy_via_trait_v<cache_ptr<Size, Align, Ops>> =
        cache_op_from_trait_v<typename Ops::destroy_type>;

// cache_ptr itself supplies copy, move, and destroy (no Trait needed).
template <class P>
inline constexpr bool cache_ptr_self_lifetime_v = false;
template <std::size_t Size, std::size_t Align, class Ops>
inline constexpr bool
    cache_ptr_self_lifetime_v<cache_ptr<Size, Align, Ops>> =
        cache_copy_live<typename Ops::copy_type>() &&
        cache_move_live<typename Ops::move_type>() &&
        cache_destroy_live<typename Ops::destroy_type>() &&
        !cache_op_from_trait_v<typename Ops::copy_type> &&
        !cache_op_from_trait_v<typename Ops::move_type> &&
        !cache_op_from_trait_v<typename Ops::destroy_type>;

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
// cache_ptr's kind follows cache_ops's destroy:
//   op_unset                         no kind (observer)
//   op_stored                        deleter (destroy function pointer is in the object)
//   op_from_trait / trivial / fixed  vt_destroy
template <class Destroy>
struct cache_pointer_kind_alias {
  using type = pointer_vt_destroy_tag;
};
template <>
struct cache_pointer_kind_alias<op_stored> {
  using type = pointer_deleter_tag;
};
template <>
struct cache_pointer_kind_alias<op_unset> {
  using type = void;
};

template <std::size_t Size, std::size_t Align, class Ops>
struct pointer_trait<cache_ptr<Size, Align, Ops>> {
  using kind =
      typename cache_pointer_kind_alias<typename Ops::destroy_type>::type;
};

namespace detail {

// Deleter-kind Ptr: destroy lives in the pointer, not the handle.
template <class P>
inline constexpr bool pointer_stores_vt_destroy_v = pointer_has_deleter_v<P>;

// Downcast checks the Trait chain only when cache_ops does not itself
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

  void operator()(void* p) const noexcept {
    if (!p)
      return;
    if (destroy)
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

// unique_ptr with the library dyn_default_delete (the default-Free
// dyn_adopt_delete): adopted deleter-kind directly (no typed wrapper needed).
template <class T>
  requires(!std::is_void_v<std::remove_cv_t<T>>)
struct dyn_ptr_bind<std::unique_ptr<T, dyn_default_delete>> {
  using element_cv = T;
  using element = std::remove_cv_t<T>;
  static constexpr bool is_raw = false;
  static constexpr bool is_sptr = false;
  static constexpr bool is_wptr = false;
  static constexpr bool is_uptr = true;

  static std::unique_ptr<void, dyn_default_delete> adopt(
      std::unique_ptr<T, dyn_default_delete> p, vt_ptr vt) noexcept {
    return std::unique_ptr<void, dyn_default_delete>{
        p.release(), dyn_default_delete{dyn_vt_destroy(vt)}};
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

  ~dyn_shared_storage() {
    void* p = payload();
    if (auto fn = untag_destroy_fn(destroy))
      fn(p);
  }
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
  // Align tag in destroy low bits. Object destroy is armed after placement
  // so a throwing construct does not run vt[-1] on uninitialized payload.
  auto p = std::allocate_shared<dyn_shared_storage>(
      pa, tag_destroy_align(nullptr, alog));
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
// SliceCall: dispatch via slice_off_ + own-section indices (sub_*); else full
// dyn table. SoftUptrDestroy: empty default-uptr OK on sub. sub_dyn never
// reads the table-origin prefix (destroy / RTTI): a section
// cannot know if it sits at origin (this Trait may be a sibling).
// vt_destroy Ptr on sub_dyn only if pointer_trait::release accepts *this.
template <class Handle>
vt_ptr vt_of(Handle const& h) noexcept;

// GCC: constrained members are hard errors inside requires { h.fn() }.
// Specialize the facet away so the member does not exist.
template <class Core, bool Enable>
struct dyn_destroy_facet {};
template <class Core>
struct dyn_destroy_facet<Core, true> {
  void destroy() {
    auto& s = static_cast<Core&>(*this);
    if (ptr_get(s.obj_) != nullptr && s.vt_ != nullptr)
      dyn_vt_destroy(s.vt_)(ptr_get(s.obj_));
    ptr_reset(s.obj_);
    s.vt_ = nullptr;
    s.slice_off_ = 0;
  }
};

template <class Core, template <class> class Trait, bool Enable>
struct dyn_typeid_facet {};
#if ASF_HAS_DYN_RTTI
template <class Core, template <class> class Trait>
struct dyn_typeid_facet<Core, Trait, true> {
  const std::type_info& typeid_of() const noexcept {
    auto const& s = static_cast<Core const&>(*this);
    if (!s.vt_)
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
    return s.vt_ ? dyn_storage_size<Trait>(s.vt_) : 0;
  }
  std::size_t align() const noexcept {
    auto const& s = static_cast<Handle const&>(*this);
    return s.vt_ ? dyn_storage_align<Trait>(s.vt_) : 0;
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
  template <class H>
  friend vt_ptr vt_of(H const&) noexcept;
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

  ptr_type obj_{};
  vt_ptr vt_{};
  std::size_t slice_off_{0};

  dyn_ptr_core() noexcept = default;
  // Erased, all-trivial, or self-lifetime cache_ptr may be adopted. Any other
  // cache_ptr is filled in place from a src, or relocated from another
  // cache_dyn.
  dyn_ptr_core(ptr_type obj, vt_ptr vt,
               std::size_t slice_off = 0) noexcept
      : obj_(std::move(obj)), vt_(vt), slice_off_(slice_off) {
    static_assert(!is_cache_ptr_v<Ptr> || cache_ptr_all_trivial_v<Ptr> ||
                      cache_ptr_erased_ops_v<Ptr> ||
                      cache_ptr_self_lifetime_v<Ptr>,
                  "cache_ptr enters dyn only as erased / all-trivial / "
                  "self-lifetime ops, by storing a src, or from another "
                  "cache_dyn");
    (void)SoftUptrDestroy;
  }
  dyn_ptr_core(void* ptr, vt_ptr vt,
               std::size_t slice_off = 0) noexcept
    requires(!is_sptr_v<Ptr> && !is_wptr_v<Ptr> && !is_cache_ptr_v<Ptr> &&
             !pointer_has_deleter_v<Ptr>)
      : obj_(ptr), vt_(vt), slice_off_(slice_off) {}

  pointer_destroy_fn destroy_fn() const noexcept {
    if (ptr_get(obj_) != nullptr && vt_ != nullptr)
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
        if (ptr_get(obj_) != nullptr && vt_ != nullptr)
          dyn_vt_destroy(vt_)(ptr_get(obj_));
        ptr_reset(obj_);
      }
      vt_ = nullptr;
      slice_off_ = 0;
    } else {
      ptr_reset(obj_);
      vt_ = nullptr;
      slice_off_ = 0;
    }
  }

  // op_from_trait is the deep vt op applied as that cache slot.
  // Other live slots are the cache_ptr's own shallow copy / move.
  void end_cache_payload()
    requires(is_cache_ptr_v<Ptr>) {
    if constexpr (pointer_trait_can_release<Ptr, Derived>()) {
      pointer_trait<Ptr>::release(static_cast<Derived&>(*this));
    } else if constexpr (cache_ptr_destroy_via_trait_v<Ptr>) {
      static_assert(!SliceCall,
                    "sub_dyn vt_destroy Ptr needs "
                    "pointer_trait::release that accepts this handle");
      if (ptr_get(obj_) != nullptr && vt_ != nullptr)
        dyn_vt_destroy(vt_)(ptr_get(obj_));
      obj_.abandon();
    } else {
      obj_.destroy_held();
    }
    vt_ = nullptr;
    slice_off_ = 0;
  }

  void cache_take_copied_from(dyn_ptr_core const& o)
    requires(is_cache_ptr_v<Ptr> && cache_ptr_copy_via_trait_v<Ptr>) {
    if (ptr_get(o.obj_) == nullptr || o.vt_ == nullptr)
      return;
    const std::size_t size = dyn_storage_size<Trait>(o.vt_);
    const std::size_t align = dyn_storage_align<Trait>(o.vt_);
    void* dst = obj_.emplace(size, align);
    dyn_construct_of<Trait, false>(o.vt_)(ptr_get(o.obj_), o.vt_, dst);
    vt_ = o.vt_;
    slice_off_ = o.slice_off_;
  }

  void cache_take_moved_from(dyn_ptr_core& o)
    requires(is_cache_ptr_v<Ptr>) {
    if (ptr_get(o.obj_) == nullptr || o.vt_ == nullptr)
      return;
    if constexpr (cache_ptr_move_via_trait_v<Ptr>) {
      const std::size_t size = dyn_storage_size<Trait>(o.vt_);
      const std::size_t align = dyn_storage_align<Trait>(o.vt_);
      void* dst = obj_.emplace(size, align);
      dyn_construct_of<Trait, true>(o.vt_)(ptr_get(o.obj_), o.vt_, dst);
      vt_ = o.vt_;
      slice_off_ = o.slice_off_;
      o.dispose_owned();
    } else {
      obj_ = std::move(o.obj_);
      vt_ = std::exchange(o.vt_, nullptr);
      slice_off_ = std::exchange(o.slice_off_, 0);
    }
  }

  void* raw() const noexcept
    requires(!is_wptr_v<Ptr>) {
    if constexpr (is_sptr_v<Ptr>)
      return ptr_get(obj_);
    else
      return ptr_get(obj_);
  }

 public:
  dyn_ptr_core(dyn_ptr_core const&)
    requires(!is_cache_ptr_v<Ptr> || cache_ptr_copy_live_v<Ptr>)
  = default;
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
      end_cache_payload();
      if constexpr (cache_ptr_copy_via_trait_v<Ptr>)
        cache_take_copied_from(o);
      else {
        obj_ = o.obj_;
        vt_ = o.vt_;
        slice_off_ = o.slice_off_;
      }
    }
    return *this;
  }
  dyn_ptr_core& operator=(dyn_ptr_core const&)
    requires(is_cache_ptr_v<Ptr> && !cache_ptr_copy_live_v<Ptr> &&
             !cache_ptr_copy_via_trait_v<Ptr>)
  = delete;

  dyn_ptr_core(dyn_ptr_core&& o) noexcept
    requires(!is_cache_ptr_v<Ptr>)
      : obj_(std::move(o.obj_)), vt_(std::exchange(o.vt_, nullptr)),
        slice_off_(std::exchange(o.slice_off_, 0)) {}

  dyn_ptr_core(dyn_ptr_core&& o) noexcept
    requires(is_cache_ptr_v<Ptr> && cache_ptr_move_live_v<Ptr> &&
             !cache_ptr_move_via_trait_v<Ptr>)
      : obj_(std::move(o.obj_)), vt_(std::exchange(o.vt_, nullptr)),
        slice_off_(std::exchange(o.slice_off_, 0)) {}

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
    requires(!is_cache_ptr_v<Ptr> && slice_proxy_ok()) {
    if (this != &o) {
      if constexpr (vt_destroys_on_dispose_v<Ptr>)
        dispose_owned();
      else {
        ptr_reset(obj_);
        vt_ = nullptr;
        slice_off_ = 0;
      }
      obj_ = std::move(o.obj_);
      vt_ = std::exchange(o.vt_, nullptr);
      slice_off_ = std::exchange(o.slice_off_, 0);
    }
    return *this;
  }
  dyn_ptr_core& operator=(dyn_ptr_core&& o)
    requires(is_cache_ptr_v<Ptr> && slice_proxy_ok() &&
             (cache_ptr_move_live_v<Ptr> || cache_ptr_move_via_trait_v<Ptr>)) {
    if (this != &o) {
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
      ptr_reset(obj_);
      vt_ = nullptr;
      slice_off_ = 0;
    }
  }

  Derived& operator=(std::nullptr_t) noexcept
    requires(slice_proxy_ok()) {
    reset();
    return *this;
  }

  std::conditional_t<Const, const void*, void*> ptr() const noexcept
    requires(!is_wptr_v<Ptr>) {
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
    return a.ptr() == b.ptr();
  }

 protected:
  vt_ptr vt() const noexcept { return vt_; }

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
          ptr_get(obj_), vt_, slice_off_,
          std::forward<decltype(args)>(args)...);
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
          ptr_get(obj_), vt_, slice_off_,
          std::forward<decltype(args)>(args)...);
    else
      return handle_call<Trait, Method, true, false>(
          raw(), vt_, nullptr, nullptr,
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
          ptr_get(obj_), vt_, slice_off_, std::forward<Args>(args)...);
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
          ptr_get(obj_), vt_, slice_off_, std::forward<Args>(args)...);
    else
      return handle_call<Trait, method, true, false>(
          raw(), vt_, nullptr, nullptr, std::forward<Args>(args)...);
  }

};

template <class Handle>
vt_ptr vt_of(Handle const& h) noexcept {
  return h.vt();
}

// Same-vt view on Layout's table. Mutable and const are separate types
// (Layout is a template template, so it cannot carry const). Public handles
// dyn_view / c_dyn_view follow this detail block.
template <template <class> class Layout, template <class> class View>
struct dyn_view_fn_box {
  struct data;
  consteval {
    std::meta::define_aggregate(
        ^^data,
        dyn_like_fn_specs<View, false>(^^dyn_view<Layout, View>));
  }
};

template <template <class> class Layout, template <class> class View>
struct c_dyn_view_fn_box {
  struct data;
  consteval {
    std::meta::define_aggregate(
        ^^data,
        dyn_like_fn_specs<View, true>(^^c_dyn_view<Layout, View>));
  }
};

template <template <class> class Layout, template <class> class View, bool Const,
          class Derived>
class dyn_view_body {
  using fns_type = std::conditional_t<
      Const, typename c_dyn_view_fn_box<Layout, View>::data,
      typename dyn_view_fn_box<Layout, View>::data>;
  void* ptr_{};
  vt_ptr vt_{};

  template <template <class> class, template <class> class>
  friend class dyn_view;
  template <template <class> class, template <class> class>
  friend class c_dyn_view;

 protected:
  dyn_view_body(void* ptr, vt_ptr vt) noexcept
      : ptr_(ptr), vt_(vt) {}

 public:
  using proxy_t = fn_proxy<Derived, fns_type>;
  using rproxy_t = fn_rproxy<Derived, fns_type>;
  using c_proxy_t = std::conditional_t<
      Const, proxy_t,
      fn_proxy<c_dyn_view<Layout, View>,
               typename c_dyn_view_fn_box<Layout, View>::data>>;

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
    return as_type<To>(ptr_, vt_);
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
      return c_dyn_view<Layout, View>(ptr_, vt_).proxy();
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

// Public same-vt views (observer only; no get/ptr/bool, no convert).
// Constructed by dyn_handle_base::as and by dyn_view_body itself.
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

  dyn_view(void* ptr, detail::vt_ptr vt) noexcept : base(ptr, vt) {}
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

  c_dyn_view(void* ptr, detail::vt_ptr vt) noexcept : base(ptr, vt) {}
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
    : public detail::dyn_ptr_core<sub_dyn<Trait, Ptr>, Trait,
                                  false, true, true, Ptr> {
  using base =
      detail::dyn_ptr_core<sub_dyn, Trait, false, true, true, Ptr>;
  using typename base::ptr_type;
  using vt_ptr = detail::vt_ptr;

  template <template <class> class T, class P>
  friend class c_sub_dyn;
  template <template <class> class T, class P>
  friend class sub_dyn;

 public:
  using base::base;
  using base::ptr;
  using base::reset;

  // Trait-keyed observer proxy (always sub_dyn<Trait> / void*) -- like dyn_proxy.
  using proxy_t = detail::fn_proxy<sub_dyn<Trait>,
                                   typename detail::sub_dyn_fn_box<Trait, false>::data>;

  sub_dyn() noexcept = default;
  sub_dyn(std::nullptr_t) noexcept : sub_dyn() {}
  sub_dyn(const sub_dyn&) = default;
  sub_dyn(sub_dyn&&) = default;
  sub_dyn& operator=(const sub_dyn&) = default;
  sub_dyn& operator=(sub_dyn&&) = default;

  template <template <class> class Final>
    requires(detail::can_sub_slice<Final, Trait>())
  explicit sub_dyn(dyn<Final, Ptr> const& handle) noexcept
      : base(handle.obj_, handle.vt_,
             detail::sub_slice_offset<Final, Trait>()) {}

  template <template <class> class Final>
    requires(detail::can_sub_slice<Final, Trait>())
  explicit sub_dyn(dyn<Final, Ptr>&& handle) noexcept
      : base(std::move(handle.obj_), handle.vt_,
             detail::sub_slice_offset<Final, Trait>()) {
    handle.vt_ = nullptr;
  }

  proxy_t operator->()
    requires(!detail::is_wptr_v<Ptr>) {
    auto obs = sub_dyn<Trait>::observe(this->raw(), this->vt_, this->slice_off_);
    return proxy_t{&obs};
  }
  proxy_t operator*()
    requires(!detail::is_wptr_v<Ptr>) {
    auto obs = sub_dyn<Trait>::observe(this->raw(), this->vt_, this->slice_off_);
    return proxy_t{&obs};
  }

  sub_dyn<Trait, std::shared_ptr<void>> lock() const
    requires(detail::is_wptr_v<Ptr>) {
    auto p = this->obj_.lock();
    if (!p)
      return {};
    return sub_dyn<Trait, std::shared_ptr<void>>::from_ptr(
        std::move(p), this->vt_, this->slice_off_);
  }

 private:
  // Internal observation factories (proxy/lock use only; not public API).
  static sub_dyn observe(void* p, vt_ptr vt,
                         std::size_t slice_off = 0) noexcept
    requires(!detail::is_sptr_v<Ptr> &&
             !detail::is_wptr_v<Ptr>) {
    return sub_dyn{ptr_type{p}, vt, slice_off};
  }

  static sub_dyn from_ptr(ptr_type p, vt_ptr vt,
                          std::size_t slice_off = 0) noexcept {
    return sub_dyn{std::move(p), vt, slice_off};
  }
};

template <template <class> class Trait, class Ptr>
class c_sub_dyn
    : public detail::dyn_ptr_core<c_sub_dyn<Trait, Ptr>, Trait,
                                  true, true, true, Ptr> {
  using base =
      detail::dyn_ptr_core<c_sub_dyn, Trait, true, true, true, Ptr>;
  using typename base::ptr_type;
  using vt_ptr = detail::vt_ptr;

 public:
  using base::base;
  using base::ptr;
  using base::reset;

  using proxy_t =
      detail::fn_proxy<c_sub_dyn<Trait>,
                       typename detail::sub_dyn_fn_box<Trait, true>::data>;

  c_sub_dyn() noexcept = default;
  c_sub_dyn(std::nullptr_t) noexcept : c_sub_dyn() {}
  c_sub_dyn(const c_sub_dyn&) = default;
  c_sub_dyn(c_sub_dyn&&) = default;
  c_sub_dyn& operator=(const c_sub_dyn&) = default;
  c_sub_dyn& operator=(c_sub_dyn&&) = default;

  explicit c_sub_dyn(sub_dyn<Trait, Ptr> const& w) noexcept
      : base(w.obj_, w.vt_, w.slice_off_) {}

  explicit c_sub_dyn(sub_dyn<Trait, Ptr>&& w) noexcept
      : base(std::move(w.obj_), w.vt_, w.slice_off_) {
    w.vt_ = nullptr;
    w.slice_off_ = 0;
  }

  template <class SrcP>
    requires(detail::dyn_ptr_rebind_from<Ptr, SrcP, SrcP const&>)
  explicit(!std::is_convertible_v<SrcP const&, Ptr>)
  c_sub_dyn(sub_dyn<Trait, SrcP> const& w)
      noexcept(std::is_nothrow_constructible_v<Ptr, SrcP const&>)
      : base(ptr_type(w.obj_), w.vt_, w.slice_off_) {}

  template <class SrcP>
    requires(detail::dyn_ptr_rebind_from<Ptr, SrcP, SrcP &&>)
  explicit(!std::is_convertible_v<SrcP &&, Ptr>)
  c_sub_dyn(sub_dyn<Trait, SrcP>&& w)
      noexcept(std::is_nothrow_constructible_v<Ptr, SrcP &&>)
      : base(ptr_type(std::move(w.obj_)), w.vt_, w.slice_off_) {
    if constexpr (!std::is_constructible_v<Ptr, SrcP const&>) {
      w.vt_ = nullptr;
      w.slice_off_ = 0;
    }
  }

  template <template <class> class Final>
    requires(detail::can_sub_slice<Final, Trait>())
  explicit c_sub_dyn(dyn<Final, Ptr> const& handle) noexcept
      : base(handle.obj_, handle.vt_,
             detail::sub_slice_offset<Final, Trait>()) {}

  template <template <class> class Final>
    requires(detail::can_sub_slice<Final, Trait>())
  explicit c_sub_dyn(dyn<Final, Ptr>&& handle) noexcept
      : base(std::move(handle.obj_), handle.vt_,
             detail::sub_slice_offset<Final, Trait>()) {
    handle.vt_ = nullptr;
  }

  template <template <class> class Final>
    requires(detail::can_sub_slice<Final, Trait>())
  explicit c_sub_dyn(c_dyn<Final, Ptr> const& handle) noexcept
      : base(handle.obj_, handle.vt_,
             detail::sub_slice_offset<Final, Trait>()) {}

  template <template <class> class Final>
    requires(detail::can_sub_slice<Final, Trait>())
  explicit c_sub_dyn(c_dyn<Final, Ptr>&& handle) noexcept
      : base(std::move(handle.obj_), handle.vt_,
             detail::sub_slice_offset<Final, Trait>()) {
    handle.vt_ = nullptr;
  }

  proxy_t operator->() const
    requires(!detail::is_wptr_v<Ptr>) {
    auto obs = c_sub_dyn<Trait>::observe(
        const_cast<void*>(static_cast<const void*>(this->raw())), this->vt_,
        this->slice_off_);
    return proxy_t{&obs};
  }
  proxy_t operator*() const
    requires(!detail::is_wptr_v<Ptr>) {
    auto obs = c_sub_dyn<Trait>::observe(
        const_cast<void*>(static_cast<const void*>(this->raw())), this->vt_,
        this->slice_off_);
    return proxy_t{&obs};
  }

  c_sub_dyn<Trait, std::shared_ptr<void>> lock() const
    requires(detail::is_wptr_v<Ptr>) {
    auto p = this->obj_.lock();
    if (!p)
      return {};
    return c_sub_dyn<Trait, std::shared_ptr<void>>::from_ptr(
        std::move(p), this->vt_, this->slice_off_);
  }

 private:
  // Internal observation factories (proxy/lock use only; not public API).
  static c_sub_dyn observe(void* p, vt_ptr vt,
                           std::size_t slice_off = 0) noexcept
    requires(!detail::is_sptr_v<Ptr> &&
             !detail::is_wptr_v<Ptr>) {
    return c_sub_dyn{ptr_type{p}, vt, slice_off};
  }

  static c_sub_dyn from_ptr(ptr_type p, vt_ptr vt,
                            std::size_t slice_off = 0) noexcept {
    return c_sub_dyn{std::move(p), vt, slice_off};
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

// cache_ptr free-construct: capacity check; empty vt if it does not fit.
// Ops are the Trait deduction (chain slot -> op_from_trait, else unsupported
// for copy/move; destroy is always op_from_trait).
// Definition follows dyn_factory_access.
template <template <class> class Trait, std::size_t Size, std::size_t Align,
          bool AsRvalue, class... Extra>
dyn<Trait, cache_ptr<Size, Align, cache_ops_from_trait<Trait>>>
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

 protected:
  using core =
      dyn_ptr_core<Handle<Trait, Ptr>, Trait, Const, false, false, Ptr>;
  using typename core::ptr_type;
  using core::core;
  using core::obj_;
  using core::raw;
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
  template <template <class> class Tr, std::size_t Sz, std::size_t Al,
            bool AsRvalue, class... Extra>
  friend dyn<Tr, cache_ptr<Sz, Al, cache_ops_from_trait<Tr>>>
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
  static Handle<To, Ptr> make(ptr_type obj,
                                          vt_ptr vt) {
    return Handle<To, Ptr>(std::move(obj), vt);
  }

  template <template <class> class To>
  Handle<To, Ptr> clone_retype() const {
    return make<To>(obj_, vt_);
  }
  template <template <class> class To>
  Handle<To, Ptr> move_retype() {
    return make<To>(std::move(obj_), vt_);
  }
  template <template <class> class To>
  Handle<To, Ptr> clone_retype_vt(vt_ptr vt) const {
    return make<To>(obj_, vt);
  }
  template <template <class> class To>
  Handle<To, Ptr> move_retype_vt(vt_ptr vt) {
    return make<To>(std::move(obj_), vt);
  }

  template <template <class> class To>
  static Handle<To, Ptr> make(void* ptr, vt_ptr vt)
    requires(!is_obs_ptr_v<Ptr>) {
    return make<To>(ptr_type{ptr}, vt);
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
    return self(ptr, vt);
  }

  static self from_owned_storage(void* ptr, vt_ptr vt)
    requires(!is_heap_ptr_v<Ptr>) {
    return self(ptr, vt);
  }

  // std::unique_ptr + new_delete (default Alloc): ::operator new block.
  static self from_owned_storage(void* ptr, vt_ptr vt)
    requires(is_new_delete_unique_v<Ptr> && !Const) {
    return self(ptr_type{ptr}, vt);
  }

  // std::unique_ptr + alloc_delete (non-default Alloc): sized deallocate.
  template <class Alloc>
  static self from_owned_storage(void* ptr, vt_ptr vt,
                                 std::size_t nbytes, Alloc alloc)
    requires(is_alloc_delete_unique_ptr_v<ptr_type> && !Const) {
    using Deleter = typename ptr_type::deleter_type;
    return self(ptr_type{ptr, Deleter{std::move(alloc), nbytes}}, vt);
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

  template <class DoConstruct>
  void cache_emplace_owned(vt_ptr vt, std::size_t size,
                           std::size_t align, DoConstruct&& do_construct)
    requires(is_cache_ptr_v<Ptr> && !Const) {
    void* dst = obj_.emplace(size, align);
    std::forward<DoConstruct>(do_construct)(dst);
    vt_ = vt;
  }

  static constexpr bool cache_owned = is_cache_ptr_v<Ptr>;
  static constexpr bool new_delete_unique_owned =
      is_new_delete_unique_v<Ptr>;

  static self from_sptr(std::shared_ptr<void> p, vt_ptr vt)
    requires(is_sptr_v<Ptr>) {
    return self(ptr_type{std::move(p)}, vt);
  }

  template <class SrcOps>
  void install_cache_typed_ops()
    requires(is_cache_ptr_v<Ptr> && !Const) {
    obj_.template install_typed_ops<SrcOps>();
  }

 public:
  using core::ptr;
  using core::reset;

  // Split ownership from the vtable. Callable on an lvalue; the Ptr is moved
  // out here. Ptr comes first. The dyn<void*> keeps this same vt and observes
  // the object; it does not own it. *this is left empty. cache_ptr and
  // weak_ptr stay intact (their address is not separable this way).
  std::pair<Ptr, dyn<Trait, void*>> release()
    requires(!is_cache_ptr_v<Ptr> && !is_wptr_v<Ptr>) {
    void* raw = ptr_get(obj_);
    vt_ptr vt = vt_;
    Ptr owned = std::move(obj_);
    vt_ = nullptr;
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
      if (ptr_get(o.obj_) == nullptr || o.vt_ == nullptr)
        return;
      auto* new_vt = Src::template upcast_vt<Trait>(o.vt_);
      const std::size_t size = dyn_storage_size<From>(o.vt_);
      const std::size_t align = dyn_storage_align<From>(o.vt_);
      void* dst = obj_.emplace(size, align);
      dyn_construct_of<From, false>(o.vt_)(ptr_get(o.obj_), o.vt_, dst);
      vt_ = new_vt;
      this->slice_off_ = o.slice_off_;
    } else {
      obj_ = o.obj_;
      vt_ = o.vt_ != nullptr ? Src::template upcast_vt<Trait>(o.vt_) : nullptr;
      this->slice_off_ = o.slice_off_;
    }
  }

  template <template <template <class> class, class> class H,
            template <class> class From, bool FromConst>
  void take_upcast_move(dyn_handle_base<H, From, FromConst, Ptr>&& o) {
    using Src = dyn_handle_base<H, From, FromConst, Ptr>;
    if constexpr (is_cache_ptr_v<Ptr> && cache_ptr_move_via_trait_v<Ptr>) {
      if (ptr_get(o.obj_) == nullptr || o.vt_ == nullptr)
        return;
      auto* new_vt = Src::template upcast_vt<Trait>(o.vt_);
      const std::size_t size = dyn_storage_size<From>(o.vt_);
      const std::size_t align = dyn_storage_align<From>(o.vt_);
      void* dst = obj_.emplace(size, align);
      dyn_construct_of<From, true>(o.vt_)(ptr_get(o.obj_), o.vt_, dst);
      vt_ = new_vt;
      this->slice_off_ = o.slice_off_;
      o.dispose_owned();
    } else {
      obj_ = std::move(o.obj_);
      vt_ = o.vt_ != nullptr ? Src::template upcast_vt<Trait>(o.vt_) : nullptr;
      o.vt_ = nullptr;
      o.slice_off_ = 0;
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
      vt_ = this->obj_ && src_vt != nullptr
                ? Src::template upcast_vt<Trait>(src_vt)
                : nullptr;
      this->slice_off_ = o.slice_off_;
    } else {
      obj_ = ptr_type(o.obj_);
      vt_ = o.vt_ != nullptr ? Src::template upcast_vt<Trait>(o.vt_) : nullptr;
      this->slice_off_ = o.slice_off_;
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
      vt_ptr src_vt = o.vt_;
      auto next = dyn_ptr_to_deleter<Ptr>(std::move(o.obj_), src_vt);
      o.vt_ = nullptr;
      if constexpr (std::is_pointer_v<SrcP>)
        o.obj_ = nullptr;
      this->slice_off_ = o.slice_off_;
      o.slice_off_ = 0;
      obj_ = std::move(next);
      vt_ = this->obj_ && src_vt != nullptr
                ? Src::template upcast_vt<Trait>(src_vt)
                : nullptr;
    } else {
      obj_ = ptr_type(std::move(o.obj_));
      vt_ = o.vt_ != nullptr ? Src::template upcast_vt<Trait>(o.vt_) : nullptr;
      this->slice_off_ = o.slice_off_;
      if constexpr (!std::is_constructible_v<Ptr, SrcP const&>) {
        o.vt_ = nullptr;
        o.slice_off_ = 0;
      }
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
    return a.ptr() == b.ptr();
  }

 private:
  template <class, template <class> class, convert_kind, special_op,
            std::size_t, bool, bool>
  friend struct special_op_slot;

  template <typename Key, special_op Op>
  decltype(auto) cast_special(auto&&... args) const {
    return handle_cast_special<Trait, Key, Op>(
        this->raw(), this->vt_, std::forward<decltype(args)>(args)...);
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
    return dyn_view<Trait, To>(raw(), vt_);
  }

  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>() && Const)
  c_dyn_view<Trait, To> as() const {
    return c_dyn_view<Trait, To>(raw(), vt_);
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
  template <class DoConstruct>
  dyn(detail::cache_owned_inplace_t, vt_ptr vt, std::size_t size,
      std::size_t align, DoConstruct&& do_construct)
    requires(detail::is_cache_ptr_v<Ptr>)
      : base() {
    this->cache_emplace_owned(vt, size, align,
                              std::forward<DoConstruct>(do_construct));
  }

  // Same, then install erased slots from the typed source ops.
  template <class SrcOps, class DoConstruct>
  dyn(detail::cache_owned_inplace_t, std::type_identity<SrcOps>,
      vt_ptr vt, std::size_t size, std::size_t align,
      DoConstruct&& do_construct)
    requires(detail::is_cache_ptr_v<Ptr>)
      : base() {
    this->cache_emplace_owned(vt, size, align,
                              std::forward<DoConstruct>(do_construct));
    this->template install_cache_typed_ops<SrcOps>();
  }

 public:
  // Pointer bind (void*): T* becomes observation Ptr. Bare T& is not accepted.
  template <typename U>
    requires(detail::is_obs_ptr_v<Ptr> && !std::is_void_v<U> &&
             !std::is_const_v<U> &&
             !detail::impl_self_is_identity_mirror<Trait, U>() &&
             (dyn_intermediate_v || detail::is_trait_final<Trait, U>()))
  dyn(U* p)
      : base(p, detail::dyn_object_slots<false, Trait, U, U>()) {}

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
      : base(detail::dyn_bind_mirror_ptr<false, Trait, T, T, Src>(v),
             detail::dyn_bind_mirror_slots<false, Trait, T, T, Src>()) {}

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
      : base(const_cast<void*>(static_cast<const void*>(p)),
             detail::dyn_object_slots<true, Trait, U, const_as<U>>()) {}

  // Sourced mirror (no [[=mirror]] required). See dyn(mirror).
  template <typename T, typename Src>
    requires(!detail::is_heap_ptr_v<Ptr> &&
             is_sourced_mirror_v<mirror<T, Src>> &&
             (detail::mirror_view_is_src_v<T, Src> ||
              detail::sourced_dyn_view_mismatch_ok<Trait>()) &&
             (dyn_intermediate_v ||
              detail::is_trait_final<Trait, const_as<T>>()))
  c_dyn(mirror<T, Src> const& v)
      : base(detail::dyn_bind_mirror_ptr<true, Trait, const_as<T>, T, Src>(v),
             detail::dyn_bind_mirror_slots<true, Trait, const_as<T>, T,
                                           Src>()) {}


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
          class Ops = cache_ops<>>
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
    return Handle{std::forward<Ptr>(p), vt};
  }
  template <class Handle, class Ptr>
  static Handle from_uptr_adopt(Ptr&& p, vt_ptr vt) {
    return Handle{std::forward<Ptr>(p), vt};
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
          tag_destroy_align(dyn_vt_destroy(vt),
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
dyn<Trait, cache_ptr<Size, Align, cache_ops_from_trait<Trait>>>
try_construct_cache(void* ptr, vt_ptr vt, Extra&&... extra)
  requires(trait_has_dyn_construct<Trait>()) {
  using Out =
      dyn<Trait, cache_ptr<Size, Align, cache_ops_from_trait<Trait>>>;
  if (ptr == nullptr || vt == nullptr)
    return {};
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
        b, Src::template upcast_vt<To>(b.vt()));
  } else if constexpr (ptr_downcast_chain_constrained_v<Ptr> &&
                       is_trait_base_of<From, To>() &&
                       !dyn_downcast_chain_ok<To, From>()) {
    return dyn<To, Ptr>{};
  } else {
    auto* new_vt = meta_of<From>(b.vt())->find_vt(&trait_id<To>::id);
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
        b, Src::template upcast_vt<To>(b.vt()));
  } else if constexpr (ptr_downcast_chain_constrained_v<Ptr> &&
                       is_trait_base_of<From, To>() &&
                       !dyn_downcast_chain_ok<To, From>()) {
    return dyn<To, Ptr>{};
  } else {
    auto* new_vt = meta_of<From>(b.vt())->find_vt(&trait_id<To>::id);
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
        b, Src::template upcast_vt<To>(b.vt()));
  } else if constexpr (ptr_downcast_chain_constrained_v<Ptr> &&
                       is_trait_base_of<From, To>() &&
                       !dyn_downcast_chain_ok<To, From>()) {
    return c_dyn<To, Ptr>{};
  } else {
    auto* new_vt = meta_of<From>(b.vt())->find_vt(&trait_id<To>::id);
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
        b, Src::template upcast_vt<To>(b.vt()));
  } else if constexpr (ptr_downcast_chain_constrained_v<Ptr> &&
                       is_trait_base_of<From, To>() &&
                       !dyn_downcast_chain_ok<To, From>()) {
    return c_dyn<To, Ptr>{};
  } else {
    auto* new_vt = meta_of<From>(b.vt())->find_vt(&trait_id<To>::id);
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
  template <class Ptr>
    requires(!Const)
  explicit dyn_obs_proxy_facet(dyn<Trait, Ptr>* host)
      : no_friend_ops(), base(&buf_),
        buf_(Obs::from_storage(host->ptr(), host->vt())) {}

  template <class Ptr>
    requires(Const)
  explicit dyn_obs_proxy_facet(c_dyn<Trait, Ptr> const* host)
      : no_friend_ops(), base(&buf_),
        buf_(Obs::from_storage(
            const_cast<void*>(static_cast<const void*>(host->ptr())),
            host->vt())) {}

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
