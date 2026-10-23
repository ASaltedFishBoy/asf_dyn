#pragma once

// iface<Trait, T> handle specializations (object / mirror / factory / const).

#include "detail/iface_proxy.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

ASF_BEGIN_NAMESPACE

template <template <class> class Trait, class Spec = void>
struct to_dyn_t;
template <template <class> class Trait,
          template <std::size_t, std::size_t, class> class CachePtr>
struct to_dyn_cache_ptr_t;
template <template <class> class Trait>
struct to_c_dyn_t;

// iface<Trait, T>
//   T object                       Self is T& (or the impl mirror when ann::mirror)
//   T = mirror<View, Src>          sourced mirror specialization
//   const T / mirror<const View, Src>  const-self surface
// c_iface<Trait, T> adds const: plain T -> const T; mirror<View, Src> ->
// mirror<const View, Src>.
// Build one with to_iface<Trait>(object).
namespace detail {

// Plain-object iface stores a pointer (pointer semantics: nullable,
// default-constructible, identity-compareable). Mirror-backed Self stays a
// value (a replaceable reference: no default construct). Sourced identity
// is src_of / mirror_src_ptr, not a public field.
template <typename Self, bool PtrSemantics>
struct iface_self_store {
  Self self_;
  constexpr Self& self() & { return self_; }
  constexpr Self const& self() const& { return self_; }
};

template <typename Self>
struct iface_self_store<Self, true> {
  std::remove_reference_t<Self>* self_ptr_{};
  constexpr Self self() const { return *self_ptr_; }
};

template <template <class> class Trait, class Derived, typename T, typename Src>
class iface_mut_body {
  using Self = typename iface_self_type<Trait, T, Src>::type;
  using fns_type = typename iface_fn_box<Trait, T, Src>::data;
  static constexpr bool allows_mirror_source =
      layout_allows_mirror_source_as<Trait, T>();
  static constexpr bool unsourced_mirror_arg =
      iface_src_is_unsourced_mirror_v<Src>;
  // Mirror Arg (sourced or unsourced): bind is ungated. [[=mirror]] only
  // selects what each call presents as Self (see invoke_method).
  // Pointer semantics for a plain object; mirror Self stays a value.
  static constexpr bool plain_pointer =
      iface_src_is_plain_v<Src> && !allows_mirror_source;
  static constexpr bool has_identity =
      plain_pointer || iface_src_is_sourced_v<Src>;
  iface_self_store<Self, plain_pointer> store_;

  template <template <class> class, class, typename, typename>
  friend class iface_mut_body;
  template <template <class> class, class, typename, typename>
  friend class iface_const_body;
  template <template <class> class, class, class>
  friend struct asf::to_dyn_t;
  template <template <class> class>
  friend struct asf::to_c_dyn_t;

 public:
  template <typename V>
    requires(!std::same_as<std::remove_cvref_t<V>, Derived> &&
             std::constructible_from<Self, V> &&
             (!is_mirror_like_v<std::remove_cvref_t<V>> ||
              allows_mirror_source || !iface_src_is_plain_v<Src>) &&
             (!plain_pointer || std::is_lvalue_reference_v<V>) &&
             (iface_src_is_plain_v<Src>
                  ? (!std::same_as<Self, identity_mirror<T>> ||
                     std::same_as<std::remove_cvref_t<V>, identity_mirror<T>>)
                  : true))
  explicit iface_mut_body(V&& object)
      : store_(make_store(std::forward<V>(object))) {}

  iface_mut_body(const iface_mut_body&) = default;
  iface_mut_body& operator=(const iface_mut_body&) = default;

  // Trait bases either way, and Arg can build this iface.
  template <template <class> class From, typename Arg>
    requires(!std::same_as<Derived, iface<From, Arg>> &&
             (is_trait_base_of<Trait, From>() ||
              is_trait_base_of<From, Trait>()) &&
             (std::constructible_from<Derived, Arg&> ||
              std::constructible_from<Derived, Arg const&>))
  iface_mut_body(iface<From, Arg> o) : iface_mut_body(o.get()) {}

  template <template <class> class From, typename Arg>
    requires(!std::same_as<Derived, iface<From, Arg>> &&
             (is_trait_base_of<Trait, From>() ||
              is_trait_base_of<From, Trait>()) &&
             (std::constructible_from<Derived, Arg&> ||
              std::constructible_from<Derived, Arg const&>))
  Derived& operator=(iface<From, Arg> const& o) {
    return *this = Derived(o.get());
  }

 private:
  template <typename V>
  static iface_self_store<Self, plain_pointer> make_store(V&& object) {
    if constexpr (plain_pointer)
      return {std::addressof(
          static_cast<std::remove_reference_t<Self>&>(object))};
    else
      return {Self(std::forward<V>(object))};
  }

 public:
  using mirror_t = mirror<T, Src>;
  using proxy_t = fn_proxy<Derived, fns_type>;
  using rproxy_t = fn_rproxy<Derived, fns_type>;
  using c_proxy_t =
      fn_proxy<c_iface_shape_t<Trait, T, Src> const,
               typename c_iface_fn_box<Trait, T, Src>::data>;

  // Identity by address (not a null check: an iface is always bound).
  friend bool operator==(Derived const& a, Derived const& b) noexcept
    requires(has_identity) {
    if constexpr (plain_pointer)
      return a.store_.self_ptr_ == b.store_.self_ptr_;
    else
      return iface_src_ptrs_equal<Src, Src>(
          static_cast<const void*>(detail::mirror_src_ptr(a.store_.self_)),
          static_cast<const void*>(detail::mirror_src_ptr(b.store_.self_)));
  }

  template <template <class> class Trait2, typename View, typename Src2>
    requires(!std::same_as<Derived, iface<Trait2, mirror<View, Src2>>> &&
             can_iface_compare<Trait, T, Src, Trait2, View, Src2>())
  friend bool operator==(Derived const& a,
                         iface<Trait2, mirror<View, Src2>> const& b) noexcept {
    if constexpr (plain_pointer)
      return false;
    else
      return iface_src_ptrs_equal<Src, Src2>(
          static_cast<const void*>(detail::mirror_src_ptr(a.store_.self_)),
          static_cast<const void*>(detail::mirror_src_ptr(b.store_.self_)));
  }

  proxy_t proxy() const {
    return proxy_t{const_cast<Derived*>(static_cast<Derived const*>(this))};
  }
  rproxy_t rproxy() const {
    return rproxy_t{const_cast<Derived*>(static_cast<Derived const*>(this))};
  }
  c_proxy_t c_proxy() const {
    return c_iface_shape_t<Trait, T, Src>{
        static_cast<Derived const&>(*this)}
        .proxy();
  }

  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>())
  auto proxy() const {
    if constexpr (std::same_as<iface_shape_t<To, T, Src>, Derived>)
      return proxy_t{const_cast<Derived*>(static_cast<Derived const*>(this))};
    else
      return as<To>().proxy();
  }
  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>())
  auto rproxy() const {
    if constexpr (std::same_as<iface_shape_t<To, T, Src>, Derived>)
      return rproxy_t{const_cast<Derived*>(static_cast<Derived const*>(this))};
    else
      return as<To>().rproxy();
  }
  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>())
  auto c_proxy() const {
    if constexpr (std::same_as<iface_shape_t<To, T, Src>, Derived>)
      return c_proxy();
    else
      return as<To>().c_proxy();
  }

  proxy_t operator->() const { return proxy(); }
  proxy_t operator*() const { return proxy(); }

  explicit operator c_iface_shape_t<Trait, T, Src>() const {
    return c_iface_shape_t<Trait, T, Src>{static_cast<Derived const&>(*this)};
  }

  // Same-object rewrap along the Trait chain (up or down). dyn::as is
  // same-vt / upcast only.
  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>() || is_trait_base_of<Trait, To>())
  iface_shape_t<To, T, Src> as() const {
    return iface_shape_t<To, T, Src>(store_.self());
  }

  template <template <class> class To>
    requires(can_iface_convert_of<
             To, Trait,
             std::conditional_t<
                 iface_src_is_plain_v<Src>, T,
                 std::conditional_t<unsourced_mirror_arg, mirror<T>,
                                    mirror<T, Src>>>>())
  iface_shape_t<To, T, Src> trait_cast() const {
    return iface_shape_t<To, T, Src>(store_.self());
  }

  template <template <class> class To>
    requires(can_iface_convert_of<
             To, Trait,
             std::conditional_t<
                 iface_src_is_plain_v<Src>, T,
                 std::conditional_t<unsourced_mirror_arg, mirror<T>,
                                    mirror<T, Src>>>>())
  c_iface_shape_t<To, T, Src> c_trait_cast() const {
    return c_iface_shape_t<To, T, Src>(store_.self());
  }

  // Copy of stored Self (T& / mirror).
  Self get() const { return store_.self(); }

 private:
  template <class, template <class> class, convert_kind, special_op,
            std::size_t, bool, bool>
  friend struct special_op_slot;
  template <std::meta::info, std::meta::info, std::meta::info>
  friend struct ::asf::call_fn;
  template <class, std::meta::info, std::meta::info, std::meta::info>
  friend struct ::asf::bound_call_fn;
  template <class, std::meta::info, std::meta::info, std::meta::info, class,
            class...>
  friend struct ::asf::bound_call_exact;
  template <class, std::size_t>
  friend struct op_slot;

  template <std::meta::info Method, std::meta::info Impl = std::meta::info{},
            std::meta::info SrcImpl = std::meta::info{}>
  decltype(auto) call(auto&&... args) {
    return invoke_method<Trait, T, Method, Impl, SrcImpl>(
        store_.self(), std::forward<decltype(args)>(args)...);
  }

  template <fixed_string Name, class... Args>
  decltype(auto) call(Args&&... args) {
    constexpr auto method =
        find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<Args>...>(
            Name.view());
    static_assert(method != std::meta::info{},
                  "Interface has no overload matching these arguments");
    return invoke_wired<Trait, T, Src, method>(store_.self(),
                                               std::forward<Args>(args)...);
  }

  template <typename To>
    requires(trait_has_convert<Trait, To>())
  To cast_to() {
    constexpr auto method =
        find_special_op_method_on<^^Trait<void>, To, special_op::convert>();
    return invoke_wired<Trait, T, Src, method>(store_.self());
  }

  template <typename From>
    requires(trait_has_assign<Trait, std::remove_cvref_t<From>>() &&
             (!method_prefers_imple(
                  find_special_op_method_on<
                      ^^Trait<void>, std::remove_cvref_t<From>,
                      special_op::assign>()) ||
              iface_src_is_sourced_v<Src>))
  decltype(auto) cast_assign(From&& rhs) {
    using Key = std::remove_cvref_t<From>;
    constexpr auto method =
        find_special_op_method_on<^^Trait<void>, Key, special_op::assign>();
    return invoke_wired<Trait, T, Src, method>(store_.self(),
                                               std::forward<From>(rhs));
  }
};

template <template <class> class Trait, class Derived, typename T,
          typename Src>
class iface_const_body {
  using Self = typename iface_self_type<Trait, const T, Src>::type;
  using MutSelf = typename iface_self_type<Trait, T, Src>::type;
  using fns_type = typename c_iface_fn_box<Trait, T, Src>::data;
  static constexpr bool allows_mirror_source =
      layout_allows_mirror_source_as<Trait, T>();
  static constexpr bool unsourced_mirror_arg =
      iface_src_is_unsourced_mirror_v<Src>;
  // Mirror Arg: bind ungated; [[=mirror]] selects presented Self at call.
  static constexpr bool plain_pointer =
      iface_src_is_plain_v<Src> && !allows_mirror_source;
  static constexpr bool has_identity =
      plain_pointer || iface_src_is_sourced_v<Src>;
  iface_self_store<Self, plain_pointer> store_;

  template <template <class> class, class, typename, typename>
  friend class iface_mut_body;
  template <template <class> class, class, typename, typename>
  friend class iface_const_body;
  template <template <class> class, class, class>
  friend struct asf::to_dyn_t;
  template <template <class> class>
  friend struct asf::to_c_dyn_t;

 public:
  using mirror_t = Self;
  using proxy_t = fn_proxy<Derived const, fns_type>;
  using rproxy_t = fn_rproxy<Derived const, fns_type>;
  using c_proxy_t = proxy_t;

  template <typename V>
    requires(!std::same_as<std::remove_cvref_t<V>, Derived> &&
             !std::same_as<std::remove_cvref_t<V>, iface_shape_t<Trait, T, Src>> &&
             !std::same_as<std::remove_cvref_t<V>, Self> &&
             !std::same_as<std::remove_cvref_t<V>, MutSelf> &&
             std::constructible_from<Self, V> &&
             (!is_mirror_like_v<std::remove_cvref_t<V>> ||
              allows_mirror_source || !iface_src_is_plain_v<Src>) &&
             (!plain_pointer || std::is_lvalue_reference_v<V>) &&
             (iface_src_is_plain_v<Src>
                  ? (!std::same_as<Self, identity_mirror<const T>> ||
                     std::same_as<std::remove_cvref_t<V>,
                                  identity_mirror<const T>>)
                  : true))
  explicit iface_const_body(V&& object)
      : store_(make_store(std::forward<V>(object))) {}

  explicit iface_const_body(Self self)
    requires(allows_mirror_source || !iface_src_is_plain_v<Src>)
      : store_(make_store(self)) {}
  explicit iface_const_body(MutSelf self)
    requires(allows_mirror_source || !iface_src_is_plain_v<Src>)
      : store_(make_store(self)) {}

  iface_const_body(const iface_const_body&) = default;
  iface_const_body& operator=(const iface_const_body&) = default;

 private:
  template <typename V>
  static iface_self_store<Self, plain_pointer> make_store(V&& object) {
    if constexpr (plain_pointer)
      return {std::addressof(
          static_cast<std::remove_reference_t<Self>&>(object))};
    else
      return {Self(std::forward<V>(object))};
  }

 public:

  template <template <class> class From, typename Arg>
    requires(!std::same_as<Derived, iface<From, Arg>> &&
             (is_trait_base_of<Trait, From>() ||
              is_trait_base_of<From, Trait>()) &&
             (std::constructible_from<Derived, Arg&> ||
              std::constructible_from<Derived, Arg const&>))
  iface_const_body(iface<From, Arg> o) : iface_const_body(o.get()) {}

  template <template <class> class From, typename Arg>
    requires(!std::same_as<Derived, iface<From, Arg>> &&
             (is_trait_base_of<Trait, From>() ||
              is_trait_base_of<From, Trait>()) &&
             (std::constructible_from<Derived, Arg&> ||
              std::constructible_from<Derived, Arg const&>))
  Derived& operator=(iface<From, Arg> const& o) {
    return *this = Derived(o.get());
  }

  // Identity by address (not a null check: an iface is always bound).
  friend bool operator==(Derived const& a, Derived const& b) noexcept
    requires(has_identity) {
    if constexpr (plain_pointer)
      return a.store_.self_ptr_ == b.store_.self_ptr_;
    else
      return iface_src_ptrs_equal<Src, Src>(
          static_cast<const void*>(detail::mirror_src_ptr(a.store_.self_)),
          static_cast<const void*>(detail::mirror_src_ptr(b.store_.self_)));
  }

  proxy_t proxy() const {
    return proxy_t{static_cast<Derived const*>(this)};
  }
  rproxy_t rproxy() const {
    return rproxy_t{static_cast<Derived const*>(this)};
  }
  c_proxy_t c_proxy() const { return proxy(); }

  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>())
  auto proxy() const {
    if constexpr (std::same_as<c_iface_shape_t<To, T, Src>, Derived>)
      return proxy_t{static_cast<Derived const*>(this)};
    else
      return as<To>().proxy();
  }
  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>())
  auto rproxy() const {
    if constexpr (std::same_as<c_iface_shape_t<To, T, Src>, Derived>)
      return rproxy_t{static_cast<Derived const*>(this)};
    else
      return as<To>().rproxy();
  }
  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>())
  auto c_proxy() const {
    return proxy<To>();
  }

  proxy_t operator->() const { return proxy(); }
  proxy_t operator*() const { return proxy(); }

  template <template <class> class To>
    requires(is_trait_base_of<To, Trait>() || is_trait_base_of<Trait, To>())
  c_iface_shape_t<To, T, Src> as() const {
    return c_iface_shape_t<To, T, Src>(store_.self());
  }

  template <template <class> class To>
    requires(can_iface_convert_of<
             To, Trait,
             std::conditional_t<
                 iface_src_is_plain_v<Src>, const T,
                 std::conditional_t<unsourced_mirror_arg, mirror<const T>,
                                    mirror<const T, Src>>>>())
  c_iface_shape_t<To, T, Src> trait_cast() const {
    return c_iface_shape_t<To, T, Src>(store_.self());
  }

  template <template <class> class To>
    requires(can_iface_convert_of<
             To, Trait,
             std::conditional_t<
                 iface_src_is_plain_v<Src>, const T,
                 std::conditional_t<unsourced_mirror_arg, mirror<const T>,
                                    mirror<const T, Src>>>>())
  c_iface_shape_t<To, T, Src> c_trait_cast() const {
    return c_iface_shape_t<To, T, Src>(store_.self());
  }

  Self get() const { return store_.self(); }

 private:
  template <class, template <class> class, convert_kind, special_op,
            std::size_t, bool, bool>
  friend struct special_op_slot;
  template <std::meta::info, std::meta::info, std::meta::info>
  friend struct ::asf::call_fn;
  template <class, std::meta::info, std::meta::info, std::meta::info>
  friend struct ::asf::bound_call_fn;
  template <class, std::meta::info, std::meta::info, std::meta::info, class,
            class...>
  friend struct ::asf::bound_call_exact;
  template <class, std::size_t>
  friend struct op_slot;

  template <std::meta::info Method, std::meta::info Impl = std::meta::info{},
            std::meta::info SrcImpl = std::meta::info{}>
  decltype(auto) call(auto&&... args) const {
    return invoke_method<Trait, const T, Method, Impl, SrcImpl>(
        store_.self(), std::forward<decltype(args)>(args)...);
  }

  template <fixed_string Name, class... Args>
  decltype(auto) call(Args&&... args) const {
    constexpr auto method =
        find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<Args>...>(
            Name.view(), 0, 1);
    static_assert(method != std::meta::info{},
                  "const iface has no const-self overload matching these "
                  "arguments");
    return invoke_wired<Trait, const T, Src, method>(
        store_.self(), std::forward<Args>(args)...);
  }

  template <typename To>
    requires(c_dyn_has_convert<Trait, To>())
  To cast_to() const {
    constexpr auto method =
        find_special_op_method_on<^^Trait<void>, To, special_op::convert>();
    return invoke_wired<Trait, const T, Src, method>(store_.self());
  }
};

}  // namespace detail

template <template <class> class Trait, typename T>
  requires(!std::is_void_v<T> && !std::is_const_v<T> && !is_mirror_v<T>)
class iface<Trait, T>
    : public detail::iface_mut_body<Trait, iface<Trait, T>, T, void> {
  using base = detail::iface_mut_body<Trait, iface<Trait, T>, T, void>;

 public:
  using base::base;
};

template <template <class> class Trait, typename T>
  requires(!std::is_void_v<T> && !is_mirror_v<T>)
class iface<Trait, const T>
    : public detail::iface_const_body<Trait, iface<Trait, const T>, T, void> {
  using base = detail::iface_const_body<Trait, iface<Trait, const T>, T, void>;

 public:
  using base::base;
};

// Unsourced mirror Arg: Self is always the impl mirror; [[=mirror]] is
// checked per method at call time (no layout gate at bind).
template <template <class> class Trait, typename T>
  requires(!std::is_const_v<T>)
class iface<Trait, mirror<T, void>>
    : public detail::iface_mut_body<Trait, iface<Trait, mirror<T>>, T,
                                    detail::unsourced_mirror_src> {
  using base =
      detail::iface_mut_body<Trait, iface<Trait, mirror<T>>, T,
                             detail::unsourced_mirror_src>;

 public:
  using base::base;
};

template <template <class> class Trait, typename T>
class iface<Trait, mirror<const T, void>>
    : public detail::iface_const_body<Trait, iface<Trait, mirror<const T>>, T,
                                      detail::unsourced_mirror_src> {
  using base =
      detail::iface_const_body<Trait, iface<Trait, mirror<const T>>, T,
                               detail::unsourced_mirror_src>;

 public:
  using base::base;
};

template <template <class> class Trait, typename T, typename Src>
  requires(!std::is_const_v<T> && !std::is_void_v<Src>)
class iface<Trait, mirror<T, Src>>
    : public detail::iface_mut_body<Trait, iface<Trait, mirror<T, Src>>, T,
                                    Src> {
  using base =
      detail::iface_mut_body<Trait, iface<Trait, mirror<T, Src>>, T, Src>;

 public:
  using base::base;
};

template <template <class> class Trait, typename T, typename Src>
  requires(!std::is_void_v<Src>)
class iface<Trait, mirror<const T, Src>>
    : public detail::iface_const_body<Trait,
                                      iface<Trait, mirror<const T, Src>>, T,
                                      Src> {
  using base = detail::iface_const_body<Trait,
                                        iface<Trait, mirror<const T, Src>>, T,
                                        Src>;

 public:
  using base::base;
};

template <template <class> class Trait, typename V>
constexpr auto to_iface(V&& object) {
  using Raw = std::remove_cvref_t<V>;
  if constexpr (is_sourced_mirror_v<Raw>) {
    // Bind ungated; [[=mirror]] selects presented Self at each call.
    return iface<Trait, Raw>(std::forward<V>(object));
  } else if constexpr (is_mirror_like_v<Raw>) {
    // Unsourced: keep mirror<View>; bind ungated.
    return iface<Trait, Raw>(std::forward<V>(object));
  } else {
    using As = detail::deduce_iface_t<V>;
    return iface<Trait, As>(std::forward<V>(object));
  }
}

// Plain T -> iface<Trait, const T>. mirror<View, Src> -> mirror<const View, Src>.
template <template <class> class Trait, typename T>
struct c_iface_select {
  using type = iface<Trait, const T>;
};

template <template <class> class Trait, typename View, typename Src>
struct c_iface_select<Trait, mirror<View, Src>> {
  using type = iface<Trait, mirror<const View, Src>>;
};

template <template <class> class Trait, typename T>
using c_iface = typename c_iface_select<Trait, T>::type;

// to_iface's const-view counterpart (symmetric with to_c_dyn next to
// to_dyn): plain object -> iface<Trait, const T>; sourced mirror ->
// iface<Trait, mirror<const View, Src>>; unsourced mirror-like ->
// iface<Trait, mirror<const View>> (Arg stays unsourced mirror).
template <template <class> class Trait, typename V>
constexpr auto to_c_iface(V&& object) {
  using Raw = std::remove_cvref_t<V>;
  if constexpr (is_sourced_mirror_v<Raw>) {
    return c_iface<Trait, Raw>(std::forward<V>(object));
  } else if constexpr (is_mirror_like_v<Raw>) {
    return c_iface<Trait, Raw>(std::forward<V>(object));
  } else {
    using As = detail::deduce_iface_t<V>;
    return c_iface<Trait, As>(std::forward<V>(object));
  }
}

ASF_END_NAMESPACE
