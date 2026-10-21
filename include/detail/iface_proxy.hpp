#pragma once

// call_fn / bound_call, fn boxes, op mixins, fn_proxy, compare helpers.

#include "iface_vtable.hpp"

#include <compare>
#include <concepts>
#include <cstddef>
#include <memory>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

ASF_BEGIN_NAMESPACE

template <template <class> class Trait, typename T>
class iface;

// Defaults use bare void* (non-owning observation). Needed for ^^dyn<Trait>
// before the complete class is visible.
template <template <class> class Trait, class Ptr = void*>
class dyn;

template <template <class> class Trait, class Ptr = void*>
class c_dyn;

template <template <class> class Trait, class Ptr = void*>
class sub_dyn;
template <template <class> class Trait, class Ptr = void*>
class c_sub_dyn;

template <std::meta::info Method, std::meta::info Impl = std::meta::info{},
          std::meta::info SrcImpl = std::meta::info{}>
struct call_fn {
  template <class Host>
  decltype(auto) operator()(Host&& host, auto&&... args) const {
    return std::forward<Host>(host).template call<Method, Impl, SrcImpl>(
        std::forward<decltype(args)>(args)...);
  }
};

template <class Host, std::meta::info Method,
          std::meta::info Impl = std::meta::info{},
          std::meta::info SrcImpl = std::meta::info{}>
struct bound_call_fn {
  Host* host{};

  decltype(auto) operator()(auto&&... args) const {
    return call_fn<Method, Impl, SrcImpl>{}(
        *host, std::forward<decltype(args)>(args)...);
  }
};

// Exact-signature binder so same-name overloads resolve via C++ overload rules.
template <class Host, std::meta::info Method, std::meta::info Impl,
          std::meta::info SrcImpl, class R, class... Ps>
struct bound_call_exact {
  Host* host{};

  explicit bound_call_exact(Host* h) noexcept : host(h) {}

  R operator()(Ps... args) const {
    if constexpr (std::is_void_v<R>) {
      call_fn<Method, Impl, SrcImpl>{}(*host, std::forward<Ps>(args)...);
    } else {
      return call_fn<Method, Impl, SrcImpl>{}(*host,
                                              std::forward<Ps>(args)...);
    }
  }
};

template <class... ExactFns>
struct bound_overload_set : ExactFns... {
  using ExactFns::operator()...;

  template <class Host>
  explicit bound_overload_set(Host* h) noexcept : ExactFns(h)... {}
};

template <>
struct bound_overload_set<> {
  template <class Host>
  explicit bound_overload_set(Host*) noexcept {}
};

namespace detail {

template <class T>
inline constexpr bool is_iface_handle_v = false;
template <template <class> class Trait, typename T>
inline constexpr bool is_iface_handle_v<iface<Trait, T>> = !std::is_void_v<T>;

// Const surface: plain const T, or mirror<const View, Src>.
template <class T>
inline constexpr bool is_c_iface_handle_v = false;
template <template <class> class Trait, typename T>
  requires(!is_mirror_v<T>)
inline constexpr bool is_c_iface_handle_v<iface<Trait, const T>> = true;
template <template <class> class Trait, typename T, typename Src>
inline constexpr bool
    is_c_iface_handle_v<iface<Trait, mirror<const T, Src>>> = true;

template <class T>
inline constexpr bool is_dyn_handle_v = false;
template <template <class> class Trait, class Ptr>
inline constexpr bool is_dyn_handle_v<dyn<Trait, Ptr>> = true;

template <class T>
inline constexpr bool is_c_dyn_handle_v = false;
template <template <class> class Trait, class Ptr>
inline constexpr bool is_c_dyn_handle_v<c_dyn<Trait, Ptr>> =
    true;

template <class T>
inline constexpr bool is_sub_dyn_handle_v = false;
template <template <class> class Trait, class Ptr>
inline constexpr bool is_sub_dyn_handle_v<sub_dyn<Trait, Ptr>> =
    true;
template <template <class> class Trait, class Ptr>
inline constexpr bool is_sub_dyn_handle_v<c_sub_dyn<Trait, Ptr>> =
    true;

template <class T>
inline constexpr bool is_any_handle_v =
    is_iface_handle_v<std::remove_cvref_t<T>> ||
    is_c_iface_handle_v<std::remove_cvref_t<T>> ||
    is_dyn_handle_v<std::remove_cvref_t<T>> ||
    is_c_dyn_handle_v<std::remove_cvref_t<T>> ||
    is_sub_dyn_handle_v<std::remove_cvref_t<T>>;

// Reverse free ops live on the iface proxy (*h / proxy()), by Trait
// self-parameter position. Dyn proxy stays no_friend_ops.
struct no_friend_ops {};

template <class X>
decltype(auto) reverse_op_self(X& x) {
  if constexpr (requires { x.buf_.get(); })
    return x.buf_.get();
  else
    return x.get();
}
template <class X>
decltype(auto) reverse_op_self(X const& x) {
  if constexpr (requires { x.buf_.get(); })
    return x.buf_.get();
  else
    return x.get();
}

// iface reverse friends on the proxy. Operator cv follows the Trait
// method's self. Dyn: self must be first or compile error; reverse is
// iface-only (no_dyn). const_self 0 = mutable self, 1 = const self.
#define ASF_IFACE_FRIEND(NAME, OP)                                             \
  template <template <class> class Trait, class D, typename T, typename Src>   \
  struct iface_op_##NAME##_friend {                                            \
    template <typename U>                                                      \
      requires(!is_any_handle_v<std::remove_cvref_t<U>> &&                     \
               trait_has_op_at<^^Trait<void>, std::remove_cvref_t<U>>(          \
                   #NAME, 1, 0))                                               \
    friend decltype(auto) operator OP(U const& a, D& b) {                      \
      constexpr auto method =                                                  \
          find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<U>>(       \
              #NAME, 1, 0);                                                    \
      return invoke_wired<Trait, T, Src, method>(reverse_op_self(b), a);        \
    }                                                                          \
    template <typename U>                                                      \
      requires(!is_any_handle_v<std::remove_cvref_t<U>> &&                     \
               trait_has_op_at<^^Trait<void>, std::remove_cvref_t<U>>(          \
                   #NAME, 1, 0))                                               \
    friend decltype(auto) operator OP(U const& a, D&& b) {                     \
      constexpr auto method =                                                  \
          find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<U>>(       \
              #NAME, 1, 0);                                                    \
      return invoke_wired<Trait, T, Src, method>(reverse_op_self(b), a);        \
    }                                                                          \
  };                                                                           \
  template <template <class> class Trait, class D, typename T, typename Src>   \
  struct iface_op_##NAME##_friend_off {};                                      \
  template <template <class> class Trait, class D, typename T, typename Src>   \
  using iface_op_##NAME##_rhs = std::conditional_t<                            \
      trait_has_op_named_at(^^Trait<void>, #NAME, 1, 0),                       \
      iface_op_##NAME##_friend<Trait, D, T, Src>,                              \
      iface_op_##NAME##_friend_off<Trait, D, T, Src>>;                         \
  template <template <class> class Trait, class D, typename T, typename Src>   \
  struct iface_op_##NAME##_cfriend {                                           \
    template <typename U>                                                      \
      requires(!is_any_handle_v<std::remove_cvref_t<U>> &&                     \
               trait_has_op_at<^^Trait<void>, std::remove_cvref_t<U>>(          \
                   #NAME, 1, 1))                                               \
    friend decltype(auto) operator OP(U const& a, D const& b) {                \
      constexpr auto method =                                                  \
          find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<U>>(       \
              #NAME, 1, 1);                                                    \
      return invoke_wired<Trait, T, Src, method>(reverse_op_self(b), a);        \
    }                                                                          \
  };                                                                           \
  template <template <class> class Trait, class D, typename T, typename Src>   \
  struct iface_op_##NAME##_cfriend_off {};                                     \
  template <template <class> class Trait, class D, typename T, typename Src>   \
  using iface_op_##NAME##_crhs = std::conditional_t<                           \
      trait_has_op_named_at(^^Trait<void>, #NAME, 1, 1),                       \
      iface_op_##NAME##_cfriend<Trait, D, T, Src>,                             \
      iface_op_##NAME##_cfriend_off<Trait, D, T, Src>>;

#define ASF_IFACE_BINOP_PAIR(NAME, OP)                                         \
  ASF_IFACE_FRIEND(NAME, OP)

ASF_IFACE_BINOP_PAIR(op_eq, ==)
ASF_IFACE_BINOP_PAIR(op_ne, !=)
ASF_IFACE_BINOP_PAIR(op_lt, <)
ASF_IFACE_BINOP_PAIR(op_gt, >)
ASF_IFACE_BINOP_PAIR(op_le, <=)
ASF_IFACE_BINOP_PAIR(op_ge, >=)
ASF_IFACE_BINOP_PAIR(op_cmp, <=>)
ASF_IFACE_BINOP_PAIR(op_add, +)
ASF_IFACE_BINOP_PAIR(op_sub, -)
ASF_IFACE_BINOP_PAIR(op_mul, *)
ASF_IFACE_BINOP_PAIR(op_div, /)
ASF_IFACE_BINOP_PAIR(op_mod, %)
ASF_IFACE_BINOP_PAIR(op_xor, ^)
ASF_IFACE_BINOP_PAIR(op_bitand, &)
ASF_IFACE_BINOP_PAIR(op_bitor, |)
ASF_IFACE_BINOP_PAIR(op_lshift, <<)
ASF_IFACE_BINOP_PAIR(op_rshift, >>)
ASF_IFACE_BINOP_PAIR(op_ptr_to_mem, ->*)

#undef ASF_IFACE_BINOP_PAIR
#undef ASF_IFACE_FRIEND

template <template <class> class Trait, class D, typename T, typename Src>
struct iface_op_op_comma_friend {
  template <typename U>
    requires(!is_any_handle_v<std::remove_cvref_t<U>> &&
             trait_has_op_at<^^Trait<void>, std::remove_cvref_t<U>>(
                 "op_comma", 1, 0))
  friend decltype(auto) operator,(U const& a, D& b) {
    constexpr auto method =
        find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<U>>(
            "op_comma", 1, 0);
    return invoke_wired<Trait, T, Src, method>(reverse_op_self(b), a);
  }
  template <typename U>
    requires(!is_any_handle_v<std::remove_cvref_t<U>> &&
             trait_has_op_at<^^Trait<void>, std::remove_cvref_t<U>>(
                 "op_comma", 1, 0))
  friend decltype(auto) operator,(U const& a, D&& b) {
    constexpr auto method =
        find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<U>>(
            "op_comma", 1, 0);
    return invoke_wired<Trait, T, Src, method>(reverse_op_self(b), a);
  }
};
template <template <class> class Trait, class D, typename T, typename Src>
struct iface_op_op_comma_friend_off {};
template <template <class> class Trait, class D, typename T, typename Src>
using iface_op_op_comma_rhs = std::conditional_t<
    trait_has_op_named_at(^^Trait<void>, "op_comma", 1, 0),
    iface_op_op_comma_friend<Trait, D, T, Src>,
    iface_op_op_comma_friend_off<Trait, D, T, Src>>;
template <template <class> class Trait, class D, typename T, typename Src>
struct iface_op_op_comma_cfriend {
  template <typename U>
    requires(!is_any_handle_v<std::remove_cvref_t<U>> &&
             trait_has_op_at<^^Trait<void>, std::remove_cvref_t<U>>(
                 "op_comma", 1, 1))
  friend decltype(auto) operator,(U const& a, D const& b) {
    constexpr auto method =
        find_method_for_args_on<^^Trait<void>, std::remove_cvref_t<U>>(
            "op_comma", 1, 1);
    return invoke_wired<Trait, T, Src, method>(reverse_op_self(b), a);
  }
};
template <template <class> class Trait, class D, typename T, typename Src>
struct iface_op_op_comma_cfriend_off {};
template <template <class> class Trait, class D, typename T, typename Src>
using iface_op_op_comma_crhs = std::conditional_t<
    trait_has_op_named_at(^^Trait<void>, "op_comma", 1, 1),
    iface_op_op_comma_cfriend<Trait, D, T, Src>,
    iface_op_op_comma_cfriend_off<Trait, D, T, Src>>;

template <template <class> class Trait, class D, typename T, typename Src,
          bool Mutating = true>
struct iface_revops;

// Const-self reverse: D const& friends. On every iface proxy.
template <template <class> class Trait, class D, typename T, typename Src>
struct iface_revops<Trait, D, T, Src, false>
    : iface_op_op_eq_crhs<Trait, D, T, Src>,
      iface_op_op_ne_crhs<Trait, D, T, Src>,
      iface_op_op_lt_crhs<Trait, D, T, Src>,
      iface_op_op_gt_crhs<Trait, D, T, Src>,
      iface_op_op_le_crhs<Trait, D, T, Src>,
      iface_op_op_ge_crhs<Trait, D, T, Src>,
      iface_op_op_cmp_crhs<Trait, D, T, Src>,
      iface_op_op_add_crhs<Trait, D, T, Src>,
      iface_op_op_sub_crhs<Trait, D, T, Src>,
      iface_op_op_mul_crhs<Trait, D, T, Src>,
      iface_op_op_div_crhs<Trait, D, T, Src>,
      iface_op_op_mod_crhs<Trait, D, T, Src>,
      iface_op_op_xor_crhs<Trait, D, T, Src>,
      iface_op_op_bitand_crhs<Trait, D, T, Src>,
      iface_op_op_bitor_crhs<Trait, D, T, Src>,
      iface_op_op_lshift_crhs<Trait, D, T, Src>,
      iface_op_op_rshift_crhs<Trait, D, T, Src>,
      iface_op_op_ptr_to_mem_crhs<Trait, D, T, Src>,
      iface_op_op_comma_crhs<Trait, D, T, Src> {};

// Mutable-self reverse: D& / D&& friends. Mutable iface proxy only.
template <template <class> class Trait, class D, typename T, typename Src>
struct iface_revops<Trait, D, T, Src, true>
    : iface_revops<Trait, D, T, Src, false>,
      iface_op_op_eq_rhs<Trait, D, T, Src>,
      iface_op_op_ne_rhs<Trait, D, T, Src>,
      iface_op_op_lt_rhs<Trait, D, T, Src>,
      iface_op_op_gt_rhs<Trait, D, T, Src>,
      iface_op_op_le_rhs<Trait, D, T, Src>,
      iface_op_op_ge_rhs<Trait, D, T, Src>,
      iface_op_op_cmp_rhs<Trait, D, T, Src>,
      iface_op_op_add_rhs<Trait, D, T, Src>,
      iface_op_op_sub_rhs<Trait, D, T, Src>,
      iface_op_op_mul_rhs<Trait, D, T, Src>,
      iface_op_op_div_rhs<Trait, D, T, Src>,
      iface_op_op_mod_rhs<Trait, D, T, Src>,
      iface_op_op_xor_rhs<Trait, D, T, Src>,
      iface_op_op_bitand_rhs<Trait, D, T, Src>,
      iface_op_op_bitor_rhs<Trait, D, T, Src>,
      iface_op_op_lshift_rhs<Trait, D, T, Src>,
      iface_op_op_rshift_rhs<Trait, D, T, Src>,
      iface_op_op_ptr_to_mem_rhs<Trait, D, T, Src>,
      iface_op_op_comma_rhs<Trait, D, T, Src> {};

}  // namespace detail

namespace detail {

consteval std::meta::info bound_call_info(
    std::meta::info host, std::meta::info method,
    std::meta::info impl = std::meta::info{},
    std::meta::info srcimpl = std::meta::info{}) {
  return std::meta::substitute(
      ^^bound_call_fn,
      {host, std::meta::reflect_constant(method),
       std::meta::reflect_constant(impl),
       std::meta::reflect_constant(srcimpl)});
}

template <std::meta::info Method>
consteval std::meta::info bound_call_exact_from(
    std::meta::info host, std::meta::info impl, std::meta::info srcimpl) {
  constexpr auto inst = signature_decl_of<Method>();
  if (inst == std::meta::info{})
    return bound_call_info(host, Method, impl, srcimpl);
  std::vector<std::meta::info> targs;
  targs.push_back(host);
  targs.push_back(std::meta::reflect_constant(Method));
  targs.push_back(std::meta::reflect_constant(impl));
  targs.push_back(std::meta::reflect_constant(srcimpl));
  try {
    targs.push_back(std::meta::return_type_of(inst));
  } catch (std::meta::exception const&) {
    return bound_call_info(host, Method, impl, srcimpl);
  }
  std::size_t self_i = self_param_index(Method);
  auto params = std::meta::parameters_of(inst);
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i == self_i)
      continue;
    targs.push_back(std::meta::type_of(params[i]));
  }
  return std::meta::substitute(^^bound_call_exact, targs);
}

consteval std::meta::info bound_call_exact_info(
    std::meta::info host, std::meta::info method,
    std::meta::info impl = std::meta::info{},
    std::meta::info srcimpl = std::meta::info{}) {
  auto inst = instantiate(method, ^^signature_self);
  if (inst == std::meta::info{})
    return bound_call_info(host, method, impl, srcimpl);
  std::vector<std::meta::info> targs;
  targs.push_back(host);
  targs.push_back(std::meta::reflect_constant(method));
  targs.push_back(std::meta::reflect_constant(impl));
  targs.push_back(std::meta::reflect_constant(srcimpl));
  try {
    targs.push_back(std::meta::return_type_of(inst));
  } catch (std::meta::exception const&) {
    return bound_call_info(host, method, impl, srcimpl);
  }
  std::size_t self_i = self_param_index(method);
  auto params = std::meta::parameters_of(inst);
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i == self_i)
      continue;
    targs.push_back(std::meta::type_of(params[i]));
  }
  return std::meta::substitute(^^bound_call_exact, targs);
}

consteval std::meta::info bound_overload_info(
    std::meta::info host, std::vector<std::meta::info> const& methods,
    auto&& impl_of, auto&& srcimpl_of) {
  if (methods.empty())
    return ^^bound_overload_set<>;
  if (methods.size() == 1)
    return bound_call_exact_info(host, methods[0], impl_of(methods[0]),
                                 srcimpl_of(methods[0]));
  std::vector<std::meta::info> exacts;
  exacts.reserve(methods.size());
  for (auto m : methods)
    exacts.push_back(
        bound_call_exact_info(host, m, impl_of(m), srcimpl_of(m)));
  return std::meta::substitute(^^bound_overload_set, exacts);
}

template <template <class> class Trait, typename T, typename Src>
consteval std::meta::info iface_mut_host() {
  if constexpr (std::is_void_v<Src>)
    return ^^iface<Trait, T>;
  else
    return ^^iface<Trait, mirror<T, Src>>;
}

template <template <class> class Trait, typename T, typename Src>
consteval std::meta::info iface_const_host() {
  if constexpr (std::is_void_v<Src>)
    return ^^iface<Trait, const T> const;
  else
    return ^^iface<Trait, mirror<const T, Src>> const;
}

template <template <class> class Trait, typename T, typename Src, std::size_t I>
consteval std::meta::info iface_bound_at() {
  constexpr auto m = trait_methods(^^Trait<void>)[I];
  if (!std::meta::has_identifier(m))
    return std::meta::info{};
  auto name = std::meta::identifier_of(m);
  if (name.starts_with("operator") || is_any_special_op_name(name))
    return std::meta::info{};
  if (self_param_index(m) != 0)
    return std::meta::info{};
  return bound_call_exact_from<m>(
      iface_mut_host<Trait, T, Src>(), trait_impl_of<Trait, T, m>(),
      src_impl_of<Trait, T,
                  std::conditional_t<std::is_void_v<Src>, T, Src>>(m));
}

template <template <class> class Trait, typename T, typename Src,
          std::size_t... I>
consteval std::vector<std::meta::info>
iface_fn_specs_impl(std::index_sequence<I...>) {
  std::meta::info bounds[] = {iface_bound_at<Trait, T, Src, I>()...};
  std::string_view names[] = {
      std::meta::has_identifier(trait_methods(^^Trait<void>)[I])
          ? std::meta::identifier_of(trait_methods(^^Trait<void>)[I])
          : std::string_view{}...};
  std::vector<std::meta::info> specs;
  std::vector<std::string_view> seen;
  constexpr std::size_t n = sizeof...(I);
  for (std::size_t i = 0; i < n; ++i) {
    if (bounds[i] == std::meta::info{})
      continue;
    auto name = names[i];
    bool dup = false;
    for (auto s : seen) {
      if (s == name) {
        dup = true;
        break;
      }
    }
    if (dup)
      continue;
    seen.push_back(name);
    std::vector<std::meta::info> exacts;
    for (std::size_t j = 0; j < n; ++j) {
      if (bounds[j] != std::meta::info{} && names[j] == name)
        exacts.push_back(bounds[j]);
    }
    std::meta::info mem_ty =
        exacts.size() == 1
            ? exacts[0]
            : std::meta::substitute(^^bound_overload_set, exacts);
    specs.push_back(
        std::meta::data_member_spec(mem_ty, {.name = std::string(name)}));
  }
  return specs;
}

template <template <class> class Trait, typename T, typename Src>
consteval std::vector<std::meta::info> iface_fn_specs() {
  constexpr auto n = trait_methods(^^Trait<void>).size();
  if constexpr (n == 0)
    return {};
  else
    return iface_fn_specs_impl<Trait, T, Src>(std::make_index_sequence<n>{});
}

template <template <class> class Trait, typename T, typename Src>
struct iface_fn_box {
  struct data;
  consteval {
    std::meta::define_aggregate(^^data, iface_fn_specs<Trait, T, Src>());
  }
};

template <template <class> class Trait, typename T, typename Src, std::size_t I>
consteval std::meta::info c_iface_bound_at() {
  constexpr auto m = trait_methods(^^Trait<void>)[I];
  if (!std::meta::has_identifier(m) || !is_const_self_method(m))
    return std::meta::info{};
  auto name = std::meta::identifier_of(m);
  if (name.starts_with("operator") || is_any_special_op_name(name))
    return std::meta::info{};
  if (self_param_index(m) != 0)
    return std::meta::info{};
  return bound_call_exact_from<m>(
      iface_const_host<Trait, T, Src>(),
      trait_impl_of<Trait, const T, m>(),
      src_impl_of<Trait, const T,
                  std::conditional_t<std::is_void_v<Src>,
                                     std::remove_cv_t<T>, Src>>(m));
}

template <template <class> class Trait, typename T, typename Src,
          std::size_t... I>
consteval std::vector<std::meta::info>
c_iface_fn_specs_impl(std::index_sequence<I...>) {
  std::meta::info bounds[] = {c_iface_bound_at<Trait, T, Src, I>()...};
  std::string_view names[] = {
      std::meta::has_identifier(trait_methods(^^Trait<void>)[I])
          ? std::meta::identifier_of(trait_methods(^^Trait<void>)[I])
          : std::string_view{}...};
  std::vector<std::meta::info> specs;
  std::vector<std::string_view> seen;
  constexpr std::size_t n = sizeof...(I);
  for (std::size_t i = 0; i < n; ++i) {
    if (bounds[i] == std::meta::info{})
      continue;
    auto name = names[i];
    bool dup = false;
    for (auto s : seen) {
      if (s == name) {
        dup = true;
        break;
      }
    }
    if (dup)
      continue;
    seen.push_back(name);
    std::vector<std::meta::info> exacts;
    for (std::size_t j = 0; j < n; ++j) {
      if (bounds[j] != std::meta::info{} && names[j] == name)
        exacts.push_back(bounds[j]);
    }
    std::meta::info mem_ty =
        exacts.size() == 1
            ? exacts[0]
            : std::meta::substitute(^^bound_overload_set, exacts);
    specs.push_back(
        std::meta::data_member_spec(mem_ty, {.name = std::string(name)}));
  }
  return specs;
}

// iface<Trait, const T>: const-self methods only (parallel to c_dyn).
template <template <class> class Trait, typename T, typename Src>
consteval std::vector<std::meta::info> c_iface_fn_specs() {
  constexpr auto n = trait_methods(^^Trait<void>).size();
  if constexpr (n == 0)
    return {};
  else
    return c_iface_fn_specs_impl<Trait, T, Src>(std::make_index_sequence<n>{});
}

template <template <class> class Trait, typename T, typename Src>
struct c_iface_fn_box {
  struct data;
  consteval {
    std::meta::define_aggregate(^^data, c_iface_fn_specs<Trait, T, Src>());
  }
};

// ConstOnly: c_dyn surface (const-self methods); host is dyn<Trait> or
// c_dyn<Trait> const for bound_call_fn.
// Proxy method list: no op_construct / op_assign / op_convert (special).
// Destroy is vt[-1] (~Src), not a method slot. Same-name overloads share
// one member. Static methods are iface-only, never on the dyn proxy.
template <template <class> class Trait, bool ConstOnly, std::size_t I>
consteval std::meta::info dyn_bound_at(std::meta::info host) {
  constexpr auto m = dyn_methods(^^Trait<void>)[I];
  if (!std::meta::has_identifier(m))
    return std::meta::info{};
  if (ConstOnly && !is_const_self_method(m))
    return std::meta::info{};
  auto name = std::meta::identifier_of(m);
  if (name.starts_with("operator") || is_any_special_op_name(name))
    return std::meta::info{};
  return bound_call_exact_from<m>(host, std::meta::info{}, std::meta::info{});
}

template <template <class> class Trait, bool ConstOnly, std::size_t... I>
consteval std::vector<std::meta::info>
dyn_like_fn_specs_impl(std::meta::info host, std::index_sequence<I...>) {
  std::meta::info bounds[] = {dyn_bound_at<Trait, ConstOnly, I>(host)...};
  std::string_view names[] = {
      std::meta::has_identifier(dyn_methods(^^Trait<void>)[I])
          ? std::meta::identifier_of(dyn_methods(^^Trait<void>)[I])
          : std::string_view{}...};
  std::vector<std::meta::info> specs;
  std::vector<std::string_view> seen;
  constexpr std::size_t n = sizeof...(I);
  for (std::size_t i = 0; i < n; ++i) {
    if (bounds[i] == std::meta::info{})
      continue;
    auto name = names[i];
    bool dup = false;
    for (auto s : seen) {
      if (s == name) {
        dup = true;
        break;
      }
    }
    if (dup)
      continue;
    seen.push_back(name);
    std::vector<std::meta::info> exacts;
    for (std::size_t j = 0; j < n; ++j) {
      if (bounds[j] != std::meta::info{} && names[j] == name)
        exacts.push_back(bounds[j]);
    }
    std::meta::info mem_ty =
        exacts.size() == 1
            ? exacts[0]
            : std::meta::substitute(^^bound_overload_set, exacts);
    specs.push_back(
        std::meta::data_member_spec(mem_ty, {.name = std::string(name)}));
  }
  return specs;
}

template <template <class> class Trait, bool ConstOnly>
consteval std::vector<std::meta::info>
dyn_like_fn_specs(std::meta::info host) {
  constexpr auto n = dyn_methods(^^Trait<void>).size();
  if constexpr (n == 0)
    return {};
  else
    return dyn_like_fn_specs_impl<Trait, ConstOnly>(
        host, std::make_index_sequence<n>{});
}

template <template <class> class Trait, bool ConstOnly>
struct dyn_like_fn_box {
  struct data;
  consteval {
    if constexpr (ConstOnly)
      std::meta::define_aggregate(
          ^^data, dyn_like_fn_specs<Trait, true>(^^c_dyn<Trait> const));
    else
      std::meta::define_aggregate(
          ^^data, dyn_like_fn_specs<Trait, false>(^^dyn<Trait>));
  }
};

template <template <class> class Trait>
using dyn_fn_box = dyn_like_fn_box<Trait, false>;

template <template <class> class Trait>
using c_dyn_fn_box = dyn_like_fn_box<Trait, true>;

// Shared dyn / c_dyn dispatch (ptr+vt erased handle core).
template <template <class> class Trait, typename Key, special_op Op>
decltype(auto) handle_cast_special(void* ptr, vt_ptr vt,
                                   auto&&... args) {
  constexpr auto index = dyn_special_op_index<Trait, Key, Op>();
  constexpr auto method = dyn_method_at<Trait, index>();
  // Construct (ann::imple or custom default): void(dst) into Key-sized buffer.
  if constexpr (Op == special_op::construct) {
    using Erased = erased_fn<void, void*>;
    alignas(Key) std::byte buf[sizeof(Key)];
    Erased::dispatch(vt[index], ptr, vt, static_cast<void*>(buf));
    Key out{std::move(*reinterpret_cast<Key*>(buf))};
    std::destroy_at(reinterpret_cast<Key*>(buf));
    return out;
  } else {
    constexpr auto inst = signature_decl_of<method>();
    static_assert(inst != std::meta::info{},
                  "cannot determine dyn special-op signature");
    using Erased = [:erased_sig(inst):];
    return Erased::dispatch(vt[index], ptr, vt,
                            std::forward<decltype(args)>(args)...);
  }
}

template <template <class> class Trait, std::meta::info Method,
          bool RequireConst, bool AllowReset>
decltype(auto) handle_call(void* ptr, vt_ptr vt,
                           void** reset_ptr, vt_ptr* reset_vt,
                           auto&&... args) {
  constexpr auto index = dyn_index_of_method<Trait>(Method);
  static_assert(index != static_cast<std::size_t>(-1),
                "method is not a dyn slot (static methods are iface-only)");
  if constexpr (RequireConst)
    static_assert(is_const_self_method(Method),
                  "c_dyn cannot call a non-const Interface method");
  // Destroy is vt[-1] (~Src), not a method. reset_* stay for call sites.
  (void)AllowReset;
  (void)reset_ptr;
  (void)reset_vt;
  constexpr auto inst = signature_decl_of<Method>();
  static_assert(inst != std::meta::info{},
                "cannot determine dyn method signature");
  using Erased = [:erased_sig(inst):];
  return Erased::dispatch(vt[index], ptr, vt,
                          std::forward<decltype(args)>(args)...);
}

// Call View's method using Layout's flat indices (same vt, no convert).
template <template <class> class Layout, template <class> class View,
          std::meta::info Method, bool RequireConst>
decltype(auto) handle_call_in(void* ptr, vt_ptr vt,
                              auto&&... args) {
  constexpr auto index =
      dyn_index_of_method_in_layout<Layout, View>(Method);
  static_assert(index != static_cast<std::size_t>(-1),
                "View method not present on Layout table");
  if constexpr (RequireConst)
    static_assert(is_const_self_method(Method),
                  "const view cannot call a non-const Interface method");
  constexpr auto inst = signature_decl_of<Method>();
  static_assert(inst != std::meta::info{},
                "cannot determine dyn method signature");
  using Erased = [:erased_sig(inst):];
  return Erased::dispatch(vt[index], ptr, vt,
                          std::forward<decltype(args)>(args)...);
}

// sub_dyn: vt_ is the Final table; slice_off selects Trait's own section.
template <template <class> class Trait, std::meta::info Method,
          bool RequireConst>
decltype(auto) handle_call_own_slice(void* ptr, vt_ptr vt,
                                     std::size_t slice_off, auto&&... args) {
  constexpr auto index = own_dyn_method_index<Trait>(Method);
  static_assert(index != static_cast<std::size_t>(-1),
                "method is not a dyn slot on this Trait section");
  if constexpr (RequireConst)
    static_assert(is_const_self_method(Method),
                  "const sub_dyn cannot call a non-const Interface method");
  constexpr auto inst = signature_decl_of<Method>();
  static_assert(inst != std::meta::info{},
                "cannot determine dyn method signature");
  using Erased = [:erased_sig(inst):];
  return Erased::dispatch(vt[slice_off + index], ptr, vt,
                          std::forward<decltype(args)>(args)...);
}

template <class Fns, class Host, std::size_t I>
constexpr auto nth_bound_fn(Host* host) {
  constexpr auto mem =
      std::meta::nonstatic_data_members_of(^^Fns, unchecked())[I];
  using Fn = [:std::meta::type_of(mem):];
  return Fn{host};
}

template <class Fns, class Host, std::size_t... I>
constexpr Fns make_bound_fns([[maybe_unused]] Host* host,
                             std::index_sequence<I...>) {
  return Fns{nth_bound_fn<Fns, Host, I>(host)...};
}

template <class Fns>
consteval std::size_t fn_member_count() {
  return std::meta::nonstatic_data_members_of(^^Fns, unchecked()).size();
}

consteval bool is_operator_name(std::string_view name) {
  return name.starts_with("operator");
}

// Trait method names (legal identifiers). Proxy still exposes C++ operators.
// Index 1 is TOKEN '-': maps to op_neg (unary) and/or op_sub (binary).
inline constexpr std::string_view op_catalog[] = {
    "op_add",    "op_sub",    "op_mul",    "op_div",    "op_mod",
    "op_xor",    "op_bitand", "op_bitor",  "op_compl",  "op_not",
    "op_lt",     "op_gt",     "op_iadd",   "op_isub",   "op_imul",
    "op_idiv",   "op_imod",   "op_ixor",   "op_ibitand","op_ibitor",
    "op_lshift", "op_rshift", "op_ilshift","op_irshift","op_eq",
    "op_ne",     "op_le",     "op_ge",     "op_cmp",    "op_and",
    "op_or",     "op_inc",    "op_dec",    "op_index",  "op_call",
    "op_comma",  "op_ptr_to_mem",
};

inline constexpr std::size_t op_count =
    sizeof(op_catalog) / sizeof(op_catalog[0]);

template <template <class> class Trait>
consteval bool iface_exposes_op(std::string_view name) {
  return find_method(^^Trait<void>, name) != std::meta::info{};
}

template <template <class> class Trait>
consteval bool dyn_exposes_op(std::string_view name) {
  return dyn_index<Trait>(name) != static_cast<std::size_t>(-1);
}

// Same dyn-slot rule as dyn_exposes_op (dyn slots only; static methods
// are never dyn slots). c_dyn additionally requires a const-self overload
// of that name.
template <template <class> class Trait>
consteval bool c_dyn_exposes_op(std::string_view name) {
  for (auto m : find_methods_named(^^Trait<void>, name)) {
    if (!is_const_self_method(m))
      continue;
    if (dyn_index_of_method<Trait>(m) != static_cast<std::size_t>(-1))
      return true;
  }
  return false;
}

// Const iface: name exists and some overload is const-self.
template <template <class> class Trait>
consteval bool c_iface_exposes_op(std::string_view name) {
  for (auto m : find_methods_named(^^Trait<void>, name)) {
    if (is_const_self_method(m))
      return true;
  }
  return false;
}

// Catalog index 1 is '-' : enable if either op_neg or op_sub exists.
template <template <class> class Trait>
consteval bool iface_exposes_op_at(std::size_t i) {
  if (i == 1)
    return iface_exposes_op<Trait>("op_neg") ||
           iface_exposes_op<Trait>("op_sub");
  return iface_exposes_op<Trait>(op_catalog[i]);
}

template <template <class> class Trait>
consteval bool dyn_exposes_op_at(std::size_t i) {
  if (i == 1)
    return dyn_exposes_op<Trait>("op_neg") || dyn_exposes_op<Trait>("op_sub");
  return dyn_exposes_op<Trait>(op_catalog[i]);
}

template <template <class> class Trait>
consteval bool c_dyn_exposes_op_at(std::size_t i) {
  if (i == 1)
    return c_dyn_exposes_op<Trait>("op_neg") ||
           c_dyn_exposes_op<Trait>("op_sub");
  return c_dyn_exposes_op<Trait>(op_catalog[i]);
}

template <template <class> class Trait>
consteval bool c_iface_exposes_op_at(std::size_t i) {
  if (i == 1)
    return c_iface_exposes_op<Trait>("op_neg") ||
           c_iface_exposes_op<Trait>("op_sub");
  return c_iface_exposes_op<Trait>(op_catalog[i]);
}

// Per-catalog-index operator mixin (only inherited when Trait exposes it).
template <class Host, std::size_t I>
struct op_slot;

#define ASF_DEF_OP(I, LIT, TOKEN)                                             \
  template <class Host>                                                       \
  struct op_slot<Host, I> {                                                   \
    Host* host_{};                                                            \
    explicit op_slot(Host* host) noexcept : host_(host) {}                    \
    decltype(auto) operator TOKEN(auto&&... args) {                           \
      return host_->template call<LIT>(                                       \
          std::forward<decltype(args)>(args)...);                             \
    }                                                                         \
    decltype(auto) operator TOKEN(auto&&... args) const {                     \
      return host_->template call<LIT>(                                       \
          std::forward<decltype(args)>(args)...);                             \
    }                                                                         \
  }

#define ASF_DEF_OP_UNARY(I, LIT, TOKEN)                                       \
  template <class Host>                                                       \
  struct op_slot<Host, I> {                                                   \
    Host* host_{};                                                            \
    explicit op_slot(Host* host) noexcept : host_(host) {}                    \
    decltype(auto) operator TOKEN() {                                         \
      return host_->template call<LIT>();                                     \
    }                                                                         \
    decltype(auto) operator TOKEN() const {                                   \
      return host_->template call<LIT>();                                     \
    }                                                                         \
  }

ASF_DEF_OP(0, "op_add", +);
// Index 1: unary - -> op_neg; binary - -> op_sub (arity distinguishes).
template <class Host>
struct op_slot<Host, 1> {
  Host* host_{};
  explicit op_slot(Host* host) noexcept : host_(host) {}
  decltype(auto) operator-() {
    return host_->template call<"op_neg">();
  }
  decltype(auto) operator-() const {
    return host_->template call<"op_neg">();
  }
  decltype(auto) operator-(auto&&... args) {
    return host_->template call<"op_sub">(
        std::forward<decltype(args)>(args)...);
  }
  decltype(auto) operator-(auto&&... args) const {
    return host_->template call<"op_sub">(
        std::forward<decltype(args)>(args)...);
  }
};
ASF_DEF_OP(2, "op_mul", *);
ASF_DEF_OP(3, "op_div", /);
ASF_DEF_OP(4, "op_mod", %);
ASF_DEF_OP(5, "op_xor", ^);
// Binary & only -- unary & must remain built-in address-of.
template <class Host>
struct op_slot<Host, 6> {
  Host* host_{};
  explicit op_slot(Host* host) noexcept : host_(host) {}
  decltype(auto) operator&(auto&& rhs) {
    return host_->template call<"op_bitand">(
        std::forward<decltype(rhs)>(rhs));
  }
  decltype(auto) operator&(auto&& rhs) const {
    return host_->template call<"op_bitand">(
        std::forward<decltype(rhs)>(rhs));
  }
};
ASF_DEF_OP(7, "op_bitor", |);
ASF_DEF_OP_UNARY(8, "op_compl", ~);
ASF_DEF_OP_UNARY(9, "op_not", !);
ASF_DEF_OP(10, "op_lt", <);
ASF_DEF_OP(11, "op_gt", >);
ASF_DEF_OP(12, "op_iadd", +=);
ASF_DEF_OP(13, "op_isub", -=);
ASF_DEF_OP(14, "op_imul", *=);
ASF_DEF_OP(15, "op_idiv", /=);
ASF_DEF_OP(16, "op_imod", %=);
ASF_DEF_OP(17, "op_ixor", ^=);
ASF_DEF_OP(18, "op_ibitand", &=);
ASF_DEF_OP(19, "op_ibitor", |=);
ASF_DEF_OP(20, "op_lshift", <<);
ASF_DEF_OP(21, "op_rshift", >>);
ASF_DEF_OP(22, "op_ilshift", <<=);
ASF_DEF_OP(23, "op_irshift", >>=);
ASF_DEF_OP(24, "op_eq", ==);
ASF_DEF_OP(25, "op_ne", !=);
ASF_DEF_OP(26, "op_le", <=);
ASF_DEF_OP(27, "op_ge", >=);
ASF_DEF_OP(28, "op_cmp", <=>);
ASF_DEF_OP(29, "op_and", &&);
ASF_DEF_OP(30, "op_or", ||);
// Index 31/32: prefix vs postfix distinguished by arity (0 vs int).
template <class Host>
struct op_slot<Host, 31> {
  Host* host_{};
  explicit op_slot(Host* host) noexcept : host_(host) {}
  decltype(auto) operator++() { return host_->template call<"op_inc">(); }
  decltype(auto) operator++(int) {
    return host_->template call<"op_inc">(0);
  }
  decltype(auto) operator++() const {
    return host_->template call<"op_inc">();
  }
  decltype(auto) operator++(int) const {
    return host_->template call<"op_inc">(0);
  }
};
template <class Host>
struct op_slot<Host, 32> {
  Host* host_{};
  explicit op_slot(Host* host) noexcept : host_(host) {}
  decltype(auto) operator--() { return host_->template call<"op_dec">(); }
  decltype(auto) operator--(int) {
    return host_->template call<"op_dec">(0);
  }
  decltype(auto) operator--() const {
    return host_->template call<"op_dec">();
  }
  decltype(auto) operator--(int) const {
    return host_->template call<"op_dec">(0);
  }
};
ASF_DEF_OP(33, "op_index", []);
ASF_DEF_OP(34, "op_call", ());
// Index 35/36: comma and ->* (TOKEN cannot go through ASF_DEF_OP macros).
template <class Host>
struct op_slot<Host, 35> {
  Host* host_{};
  explicit op_slot(Host* host) noexcept : host_(host) {}
  decltype(auto) operator,(auto&& rhs) {
    return host_->template call<"op_comma">(
        std::forward<decltype(rhs)>(rhs));
  }
  decltype(auto) operator,(auto&& rhs) const {
    return host_->template call<"op_comma">(
        std::forward<decltype(rhs)>(rhs));
  }
};
template <class Host>
struct op_slot<Host, 36> {
  Host* host_{};
  explicit op_slot(Host* host) noexcept : host_(host) {}
  decltype(auto) operator->*(auto&& ptm) {
    return host_->template call<"op_ptr_to_mem">(
        std::forward<decltype(ptm)>(ptm));
  }
  decltype(auto) operator->*(auto&& ptm) const {
    return host_->template call<"op_ptr_to_mem">(
        std::forward<decltype(ptm)>(ptm));
  }
};

#undef ASF_DEF_OP
#undef ASF_DEF_OP_UNARY

// Only enabled op_slot<Host,I> types -- no bool_pack / empty disabled bases.
template <class... Ops>
struct op_mixins : Ops... {
  template <class Host>
  explicit op_mixins(Host* host) noexcept : Ops(host)... {}
};

template <>
struct op_mixins<> {
  template <class Host>
  explicit op_mixins(Host*) noexcept {}
};

template <class... Ts>
using op_tuple_cat_t = decltype(std::tuple_cat(
    std::declval<Ts>()...));

template <class Tuple>
struct op_mixins_from_tuple;
template <class... Ops>
struct op_mixins_from_tuple<std::tuple<Ops...>> {
  using type = op_mixins<Ops...>;
};

template <template <class> class Trait, class Host, std::size_t... I>
consteval auto iface_op_mixins_fn(std::index_sequence<I...>)
    -> typename op_mixins_from_tuple<op_tuple_cat_t<std::conditional_t<
        iface_exposes_op_at<Trait>(I), std::tuple<op_slot<Host, I>>,
        std::tuple<>>...>>::type;

template <template <class> class Trait, class Host, std::size_t... I>
consteval auto dyn_op_mixins_fn(std::index_sequence<I...>)
    -> typename op_mixins_from_tuple<op_tuple_cat_t<std::conditional_t<
        dyn_exposes_op_at<Trait>(I), std::tuple<op_slot<Host, I>>,
        std::tuple<>>...>>::type;

template <template <class> class Trait, class Host, std::size_t... I>
consteval auto c_dyn_op_mixins_fn(std::index_sequence<I...>)
    -> typename op_mixins_from_tuple<op_tuple_cat_t<std::conditional_t<
        c_dyn_exposes_op_at<Trait>(I), std::tuple<op_slot<Host, I>>,
        std::tuple<>>...>>::type;

template <template <class> class Trait, class Host, std::size_t... I>
consteval auto c_iface_op_mixins_fn(std::index_sequence<I...>)
    -> typename op_mixins_from_tuple<op_tuple_cat_t<std::conditional_t<
        c_iface_exposes_op_at<Trait>(I), std::tuple<op_slot<Host, I>>,
        std::tuple<>>...>>::type;

template <template <class> class Trait, class Host>
using iface_ops_for = decltype(iface_op_mixins_fn<Trait, Host>(
    std::make_index_sequence<op_count>{}));

template <template <class> class Trait, class Host>
using dyn_ops_for = decltype(dyn_op_mixins_fn<Trait, Host>(
    std::make_index_sequence<op_count>{}));

template <template <class> class Trait, class Host>
using c_dyn_ops_for = decltype(c_dyn_op_mixins_fn<Trait, Host>(
    std::make_index_sequence<op_count>{}));

template <template <class> class Trait, class Host>
using c_iface_ops_for = decltype(c_iface_op_mixins_fn<Trait, Host>(
    std::make_index_sequence<op_count>{}));

// True when assign should use a templated operator= (Src-like / auto RHS).
// Convert is always concrete operator T() (T may be Self); never templated To.
// Construct does not generate conversion operators at all.
template <template <class> class Trait, convert_kind Kind, special_op Op,
          std::size_t I>
consteval bool special_op_is_impl_keyed() {
  if (Op == special_op::convert || Op == special_op::construct)
    return false;
  constexpr auto method = special_op_method_at<Trait, Kind, Op, I>();
  if (method_prefers_imple(method))
    return true;
  if (!is_assign_op(Op))
    return false;
  return is_impl_keyed_assign(method);
}

// Self for conversion exclude: signature_self / mirror / identity_mirror.
consteval bool is_self_like_type(std::meta::info t) {
  t = std::meta::remove_cvref(t);
  if (t == ^^signature_self)
    return true;
  if (!std::meta::has_template_arguments(t))
    return false;
  auto tmpl = std::meta::template_of(t);
  return tmpl == ^^mirror || tmpl == ^^identity_mirror;
}

// Convert: concrete operator T() only (T may equal Self).
// Assign FromImpl: templated operator=; construct never generates conversions.
// Explicitness of op_convert: read with the iface object type when the host
// has one (body is valid for it); dyn carriers fall back to the probe.
template <class Host>
struct host_iface_object {};
template <template <class> class Trait, typename T>
struct host_iface_object<iface<Trait, T>> {
  using type = T;
};
template <template <class> class Trait, typename T>
struct host_iface_object<iface<Trait, const T>> {
  using type = T;
};
template <template <class> class Trait, typename T, typename Src>
struct host_iface_object<iface<Trait, mirror<T, Src>>> {
  using type = T;
};
template <template <class> class Trait, typename T, typename Src>
struct host_iface_object<iface<Trait, mirror<const T, Src>>> {
  using type = T;
};

template <class Host, template <class> class Trait, convert_kind Kind,
          special_op Op, std::size_t I>
consteval bool convert_is_explicit_host() {
  constexpr auto method = special_op_method_at<Trait, Kind, Op, I>();
  if constexpr (requires { typename host_iface_object<Host>::type; })
    return convert_is_explicit_as<Trait,
                                  typename host_iface_object<Host>::type>(
        method);
  else
    return convert_is_explicit(method);
}

template <class Host, template <class> class Trait, convert_kind Kind,
          special_op Op, std::size_t I,
          bool Explicit = convert_is_explicit_host<Host, Trait, Kind, Op, I>(),
          bool FromImpl = special_op_is_impl_keyed<Trait, Kind, Op, I>()>
struct special_op_slot;

template <class Host, template <class> class Trait, convert_kind Kind,
          std::size_t I>
struct special_op_slot<Host, Trait, Kind, special_op::convert, I, true, false> {
  Host* host_{};
  explicit special_op_slot(Host* host) noexcept : host_(host) {}
  static constexpr auto method =
      special_op_method_at<Trait, Kind, special_op::convert, I>();
  using To = [:std::meta::return_type_of(signature_decl_of<method>()):];
  explicit operator To() const { return host_->template cast_to<To>(); }
};

template <class Host, template <class> class Trait, convert_kind Kind,
          std::size_t I>
struct special_op_slot<Host, Trait, Kind, special_op::convert, I, false,
                       false> {
  Host* host_{};
  explicit special_op_slot(Host* host) noexcept : host_(host) {}
  static constexpr auto method =
      special_op_method_at<Trait, Kind, special_op::convert, I>();
  using To = [:std::meta::return_type_of(signature_decl_of<method>()):];
  operator To() const { return host_->template cast_to<To>(); }
};

// If convert were ever impl-keyed (templated operator To), Self is forbidden.
template <class Host, template <class> class Trait, convert_kind Kind,
          std::size_t I, bool Explicit>
struct special_op_slot<Host, Trait, Kind, special_op::convert, I, Explicit,
                       true> {
  Host* host_{};
  explicit special_op_slot(Host* host) noexcept : host_(host) {}
  template <typename To>
    requires(!is_self_like_type(^^To) &&
             requires { host_->template cast_to<To>(); })
  explicit(Explicit) operator To() const {
    return host_->template cast_to<To>();
  }
};

// Assignment: never explicit. auto RHS -> templated From; else concrete key.
template <class Host, template <class> class Trait, convert_kind Kind,
          std::size_t I>
struct special_op_slot<Host, Trait, Kind, special_op::assign, I, false, true> {
  Host* host_{};
  explicit special_op_slot(Host* host) noexcept : host_(host) {}
  template <typename From>
  decltype(auto) operator=(From&& rhs) {
    return host_->cast_assign(std::forward<From>(rhs));
  }
};

template <class Host, template <class> class Trait, convert_kind Kind,
          std::size_t I>
struct special_op_slot<Host, Trait, Kind, special_op::assign, I, false,
                       false> {
  Host* host_{};
  explicit special_op_slot(Host* host) noexcept : host_(host) {}
  static consteval std::meta::info from_type() {
    constexpr auto method =
        special_op_method_at<Trait, Kind, special_op::assign, I>();
    constexpr auto inst = signature_decl_of<method>();
    return special_op_key_type(inst, special_op::assign);
  }
  using From = [:from_type():];
  decltype(auto) operator=(From rhs) {
    return host_->cast_assign(std::move(rhs));
  }
};

template <class Host, template <class> class Trait, convert_kind Kind,
          special_op Op, class Seq>
struct special_op_slots_for;

template <class Host, template <class> class Trait, convert_kind Kind,
          special_op Op, std::size_t... I>
struct special_op_slots_for<Host, Trait, Kind, Op, std::index_sequence<I...>>
    : special_op_slot<Host, Trait, Kind, Op, I>... {
  explicit special_op_slots_for(Host* host) noexcept
      : special_op_slot<Host, Trait, Kind, Op, I>(host)... {
    if constexpr (sizeof...(I) == 0)
      (void)host;
  }
};

// Bring base operator= into overload set (implicit assign would hide them).
template <class Host, template <class> class Trait, convert_kind Kind,
          std::size_t... I>
struct special_op_slots_for<Host, Trait, Kind, special_op::assign,
                            std::index_sequence<I...>>
    : special_op_slot<Host, Trait, Kind, special_op::assign, I>... {
  using special_op_slot<Host, Trait, Kind, special_op::assign, I>::operator=...;
  explicit special_op_slots_for(Host* host) noexcept
      : special_op_slot<Host, Trait, Kind, special_op::assign, I>(host)... {
    if constexpr (sizeof...(I) == 0)
      (void)host;
  }
};

// Carrier surface: convert + assign only (construct has no conversion ops).
template <class Host, template <class> class Trait, convert_kind Kind>
struct special_ops_bundle
    : special_op_slots_for<
          Host, Trait, Kind, special_op::convert,
          std::make_index_sequence<
              special_op_count<Trait, Kind, special_op::convert>()>>,
      special_op_slots_for<
          Host, Trait, Kind, special_op::assign,
          std::make_index_sequence<
              special_op_count<Trait, Kind, special_op::assign>()>> {
  using assign_slots = special_op_slots_for<
      Host, Trait, Kind, special_op::assign,
      std::make_index_sequence<
          special_op_count<Trait, Kind, special_op::assign>()>>;
  using assign_slots::operator=;

  explicit special_ops_bundle(Host* host) noexcept
      : special_op_slots_for<
            Host, Trait, Kind, special_op::convert,
            std::make_index_sequence<
                special_op_count<Trait, Kind, special_op::convert>()>>(host),
        assign_slots(host) {}
};

template <class Host>
struct convert_carrier {
  explicit convert_carrier(Host*) noexcept {}
};

// Sourced mirror iface: convert + assign.
template <template <class> class Trait, typename T, typename Src>
  requires(!std::is_void_v<Src> && !std::is_const_v<T>)
struct convert_carrier<iface<Trait, mirror<T, Src>>>
    : special_ops_bundle<iface<Trait, mirror<T, Src>>, Trait,
                         convert_kind::iface> {
  using base = special_ops_bundle<iface<Trait, mirror<T, Src>>, Trait,
                                  convert_kind::iface>;
  using base::base;
  using base::operator=;
};

// Plain object iface: convert + concrete assign only.
template <template <class> class Trait, typename T>
  requires(!std::is_void_v<T> && !std::is_const_v<T> && !is_mirror_v<T>)
struct convert_carrier<iface<Trait, T>>
    : special_ops_bundle<iface<Trait, T>, Trait, convert_kind::unsourced_iface> {
  using base =
      special_ops_bundle<iface<Trait, T>, Trait, convert_kind::unsourced_iface>;
  using base::base;
  using base::operator=;
};

template <template <class> class Trait, typename T, typename Src>
  requires(!std::is_void_v<Src>)
struct convert_carrier<iface<Trait, mirror<const T, Src>>>
    : special_ops_bundle<iface<Trait, mirror<const T, Src>>, Trait,
                         convert_kind::c_dyn> {
  using base = special_ops_bundle<iface<Trait, mirror<const T, Src>>, Trait,
                                  convert_kind::c_dyn>;
  using base::base;
  using base::operator=;
};

template <template <class> class Trait, typename T>
  requires(!is_mirror_v<T>)
struct convert_carrier<iface<Trait, const T>>
    : special_ops_bundle<iface<Trait, const T>, Trait,
                         convert_kind::unsourced_iface> {
  using base = special_ops_bundle<iface<Trait, const T>, Trait,
                                  convert_kind::unsourced_iface>;
  using base::base;
  using base::operator=;
};

template <template <class> class Trait, typename T, typename Src>
  requires(!std::is_void_v<Src>)
struct convert_carrier<iface<Trait, mirror<const T, Src>> const>
    : special_ops_bundle<iface<Trait, mirror<const T, Src>> const, Trait,
                         convert_kind::c_dyn> {
  using base =
      special_ops_bundle<iface<Trait, mirror<const T, Src>> const, Trait,
                         convert_kind::c_dyn>;
  using base::base;
  using base::operator=;
};

template <template <class> class Trait, typename T>
  requires(!is_mirror_v<T>)
struct convert_carrier<iface<Trait, const T> const>
    : special_ops_bundle<iface<Trait, const T> const, Trait,
                         convert_kind::unsourced_iface> {
  using base = special_ops_bundle<iface<Trait, const T> const, Trait,
                                  convert_kind::unsourced_iface>;
  using base::base;
  using base::operator=;
};

template <template <class> class Trait, class Ptr>
struct convert_carrier<dyn<Trait, Ptr>>
    : special_ops_bundle<dyn<Trait, Ptr>, Trait,
                         convert_kind::dyn> {
  using base = special_ops_bundle<dyn<Trait, Ptr>, Trait,
                                  convert_kind::dyn>;
  using base::base;
  using base::operator=;
};

template <template <class> class Trait, class Ptr>
struct convert_carrier<c_dyn<Trait, Ptr>>
    : special_ops_bundle<c_dyn<Trait, Ptr>, Trait,
                         convert_kind::c_dyn> {
  using base = special_ops_bundle<c_dyn<Trait, Ptr>, Trait,
                                  convert_kind::c_dyn>;
  using base::base;
  using base::operator=;
};

template <template <class> class Trait, class Ptr>
struct convert_carrier<c_dyn<Trait, Ptr> const>
    : special_ops_bundle<c_dyn<Trait, Ptr> const, Trait,
                         convert_kind::c_dyn> {
  using base =
      special_ops_bundle<c_dyn<Trait, Ptr> const, Trait,
                         convert_kind::c_dyn>;
  using base::base;
  using base::operator=;
};

template <class Host, class Fns, class Ops>
struct fn_proxy_impl : Fns, Ops, convert_carrier<Host> {
  // Derived implicit operator= would hide trait assign ops in bases.
  using convert_carrier<Host>::operator=;

  explicit fn_proxy_impl(Host* host)
      : Fns(make_bound_fns<Fns>(
            host, std::make_index_sequence<fn_member_count<Fns>()>{})),
        Ops(host),
        convert_carrier<Host>(host) {}

  Fns* operator->() { return this; }
  Fns const* operator->() const { return this; }
};

// Enabled op mixins for Host (variadic types, not bool_pack).
template <class Host>
struct host_op_mixins;

template <template <class> class Trait, typename T>
  requires(!std::is_void_v<T> && !std::is_const_v<T> && !is_mirror_v<T>)
struct host_op_mixins<iface<Trait, T>> {
  using type = iface_ops_for<Trait, iface<Trait, T>>;
};

template <template <class> class Trait, typename T, typename Src>
  requires(!std::is_const_v<T>)
struct host_op_mixins<iface<Trait, mirror<T, Src>>> {
  using type = iface_ops_for<Trait, iface<Trait, mirror<T, Src>>>;
};

template <template <class> class Trait, class Ptr>
struct host_op_mixins<dyn<Trait, Ptr>> {
  using type = dyn_ops_for<Trait, dyn<Trait, Ptr>>;
};

template <template <class> class Trait, class Ptr>
struct host_op_mixins<c_dyn<Trait, Ptr>> {
  using type = c_dyn_ops_for<Trait, c_dyn<Trait, Ptr>>;
};

template <template <class> class Trait, class Ptr>
struct host_op_mixins<c_dyn<Trait, Ptr> const> {
  using type = c_dyn_ops_for<Trait, c_dyn<Trait, Ptr> const>;
};

template <template <class> class Trait, typename T>
  requires(!is_mirror_v<T>)
struct host_op_mixins<iface<Trait, const T>> {
  using type = c_iface_ops_for<Trait, iface<Trait, const T>>;
};

template <template <class> class Trait, typename T>
  requires(!is_mirror_v<T>)
struct host_op_mixins<iface<Trait, const T> const> {
  using type = c_iface_ops_for<Trait, iface<Trait, const T> const>;
};

template <template <class> class Trait, typename T, typename Src>
struct host_op_mixins<iface<Trait, mirror<const T, Src>>> {
  using type = c_iface_ops_for<Trait, iface<Trait, mirror<const T, Src>>>;
};

template <template <class> class Trait, typename T, typename Src>
struct host_op_mixins<iface<Trait, mirror<const T, Src>> const> {
  using type = c_iface_ops_for<Trait, iface<Trait, mirror<const T, Src>> const>;
};

struct no_revops {};

template <class Host, class D>
struct host_revops : no_revops {};

template <class Host, class D>
struct host_revops<Host const, D> : host_revops<Host, D> {};

template <template <class> class Trait, typename T, class D>
  requires(!is_mirror_v<T>)
struct host_revops<iface<Trait, T>, D>
    : iface_revops<Trait, D, T, void, !std::is_const_v<T>> {};

template <template <class> class Trait, typename View, typename Src, class D>
struct host_revops<iface<Trait, mirror<View, Src>>, D>
    : iface_revops<Trait, D, View, Src, !std::is_const_v<View>> {};

// Public proxy: Host + Fns. Reverse free ops on iface proxy only
// (Trait self position). Dyn proxy stays no_friend_ops.
// Ops = only exposed op_slot types (variadic MI), not a bool mask.
// Layout: Host buf_ only (same as *dyn); Fns/Ops bind to &buf_.
template <class Host, class Fns,
          class Ops = typename host_op_mixins<Host>::type>
struct fn_proxy : host_revops<Host, fn_proxy<Host, Fns, Ops>>,
                  fn_proxy_impl<Host, Fns, Ops> {
  using revops = host_revops<Host, fn_proxy>;
  using base = fn_proxy_impl<Host, Fns, Ops>;
  using base::operator=;

  Host buf_{};

  // base(&buf_) before buf_ is constructed: impl only stores the pointer
  // (no deref). GCC -Wmaybe-uninitialized false positive on that order.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
  explicit fn_proxy(Host* host)
      : revops(), base(&buf_), buf_(*host) {}

  fn_proxy(const fn_proxy& o)
      : revops(), base(&buf_), buf_(o.buf_) {}

  fn_proxy(fn_proxy&& o) noexcept
      : revops(), base(&buf_), buf_(std::move(o.buf_)) {}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
};

// Rvalue twin of fn_proxy. Same observation; the type is what selects rvalue
// (construct move on dyn uses dyn_rproxy, not this).
template <class Host, class Fns,
          class Ops = typename host_op_mixins<Host>::type>
struct fn_rproxy : fn_proxy<Host, Fns, Ops> {
  using proxy_t = fn_proxy<Host, Fns, Ops>;
  using rproxy_t = fn_rproxy;
  static constexpr bool is_rvalue = true;

  explicit fn_rproxy(Host* host) : proxy_t(host) {}
  explicit fn_rproxy(proxy_t p) : proxy_t(std::move(p)) {}
};

template <template <class> class Trait, typename T>
using impl_self_t = [:impl_self_info<Trait, T>():];

template <template <class> class Trait, typename T, typename Src>
struct iface_self_type {
  // ann::mirror: second parameter is the view T, self is that mirror.
  // Otherwise the second parameter is Src, and self is Src&.
  using type = std::conditional_t<layout_allows_mirror_source_as<Trait, T>(),
                                  impl_self_t<Trait, T>,
                                  std::add_lvalue_reference_t<T>>;
};

template <template <class> class Trait, typename T, typename Src>
  requires(!std::is_void_v<Src> && layout_allows_unsourced_mirror<Trait>())
struct iface_self_type<Trait, T, Src> {
  using type = mirror<T, Src>;
};

// Compare object identity pointers with static_cast adjustment for MI.
template <typename SrcA, typename SrcB>
constexpr bool iface_src_ptrs_equal(const void* pa,
                                    const void* pb) noexcept {
  if (pa == nullptr || pb == nullptr)
    return pa == pb;
  using A = std::remove_cv_t<SrcA>;
  using B = std::remove_cv_t<SrcB>;
  if constexpr (std::same_as<A, B>)
    return pa == pb;
  else if constexpr (std::is_base_of_v<A, B>)
    return pa == static_cast<const void*>(
               static_cast<A const*>(static_cast<B const*>(pb)));
  else if constexpr (std::is_base_of_v<B, A>)
    return static_cast<const void*>(
               static_cast<B const*>(static_cast<A const*>(pa))) == pb;
  else
    return false;
}

template <template <class> class Trait, typename View, typename Src>
using iface_shape_t =
    std::conditional_t<std::is_void_v<Src>, iface<Trait, View>,
                       iface<Trait, mirror<View, Src>>>;

template <template <class> class Trait, typename View, typename Src>
using c_iface_shape_t =
    std::conditional_t<std::is_void_v<Src>, iface<Trait, const View>,
                       iface<Trait, mirror<const View, Src>>>;

// Trait bases either way, and Arg can build iface<To, Arg>.
template <template <class> class To, template <class> class From, typename Arg>
consteval bool can_iface_convert_of() {
  if constexpr (!(is_trait_base_of<To, From>() ||
                  is_trait_base_of<From, To>()))
    return false;
  else
    return std::constructible_from<iface<To, Arg>, Arg&> ||
           std::constructible_from<iface<To, Arg>, Arg const&>;
}

// Sourced iface only: Trait related and Src related; same mirror target.
template <template <class> class TraitA, typename TA, typename SrcA,
          template <class> class TraitB, typename TB, typename SrcB>
consteval bool can_iface_compare() {
  if constexpr (std::is_void_v<SrcA> || std::is_void_v<SrcB>)
    return false;
  if constexpr (!std::same_as<mirror_class<TA>, mirror_class<TB>>)
    return false;
  if constexpr (!(is_trait_base_of<TraitA, TraitB>() ||
                  is_trait_base_of<TraitB, TraitA>()))
    return false;
  using A = std::remove_cv_t<SrcA>;
  using B = std::remove_cv_t<SrcB>;
  return std::same_as<A, B> || std::is_base_of_v<A, B> ||
         std::is_base_of_v<B, A>;
}

template <template <class> class TraitA, template <class> class TraitB>
consteval bool can_dyn_compare() {
  return is_trait_base_of<TraitA, TraitB>() ||
         is_trait_base_of<TraitB, TraitA>();
}

}  // namespace detail

ASF_END_NAMESPACE
