#pragma once

// ref<P> / cref<P>: marks P as "pointer used as ref" -- no direct ctor/assign
// from P. Build only via as_ref / as_cref; no null checks (type seal only).
// Const lives on the hidden base (detail::ref_base), not on the public API.
// cref is const of pointer *: *cref -> const pointee& (like *const T*).
// If P's pointee is already const, ref<P> is exactly cref<P> (std::same_as).

#include "detail/iface_config.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

ASF_BEGIN_NAMESPACE
namespace detail {

template <class P>
constexpr auto ptr_get(P const& p) noexcept {
  if constexpr (std::is_pointer_v<P>)
    return p;
  else
    return p.get();
}

template <class P>
concept pointer_semantic =
    requires(P const& p) {
      { ptr_get(p) };
    } && !std::is_void_v<
             std::remove_pointer_t<decltype(ptr_get(std::declval<P const&>()))>>;

template <class P>
using ptr_pointee_t =
    std::remove_pointer_t<decltype(ptr_get(std::declval<P const&>()))>;

template <class P>
inline constexpr bool is_const_pointee_v = std::is_const_v<ptr_pointee_t<P>>;

struct ref_adopt_t {
  explicit ref_adopt_t() = default;
};
inline constexpr ref_adopt_t ref_adopt{};

template <bool Const, class RawPtr>
constexpr auto ref_view_ptr(RawPtr p) noexcept {
  using Pointee = std::remove_pointer_t<RawPtr>;
  if constexpr (Const)
    return static_cast<Pointee const*>(p);
  else
    return p;
}

template <pointer_semantic P, bool Const>
class ref_base {
  P p_;

 protected:
  constexpr ref_base(ref_adopt_t,
                     P p) noexcept(std::is_nothrow_move_constructible_v<P>)
      : p_(std::move(p)) {}

  template <pointer_semantic U, bool C>
  friend class ref_base;

 public:
  using pointer = P;
  static constexpr bool is_const = Const;

  ref_base() = delete;
  ref_base(std::nullptr_t) = delete;
  ref_base& operator=(std::nullptr_t) = delete;

  ref_base(P const&) = delete;
  ref_base(P&&) = delete;
  ref_base& operator=(P const&) = delete;
  ref_base& operator=(P&&) = delete;

  constexpr ref_base(ref_base const&) = default;
  constexpr ref_base(ref_base&&) = default;
  constexpr ref_base& operator=(ref_base const&) = default;
  constexpr ref_base& operator=(ref_base&&) = default;

  // Mutable base -> const base (like T* -> const T* for *).
  constexpr ref_base(ref_base<P, false> const& o) noexcept
    requires(Const)
      : p_(o.p_) {}
  constexpr ref_base(ref_base<P, false>&& o) noexcept
    requires(Const)
      : p_(std::move(o.p_)) {}

  constexpr P const& get() const noexcept { return p_; }
  constexpr P& get() noexcept { return p_; }

  constexpr decltype(auto) operator*() const
    requires(requires { *ptr_get(p_); }) {
    return *ref_view_ptr<Const>(ptr_get(p_));
  }

  constexpr auto operator->() const noexcept
    requires(std::is_pointer_v<decltype(ptr_get(p_))>) {
    return ref_view_ptr<Const>(ptr_get(p_));
  }

  template <class... Args>
  constexpr decltype(auto) operator()(Args&&... args) const
    requires(requires {
      (*ref_view_ptr<Const>(ptr_get(p_)))(std::forward<Args>(args)...);
    }) {
    return (*ref_view_ptr<Const>(ptr_get(p_)))(std::forward<Args>(args)...);
  }

  constexpr operator P const&() const noexcept { return p_; }
  constexpr operator P&() noexcept { return p_; }

  template <bool C>
  friend constexpr bool operator==(ref_base const& a,
                                   ref_base<P, C> const& b) noexcept {
    return ptr_get(a.p_) == ptr_get(b.get());
  }
};

struct ref_access;

template <pointer_semantic P>
class ref_mut : public ref_base<P, false> {
  using base = ref_base<P, false>;
  friend struct ref_access;

  constexpr ref_mut(ref_adopt_t a, P p) noexcept(
      std::is_nothrow_move_constructible_v<P>)
      : base(a, std::move(p)) {}

 public:
  constexpr ref_mut(ref_mut const&) = default;
  constexpr ref_mut(ref_mut&&) = default;
  constexpr ref_mut& operator=(ref_mut const&) = default;
  constexpr ref_mut& operator=(ref_mut&&) = default;
};

}  // namespace detail

template <detail::pointer_semantic P>
class cref : public detail::ref_base<P, true> {
  using base = detail::ref_base<P, true>;
  friend struct detail::ref_access;

  constexpr cref(detail::ref_adopt_t a, P p) noexcept(
      std::is_nothrow_move_constructible_v<P>)
      : base(a, std::move(p)) {}

 public:
  constexpr cref(cref const&) = default;
  constexpr cref(cref&&) = default;
  constexpr cref& operator=(cref const&) = default;
  constexpr cref& operator=(cref&&) = default;

  // ref_mut / mutable ref_base -> cref
  constexpr cref(detail::ref_base<P, false> const& o) noexcept : base(o) {}
  constexpr cref(detail::ref_base<P, false>&& o) noexcept : base(std::move(o)) {}
};

// Deduce const from P's pointee: const pointee => ref<P> is exactly cref<P>.
template <detail::pointer_semantic P>
using ref = std::conditional_t<detail::is_const_pointee_v<P>, cref<P>,
                               detail::ref_mut<P>>;

namespace detail {

struct ref_access {
  template <class R, class P>
  static constexpr R adopt(P p) noexcept(
      std::is_nothrow_move_constructible_v<P>) {
    return R{ref_adopt, std::move(p)};
  }
};

}  // namespace detail

template <detail::pointer_semantic P>
constexpr ref<P> as_ref(P p) noexcept(std::is_nothrow_move_constructible_v<P>) {
  using R = ref<P>;
  return detail::ref_access::adopt<R>(std::move(p));
}

template <detail::pointer_semantic P>
constexpr cref<P> as_cref(P p) noexcept(
    std::is_nothrow_move_constructible_v<P>) {
  return detail::ref_access::adopt<cref<P>>(std::move(p));
}

ASF_END_NAMESPACE
