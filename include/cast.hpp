#pragma once

// Non-invasive handle construction / base conversion (call form):
//   trait_cast/c_trait_cast<Trait>           iface / observer dyn / owning dyn
//   to_iface<Trait>(obj)                     build iface (see iface.hpp)
//   to_dyn/to_c_dyn                          source must become Ptr (or mirror)
//   to_dyn<Trait>(ptr|handle)                mirror only if ann::mirror
//   to_dyn<Trait, cache_ptr>(value|mirror)   -> cache_dyn (empty vt if no fit)
//   to_dyn<Trait, uptr>([alloc, ]value)      forward into heap uptr
//   instantiate_dyn<Trait, As>               type: ODR-use final-leaf object vt
//     (explicit: template struct asf::instantiate_dyn<Trait, As>;)
//   trait_cast<Trait>(iface | dyn | c_dyn)   iface: bases either way; dyn: upcast
//   c_trait_cast<Trait>(iface | dyn | c_dyn) const surface; dyn upcast to c_dyn
//   to_dyn<Trait, deleter Ptr>(dyn&&)        shallow handoff onto that Ptr
//   to_sub_dyn / to_c_sub_dyn
//   get_proxy<To>(h)                         proxy<To>()   (const&)
//   get_rproxy<To>(h)                        rproxy<To>()  (const&)
//   get_forward_proxy<To>(h)                 lvalue proxy / rvalue rproxy
//   construct<Ptr...>(*dyn[, extras...])     extras -> op_construct
//   construct<Ptr...>(alloc, *dyn[, extras]) alloc first iff *dyn is 2nd
//   construct<Ptr...>(rproxy / d.rproxy())   rproxy_t -> move (!Const)
//   construct<cache_ptr...>(*dyn|rproxy)     -> cache_dyn (empty vt if no fit)
//   construct(*c_dyn|c_dyn.rproxy())         always copy (Const rproxy != move)
//   dyn_cast / unsafe_downcast
//
// Call `()` left-associates with `.` so `to_dyn<I>(ptr)->method()` needs no
// extra parentheses.

#include "interface.hpp"

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

ASF_BEGIN_NAMESPACE
namespace detail {

template <class T>
concept object_for_cast = !is_any_handle_v<T>;

// Value that can be forwarded into an owning Ptr (not pointer/mirror/handle).
template <class T>
concept value_for_owning_ptr =
    object_for_cast<T> && !dyn_bindable_ptr<std::remove_cvref_t<T>> &&
    !is_mirror_like_v<T>;

template <template <class> class To, template <class> class From>
consteval bool can_iface_cast() {
  return is_trait_base_of<To, From>() || is_trait_base_of<From, To>();
}

template <class T>
inline constexpr bool is_cache_ptr_spec_v = false;
template <std::size_t Size, std::size_t Align, class Ops>
inline constexpr bool
    is_cache_ptr_spec_v<cache_ptr<Size, Align, Ops>> = true;

template <class Spec>
struct cache_spec_ops {
  using type = cache_ops<>;
};
template <std::size_t Size, std::size_t Align, class Ops>
struct cache_spec_ops<cache_ptr<Size, Align, Ops>> {
  using type = Ops;
};

template <template <class> class Trait, class Spec>
using cache_dyn_t = dyn<Trait, cache_ptr<Spec::size, Spec::align>>;

// Into cache_dyn: check Spec ops against Obj, then erase: Trait chain ->
// op_from_trait, else usable source ops -> detail::op_modified / op_trivial.
template <template <class> class Trait, class Spec, class Obj>
using cache_dyn_ptr_for_t =
    cache_ptr<Spec::size, Spec::align,
              resolve_cache_dyn_ops_t<
                  Trait, Obj, typename cache_spec_ops<Spec>::type>>;

template <template <class> class Trait, class Spec, class Obj>
using cache_dyn_for_t = dyn<Trait, cache_dyn_ptr_for_t<Trait, Spec, Obj>>;

template <template <class> class Trait, class As>
consteval bool can_ptr_bind_as() {
  return !impl_self_is_identity_mirror<Trait, As>() &&
         (dyn_intermediate_v || is_trait_final<Trait, As>());
}

// Forward U into final cache Storage; no intermediate dyn / virt op_construct.
template <template <class> class Trait, class Spec, class U>
constexpr auto try_cache_forward_value(U&& object)
    -> cache_dyn_for_t<Trait, Spec, std::remove_cvref_t<U>> {
  using Obj = std::remove_cvref_t<U>;
  using Out = cache_dyn_for_t<Trait, Spec, Obj>;
  static_assert(can_ptr_bind_as<Trait, Obj>(),
                "cache_ptr value adopt requires Trait final for Obj "
                "(or ASF_HAS_DYN_INTERMEDIATE)");
  if (sizeof(Obj) > Spec::size || alignof(Obj) > Spec::align)
    return {};
  vt_ptr vt = dyn_object_slots<false, Trait, Obj, Obj>();
  return dyn_factory_access::from_cache_inplace<Out>(
      cache_owned_inplace, vt, sizeof(Obj), alignof(Obj), [&](void* dst) {
        std::construct_at(static_cast<Obj*>(dst), std::forward<U>(object));
      });
}

// Forward Src from mirror into final cache Storage.
template <template <class> class Trait, class Spec, typename T, typename Src>
constexpr auto try_cache_forward_mirror(mirror<T, Src> m, bool as_move)
    -> cache_dyn_for_t<Trait, Spec, Src> {
  constexpr bool allow_mirror = layout_allows_mirror_source_as<Trait, T>();
  using Out = cache_dyn_for_t<Trait, Spec, Src>;
  static_assert(allow_mirror,
                "mirror -> dyn requires [[=asf_ann::mirror]] on every Trait "
                "in the inheritance layout");
  static_assert(can_ptr_bind_as<Trait, T>(),
                "cache_ptr mirror adopt requires Trait final for T "
                "(or ASF_HAS_DYN_INTERMEDIATE)");
  if (sizeof(Src) > Spec::size || alignof(Src) > Spec::align)
    return {};
  vt_ptr vt = dyn_bind_mirror_slots<false, Trait, T, T, Src>();
  return dyn_factory_access::from_cache_inplace<Out>(
      cache_owned_inplace, vt, sizeof(Src), alignof(Src), [&](void* dst) {
        if (as_move)
          std::construct_at(static_cast<Src*>(dst), std::move(src_of(m)));
        else
          std::construct_at(static_cast<Src*>(dst), src_of(m));
      });
}

// Forward U into heap unique_ptr Storage (no intermediate dyn).
// Optional allocator is the public first argument of to_dyn; internally
// it is still the Args pack here. Empty, or std::allocator, uses new_delete.
template <template <class> class Trait, class U, class... Args>
constexpr auto make_uptr_forward_value(U&& object, Args&&... args) {
  using Obj = std::remove_cvref_t<U>;
  static_assert(can_ptr_bind_as<Trait, Obj>(),
                "unique_ptr value adopt requires Trait final for Obj "
                "(or ASF_HAS_DYN_INTERMEDIATE)");
  static_assert(sizeof...(Args) <= 1,
                "unique_ptr generation forwards at most an allocator");
  vt_ptr vt = dyn_object_slots<false, Trait, Obj, Obj>();
  auto place = [&](void* dst) {
    std::construct_at(static_cast<Obj*>(dst), std::forward<U>(object));
  };
  if constexpr (sizeof...(Args) == 0) {
    using Out = dyn<Trait, std::unique_ptr<void, detail::new_delete>>;
    void* dst = ::operator new(sizeof(Obj));
    place(dst);
    return dyn_factory_access::from_owned_storage<Out>(dst, vt);
  } else {
    return [&]<class A>(A&& alloc) {
      using AA = std::remove_cvref_t<A>;
      using VoidA =
          typename std::allocator_traits<AA>::template rebind_alloc<void>;
      if constexpr (is_std_allocator_v<VoidA>) {
        (void)alloc;
        using Out = dyn<Trait, std::unique_ptr<void, detail::new_delete>>;
        void* dst = ::operator new(sizeof(Obj));
        place(dst);
        return dyn_factory_access::from_owned_storage<Out>(dst, vt);
      } else {
        using Out = dyn<Trait, std::unique_ptr<void, alloc_delete<AA>>>;
        void* dst = dyn_allocate_bytes(alloc, sizeof(Obj));
        place(dst);
        return dyn_factory_access::from_owned_storage<Out>(
            dst, vt, sizeof(Obj), std::forward<A>(alloc));
      }
    }(std::forward<Args>(args)...);
  }
}

template <template <class> class Trait, template <class...> class Ptr,
          class Tuple>
struct apply_dyn_ptr_args;
template <template <class> class Trait, template <class...> class Ptr,
          class... Args>
struct apply_dyn_ptr_args<Trait, Ptr, std::tuple<Args...>> {
  using type = dyn<Trait, erased_ptr_t<Ptr, Args...>>;
};
template <template <class> class Trait>
struct apply_dyn_ptr_args<Trait, std::unique_ptr, std::tuple<>> {
  using type = dyn<Trait, std::unique_ptr<void, detail::new_delete>>;
};
template <template <class> class Trait, template <class...> class Ptr,
          class... PtrArgs>
using construct_dyn_t = typename apply_dyn_ptr_args<
    Trait, Ptr, typename unique_ptr_dyn_args<Ptr, PtrArgs...>::type>::type;

}  // namespace detail

// iface: Trait bases either way when the second parameter builds that iface.
// dyn-family:
// Trait upcast via the converting constructor (rvalue may move). Objects and
// mirrors go through to_iface / iface, not trait_cast.
template <template <class> class Trait>
struct trait_cast_t {
  template <template <class> class From, typename T>
    requires(detail::can_iface_convert_of<Trait, From, T>())
  constexpr auto operator()(iface<From, T> handle) const {
    return iface<Trait, T>(handle.get());
  }

  template <template <class> class From, class Ptr>
    requires(detail::can_dyn_upcast<Trait, From>())
  constexpr auto operator()(dyn<From, Ptr> const& handle) const {
    return dyn<Trait, Ptr>(handle);
  }

  template <template <class> class From, class Ptr>
    requires(detail::can_dyn_upcast<Trait, From>())
  constexpr auto operator()(dyn<From, Ptr>&& handle) const {
    return dyn<Trait, Ptr>(std::move(handle));
  }

  template <template <class> class From, class Ptr>
    requires(detail::can_dyn_upcast<Trait, From>())
  constexpr auto operator()(c_dyn<From, Ptr> const& handle) const {
    return c_dyn<Trait, Ptr>(handle);
  }

  template <template <class> class From, class Ptr>
    requires(detail::can_dyn_upcast<Trait, From>())
  constexpr auto operator()(c_dyn<From, Ptr>&& handle) const {
    return c_dyn<Trait, Ptr>(std::move(handle));
  }
};

template <template <class> class Trait>
inline constexpr trait_cast_t<Trait> trait_cast{};

// Const surface from a handle. iface -> c_iface. dyn / c_dyn upcast to c_dyn.
template <template <class> class Trait>
struct c_trait_cast_t {
  template <template <class> class From, typename T>
    requires(detail::can_iface_convert_of<Trait, From, T>())
  constexpr auto operator()(iface<From, T> handle) const {
    return c_iface<Trait, T>(handle.get());
  }

  template <template <class> class From, class Ptr>
    requires(detail::can_dyn_upcast<Trait, From>())
  constexpr auto operator()(dyn<From, Ptr> const& handle) const {
    return c_dyn<Trait, Ptr>(handle);
  }

  template <template <class> class From, class Ptr>
    requires(detail::can_dyn_upcast<Trait, From>())
  constexpr auto operator()(dyn<From, Ptr>&& handle) const {
    return c_dyn<Trait, Ptr>(std::move(handle));
  }

  template <template <class> class From, class Ptr>
    requires(detail::can_dyn_upcast<Trait, From>())
  constexpr auto operator()(c_dyn<From, Ptr> const& handle) const {
    return c_dyn<Trait, Ptr>(handle);
  }

  template <template <class> class From, class Ptr>
    requires(detail::can_dyn_upcast<Trait, From>())
  constexpr auto operator()(c_dyn<From, Ptr>&& handle) const {
    return c_dyn<Trait, Ptr>(std::move(handle));
  }
};

template <template <class> class Trait>
inline constexpr c_trait_cast_t<Trait> c_trait_cast{};

// Spec=void: adopt ptr / mirror / handle. Spec=cache_ptr: forward value into buf.
template <template <class> class Trait, class Spec>
struct to_dyn_t {
  static constexpr bool cache = detail::is_cache_ptr_spec_v<Spec>;
  static constexpr bool allows_mirror_source =
      detail::layout_allows_unsourced_mirror<Trait>();

  template <detail::value_for_owning_ptr U>
    requires(cache)
  constexpr auto operator()(U& object) const {
    return detail::try_cache_forward_value<Trait, Spec>(object);
  }

  template <detail::value_for_owning_ptr U>
    requires(cache && !std::is_lvalue_reference_v<U>)
  constexpr auto operator()(U&& object) const {
    return detail::try_cache_forward_value<Trait, Spec>(
        std::forward<U>(object));
  }

  template <typename T, typename Src>
    requires(!cache && is_sourced_mirror_v<mirror<T, Src>> &&
             !std::is_const_v<T> &&
             (detail::mirror_view_is_src_v<T, Src> ||
              detail::sourced_dyn_view_mismatch_ok<Trait>()))
  // const&: observer bind. A by-value copy would dangle when Storage is
  // the mirror (view != Src, or identity_mirror Self).
  constexpr auto operator()(mirror<T, Src> const& m) const {
    return dyn<Trait>{m};
  }

  template <typename T, typename Src>
    requires(cache && is_sourced_mirror_v<mirror<T, Src>> &&
             !std::is_const_v<T> &&
             detail::mirror_view_is_src_v<T, Src>)
  constexpr auto operator()(mirror<T, Src> m) const {
    return detail::try_cache_forward_mirror<Trait, Spec>(std::move(m), false);
  }

  template <typename T, typename Src>
    requires(cache && is_sourced_mirror_v<mirror<T, Src>> &&
             !std::is_const_v<T> &&
             detail::mirror_view_is_src_v<T, Src>)
  constexpr auto operator()(mirror<T, Src>&& m) const {
    return detail::try_cache_forward_mirror<Trait, Spec>(m, true);
  }

  template <class P>
    requires(!cache && detail::dyn_bindable_ptr<std::remove_cvref_t<P>>)
  constexpr auto operator()(P&& p) const {
    return detail::bind_dyn_from_ptr<Trait, false>(std::forward<P>(p));
  }

  // iface<From, Arg> -> dyn<Trait> when Arg can build that dyn.
  template <template <class> class From, typename Arg>
    requires(!cache && std::constructible_from<dyn<Trait>, Arg&> &&
             requires(iface<From, Arg> h) { dyn<Trait>(h.get()); })
  constexpr auto operator()(iface<From, Arg> handle) const {
    return dyn<Trait>(handle.get());
  }

  template <template <class> class From, typename Arg>
    requires(cache && requires(Arg& a, iface<From, Arg> h) {
      to_dyn_t{}.operator()(a);
      to_dyn_t{}.operator()(h.get());
    })
  constexpr auto operator()(iface<From, Arg> handle) const {
    return (*this)(handle.get());
  }
};

// to_dyn<Trait, cache_ptr>(value|mirror). Size/Align = sizeof/alignof.
// Forwards into the cache buf (no intermediate dyn).
template <template <class> class Trait,
          template <std::size_t, std::size_t, class> class CachePtr>
struct to_dyn_cache_ptr_t {
  static constexpr bool allows_mirror_source =
      detail::layout_allows_unsourced_mirror<Trait>();

  template <detail::value_for_owning_ptr U>
  constexpr auto operator()(U& object) const {
    using Obj = std::remove_cvref_t<U>;
    using Spec = CachePtr<sizeof(Obj), alignof(Obj), cache_ops<>>;
    return detail::try_cache_forward_value<Trait, Spec>(object);
  }

  template <detail::value_for_owning_ptr U>
    requires(!std::is_lvalue_reference_v<U>)
  constexpr auto operator()(U&& object) const {
    using Obj = std::remove_cvref_t<U>;
    using Spec = CachePtr<sizeof(Obj), alignof(Obj), cache_ops<>>;
    return detail::try_cache_forward_value<Trait, Spec>(
        std::forward<U>(object));
  }

  template <typename T, typename Src>
    requires(is_sourced_mirror_v<mirror<T, Src>> && !std::is_const_v<T> &&
             detail::mirror_view_is_src_v<T, Src>)
  constexpr auto operator()(mirror<T, Src> m) const {
    using Spec = CachePtr<sizeof(Src), alignof(Src), cache_ops<>>;
    return detail::try_cache_forward_mirror<Trait, Spec>(std::move(m), false);
  }

  template <typename T, typename Src>
    requires(is_sourced_mirror_v<mirror<T, Src>> && !std::is_const_v<T> &&
             detail::mirror_view_is_src_v<T, Src>)
  constexpr auto operator()(mirror<T, Src>&& m) const {
    using Spec = CachePtr<sizeof(Src), alignof(Src), cache_ops<>>;
    return detail::try_cache_forward_mirror<Trait, Spec>(m, true);
  }

  // Same acceptance as calling this with the iface's second parameter.
  template <template <class> class From, typename Arg>
    requires(requires(Arg& a, iface<From, Arg> h) {
      to_dyn_cache_ptr_t{}.operator()(a);
      to_dyn_cache_ptr_t{}.operator()(h.get());
    })
  constexpr auto operator()(iface<From, Arg> handle) const {
    return (*this)(handle.get());
  }
};

// to_dyn<Trait, std::unique_ptr>([alloc, ]value): alloc is optional first.
template <template <class> class Trait, template <class...> class Ptr>
struct to_dyn_uptr_t {
  static constexpr bool ok_ptr =
      std::same_as<Ptr<void>, std::unique_ptr<void>>;

  template <detail::value_for_owning_ptr U>
    requires(ok_ptr)
  constexpr auto operator()(U&& object) const {
    return detail::make_uptr_forward_value<Trait>(std::forward<U>(object));
  }

  template <class Alloc, detail::value_for_owning_ptr U>
    requires(ok_ptr && requires {
              typename std::allocator_traits<
                  std::remove_cvref_t<Alloc>>::value_type;
            })
  constexpr auto operator()(Alloc&& alloc, U&& object) const {
    return detail::make_uptr_forward_value<Trait>(
        std::forward<U>(object), std::forward<Alloc>(alloc));
  }
};

// Function overloads (variable templates cannot overload by param kind).
template <template <class> class Trait, class Arg>
  requires(!detail::is_dyn_handle_v<std::remove_cvref_t<Arg>>)
constexpr auto to_dyn(Arg&& arg)
    -> decltype(to_dyn_t<Trait, void>{}(std::forward<Arg>(arg))) {
  return to_dyn_t<Trait, void>{}(std::forward<Arg>(arg));
}

template <template <class> class Trait,
          template <std::size_t, std::size_t, class> class CachePtr, class Arg>
constexpr auto to_dyn(Arg&& arg) -> decltype(to_dyn_cache_ptr_t<
    Trait, CachePtr>{}(std::forward<Arg>(arg))) {
  return to_dyn_cache_ptr_t<Trait, CachePtr>{}(std::forward<Arg>(arg));
}

template <template <class> class Trait, class Spec, class Arg>
  requires(detail::is_cache_ptr_spec_v<Spec>)
constexpr auto to_dyn(Arg&& arg)
    -> decltype(to_dyn_t<Trait, Spec>{}(std::forward<Arg>(arg))) {
  return to_dyn_t<Trait, Spec>{}(std::forward<Arg>(arg));
}

template <template <class> class Trait, template <class...> class Ptr,
          class Arg>
  requires(std::same_as<Ptr<void>, std::unique_ptr<void>>)
constexpr auto to_dyn(Arg&& arg)
    -> decltype(to_dyn_uptr_t<Trait, Ptr>{}(std::forward<Arg>(arg))) {
  return to_dyn_uptr_t<Trait, Ptr>{}(std::forward<Arg>(arg));
}

template <template <class> class Trait, template <class...> class Ptr,
          class Alloc, class Arg>
  requires(std::same_as<Ptr<void>, std::unique_ptr<void>> &&
           detail::value_for_owning_ptr<Arg>)
constexpr auto to_dyn(Alloc&& alloc, Arg&& arg)
    -> decltype(to_dyn_uptr_t<Trait, Ptr>{}(std::forward<Alloc>(alloc),
                                            std::forward<Arg>(arg))) {
  return to_dyn_uptr_t<Trait, Ptr>{}(std::forward<Alloc>(alloc),
                                     std::forward<Arg>(arg));
}

// Different deleter Ptr: shallow handoff from vt_destroy as P(raw, d).
// Result is Ptr<void>.
template <template <class> class Trait, template <class...> class Ptr,
          template <class> class From, class SrcP>
  requires(detail::can_dyn_upcast<Trait, From>() &&
           detail::pointer_stores_vt_destroy_v<Ptr<void>>)
constexpr dyn<Trait, Ptr<void>> to_dyn(dyn<From, SrcP>&& handle) {
  return detail::bridge_owned_dyn<Trait, Ptr<void>>(std::move(handle));
}

template <template <class> class Trait>
struct to_c_dyn_t {
  template <typename T, typename Src>
    requires(is_sourced_mirror_v<mirror<T, Src>> &&
             (detail::mirror_view_is_src_v<T, Src> ||
              detail::sourced_dyn_view_mismatch_ok<Trait>()))
  constexpr auto operator()(mirror<T, Src> const& m) const {
    return c_dyn<Trait>{m};
  }

  template <class P>
    requires(detail::dyn_bindable_ptr<std::remove_cvref_t<P>>)
  constexpr auto operator()(P&& p) const {
    return detail::bind_dyn_from_ptr<Trait, true>(std::forward<P>(p));
  }

  // iface<From, Arg> -> c_dyn<Trait> when Arg can build that c_dyn.
  template <template <class> class From, typename Arg>
    requires(std::constructible_from<c_dyn<Trait>, Arg&> ||
             std::constructible_from<c_dyn<Trait>, Arg const&>)
  constexpr auto operator()(iface<From, Arg> handle) const
    requires(requires { c_dyn<Trait>(handle.get()); }) {
    return c_dyn<Trait>(handle.get());
  }
};

template <template <class> class Trait>
inline constexpr to_c_dyn_t<Trait> to_c_dyn{};

// ODR-use object-bind vtables for final leaf Trait<As> (mut + const).
// Same inject path as pointer bind (`dyn_object_slots`). Explicitly
// instantiate at namespace scope when a base is used as dyn and the program
// has no covering final pointer/mirror bind:
//   template struct asf::instantiate_dyn<Trait, As>;
template <template <class> class Trait, class As>
struct instantiate_dyn {
  using Raw = std::remove_cvref_t<As>;
  static_assert(detail::is_trait_final<Trait, Raw>(),
                "instantiate_dyn requires Trait final for As");
  static_assert(!detail::impl_self_is_identity_mirror<Trait, Raw>(),
                "instantiate_dyn requires identity-mirror-free Trait");

  static inline detail::vt_ptr const mut_slots =
      detail::dyn_object_slots<false, Trait, Raw, Raw>();
  static inline detail::vt_ptr const c_slots =
      detail::dyn_object_slots<true, Trait, std::add_const_t<Raw>,
                               std::add_const_t<Raw>>();
};

// Isolated Trait section slice from a leaf/final dyn. const& copies Ptr;
// && moves Ptr. dyn->sub_dyn, c_dyn->c_sub_dyn (preserves constness). Not
// from sub_dyn (no secondary upcast).
template <template <class> class Trait>
struct to_sub_dyn_t {
  template <template <class> class Final, class Ptr>
    requires(detail::can_sub_slice<Final, Trait>())
  constexpr sub_dyn<Trait, Ptr> operator()(
      dyn<Final, Ptr> const& handle) const {
    return sub_dyn<Trait, Ptr>{handle};
  }

  template <template <class> class Final, class Ptr>
    requires(detail::can_sub_slice<Final, Trait>())
  constexpr sub_dyn<Trait, Ptr> operator()(
      dyn<Final, Ptr>&& handle) const {
    return sub_dyn<Trait, Ptr>{std::move(handle)};
  }

  template <template <class> class Final, class Ptr>
    requires(detail::can_sub_slice<Final, Trait>())
  constexpr c_sub_dyn<Trait, Ptr> operator()(
      c_dyn<Final, Ptr> const& handle) const {
    return c_sub_dyn<Trait, Ptr>{handle};
  }

  template <template <class> class Final, class Ptr>
    requires(detail::can_sub_slice<Final, Trait>())
  constexpr c_sub_dyn<Trait, Ptr> operator()(
      c_dyn<Final, Ptr>&& handle) const {
    return c_sub_dyn<Trait, Ptr>{std::move(handle)};
  }
};
template <template <class> class Trait>
inline constexpr to_sub_dyn_t<Trait> to_sub_dyn{};

template <template <class> class Trait>
struct to_c_sub_dyn_t {
  template <template <class> class Final, class Ptr>
    requires(detail::can_sub_slice<Final, Trait>())
  constexpr c_sub_dyn<Trait, Ptr> operator()(
      dyn<Final, Ptr> const& handle) const {
    return c_sub_dyn<Trait, Ptr>{handle};
  }

  template <template <class> class Final, class Ptr>
    requires(detail::can_sub_slice<Final, Trait>())
  constexpr c_sub_dyn<Trait, Ptr> operator()(
      dyn<Final, Ptr>&& handle) const {
    return c_sub_dyn<Trait, Ptr>{std::move(handle)};
  }

  template <template <class> class Final, class Ptr>
    requires(detail::can_sub_slice<Final, Trait>())
  constexpr c_sub_dyn<Trait, Ptr> operator()(
      c_dyn<Final, Ptr> const& handle) const {
    return c_sub_dyn<Trait, Ptr>{handle};
  }

  template <template <class> class Final, class Ptr>
    requires(detail::can_sub_slice<Final, Trait>())
  constexpr c_sub_dyn<Trait, Ptr> operator()(
      c_dyn<Final, Ptr>&& handle) const {
    return c_sub_dyn<Trait, Ptr>{std::move(handle)};
  }

  template <class Ptr>
  constexpr c_sub_dyn<Trait, Ptr> operator()(
      sub_dyn<Trait, Ptr> const& w) const {
    return c_sub_dyn<Trait, Ptr>{w};
  }

  template <class Ptr>
  constexpr c_sub_dyn<Trait, Ptr> operator()(
      sub_dyn<Trait, Ptr>&& w) const {
    return c_sub_dyn<Trait, Ptr>{std::move(w)};
  }
};

template <template <class> class Trait>
inline constexpr to_c_sub_dyn_t<Trait> to_c_sub_dyn{};

template <template <class> class Trait>
struct get_proxy_t {
  template <template <class> class From, class Ptr>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(dyn<From, Ptr> const& handle) const {
    return handle.template proxy<Trait>();
  }

  template <template <class> class From, class Ptr>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(c_dyn<From, Ptr> const& handle) const {
    return handle.template proxy<Trait>();
  }

  template <template <class> class Layout, template <class> class View>
    requires(detail::is_trait_base_of<Trait, View>())
  constexpr auto operator()(
      dyn_view<Layout, View> const& handle) const {
    return handle.template proxy<Trait>();
  }

  template <template <class> class Layout, template <class> class View>
    requires(detail::is_trait_base_of<Trait, View>())
  constexpr auto operator()(
      c_dyn_view<Layout, View> const& handle) const {
    return handle.template proxy<Trait>();
  }

  template <template <class> class From, typename T>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(iface<From, T> const& handle) const {
    return handle.template proxy<Trait>();
  }
};

template <template <class> class Trait>
inline constexpr get_proxy_t<Trait> get_proxy{};

template <template <class> class Trait>
struct get_rproxy_t {
  template <template <class> class From, class Ptr>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(dyn<From, Ptr> const& handle) const {
    return handle.template rproxy<Trait>();
  }

  template <template <class> class From, class Ptr>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(c_dyn<From, Ptr> const& handle) const {
    return handle.template rproxy<Trait>();
  }

  template <template <class> class Layout, template <class> class View>
    requires(detail::is_trait_base_of<Trait, View>())
  constexpr auto operator()(
      dyn_view<Layout, View> const& handle) const {
    return handle.template rproxy<Trait>();
  }

  template <template <class> class Layout, template <class> class View>
    requires(detail::is_trait_base_of<Trait, View>())
  constexpr auto operator()(
      c_dyn_view<Layout, View> const& handle) const {
    return handle.template rproxy<Trait>();
  }

  template <template <class> class From, typename T>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(iface<From, T> const& handle) const {
    return handle.template rproxy<Trait>();
  }
};

template <template <class> class Trait>
inline constexpr get_rproxy_t<Trait> get_rproxy{};

template <template <class> class Trait>
struct get_forward_proxy_t {
  template <template <class> class From, class Ptr>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(dyn<From, Ptr> const& handle) const {
    return handle.template proxy<Trait>();
  }

  template <template <class> class From, class Ptr>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(dyn<From, Ptr>&& handle) const {
    return std::move(handle).template rproxy<Trait>();
  }

  template <template <class> class From, class Ptr>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(c_dyn<From, Ptr> const& handle) const {
    return handle.template proxy<Trait>();
  }

  template <template <class> class From, class Ptr>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(c_dyn<From, Ptr>&& handle) const {
    return std::move(handle).template rproxy<Trait>();
  }

  template <template <class> class Layout, template <class> class View>
    requires(detail::is_trait_base_of<Trait, View>())
  constexpr auto operator()(
      dyn_view<Layout, View> const& handle) const {
    return handle.template proxy<Trait>();
  }

  template <template <class> class Layout, template <class> class View>
    requires(detail::is_trait_base_of<Trait, View>())
  constexpr auto operator()(dyn_view<Layout, View>&& handle) const {
    return std::move(handle).template rproxy<Trait>();
  }

  template <template <class> class Layout, template <class> class View>
    requires(detail::is_trait_base_of<Trait, View>())
  constexpr auto operator()(
      c_dyn_view<Layout, View> const& handle) const {
    return handle.template proxy<Trait>();
  }

  template <template <class> class Layout, template <class> class View>
    requires(detail::is_trait_base_of<Trait, View>())
  constexpr auto operator()(
      c_dyn_view<Layout, View>&& handle) const {
    return std::move(handle).template rproxy<Trait>();
  }

  template <template <class> class From, typename T>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(iface<From, T> const& handle) const {
    return handle.template proxy<Trait>();
  }

  template <template <class> class From, typename T>
    requires(detail::is_trait_base_of<Trait, From>())
  constexpr auto operator()(iface<From, T>&& handle) const {
    return std::move(handle).template rproxy<Trait>();
  }
};

template <template <class> class Trait>
inline constexpr get_forward_proxy_t<Trait> get_forward_proxy{};

// Vt op_construct from *dyn (not obj|mirror). Extra args after the proxy
// dispatch to the matching op_construct overload. Alloc is optional first:
// first arg is proxy => no alloc; second arg is proxy => first is alloc.
//   construct<OutPtr...>(proxy_t[, extras...]) / (rproxy_t[, extras...])
//   construct<OutPtr...>(alloc, proxy_t|rproxy_t[, extras...])
//   construct<cache_ptr...>(proxy_t|rproxy_t[, extras...])
namespace detail {

template <class T>
concept dyn_construct_proxy = is_dyn_proxy_facet_v<std::remove_cvref_t<T>>;

template <class... Extra>
concept dyn_construct_extras = (!dyn_construct_proxy<Extra> && ...);

template <template <class> class Trait, class Alloc>
struct unique_dyn_for_alloc {
  using A = std::remove_cvref_t<Alloc>;
  using type = std::conditional_t<
      is_std_allocator_v<A>,
      dyn<Trait, std::unique_ptr<void, new_delete>>,
      dyn<Trait, std::unique_ptr<void, alloc_delete<A>>>>;
};

template <std::size_t Size, std::size_t Align, template <class> class Trait,
          bool Const, class SrcP, bool Rvalue, class... Extra>
constexpr auto construct_via_cache_ptr(
    dyn_proxy_facet<Trait, Const, SrcP, Rvalue> const& p, Extra&&... extra)
  requires(trait_has_dyn_construct<Trait>()) {
  auto const& h = p.buf_;
  return try_construct_cache<Trait, Size, Align, Rvalue && !Const>(
      const_cast<void*>(static_cast<const void*>(h.ptr())), vt_of(h),
      std::forward<Extra>(extra)...);
}

template <template <class> class Trait, class Out, bool Const, class SrcP,
          bool Rvalue, class Alloc, class... Extra>
Out construct_erased_from_proxy(
    dyn_proxy_facet<Trait, Const, SrcP, Rvalue> const& p, Alloc&& alloc,
    Extra&&... extra)
  requires(trait_has_dyn_construct<Trait>()) {
  auto const& h = p.buf_;
  return construct_erased_to<Trait, Out, Rvalue && !Const>(
      const_cast<void*>(static_cast<const void*>(h.ptr())), vt_of(h),
      std::forward<Alloc>(alloc), std::forward<Extra>(extra)...);
}

}  // namespace detail

// *dyn / *c_dyn -> dyn. Destroy is always vt[-1].
// Alloc: explicit (first, before *dyn) > OutPtr baked > source SrcP > default.
// No OutPtr => Out follows source erased Ptr (SrcP).
template <template <class> class Trait, bool Const, class SrcP, bool Rvalue,
          class... Extra>
  requires(detail::trait_has_dyn_construct<Trait>() &&
           detail::dyn_construct_extras<Extra...>)
constexpr auto construct(
    detail::dyn_proxy_facet<Trait, Const, SrcP, Rvalue> const& p,
    Extra&&... extra) {
  using Out = detail::dyn_for_erased_ptr_t<Trait, SrcP>;
  using Alloc = detail::dyn_handle_allocator_t<Out>;
  return detail::construct_erased_from_proxy<Trait, Out>(
      p, Alloc{}, std::forward<Extra>(extra)...);
}

template <template <class...> class OutPtr, class... OutPtrArgs,
          template <class> class Trait, bool Const, class SrcP, bool Rvalue,
          class... Extra>
  requires(detail::trait_has_dyn_construct<Trait>() &&
           detail::dyn_construct_extras<Extra...>)
constexpr auto construct(
    detail::dyn_proxy_facet<Trait, Const, SrcP, Rvalue> const& p,
    Extra&&... extra) {
  using Out = detail::construct_dyn_t<Trait, OutPtr, OutPtrArgs...>;
  using Alloc = detail::dyn_handle_allocator_t<Out>;
  return detail::construct_erased_from_proxy<Trait, Out>(
      p, Alloc{}, std::forward<Extra>(extra)...);
}

template <template <class...> class OutPtr, class... OutPtrArgs,
          template <class> class Trait, bool Const, class SrcP, bool Rvalue,
          class Alloc, class... Extra>
  requires(sizeof...(OutPtrArgs) == 0 &&
           detail::trait_has_dyn_construct<Trait>() &&
           !detail::dyn_construct_proxy<Alloc> &&
           detail::dyn_construct_extras<Extra...>)
constexpr auto construct(
    Alloc&& alloc,
    detail::dyn_proxy_facet<Trait, Const, SrcP, Rvalue> const& p,
    Extra&&... extra) {
  using Out = std::conditional_t<
      std::same_as<OutPtr<void>, std::unique_ptr<void>>,
      typename detail::unique_dyn_for_alloc<Trait, Alloc>::type,
      detail::construct_dyn_t<Trait, OutPtr, OutPtrArgs...>>;
  return detail::construct_erased_from_proxy<Trait, Out>(
      p, std::forward<Alloc>(alloc), std::forward<Extra>(extra)...);
}

// *dyn / rproxy -> cache_dyn with Trait-deduced cache_ops (empty vt if no fit).
template <class Spec, template <class> class Trait, bool Const, class SrcP,
          bool Rvalue, class... Extra>
  requires(detail::is_cache_ptr_spec_v<Spec> &&
           detail::trait_has_dyn_construct<Trait>() &&
           detail::dyn_construct_extras<Extra...>)
constexpr auto construct(
    detail::dyn_proxy_facet<Trait, Const, SrcP, Rvalue> const& p,
    Extra&&... extra) {
  return detail::construct_via_cache_ptr<Spec::size, Spec::align>(
      p, std::forward<Extra>(extra)...);
}

// Retype Trait only. Not static_cast: under interface MI the current vt may be
// a Redirect cover - naming a more-derived Trait without adjusting vt is UB.
template <template <class> class Trait>
struct unsafe_downcast_t {
  template <template <class> class From, class Ptr>
  constexpr auto operator()(dyn<From, Ptr> const& handle) const
      -> decltype(detail::unsafe_downcast<Trait>(handle)) {
    return detail::unsafe_downcast<Trait>(handle);
  }

  template <template <class> class From, class Ptr>
  constexpr auto operator()(dyn<From, Ptr>&& handle) const
      -> decltype(detail::unsafe_downcast<Trait>(std::move(handle))) {
    return detail::unsafe_downcast<Trait>(std::move(handle));
  }

  template <template <class> class From, class Ptr>
  constexpr auto operator()(c_dyn<From, Ptr> const& handle) const
      -> decltype(detail::unsafe_downcast<Trait>(handle)) {
    return detail::unsafe_downcast<Trait>(handle);
  }

  template <template <class> class From, class Ptr>
  constexpr auto operator()(c_dyn<From, Ptr>&& handle) const
      -> decltype(detail::unsafe_downcast<Trait>(std::move(handle))) {
    return detail::unsafe_downcast<Trait>(std::move(handle));
  }
};

template <template <class> class Trait>
inline constexpr unsafe_downcast_t<Trait> unsafe_downcast{};

#if ASF_HAS_DYN_RTTI

template <template <class> class Trait>
struct dyn_cast_t {
  template <template <class> class From, class Ptr>
  constexpr auto operator()(dyn<From, Ptr> const& handle) const {
    return detail::rtti_cast<Trait>(handle);
  }

  template <template <class> class From, class Ptr>
  constexpr auto operator()(dyn<From, Ptr>&& handle) const {
    return detail::rtti_cast<Trait>(std::move(handle));
  }

  template <template <class> class From, class Ptr>
  constexpr auto operator()(c_dyn<From, Ptr> const& handle) const {
    return detail::rtti_cast<Trait>(handle);
  }

  template <template <class> class From, class Ptr>
  constexpr auto operator()(c_dyn<From, Ptr>&& handle) const {
    return detail::rtti_cast<Trait>(std::move(handle));
  }
};

template <template <class> class Trait>
inline constexpr dyn_cast_t<Trait> dyn_cast{};

// Bound object type_info (ngcpp proxy_typeid analogue). Empty -> typeid(void).
template <template <class> class Trait, class Ptr>
const std::type_info& dyn_typeid(dyn<Trait, Ptr> const& h) {
  return h.typeid_of();
}

template <template <class> class Trait, class Ptr>
const std::type_info& dyn_typeid(c_dyn<Trait, Ptr> const& h) {
  return h.typeid_of();
}

#endif  // ASF_HAS_DYN_RTTI

ASF_END_NAMESPACE
