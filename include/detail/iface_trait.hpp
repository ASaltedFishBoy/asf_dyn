#pragma once

// Trait introspection, layout, method wiring, erased invoke helpers.
// Included by iface_vtable.hpp / interface.hpp umbrella.

#include "iface_config.hpp"
#include "mirror.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

ASF_BEGIN_NAMESPACE
namespace detail {

// Every vtable slot is a function pointer. Call sites convert to the concrete
// signature (fn-to-fn). Object pointers are not stored in the table.
using vt_fn = void (*)();
using vt_ptr = vt_fn const*;

template <class Fp>
constexpr vt_fn as_vt_fn(Fp* fp) noexcept {
  return reinterpret_cast<vt_fn>(fp);
}

template <class Fp>
constexpr Fp* from_vt_fn(vt_fn fp) noexcept {
  return reinterpret_cast<Fp*>(fp);
}

// Probe tag for Trait method templates. Not a fake object: substituting a
// dummy Self instantiates the user body (GCC). Declaration probing uses
// `decltype(&template [:M:]<signature_self...>)` when the return type is
// written on the declaration. `auto` / `decltype(auto)` return is only
// allowed on op_construct: the vtable slot is always the erased construct
// thunk (void(void*, vt_ptr, void*) plus extras); a real instantiate uses
// Src, which has the members the body may access.
struct signature_self {};

// Annotation probe only. Function templates are not annotation targets
// until substituted; this empty aggregate is that substitute (construct
// `value_of` / `return self` compile). Not a dyn Self: after the tags
// are known, invoke uses mirror or Src. Construct is never a mirror.
struct ann_probe_self {};

template <class R, class... Args>
R signature_probe_fn(Args...);

template <class T, std::size_t>
using signature_repeat = T;

template <class MP>
struct signature_from_mp;

template <class R, class C, class... Args>
struct signature_from_mp<R (C::*)(Args...)> {
  static consteval std::meta::info fn() {
    return ^^signature_probe_fn<R, Args...>;
  }
};

template <class R, class C, class... Args>
struct signature_from_mp<R (C::*)(Args...) const> {
  static consteval std::meta::info fn() {
    return ^^signature_probe_fn<R, Args...>;
  }
};

template <class R, class C, class... Args>
struct signature_from_mp<R (C::*)(Args...) noexcept> {
  static consteval std::meta::info fn() {
    return ^^signature_probe_fn<R, Args...>;
  }
};

template <class R, class C, class... Args>
struct signature_from_mp<R (C::*)(Args...) const noexcept> {
  static consteval std::meta::info fn() {
    return ^^signature_probe_fn<R, Args...>;
  }
};

template <class R, class... Args>
struct signature_from_mp<R (*)(Args...)> {
  static consteval std::meta::info fn() {
    return ^^signature_probe_fn<R, Args...>;
  }
};

template <class R, class... Args>
struct signature_from_mp<R (*)(Args...) noexcept> {
  static consteval std::meta::info fn() {
    return ^^signature_probe_fn<R, Args...>;
  }
};

template <std::meta::info Method, class Self, std::size_t... I>
consteval auto signature_mp(std::index_sequence<I...>)
    -> decltype(&template [:Method:]<signature_repeat<Self, I>...>);

template <std::meta::info Method, std::size_t N>
consteval bool can_signature_arity() {
  return requires {
    typename std::type_identity<decltype(signature_mp<Method, signature_self>(
        std::make_index_sequence<N>{}))>::type;
  };
}

template <std::meta::info Method, std::size_t N>
consteval std::meta::info signature_decl() {
  using MP = decltype(signature_mp<Method, signature_self>(
      std::make_index_sequence<N>{}));
  return signature_from_mp<MP>::fn();
}

consteval bool display_is_auto_return(std::string_view t) {
  while (!t.empty() && (t.front() == ' ' || t.front() == '\t'))
    t.remove_prefix(1);
  if (t.starts_with("decltype(auto)")) {
    auto rest = t.substr(14);
    return rest.empty() || rest.front() == ' ' || rest.front() == '&' ||
           rest.front() == '\n';
  }
  if (t.starts_with("auto")) {
    auto rest = t.substr(4);
    return rest.empty() || rest.front() == ' ' || rest.front() == '&' ||
           rest.front() == '\n';
  }
  return false;
}

// Leading blanks and declaration specifiers (static / constexpr / ...)
// stripped before reading the return type. The display may read
// `template<...> static auto X::f(...)`; the auto check needs the
// return-type token, not the specifiers.
consteval std::string_view display_strip_head(std::string_view t) {
  auto skip_blanks = [&t]() {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t'))
      t.remove_prefix(1);
  };
  skip_blanks();
  for (;;) {
    std::string_view kws[] = {"static", "constexpr", "consteval", "inline",
                              "virtual", "explicit", "friend"};
    bool stripped = false;
    for (auto kw : kws) {
      if (t.starts_with(kw) && t.size() > kw.size() &&
          (t[kw.size()] == ' ' || t[kw.size()] == '\t')) {
        t.remove_prefix(kw.size());
        skip_blanks();
        stripped = true;
        break;
      }
    }
    if (!stripped)
      break;
  }
  return t;
}

// Placeholder only when the declared return is auto / decltype(auto).
// A trailing `-> T` (including remove_cvref_t<decltype(self)>) is written.
consteval bool display_has_placeholder_return(std::string_view s) {
  auto lp = s.find('(');
  auto rp = s.rfind(')');
  if (lp != std::string_view::npos && rp != std::string_view::npos &&
      rp > lp) {
    auto after = s.substr(rp + 1);
    auto arrow = after.find("->");
    if (arrow != std::string_view::npos)
      return display_is_auto_return(after.substr(arrow + 2));
  }
  for (std::size_t pos = s.find('>'); pos != std::string_view::npos;
       pos = s.find('>', pos + 1)) {
    std::size_t i = pos + 1;
    if (display_is_auto_return(display_strip_head(s.substr(i))))
      return true;
  }
  return display_is_auto_return(display_strip_head(s));
}

template <std::meta::info Method>
consteval bool has_placeholder_return() {
  if constexpr (!std::meta::is_function_template(Method))
    return false;
  else {
    try {
      return display_has_placeholder_return(std::meta::display_string_of(Method));
    } catch (std::meta::exception const&) {
      return false;
    }
  }
}

consteval bool has_placeholder_return(std::meta::info method) {
  if (!std::meta::is_function_template(method))
    return false;
  try {
    return display_has_placeholder_return(std::meta::display_string_of(method));
  } catch (std::meta::exception const&) {
    return false;
  }
}

consteval int display_auto_count(std::meta::info method) {
  try {
    auto s = std::meta::display_string_of(method);
    int n = 0;
    for (std::size_t i = 0; i + 11 <= s.size(); ++i) {
      if (s.substr(i, 11) == "class auto:")
        ++n;
    }
    return n;
  } catch (std::meta::exception const&) {
    return -1;
  }
}

template <std::meta::info Method>
consteval int display_auto_count() {
  return display_auto_count(Method);
}

consteval int display_param_count(std::meta::info method) {
  try {
    auto s = std::meta::display_string_of(method);
    auto lp = s.find('(');
    auto rp = s.rfind(')');
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp)
      return 0;
    auto inside = s.substr(lp + 1, rp - lp - 1);
    std::size_t first = 0;
    while (first < inside.size() &&
           (inside[first] == ' ' || inside[first] == '\t'))
      ++first;
    if (first == inside.size())
      return 0;
    int n = 1;
    int depth = 0;
    for (std::size_t i = first; i < inside.size(); ++i) {
      char c = inside[i];
      if (c == '<' || c == '(')
        ++depth;
      else if ((c == '>' || c == ')') && depth > 0)
        --depth;
      else if (c == ',' && depth == 0)
        ++n;
    }
    return n;
  } catch (std::meta::exception const&) {
    return 0;
  }
}

// Extra types from decltype(&template [:M:]<signature_self>). Trailing
// return `-> std::remove_cvref_t<decltype(self)>` is a declaration type:
// dummy Self is only substituted into the signature, not the body.
template <class... Decl>
struct mp_decl_extras {
  template <class... Extra>
  static consteval bool match() {
    if constexpr (sizeof...(Decl) != sizeof...(Extra))
      return false;
    else
      return (std::is_same_v<std::remove_cvref_t<Decl>,
                             std::remove_cvref_t<Extra>> && ...);
  }
};

template <class MP>
struct mp_extras_of;

template <class R, class C, class Self, class... Decl>
struct mp_extras_of<R (C::*)(Self, Decl...)> {
  template <class... Extra>
  static consteval bool match() {
    return mp_decl_extras<Decl...>::template match<Extra...>();
  }
};

template <class R, class C, class Self, class... Decl>
struct mp_extras_of<R (C::*)(Self, Decl...) const> : mp_extras_of<R (C::*)(Self, Decl...)> {
};

template <class R, class C, class Self, class... Decl>
struct mp_extras_of<R (C::*)(Self, Decl...) noexcept>
    : mp_extras_of<R (C::*)(Self, Decl...)> {};

template <class R, class C, class Self, class... Decl>
struct mp_extras_of<R (C::*)(Self, Decl...) const noexcept>
    : mp_extras_of<R (C::*)(Self, Decl...)> {};

template <class R, class Self, class... Decl>
struct mp_extras_of<R (*)(Self, Decl...)> {
  template <class... Extra>
  static consteval bool match() {
    return mp_decl_extras<Decl...>::template match<Extra...>();
  }
};

consteval bool display_return_is(std::meta::info method, std::meta::info ty) {
  try {
    auto s = std::meta::display_string_of(method);
    auto w = std::meta::display_string_of(std::meta::remove_cvref(ty));
    if (w.empty())
      return false;
    auto starts_with_ty = [&](std::string_view rest) {
      return rest.starts_with(w) &&
             (rest.size() == w.size() || rest[w.size()] == ' ' ||
              rest[w.size()] == '&');
    };
    for (std::size_t pos = s.find('>'); pos != std::string_view::npos;
         pos = s.find('>', pos + 1)) {
      std::size_t i = pos + 1;
      while (i < s.size() && s[i] == ' ')
        ++i;
      if (starts_with_ty(s.substr(i)))
        return true;
    }
    std::size_t i = 0;
    while (i < s.size() && s[i] == ' ')
      ++i;
    return starts_with_ty(s.substr(i));
  } catch (std::meta::exception const&) {
    return false;
  }
}

template <std::meta::info Method>
struct signature_arity {
  static constexpr int value = []() consteval {
    if constexpr (!std::meta::is_function_template(Method))
      return 0;
    else if constexpr (has_placeholder_return<Method>())
      return display_auto_count<Method>();
    else if constexpr (can_signature_arity<Method, 1>())
      return 1;
    else if constexpr (can_signature_arity<Method, 2>())
      return 2;
    else if constexpr (can_signature_arity<Method, 3>())
      return 3;
    else if constexpr (can_signature_arity<Method, 4>())
      return 4;
    else
      return -1;
  }();
};

template <std::meta::info Method>
consteval std::meta::info signature_decl_of() {
  if constexpr (!std::meta::is_function_template(Method))
    return Method;
  else if constexpr (has_placeholder_return<Method>())
    return std::meta::info{};
  else {
    constexpr int n = signature_arity<Method>::value;
    if constexpr (n == 1)
      return signature_decl<Method, 1>();
    else if constexpr (n == 2)
      return signature_decl<Method, 2>();
    else if constexpr (n == 3)
      return signature_decl<Method, 3>();
    else if constexpr (n == 4)
      return signature_decl<Method, 4>();
    else
      return std::meta::info{};
  }
}

template <std::meta::info Method, std::meta::info Self>
struct instantiate_of {
  static consteval std::meta::info get() {
    constexpr int n = signature_arity<Method>::value;
    if constexpr (n <= 0)
      return std::meta::info{};
    else if constexpr (Self == ^^signature_self) {
      // Placeholder return (op_construct): forming a function type would
      // instantiate the body. The erased construct slot does not need it.
      if constexpr (has_placeholder_return<Method>())
        return std::meta::info{};
      else
        return signature_decl<Method, static_cast<std::size_t>(n)>();
    } else {
      std::vector<std::meta::info> args(static_cast<std::size_t>(n), Self);
      if (!std::meta::can_substitute(Method, args))
        return std::meta::info{};
      return std::meta::substitute(Method, args);
    }
  }
};

template <typename R, typename... Args>
struct erased_fn {
  template <template <class> class Trait, typename Storage, typename As,
            std::size_t I>
  static R call(void* ptr, vt_ptr vt, Args... args);

  static R stub(void*, vt_ptr, Args...) { std::unreachable(); }

  static R dispatch(vt_fn slot, void* ptr, vt_ptr vt,
                    std::type_identity_t<Args>... args) {
    return from_vt_fn<R(void*, vt_ptr, Args...)>(slot)(
        ptr, vt, std::forward<Args>(args)...);
  }
};

consteval bool is_trait_method(std::meta::info member) {
  if (!std::meta::has_identifier(member))
    return false;
  if (std::meta::is_constructor(member) || std::meta::is_destructor(member) ||
      std::meta::is_assignment(member) ||
      std::meta::is_special_member_function(member))
    return false;
  return std::meta::is_function_template(member) ||
         std::meta::is_function(member);
}

// Forward: defined with method_ann_inst after has_annotation.
consteval bool method_prefers_imple(std::meta::info method);
consteval std::meta::info method_ann_inst(std::meta::info method);
consteval int auto_param_count(std::meta::info method);
consteval std::meta::info instantiate(std::meta::info method,
                                      std::meta::info self);
template <template <class> class Trait, typename T>
consteval std::meta::info impl_self_info();
template <typename Src>
consteval void check_construct_return_src(std::meta::info method);
template <std::meta::info Method>
consteval void check_construct_return_decl();
consteval bool is_rvalue_self_method(std::meta::info method);
consteval bool is_const_self_method(std::meta::info method);
consteval std::size_t self_param_index(std::meta::info method);

// Declaring Trait<void> (section). Trait-level annotations live here.
consteval bool declaring_trait_has(std::meta::info member, std::meta::info tag) {
  if (member == std::meta::info{})
    return false;
  auto cls = std::meta::parent_of(member);
  if (cls == std::meta::info{})
    return false;
  return !std::meta::annotations_of_with_type(cls, tag).empty();
}

// Yes-tag / no-tag. Method overrides Trait. default_yes when neither is set.
// Both forms on one declaration are ill-formed.
consteval bool ann_enabled(std::meta::info method, std::meta::info yes_tag,
                           std::meta::info no_tag, bool default_yes);

// Dyn by default. `static` on Trait<void> opts out (iface-only).
// Dyn slot: a non-static interface method with self first (and at most one
// deduced template parameter, checked in dyn_methods). `static` on Trait<void>
// opts out of the dyn vtable (iface-only). Static-ness and self position are
// declaration properties: no body instantiation, no annotation read.
consteval bool is_dyn_slot(std::meta::info member) {
  if (member == std::meta::info{})
    return false;
  if (!std::meta::is_function(member) &&
      !std::meta::is_function_template(member))
    return false;
  if (std::meta::is_static_member(member))
    return false;
  return self_param_index(member) == 0;
}

// Dyn methods: at most one abbreviated-auto parameter (the Self), and it must
// be the first parameter. A later self is iface-only (no_dyn). Any further
// auto is a non-Self and is ill-formed.
// Src/auto-keyed assign cannot be dyn (same as carrier: only construct is
// Src-related dyn-dispatch). Mark those `static`. Defined after
// special_op helpers.
consteval bool dyn_ann_auto_params_ok(std::meta::info method);
consteval bool dyn_ann_assign_ok(std::meta::info method);
consteval void check_dyn_ann_method(std::meta::info method);
// op_construct does not follow Trait [[=mirror]] (bases via cache).
template <template <class> class Trait>
consteval void ensure_lifetime_not_mirror();

consteval std::vector<std::meta::info> trait_methods(std::meta::info cls) {
  std::vector<std::meta::info> out;
  for (auto member : std::meta::members_of(cls, unchecked())) {
    // members_of may list inherited members; layout uses direct decls only.
    if (std::meta::parent_of(member) != cls)
      continue;
    if (is_trait_method(member))
      out.push_back(member);
  }
  return out;
}

consteval std::vector<std::meta::info> dyn_methods(std::meta::info cls) {
  std::vector<std::meta::info> out;
  for (auto member : trait_methods(cls)) {
    if (!is_dyn_slot(member))
      continue;
    check_dyn_ann_method(member);
    out.push_back(member);
  }
  return out;
}

consteval std::meta::info find_method(std::meta::info cls,
                                      std::string_view name) {
  for (auto member : trait_methods(cls)) {
    if (std::meta::identifier_of(member) == name)
      return member;
  }
  return std::meta::info{};
}

consteval std::vector<std::meta::info> find_methods_named(
    std::meta::info cls, std::string_view name) {
  std::vector<std::meta::info> out;
  for (auto member : trait_methods(cls)) {
    if (std::meta::identifier_of(member) == name)
      out.push_back(member);
  }
  return out;
}

enum class special_op { convert, construct, assign };

consteval bool is_assign_op(special_op kind) {
  return kind == special_op::assign;
}

consteval bool is_special_op_name(std::string_view name, special_op kind) {
  switch (kind) {
  case special_op::convert:
    return name == "op_convert" || name.starts_with("op_convert_");
  case special_op::construct:
    return name == "op_construct" || name.starts_with("op_construct_");
  case special_op::assign:
    return name == "op_assign" || name.starts_with("op_assign_");
  }
  return false;
}

// op_construct*. Never follows a Trait [[=mirror]] tag. Destroy is vt[-1],
// not a Trait method.
consteval bool is_lifetime_dyn_op_name(std::string_view name) {
  return is_special_op_name(name, special_op::construct);
}

consteval bool is_any_special_op_name(std::string_view name) {
  return is_special_op_name(name, special_op::convert) ||
         is_special_op_name(name, special_op::construct) ||
         is_special_op_name(name, special_op::assign);
}

// Catalog operator names (plus unary op_neg, which shares '-' with op_sub).
// imple does not look these up by the op_* identifier.
consteval bool is_catalog_op_name(std::string_view name) {
  return name == "op_add" || name == "op_sub" || name == "op_mul" ||
         name == "op_div" || name == "op_mod" || name == "op_xor" ||
         name == "op_bitand" || name == "op_bitor" || name == "op_compl" ||
         name == "op_not" || name == "op_lt" || name == "op_gt" ||
         name == "op_iadd" || name == "op_isub" || name == "op_imul" ||
         name == "op_idiv" || name == "op_imod" || name == "op_ixor" ||
         name == "op_ibitand" || name == "op_ibitor" || name == "op_lshift" ||
         name == "op_rshift" || name == "op_ilshift" || name == "op_irshift" ||
         name == "op_eq" || name == "op_ne" || name == "op_le" ||
         name == "op_ge" || name == "op_cmp" || name == "op_and" ||
         name == "op_or" || name == "op_inc" || name == "op_dec" ||
         name == "op_index" || name == "op_call" || name == "op_comma" ||
         name == "op_ptr_to_mem" || name == "op_neg";
}

// imple: op_* uses a special lookup. Anything else is a same-named member.
consteval bool is_imple_op_name(std::string_view name) {
  return is_any_special_op_name(name) || is_catalog_op_name(name);
}

consteval bool is_convert_method_name(std::string_view name) {
  return is_special_op_name(name, special_op::convert);
}

consteval std::vector<std::meta::info> sections_only_of(std::meta::info trait_void);

// Static methods are iface-only: a dyn cannot name the concrete mirror
// view a static self expects, and dispatching it through the vtable would
// just be a dyn method with extra steps. A user who needs a mirror from a
// dyn can write a dyn method that returns one.
enum class layout_kind { section, redirect };

struct layout_entry {
  layout_kind kind{};
  std::meta::info trait_void{};
};

enum class slot_kind { method, redirect };

struct flat_slot {
  slot_kind kind{};
  std::meta::info trait_void{};
  std::size_t method_index{};
};

consteval bool contains_info(const std::vector<std::meta::info>& v,
                             std::meta::info x) {
  for (auto e : v) {
    if (e == x)
      return true;
  }
  return false;
}

// Direct bases from `struct Trait<void> : B0<void>, B1<void>, ...`.
consteval std::vector<std::meta::info> trait_bases_of(
    std::meta::info trait_void) {
  std::vector<std::meta::info> out;
  for (auto b : std::meta::bases_of(trait_void, unchecked()))
    out.push_back(std::meta::type_of(b));
  return out;
}

// Dyn object-bind leaf:
//   Trait<void> final  -- whole trait is a leaf; every Trait<T> counts as final
//                        (and C++ forbids further : Trait<void> bases).
//   Trait<T> final     -- only that As is a leaf (incomplete Trait<T>: not final).
template <template <class> class Trait, typename T>
consteval bool is_trait_final() {
  if constexpr (std::meta::is_complete_type(^^Trait<void>) &&
                std::is_final_v<Trait<void>>)
    return true;
  using Raw = std::remove_cvref_t<T>;
  if constexpr (!std::meta::is_complete_type(^^Trait<Raw>))
    return false;
  else
    return std::is_final_v<Trait<Raw>>;
}

// Info-keyed (for Redirect cover search over ancestor infos).
consteval std::vector<std::meta::info> sections_only_of(
    std::meta::info trait_void) {
  auto bases = trait_bases_of(trait_void);
  if (bases.empty())
    return {trait_void};

  auto result = sections_only_of(bases[0]);
  for (std::size_t i = 1; i < bases.size(); ++i) {
    for (auto x : sections_only_of(bases[i])) {
      if (!contains_info(result, x))
        result.push_back(x);
    }
  }
  result.push_back(trait_void);
  return result;
}


template <template <class> class Trait, bool Rvalue>
consteval bool chain_has_dyn_construct_as() {
  for (auto s : sections_only_of(^^Trait<void>)) {
    for (auto m : dyn_methods(s)) {
      if (!std::meta::has_identifier(m) ||
          !is_special_op_name(std::meta::identifier_of(m),
                              special_op::construct))
        continue;
      if (is_rvalue_self_method(m) == Rvalue)
        return true;
    }
  }
  return false;
}
template <template <class> class Trait>
consteval bool chain_has_dyn_copy_construct() {
  return chain_has_dyn_construct_as<Trait, false>();
}
template <template <class> class Trait>
consteval bool chain_has_dyn_move_construct() {
  return chain_has_dyn_construct_as<Trait, true>();
}

// Grid-merge: sections(B0) then new from Bi... then self.
template <template <class> class Trait>
consteval std::vector<std::meta::info> sections_only() {
  return sections_only_of(^^Trait<void>);
}

// True if sections[I] (not self) is Parent<As> final.
template <template <class> class Trait, typename T, std::size_t I>
consteval bool final_trait_parent_at() {
  constexpr auto sec = sections_only_of(^^Trait<void>)[I];
  if constexpr (sec == ^^Trait<void>)
    return false;
  else {
    constexpr auto check = std::meta::substitute(
        ^^is_trait_final,
        {std::meta::template_of(sec), ^^std::remove_cvref_t<T>});
    return [:check:]();
  }
}

template <template <class> class Trait, typename T, std::size_t... I>
consteval bool has_final_trait_parent_pack(std::index_sequence<I...>) {
  return (final_trait_parent_at<Trait, T, I>() || ...);
}

// True if any ancestor section (not self) is Parent<As> final.
template <template <class> class Trait, typename T>
consteval bool has_final_trait_parent() {
  constexpr auto n = sections_only_of(^^Trait<void>).size();
  return has_final_trait_parent_pack<Trait, T>(std::make_index_sequence<n>{});
}

template <template <class> class Trait, typename T>
consteval void assert_no_final_trait_parent() {
  static_assert(!has_final_trait_parent<Trait, T>(),
                "a parent Trait is final for this type; cannot instantiate a "
                "derived Trait under a final parent");
}

consteval bool is_section_prefix_lists(const std::vector<std::meta::info>& a,
                                       const std::vector<std::meta::info>& b) {
  if (a.size() > b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i])
      return false;
  }
  return true;
}

consteval bool is_section_prefix(std::meta::info prefix,
                                 std::meta::info full) {
  return is_section_prefix_lists(sections_only_of(prefix),
                                 sections_only_of(full));
}

// truncate / truncated layout cuts (MULTI_VT Redirect pruning). Not methods.
consteval bool has_ann_tag(std::meta::info entity, std::meta::info tag) {
  return entity != std::meta::info{} &&
         !std::meta::annotations_of_with_type(entity, tag).empty();
}

consteval bool trait_has_truncate(std::meta::info trait_void) {
  return has_ann_tag(trait_void, ^^ann::truncate_tag);
}

consteval bool trait_has_truncated(std::meta::info trait_void) {
  return has_ann_tag(trait_void, ^^ann::truncated_tag);
}

// Direct bases that still participate in dyn upcast (same cut rules as
// dyn_upcast_reachable_of). truncate / truncated edges are allowed under
// ASF_HAS_DYN_SINGLE_INHERIT and do not count as multi-inherit.
consteval std::size_t live_dyn_base_count(std::meta::info trait_void) {
  std::size_t n = 0;
  for (auto b : std::meta::bases_of(trait_void, unchecked())) {
    auto base = std::meta::type_of(b);
    if (has_ann_tag(b, ^^ann::truncate_tag) || trait_has_truncated(base))
      continue;
    ++n;
  }
  return n;
}

// True if any section has more than one *live* (non-truncate) Trait base.
consteval bool trait_has_multi_inherit(std::meta::info trait_void) {
  for (auto s : sections_only_of(trait_void)) {
    if (live_dyn_base_count(s) > 1)
      return true;
  }
  return false;
}

template <template <class> class Trait>
consteval bool trait_has_multi_inherit() {
  return trait_has_multi_inherit(^^Trait<void>);
}

// ASF_HAS_DYN_SINGLE_INHERIT: dyn generation requires SI among live edges;
// truncate / truncated side branches are allowed.
template <template <class> class Trait>
consteval void assert_single_inherit() {
  if constexpr (dyn_single_inherit_v) {
    static_assert(!trait_has_multi_inherit<Trait>(),
                  "ASF_HAS_DYN_SINGLE_INHERIT requires a single-inheritance "
                  "Trait DAG among live (non-truncate) bases "
                  "(checked at dyn generation)");
  }
}

// Storeable dyn upcast targets from T (includes T). Honors:
//   [[=truncate]] on T -> stop (no proper bases)
//   [[=truncate]] on base-specifier T:B -> skip that branch
//   [[=truncated]] on B -> inheriting B is a truncate edge (skip B)
// Ancestor [[=truncate]] still applies when reached via a live edge.
consteval std::vector<std::meta::info> dyn_upcast_reachable_of(
    std::meta::info trait_void) {
  std::vector<std::meta::info> result;
  result.push_back(trait_void);
  if (trait_has_truncate(trait_void))
    return result;

  for (auto b : std::meta::bases_of(trait_void, unchecked())) {
    auto base = std::meta::type_of(b);
    if (has_ann_tag(b, ^^ann::truncate_tag) || trait_has_truncated(base))
      continue;
    for (auto s : dyn_upcast_reachable_of(base)) {
      if (!contains_info(result, s))
        result.push_back(s);
    }
  }
  return result;
}

consteval bool is_dyn_upcast_reachable(std::meta::info base,
                                       std::meta::info derived) {
  return contains_info(dyn_upcast_reachable_of(derived), base);
}

// Cover table for non-prefix base B under T: largest *reachable* M in Anc(T)
// whose sections are a prefix-superset of B's; at least B itself (as-if-final).
// Unreachable covers (truncate / truncated paths) are skipped so MULTI_VT
// does not instantiate extra tables for cut branches.
consteval std::meta::info redirect_cover_for(std::meta::info B,
                                             std::meta::info T) {
  auto b_list = sections_only_of(B);
  auto reachable = dyn_upcast_reachable_of(T);
  std::meta::info best = B;
  std::size_t best_size = b_list.size();
  for (auto M : sections_only_of(T)) {
    if (!contains_info(reachable, M))
      continue;
    auto m_list = sections_only_of(M);
    if (!is_section_prefix_lists(b_list, m_list))
      continue;
    if (m_list.size() > best_size) {
      best = M;
      best_size = m_list.size();
    }
  }
  return best;
}

// Sections in grid-merge order. With ASF_HAS_DYN_MULTI_VT: after each
// non-prefix *reachable* section S, Redirect to the as-if-final cover of S
// (diagram: I1,I2,I3,to I3,I4,I5,I6,to I6,I7). Without: sections only.
// truncate/truncated prune Redirects (and thus cover vt instances).
consteval std::vector<layout_entry> primary_layout_of(
    std::meta::info trait_void) {
  std::vector<layout_entry> result;
  std::vector<std::meta::info> emitted_redirects;
#if ASF_HAS_DYN_MULTI_VT
  auto reachable = dyn_upcast_reachable_of(trait_void);
#endif
  for (auto s : sections_only_of(trait_void)) {
    result.push_back(layout_entry{layout_kind::section, s});
#if ASF_HAS_DYN_MULTI_VT
    if (s == trait_void || is_section_prefix(s, trait_void))
      continue;
    if (!contains_info(reachable, s))
      continue;
    auto cover = redirect_cover_for(s, trait_void);
    if (contains_info(emitted_redirects, cover))
      continue;
    emitted_redirects.push_back(cover);
    result.push_back(layout_entry{layout_kind::redirect, cover});
#else
    (void)emitted_redirects;
#endif
  }
  return result;
}

template <template <class> class Trait>
consteval std::vector<layout_entry> primary_layout() {
  return primary_layout_of(^^Trait<void>);
}

// Distinct Redirect cover identities in T's layout (for vt instance collect).
consteval std::vector<std::meta::info> redirect_targets_of(
    std::meta::info trait_void) {
  std::vector<std::meta::info> targets;
  for (auto const& e : primary_layout_of(trait_void)) {
    if (e.kind == layout_kind::redirect && !contains_info(targets, e.trait_void))
      targets.push_back(e.trait_void);
  }
  return targets;
}

// Layout flatten (no lifetime gate). Prefer flatten_layout<Trait>(), which
// ensures the per-Trait instantiation cache before building slots.
consteval std::vector<flat_slot> flatten_layout(std::meta::info trait_void) {
  std::vector<flat_slot> out;
  for (auto const& entry : primary_layout_of(trait_void)) {
    if (entry.kind == layout_kind::section) {
      auto methods = dyn_methods(entry.trait_void);
      for (std::size_t i = 0; i < methods.size(); ++i)
        out.push_back(flat_slot{slot_kind::method, entry.trait_void, i});
    } else {
      out.push_back(flat_slot{slot_kind::redirect, entry.trait_void, 0});
    }
  }
  return out;
}

template <template <class> class Trait>
consteval std::vector<flat_slot> flatten_layout() {
  ensure_lifetime_not_mirror<Trait>();
  return flatten_layout(^^Trait<void>);
}

// Section-prefix: same physical table can serve the prefix interface.
consteval bool layout_is_prefix_of(std::meta::info prefix,
                                   std::meta::info full) {
  return is_section_prefix(prefix, full);
}

consteval bool is_trait_base_of_info(std::meta::info base,
                                     std::meta::info derived) {
  if (base == derived)
    return true;
  return contains_info(sections_only_of(derived), base);
}

template <template <class> class Base, template <class> class Derived>
consteval bool is_trait_base_of() {
  return is_trait_base_of_info(^^Base<void>, ^^Derived<void>);
}

// Same physical table: Base's sections are a prefix of Derived's (or equal).
template <template <class> class Base, template <class> class Derived>
consteval bool is_section_prefix_of() {
  return (^^Base<void>) == (^^Derived<void>) ||
         is_section_prefix(^^Base<void>, ^^Derived<void>);
}

// MULTI_VT: Trait-base upcast along truncate/truncated-reachable paths
// (Redirect/cover when not section-prefix). Else: section-prefix / same
// physical vt, still gated by reachability. SINGLE_INHERIT does not change
// this (an SI live DAG makes reachable bases prefixes).
template <template <class> class Base, template <class> class Derived>
consteval bool can_dyn_upcast() {
  if (!is_dyn_upcast_reachable(^^Base<void>, ^^Derived<void>))
    return false;
  if constexpr (dyn_multi_vt_v)
    return true;
  else
    return is_section_prefix_of<Base, Derived>();
}

// cache_dyn ops were erased against From's chain. A downcast is valid only
// when To's chain does not add copy/move op_construct (destroy is always
// vt[-1] ~Src on every table).
template <template <class> class To, template <class> class From>
consteval bool dyn_downcast_chain_ok() {
  return (!chain_has_dyn_copy_construct<To>() ||
          chain_has_dyn_copy_construct<From>()) &&
         (!chain_has_dyn_move_construct<To>() ||
          chain_has_dyn_move_construct<From>());
}

// Downcast along the Trait DAG: From is a base of To (includes From == To).
// Ptr-specific lifetime constraints are applied by the cast, not here.
template <template <class> class To, template <class> class From>
consteval bool can_dyn_downcast() {
  return is_trait_base_of<From, To>();
}

consteval std::size_t find_redirect_slot(std::meta::info from,
                                         std::meta::info to);

#if ASF_HAS_DYN_RTTI

template <template <class> class Trait>
struct trait_id {
  static constexpr char id{};
};

template <template <class> class Trait>
consteval const void* trait_id_addr() {
  return &trait_id<Trait>::id;
}

// RTTI meta (per physical vt key; not a shared parent tree).
// Layout: { find, type } header, then a suffix of x distinct vt pointers
// (leaf + Redirect covers) -- not a separate pointer, not one slot per
// trait_id target. find: pure Final-level map trait_id -> tables index
// ((std::size_t)-1 on miss); no self, one fn shared by every key of a
// Final. type: typeid(Storage) reference (proxy_typeid analogue).
struct vtable_meta {
  using find_fn = std::size_t (*)(const void* to_id);
  find_fn find;
  const std::type_info& type;

  // tables[0..x) immediately after this header (see vtable_meta_inst).
  vt_ptr const* tables() const noexcept {
    return reinterpret_cast<vt_ptr const*>(this + 1);
  }

  vt_ptr find_vt(const void* to_id) const noexcept {
    if (!find)
      return nullptr;
    std::size_t k = find(to_id);
    return k == static_cast<std::size_t>(-1) ? nullptr : tables()[k];
  }

  const std::type_info& typeid_of() const noexcept { return type; }
};

// Negative prefix (relative to method-region origin ptr):
//   always:              [-1]=destroy (~Storage / Src)
//   RTTI:                [-2]=meta
// Storage size trails methods only when Trait has op_construct.
// op_construct overloads are method slots (rproxy selects rvalue overload).
template <template <class> class Trait>
inline const vtable_meta* meta_of(vt_ptr vt) {
  return from_vt_fn<const vtable_meta*()>(vt[-2])();
}

#endif  // ASF_HAS_DYN_RTTI

using vt_destroy_fn = void (*)(void* p);
using vt_construct_fn = void (*)(void*, vt_ptr, void*);

inline vt_destroy_fn dyn_vt_destroy(vt_ptr vt) {
  return from_vt_fn<void(void*)>(vt[-1]);
}

// Size value slot: sizeof in low bits, log2(alignof) in high bits (not a
// function). Tag width from alignof(max_align_t); static_assert if Storage is
// too large / over-aligned. make_dyn_sptr moves align tag into destroy low bits.
consteval std::size_t dyn_align_log2(std::size_t a) {
  std::size_t log = 0;
  while ((std::size_t{1} << log) < a)
    ++log;
  return log;
}

consteval unsigned dyn_bit_width(std::size_t n) {
  unsigned w = 0;
  do {
    ++w;
    n >>= 1;
  } while (n != 0);
  return w;
}

inline constexpr std::size_t dyn_max_align = alignof(std::max_align_t);
inline constexpr std::size_t dyn_max_align_log = dyn_align_log2(dyn_max_align);
inline constexpr unsigned dyn_align_tag_bits = dyn_bit_width(dyn_max_align_log);
inline constexpr unsigned dyn_size_value_bits =
    static_cast<unsigned>(sizeof(std::size_t) * 8) - dyn_align_tag_bits;
inline constexpr std::size_t dyn_size_value_mask =
    (std::size_t{1} << dyn_size_value_bits) - 1;
inline constexpr std::size_t dyn_align_tag_mask =
    (std::size_t{1} << dyn_align_tag_bits) - 1;

// Not consteval: integer-to-pointer is not a constant expression.
template <typename Storage>
inline vt_fn storage_size_slot() {
  constexpr std::size_t sz = sizeof(Storage);
  constexpr std::size_t al = alignof(Storage);
  static_assert((al & (al - 1)) == 0, "alignof(Storage) must be power of 2");
  static_assert(al <= dyn_max_align,
                "alignof(Storage) exceeds alignof(std::max_align_t)");
  static_assert(sz <= dyn_size_value_mask,
                "sizeof(Storage) exceeds size-slot capacity with align tag");
  const std::size_t enc = sz | (dyn_align_log2(al) << dyn_size_value_bits);
  return reinterpret_cast<vt_fn>(enc);
}

inline std::size_t storage_size_from_slot(vt_fn slot) {
  return reinterpret_cast<std::size_t>(slot) & dyn_size_value_mask;
}

inline std::size_t storage_align_log_from_slot(vt_fn slot) {
  return reinterpret_cast<std::size_t>(slot) >> dyn_size_value_bits;
}

inline std::size_t storage_align_from_slot(vt_fn slot) {
  return std::size_t{1} << storage_align_log_from_slot(slot);
}

// Packs align_log into the low bits of a function pointer. Platform bet:
// function addresses keep the low dyn_align_tag_bits bits zero (>= 8-byte
// function alignment). Not constexpr-checkable (addresses are runtime), so
// assert at the point of tagging; a platform that misaligns must disable this
// packing and carry align separately.
inline vt_destroy_fn tag_destroy_align(vt_destroy_fn fn, std::size_t align_log) {
  auto bits = reinterpret_cast<std::uintptr_t>(fn);
  assert((bits & dyn_align_tag_mask) == 0 &&
         "function pointer low bits not free for the align tag");
  return reinterpret_cast<vt_destroy_fn>(
      bits | (align_log & dyn_align_tag_mask));
}

inline vt_destroy_fn untag_destroy_fn(vt_destroy_fn tagged) {
  auto bits = reinterpret_cast<std::uintptr_t>(tagged);
  return reinterpret_cast<vt_destroy_fn>(
      bits & ~static_cast<std::uintptr_t>(dyn_align_tag_mask));
}

inline std::size_t align_log_from_destroy(vt_destroy_fn tagged) {
  auto bits = reinterpret_cast<std::uintptr_t>(tagged);
  return bits & dyn_align_tag_mask;
}

// Single-hop: exact Redirect(to), or Redirect(cover) with to => cover (sections).
consteval std::size_t find_redirect_slot(std::meta::info from,
                                         std::meta::info to) {
  auto flat = flatten_layout(from);
  for (std::size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].kind == slot_kind::redirect && flat[i].trait_void == to)
      return i;
  }
  for (std::size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].kind == slot_kind::redirect &&
        is_section_prefix(to, flat[i].trait_void))
      return i;
  }
  return static_cast<std::size_t>(-1);
}

// Cover table identity for From->To (From if section-prefix, else Redirect target).
consteval std::meta::info convert_cover_of(std::meta::info from,
                                           std::meta::info to) {
  if (from == to || is_section_prefix(to, from))
    return from;
  auto idx = find_redirect_slot(from, to);
  if (idx == static_cast<std::size_t>(-1))
    return std::meta::info{};
  return flatten_layout(from)[idx].trait_void;
}

template <template <class> class From, template <class> class To>
consteval std::meta::info convert_cover_of() {
  return convert_cover_of(^^From<void>, ^^To<void>);
}

consteval void collect_vtable_traits(std::meta::info trait,
                                     std::vector<std::meta::info>& out) {
  if (contains_info(out, trait))
    return;
  out.push_back(trait);
  for (auto r : redirect_targets_of(trait))
    collect_vtable_traits(r, out);
}

// Distinct physical table Trait identities (leaf + recursive cover Redirects).
consteval std::vector<std::meta::info> vtable_traits_of(
    std::meta::info trait_void) {
  std::vector<std::meta::info> out;
  collect_vtable_traits(trait_void, out);
  return out;
}

// Distinct tables for Trait (root + recursive cover Redirects). Shared once.
template <template <class> class Trait>
consteval std::size_t vtable_instance_count() {
  return vtable_traits_of(^^Trait<void>).size();
}

#if ASF_HAS_DYN_RTTI
// trait_id switch targets: Final first, then each dyn-upcast-reachable section
// that has a cover under Final. find returns one of the x distinct tables
// (vtable_traits_of), not a per-target slot.
consteval std::vector<std::meta::info> rtti_targets_of(
    std::meta::info final_void) {
  std::vector<std::meta::info> result;
  result.push_back(final_void);
  auto reachable = dyn_upcast_reachable_of(final_void);
  for (auto s : sections_only_of(final_void)) {
    if (s == final_void)
      continue;
    if (!contains_info(reachable, s))
      continue;
    if (convert_cover_of(final_void, s) == std::meta::info{})
      continue;
    result.push_back(s);
  }
  return result;
}

// Index of convert_cover(Final, target) within vtable_traits_of(Final).
consteval std::size_t rtti_table_index_of(std::meta::info final_void,
                                          std::meta::info target) {
  auto cover = convert_cover_of(final_void, target);
  auto tables = vtable_traits_of(final_void);
  for (std::size_t i = 0; i < tables.size(); ++i) {
    if (tables[i] == cover)
      return i;
  }
  return static_cast<std::size_t>(-1);
}
#endif

// How many deduced template params the method has (`auto` or template <class>).
// Counted from display_string_of; dummy Self is never substituted.
consteval int auto_param_count(std::meta::info method) {
  if (!std::meta::is_function_template(method))
    return 0;
  return display_auto_count(method);
}

consteval std::size_t display_self_index(std::meta::info method) {
  try {
    auto s = std::meta::display_string_of(method);
    auto lp = s.find('(');
    auto rp = s.rfind(')');
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp)
      return 0;
    auto inside = s.substr(lp + 1, rp - lp - 1);
    std::size_t idx = 0;
    int depth = 0;
    std::size_t start = 0;
    auto consider = [&](std::string_view part) {
      if (part.find("auto:") != std::string_view::npos)
        return true;
      return false;
    };
    for (std::size_t i = 0; i <= inside.size(); ++i) {
      char c = i < inside.size() ? inside[i] : ',';
      if (c == '<' || c == '(')
        ++depth;
      else if ((c == '>' || c == ')') && depth > 0)
        --depth;
      else if ((c == ',' && depth == 0) || i == inside.size()) {
        auto part = inside.substr(start, i - start);
        if (consider(part))
          return idx;
        ++idx;
        start = i + 1;
      }
    }
    return 0;
  } catch (std::meta::exception const&) {
    return 0;
  }
}

// Index of the mirror/`auto` self parameter (the auto). Concrete Trait<T>
// methods keep the conventional 0. A dyn slot must report 0.
// Declaration only: dummy Self is not substituted.
consteval std::size_t self_param_index(std::meta::info method) {
  if (!std::meta::is_function_template(method))
    return 0;
  return display_self_index(method);
}

// Dummy Self is never substituted (GCC instantiates the body). Declaration
// facts use display_string_of / signature_decl_of (NTTP). A real instantiate
// passes Src.
consteval std::meta::info instantiate(std::meta::info method,
                                      std::meta::info self) {
  if (!std::meta::is_function_template(method))
    return method;
  if (self == ^^signature_self)
    return std::meta::info{};
  int n = auto_param_count(method);
  if (n <= 0) {
    // Named `template<class T>` (not abbreviated auto:) still needs Src.
    for (int k = 1; k <= 4; ++k) {
      std::vector<std::meta::info> args(static_cast<std::size_t>(k), self);
      if (std::meta::can_substitute(method, args))
        return std::meta::substitute(method, args);
    }
    return std::meta::info{};
  }
  std::vector<std::meta::info> args(static_cast<std::size_t>(n), self);
  if (!std::meta::can_substitute(method, args))
    return std::meta::info{};
  return std::meta::substitute(method, args);
}

// Non-self parameter count after Self substitution (overload resolution).
consteval std::size_t method_nonself_arity(std::meta::info method) {
  auto inst = instantiate(method, ^^signature_self);
  if (inst == std::meta::info{}) {
    int n = display_param_count(method);
    if (n <= 0)
      return static_cast<std::size_t>(-1);
    return static_cast<std::size_t>(n - 1);
  }
  auto params = std::meta::parameters_of(inst);
  std::size_t self_i = self_param_index(method);
  if (self_i >= params.size())
    return params.size();
  return params.size() - 1;
}

consteval std::meta::info method_nonself_param_type(std::meta::info method,
                                                    std::size_t k) {
  auto inst = instantiate(method, ^^signature_self);
  auto params = std::meta::parameters_of(inst);
  std::size_t self_i = self_param_index(method);
  std::size_t j = 0;
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i == self_i)
      continue;
    if (j == k)
      return std::meta::type_of(params[i]);
    ++j;
  }
  return std::meta::info{};
}

consteval bool method_args_match_arity(std::meta::info method,
                                       std::size_t nargs) {
  auto n = method_nonself_arity(method);
  if (n == static_cast<std::size_t>(-1))
    return false;
  return n == nargs;
}

// Extra params from display_string_of (dummy Self is not substituted).
// Skip the auto self; compare the rest to Args display names.
template <class... Args>
consteval bool display_extras_match(std::meta::info method) {
  try {
    auto s = std::meta::display_string_of(method);
    auto lp = s.find('(');
    auto rp = s.rfind(')');
    if (lp == std::string_view::npos || rp == std::string_view::npos ||
        rp <= lp)
      return false;
    auto inside = s.substr(lp + 1, rp - lp - 1);
    std::vector<std::string_view> parts;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= inside.size(); ++i) {
      char c = i < inside.size() ? inside[i] : ',';
      if (c == '<' || c == '(')
        ++depth;
      else if ((c == '>' || c == ')') && depth > 0)
        --depth;
      else if ((c == ',' && depth == 0) || i == inside.size()) {
        auto part = inside.substr(start, i - start);
        std::size_t a = 0, b = part.size();
        while (a < b && (part[a] == ' ' || part[a] == '\t'))
          ++a;
        while (b > a && (part[b - 1] == ' ' || part[b - 1] == '\t'))
          --b;
        if (a < b)
          parts.push_back(part.substr(a, b - a));
        start = i + 1;
      }
    }
    std::size_t self_i = display_self_index(method);
    if (self_i >= parts.size())
      return false;
    std::vector<std::string_view> extras;
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (i != self_i)
        extras.push_back(parts[i]);
    }
    if (extras.size() != sizeof...(Args))
      return false;
    std::meta::info want[] = {^^std::remove_cvref_t<Args>...};
    for (std::size_t k = 0; k < extras.size(); ++k) {
      auto w = std::meta::display_string_of(want[k]);
      auto got = extras[k];
      if (std::meta::is_array_type(want[k]))
        w = std::meta::display_string_of(
            std::meta::add_pointer(std::meta::remove_extent(want[k])));
      if (got.size() < w.size() || !got.starts_with(w))
        return false;
      if (got.size() > w.size()) {
        char c = got[w.size()];
        if (c != ' ' && c != '&' && c != ',' && c != ')' && c != '\t')
          return false;
      }
    }
    return true;
  } catch (std::meta::exception const&) {
    return false;
  }
}

// Resolve overload: arity first, then remove_cvref param types vs Args...
// (op_call int/double/const char*, prefix/postfix ++, log overloads, etc.).
// Single same-name method: return it without matching (decl-only Trait methods
// often cannot instantiate for signature probing).
template <class... Args>
consteval bool method_args_match_types(std::meta::info method) {
  constexpr std::size_t nargs = sizeof...(Args);
  if (!method_args_match_arity(method, nargs))
    return false;
  if constexpr (nargs == 0)
    return true;
  else {
    auto inst = instantiate(method, ^^signature_self);
    if (inst == std::meta::info{})
      return display_extras_match<Args...>(method);
    std::meta::info want[] = {^^Args...};
    for (std::size_t k = 0; k < nargs; ++k) {
      auto pt = method_nonself_param_type(method, k);
      if (pt == std::meta::info{})
        return false;
      // Decay arrays to pointers for char[N] vs char* / const char*.
      auto got = std::meta::remove_cvref(pt);
      auto w = std::meta::remove_cvref(want[k]);
      if (std::meta::is_array_type(w))
        w = std::meta::add_pointer(std::meta::remove_extent(w));
      if (got != w)
        return false;
    }
    return true;
  }
}

// self_idx: 0 = handle on the left ((*h) op x / h op x); 1 = other operand
// first (x op h). Dyn only collects self_idx 0. Reverse (idx 1) is iface-only
// and must be `static` (iface-only).
// const_self: -1 any, 0 mutable self, 1 const self.
template <class... Args>
consteval std::meta::info find_method_for_args(std::meta::info cls,
                                               std::string_view name,
                                               std::size_t self_idx = 0,
                                               int const_self = -1) {
  auto named = find_methods_named(cls, name);
  std::vector<std::meta::info> at;
  for (auto m : named) {
    if (self_param_index(m) != self_idx)
      continue;
    if (const_self >= 0 &&
        is_const_self_method(m) != static_cast<bool>(const_self))
      continue;
    at.push_back(m);
  }
  if (at.empty())
    return std::meta::info{};
  if (at.size() == 1)
    return at[0];
  constexpr std::size_t nargs = sizeof...(Args);
  std::meta::info arity_hit = std::meta::info{};
  std::size_t arity_hits = 0;
  for (auto m : at) {
    if (!method_args_match_arity(m, nargs))
      continue;
    ++arity_hits;
    arity_hit = m;
    if (method_args_match_types<Args...>(m))
      return m;
  }
  // Ambiguous same-arity (e.g. int vs double) without a type match: fail.
  if (arity_hits == 1)
    return arity_hit;
  return std::meta::info{};
}

template <std::meta::info Method, class... Args>
consteval bool method_args_match_types_of() {
  if (!method_args_match_arity(Method, sizeof...(Args)))
    return false;
  if constexpr (sizeof...(Args) == 0)
    return true;
  else {
    constexpr auto inst = signature_decl_of<Method>();
    if (inst == std::meta::info{})
      return display_extras_match<Args...>(Method);
    std::meta::info want[] = {^^Args...};
    std::size_t self_i = self_param_index(Method);
    auto params = std::meta::parameters_of(inst);
    std::size_t k = 0;
    for (std::size_t i = 0; i < params.size(); ++i) {
      if (i == self_i)
        continue;
      if (k >= sizeof...(Args))
        return false;
      auto got = std::meta::remove_cvref(std::meta::type_of(params[i]));
      auto w = std::meta::remove_cvref(want[k]);
      if (std::meta::is_array_type(w))
        w = std::meta::add_pointer(std::meta::remove_extent(w));
      if (got != w)
        return false;
      ++k;
    }
    return k == sizeof...(Args);
  }
}

template <std::meta::info Cls, std::size_t I>
consteval std::meta::info find_method_named_at(std::string_view name,
                                               std::size_t self_idx,
                                               int const_self) {
  constexpr auto m = trait_methods(Cls)[I];
  if (!std::meta::has_identifier(m) || std::meta::identifier_of(m) != name)
    return std::meta::info{};
  if (self_param_index(m) != self_idx)
    return std::meta::info{};
  if (const_self >= 0 &&
      is_const_self_method(m) != static_cast<bool>(const_self))
    return std::meta::info{};
  return m;
}

template <std::meta::info Cls, std::size_t I, class... Args>
consteval std::meta::info find_method_hit(std::string_view name,
                                          std::size_t self_idx,
                                          int const_self) {
  constexpr auto m = trait_methods(Cls)[I];
  if (!std::meta::has_identifier(m) || std::meta::identifier_of(m) != name)
    return std::meta::info{};
  if (self_param_index(m) != self_idx)
    return std::meta::info{};
  if (const_self >= 0 &&
      is_const_self_method(m) != static_cast<bool>(const_self))
    return std::meta::info{};
  if (!method_args_match_types_of<m, Args...>())
    return std::meta::info{};
  return m;
}

template <std::meta::info Cls, class... Args, std::size_t... I>
consteval std::meta::info find_method_unrolled(std::string_view name,
                                               std::size_t self_idx,
                                               int const_self,
                                               std::index_sequence<I...>) {
  std::meta::info typed[] = {
      find_method_hit<Cls, I, Args...>(name, self_idx, const_self)...};
  std::meta::info named[] = {
      find_method_named_at<Cls, I>(name, self_idx, const_self)...};
  std::meta::info typed_hit{};
  int typed_n = 0;
  for (auto h : typed) {
    if (h == std::meta::info{})
      continue;
    ++typed_n;
    typed_hit = h;
  }
  if (typed_n == 1)
    return typed_hit;
  if (typed_n > 1)
    return std::meta::info{};
  std::meta::info named_hit{};
  int named_n = 0;
  for (auto h : named) {
    if (h == std::meta::info{})
      continue;
    ++named_n;
    named_hit = h;
  }
  return named_n == 1 ? named_hit : std::meta::info{};
}

template <std::meta::info Cls, class... Args>
consteval std::meta::info find_method_for_args_on(
    std::string_view name, std::size_t self_idx = 0, int const_self = -1) {
  constexpr auto n = trait_methods(Cls).size();
  if constexpr (n == 0)
    return std::meta::info{};
  else
    return find_method_unrolled<Cls, Args...>(
        name, self_idx, const_self, std::make_index_sequence<n>{});
}

template <std::meta::info Cls, class... Args>
consteval bool trait_has_op_at(std::string_view name, std::size_t self_idx,
                               int const_self = -1) {
  return find_method_for_args_on<Cls, Args...>(name, self_idx, const_self) !=
         std::meta::info{};
}

// Overload with self at self_idx (used to pick mixin inheritance).
// const_self: -1 any, 0 mutable self, 1 const self.
consteval bool trait_has_op_named_at(std::meta::info cls, std::string_view name,
                                     std::size_t self_idx,
                                     int const_self = -1) {
  for (auto m : find_methods_named(cls, name)) {
    if (self_param_index(m) != self_idx)
      continue;
    if (const_self >= 0 &&
        is_const_self_method(m) != static_cast<bool>(const_self))
      continue;
    return true;
  }
  return false;
}

// Match key: return type for convert/construct; RHS param type for assign.
// Construct Src-wildcard: void / undeduced auto / signature_self (placeholder
// body `return self`) -> imple_t. Avoid requiring a concrete return_type_of.
consteval std::meta::info special_op_key_type(std::meta::info inst,
                                              special_op kind) {
  if (is_assign_op(kind)) {
    auto params = std::meta::parameters_of(inst);
    if (params.size() < 2)
      return std::meta::info{};
    std::size_t self_i = 0;
    for (std::size_t i = 0; i < params.size(); ++i) {
      if (std::meta::remove_cvref(std::meta::type_of(params[i])) ==
          ^^signature_self) {
        self_i = i;
        break;
      }
    }
    std::size_t key_i = self_i == 0 ? 1 : 0;
    auto key = std::meta::remove_cvref(std::meta::type_of(params[key_i]));
    // auto RHS -> Src-like wildcard.
    if (key == ^^signature_self)
      return ^^imple_t;
    return key;
  }
  if (kind == special_op::construct) {
    try {
      auto ret = std::meta::remove_cvref(std::meta::return_type_of(inst));
      // void / Self placeholder -> Src wildcard (real type is Storage/Src).
      if (ret == ^^void || ret == ^^signature_self)
        return ^^imple_t;
      return ret;
    } catch (std::meta::exception const&) {
      // Undeduced auto return -> same as Src wildcard.
      return ^^imple_t;
    }
  }
  return std::meta::remove_cvref(std::meta::return_type_of(inst));
}

// 0 = none, 1 = concrete Key, 2 = imple/auto wildcard.
template <std::meta::info Cls, class Key, special_op Kind, std::size_t I>
consteval int find_special_op_kind() {
  constexpr auto member = trait_methods(Cls)[I];
  if (!std::meta::has_identifier(member) ||
      !is_special_op_name(std::meta::identifier_of(member), Kind))
    return 0;
  constexpr auto inst = signature_decl_of<member>();
  if (inst == std::meta::info{})
    return 0;
  auto key = method_prefers_imple(member) ? ^^imple_t
                                         : special_op_key_type(inst, Kind);
  if (key == std::meta::info{})
    return 0;
  if (key == ^^imple_t) {
    if (Kind == special_op::construct && is_rvalue_self_method(member))
      return 0;
    return 2;
  }
  if (key != std::meta::remove_cvref(^^Key))
    return 0;
  return 1;
}

template <std::meta::info Cls, class Key, special_op Kind, std::size_t... I>
consteval std::meta::info find_special_op_unrolled(std::index_sequence<I...>) {
  int kinds[] = {find_special_op_kind<Cls, Key, Kind, I>()...};
  std::meta::info members[] = {trait_methods(Cls)[I]...};
  std::meta::info concrete{};
  std::meta::info wildcard{};
  int nc = 0;
  int nw = 0;
  for (std::size_t i = 0; i < sizeof...(I); ++i) {
    if (kinds[i] == 1) {
      ++nc;
      concrete = members[i];
    } else if (kinds[i] == 2) {
      ++nw;
      wildcard = members[i];
    }
  }
  if (nc == 1)
    return concrete;
  if (nc > 1)
    return std::meta::info{};
  return nw == 1 ? wildcard : std::meta::info{};
}

template <std::meta::info Cls, class Key, special_op Kind>
consteval std::meta::info find_special_op_method_on() {
  constexpr auto n = trait_methods(Cls).size();
  if constexpr (n == 0)
    return std::meta::info{};
  else
    return find_special_op_unrolled<Cls, Key, Kind>(
        std::make_index_sequence<n>{});
}

template <template <class> class Trait, typename To>
consteval bool trait_has_convert() {
  return find_special_op_method_on<^^Trait<void>, To, special_op::convert>() !=
         std::meta::info{};
}

template <template <class> class Trait, typename To>
consteval bool trait_has_construct() {
  return find_special_op_method_on<^^Trait<void>, To,
                                   special_op::construct>() !=
         std::meta::info{};
}

template <template <class> class Trait, typename From>
consteval bool trait_has_assign() {
  return find_special_op_method_on<^^Trait<void>, From,
                                   special_op::assign>() !=
         std::meta::info{};
}

enum class convert_kind { iface, unsourced_iface, dyn, c_dyn };

template <convert_kind Kind>
consteval bool special_op_require_const() {
  return Kind == convert_kind::c_dyn;
}
template <convert_kind Kind>
consteval bool special_op_require_dyn() {
  return Kind == convert_kind::dyn || Kind == convert_kind::c_dyn;
}
template <convert_kind Kind>
consteval bool special_op_allow_impl() {
  return Kind != convert_kind::unsourced_iface;
}

// Key from the declaration probe (written return). No dummy instantiate.
template <std::meta::info Cls, special_op Op, convert_kind Kind, std::size_t I>
consteval std::meta::info special_op_entry_key() {
  constexpr auto member = trait_methods(Cls)[I];
  if (!std::meta::has_identifier(member) ||
      !is_special_op_name(std::meta::identifier_of(member), Op))
    return std::meta::info{};
  if (!special_op_allow_impl<Kind>() && Op == special_op::construct)
    return std::meta::info{};
  if (special_op_require_const<Kind>() && !is_const_self_method(member))
    return std::meta::info{};
  if (special_op_require_dyn<Kind>() && !is_dyn_slot(member))
    return std::meta::info{};
  constexpr auto inst = signature_decl_of<member>();
  if (inst == std::meta::info{})
    return std::meta::info{};
  if (method_prefers_imple(member))
    return ^^imple_t;
  return special_op_key_type(inst, Op);
}

template <std::meta::info Cls, special_op Op, convert_kind Kind, std::size_t I>
consteval std::meta::info special_op_entry_member() {
  constexpr auto key = special_op_entry_key<Cls, Op, Kind, I>();
  if (key == std::meta::info{})
    return std::meta::info{};
  if (Op == special_op::convert && key == ^^imple_t)
    return std::meta::info{};
  if (Op == special_op::assign &&
      (key == ^^imple_t || method_prefers_imple(trait_methods(Cls)[I])))
    return std::meta::info{};
  return trait_methods(Cls)[I];
}

template <std::meta::info Cls, special_op Op, convert_kind Kind,
          std::size_t... I>
consteval std::vector<std::meta::info>
collect_for_kind_unrolled(std::index_sequence<I...>) {
  std::meta::info members[] = {
      special_op_entry_member<Cls, Op, Kind, I>()...};
  std::meta::info keys[] = {special_op_entry_key<Cls, Op, Kind, I>()...};
  std::vector<std::meta::info> out;
  std::vector<std::meta::info> seen;
  constexpr std::size_t n = sizeof...(I);
  for (std::size_t i = 0; i < n; ++i) {
    if (members[i] == std::meta::info{} || keys[i] == std::meta::info{})
      continue;
    bool dup = false;
    for (auto s : seen) {
      if (s == keys[i]) {
        dup = true;
        break;
      }
    }
    if (dup)
      continue;
    seen.push_back(keys[i]);
    out.push_back(members[i]);
  }
  return out;
}

template <std::meta::info Cls, special_op Op, convert_kind Kind>
consteval std::vector<std::meta::info> collect_for_kind_on() {
  constexpr auto n = trait_methods(Cls).size();
  if constexpr (n == 0)
    return {};
  else
    return collect_for_kind_unrolled<Cls, Op, Kind>(
        std::make_index_sequence<n>{});
}

template <template <class> class Trait, convert_kind Kind, special_op Op>
consteval std::size_t special_op_count() {
  return collect_for_kind_on<^^Trait<void>, Op, Kind>().size();
}

template <template <class> class Trait, convert_kind Kind, special_op Op,
          std::size_t I>
consteval std::meta::info special_op_method_at() {
  return collect_for_kind_on<^^Trait<void>, Op, Kind>()[I];
}

template <template <class> class Trait, convert_kind Kind>
consteval std::size_t convert_count() {
  return special_op_count<Trait, Kind, special_op::convert>();
}

template <template <class> class Trait, convert_kind Kind, std::size_t I>
consteval std::meta::info convert_method_at() {
  return special_op_method_at<Trait, Kind, special_op::convert, I>();
}

template <template <class> class Trait, convert_kind Kind>
consteval std::size_t construct_count() {
  return special_op_count<Trait, Kind, special_op::construct>();
}

template <template <class> class Trait, convert_kind Kind, std::size_t I>
consteval std::meta::info construct_method_at() {
  return special_op_method_at<Trait, Kind, special_op::construct, I>();
}

consteval bool has_annotation(std::meta::info r, std::meta::info tag) {
  if (r == std::meta::info{})
    return false;
  try {
    return !std::meta::annotations_of_with_type(r, tag).empty();
  } catch (std::meta::exception const&) {
    return false;
  }
}

// Substitute ann_probe_self so method-level tags are readable.
// Construct: only a declared mirror/rmirror self (value_of). Object-self
// construct may touch fields; those tags wait for Src at invoke.
// Other methods: void return only (`self.field` default bodies stay off).
consteval bool display_return_void(std::meta::info method) {
  try {
    auto s = std::meta::display_string_of(method);
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
      ++i;
    if (s.substr(i).starts_with("template")) {
      i += 8;
      while (i < s.size() && s[i] != '<')
        ++i;
      if (i < s.size() && s[i] == '<') {
        int depth = 0;
        for (; i < s.size(); ++i) {
          if (s[i] == '<')
            ++depth;
          else if (s[i] == '>') {
            --depth;
            if (depth == 0) {
              ++i;
              break;
            }
          }
        }
      }
    }
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
      ++i;
    return s.substr(i).starts_with("void") &&
           (i + 4 >= s.size() || s[i + 4] == ' ' || s[i + 4] == '\t');
  } catch (std::meta::exception const&) {
    return false;
  }
}

consteval bool display_self_is_mirror(std::meta::info method) {
  try {
    auto s = std::meta::display_string_of(method);
    return s.find("mirror") != std::string_view::npos;
  } catch (std::meta::exception const&) {
    return false;
  }
}

consteval bool method_ann_should_probe(std::meta::info method,
                                       std::meta::info yes_tag) {
  if (!std::meta::is_function_template(method))
    return false;
  bool construct = std::meta::has_identifier(method) &&
                   is_special_op_name(std::meta::identifier_of(method),
                                      special_op::construct);
  if (yes_tag == ^^ann::imple_tag || yes_tag == ^^ann::no_imple_tag)
    return construct && method_nonself_arity(method) == 0 &&
           display_self_is_mirror(method);
  if (yes_tag == ^^ann::mirror_tag || yes_tag == ^^ann::no_mirror_tag)
    return !construct && display_return_void(method);
  return false;
}

consteval std::meta::info method_ann_inst(std::meta::info method,
                                          std::meta::info yes_tag) {
  if (!method_ann_should_probe(method, yes_tag))
    return method;
  auto inst = instantiate(method, ^^ann_probe_self);
  return inst == std::meta::info{} ? method : inst;
}

consteval std::meta::info method_ann_inst(std::meta::info method) {
  return method_ann_inst(method, ^^ann::imple_tag);
}

enum class ann_choice { unset, yes, no };

// Read yes/no on one declaration (template or instantiation).
consteval ann_choice ann_choice_of(std::meta::info entity,
                                   std::meta::info yes_tag,
                                   std::meta::info no_tag) {
  if (entity == std::meta::info{})
    return ann_choice::unset;
  bool yes = has_annotation(entity, yes_tag);
  bool no = has_annotation(entity, no_tag);
  if (yes && no)
    throw std::meta::exception(
        "annotation and its no-form are both present", entity,
        std::source_location::current());
  if (yes)
    return ann_choice::yes;
  if (no)
    return ann_choice::no;
  return ann_choice::unset;
}

// Template first (throws -> unset), then the ann_probe_self substitute.
consteval ann_choice ann_choice_on_method(std::meta::info method,
                                          std::meta::info yes_tag,
                                          std::meta::info no_tag) {
  auto on_decl = ann_choice_of(method, yes_tag, no_tag);
  if (on_decl != ann_choice::unset)
    return on_decl;
  auto inst = method_ann_inst(method, yes_tag);
  if (inst == method)
    return ann_choice::unset;
  return ann_choice_of(inst, yes_tag, no_tag);
}

consteval bool ann_enabled(std::meta::info method, std::meta::info yes_tag,
                           std::meta::info no_tag, bool default_yes) {
  if (method == std::meta::info{})
    return false;
  auto on_method = ann_choice_on_method(method, yes_tag, no_tag);
  if (on_method == ann_choice::yes)
    return true;
  if (on_method == ann_choice::no)
    return false;
  auto cls = std::meta::parent_of(method);
  auto on_trait = ann_choice_of(cls, yes_tag, no_tag);
  if (on_trait == ann_choice::yes)
    return true;
  if (on_trait == ann_choice::no)
    return false;
  return default_yes;
}

// One layout section (Trait<void>): every method must allow a user mirror.
// Method mirror/no_mirror overrides the Trait tag. Default is no_mirror.
// op_construct is not part of that agreement (it never follows the Trait
// tag). A section with only those methods follows the Trait tag.
consteval bool section_allows_mirror_source(std::meta::info section) {
  auto methods = trait_methods(section);
  bool saw = false;
  for (auto method : methods) {
    if (std::meta::has_identifier(method) &&
        is_lifetime_dyn_op_name(std::meta::identifier_of(method)))
      continue;
    saw = true;
    if (!ann_enabled(method, ^^ann::mirror_tag, ^^ann::no_mirror_tag, false))
      return false;
  }
  if (!saw)
    return ann_choice_of(section, ^^ann::mirror_tag, ^^ann::no_mirror_tag) ==
           ann_choice::yes;
  return true;
}

// User mirror with no Src -> dyn/iface: every Trait in the inheritance
// layout (sections_only_of) must allow mirror source. Empty layout does not
// allow. Sourced mirror bind does not use this gate (see dyn_bind_mirror_*).
template <template <class> class Trait>
consteval bool layout_allows_unsourced_mirror() {
  auto sections = sections_only_of(^^Trait<void>);
  if (sections.empty())
    return false;
  for (auto section : sections) {
    if (!section_allows_mirror_source(section))
      return false;
  }
  return true;
}

// Any layout section opts into mirror Self (user mirror / view As = T).
template <template <class> class Trait>
consteval bool any_section_allows_mirror_source() {
  for (auto section : sections_only_of(^^Trait<void>)) {
    if (section_allows_mirror_source(section))
      return true;
  }
  return false;
}

// Method-level [[=mirror]] on op_construct is ill-formed.
// Trait-level mirror is ignored. Cached per Trait; bases are not re-scanned.
template <template <class> class Trait>
struct lifetime_not_mirror_gate {
  static consteval void check_own() {
    for (auto m : trait_methods(^^Trait<void>)) {
      if (!std::meta::has_identifier(m) ||
          !is_lifetime_dyn_op_name(std::meta::identifier_of(m)))
        continue;
      if (ann_choice_on_method(m, ^^ann::mirror_tag, ^^ann::no_mirror_tag) ==
          ann_choice::yes)
        throw std::meta::exception(
            "op_construct cannot be [[=asf_ann::mirror]]; "
            "it does not follow the Trait mirror tag",
            m, std::source_location::current());
    }
  }

  static consteval std::size_t base_count() {
    return trait_bases_of(^^Trait<void>).size();
  }

  template <std::size_t I>
  static consteval void check_base() {
    constexpr auto b = trait_bases_of(^^Trait<void>)[I];
    constexpr auto tmpl = std::meta::template_of(b);
    constexpr auto ens =
        std::meta::substitute(^^ensure_lifetime_not_mirror, {tmpl});
    [:ens:]();
  }

  template <std::size_t... I>
  static consteval void check_bases(std::index_sequence<I...>) {
    (check_base<I>(), ...);
  }

  static consteval void compute() {
    check_own();
    check_bases(std::make_index_sequence<base_count()>{});
  }

  static constexpr int cached = (compute(), 0);
};

template <template <class> class Trait>
consteval void ensure_lifetime_not_mirror() {
  (void)lifetime_not_mirror_gate<Trait>::cached;
}

// Destroy is vt[-1] (~Src), so view != Src may still use dyn. The gate still
// runs so an op_construct marked mirror fails.
template <template <class> class Trait>
consteval bool sourced_dyn_view_mismatch_ok() {
  ensure_lifetime_not_mirror<Trait>();
  return true;
}

// [[=asf_ann::imple]] skips the Trait body. Non-op methods call the
// same-named member. op_* does not look up that name: construct, assign,
// convert, and catalog operators each have their own lookup.
// [[=asf_ann::no_imple]] overrides a Trait-level imple. Default is no_imple.
consteval bool method_prefers_imple(std::meta::info method) {
  return ann_enabled(method, ^^ann::imple_tag, ^^ann::no_imple_tag, false);
}

// The body-valid Self for annotation probes.
// - op_construct with a mirror-shaped named self (mirror<T>): its body builds
//   via value_of, which materializes trivially for the empty aggregate (no
//   fields, no ctor) -- safe for any As, and T stays a placeholder (fine for
//   reading annotations, never for dispatch).
// - a named mirror<T> self elsewhere, or any named non-mirror self: the
//   object type (field bodies compile, T deduces correctly).
// - an `auto` self: the impl mirror (field access on mirror<As>).
template <template <class> class Trait, typename As>
consteval std::meta::info ann_probe_self_for(std::meta::info method) {
  bool construct =
      std::meta::has_identifier(method) &&
      is_special_op_name(std::meta::identifier_of(method),
                         special_op::construct);
  if (construct && display_self_is_mirror(method))
    return ^^ann_probe_self;
  if (!std::meta::is_function_template(method))
    return method;
  if (display_self_is_mirror(method) || construct)
    return ^^std::remove_cvref_t<As>;
  return impl_self_info<Trait, As>();
}

// Read [[=imple]] with the body-valid Self substituted (see above). This is
// how imple on a non-construct template method is honored.
template <template <class> class Trait, typename As>
consteval bool method_prefers_imple_as(std::meta::info method) {
  auto on_decl = ann_choice_of(method, ^^ann::imple_tag, ^^ann::no_imple_tag);
  if (on_decl != ann_choice::unset)
    return on_decl == ann_choice::yes;
  if (!std::meta::is_function_template(method))
    return false;
  auto inst = instantiate(method, ann_probe_self_for<Trait, As>(method));
  if (inst == std::meta::info{})
    return false;
  return ann_choice_of(inst, ^^ann::imple_tag, ^^ann::no_imple_tag) ==
         ann_choice::yes;
}

consteval bool dyn_ann_auto_params_ok(std::meta::info method) {
  int n = auto_param_count(method);
  // n==-1: more than 4 deduced params, or an unreadable declaration.
  return n >= 0 && n <= 1;
}

// Src/auto-keyed assign: never dyn-dispatch (carrier and dyn * alike).
// Written return + one auto (Self) + concrete RHS is not impl-keyed.
// Two autos means Self + auto RHS.
consteval bool is_impl_keyed_assign(std::meta::info method) {
  if (!std::meta::has_identifier(method) ||
      !is_special_op_name(std::meta::identifier_of(method),
                          special_op::assign))
    return false;
  if (method_prefers_imple(method))
    return true;
  return auto_param_count(method) == 2;
}

consteval bool dyn_ann_assign_ok(std::meta::info method) {
  return !is_impl_keyed_assign(method);
}

consteval void check_dyn_ann_method(std::meta::info method) {
  if (has_placeholder_return(method))
    throw std::meta::exception(
        "dyn method must write a deduced return on the declaration "
        "(auto / decltype(auto) return is not allowed; use a trailing "
        "return, e.g. -> std::remove_cvref_t<decltype(self)>). "
        "Mark the method `static` to opt out",
        method, std::source_location::current());
  if (!dyn_ann_auto_params_ok(method))
    throw std::meta::exception(
        "dyn method may have at most one deduced template parameter (Self); "
        "further deduced parameters are not allowed "
        "(mark the method `static` to opt out)",
        method, std::source_location::current());
  if (self_param_index(method) != 0)
    throw std::meta::exception(
        "dyn slot requires self as the first parameter; "
        "a later self is iface-only (mark the method `static`)",
        method, std::source_location::current());
  if (!dyn_ann_assign_ok(method))
    throw std::meta::exception(
        "Src/auto op_assign cannot be a dyn slot; only concrete RHS assign "
        "is dyn-dispatchable (Src-related dyn op is construct only). "
        "Mark the method `static`",
        method, std::source_location::current());
}

// Conversion function -> language explicit; else [[=asf_ann::expl]] on named ops.
// Default implicit (like operator T()). Assignment ops are never explicit.
// Annotations are read from an instantiation (not the bare template).
consteval bool convert_is_explicit(std::meta::info member) {
  if (std::meta::has_identifier(member)) {
    auto name = std::meta::identifier_of(member);
    if (is_special_op_name(name, special_op::assign))
      return false;
  }

  if (std::meta::is_function_template(member))
    return ann_enabled(member, ^^ann::expl_tag, ^^ann::no_expl_tag, false);
  if (std::meta::is_function(member)) {
    if (std::meta::is_conversion_function(member))
      return std::meta::is_explicit(member);
    return ann_enabled(member, ^^ann::expl_tag, ^^ann::no_expl_tag, false);
  }
  return false;
}

consteval std::meta::info self_param_type_of(std::meta::info method) {
  if (!std::meta::is_function(method))
    return std::meta::info{};
  auto params = std::meta::parameters_of(method);
  if (params.empty())
    return std::meta::info{};
  std::size_t si = self_param_index(method);
  if (si >= params.size())
    si = 0;
  return std::meta::type_of(params[si]);
}

// How a declared self parameter is passed to the impl type.
// auto&& and other unqualified && are rvalue refs (move), same as
// by-value mirror<T&&> / c_rmirror (T carries &&). const&& stays a
// const rvalue. auto& / const auto& keep that lvalue ref. By-value
// mirror<T> is a copy (const lvalue).
enum class self_ref_kind { lvalue, const_lvalue, rvalue, const_rvalue };

// Placeholder-return construct has no declaration inst; read cvref from
// display_string_of (e.g. `const auto:6&` / `auto:6&&`).
consteval self_ref_kind self_ref_from_display(std::meta::info method) {
  try {
    auto s = std::meta::display_string_of(method);
    auto lp = s.find('(');
    auto rp = s.rfind(')');
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp)
      return self_ref_kind::const_lvalue;
    auto inside = s.substr(lp + 1, rp - lp - 1);
    std::string_view self_part = inside;
    int depth = 0;
    std::size_t start = 0;
    std::size_t idx = 0;
    std::size_t self_i = display_self_index(method);
    for (std::size_t i = 0; i <= inside.size(); ++i) {
      char c = i < inside.size() ? inside[i] : ',';
      if (c == '<' || c == '(')
        ++depth;
      else if ((c == '>' || c == ')') && depth > 0)
        --depth;
      else if ((c == ',' && depth == 0) || i == inside.size()) {
        if (idx == self_i) {
          self_part = inside.substr(start, i - start);
          break;
        }
        ++idx;
        start = i + 1;
      }
    }
    bool cnst = self_part.find("const") != std::string_view::npos;
    if (self_part.find("rmirror") != std::string_view::npos)
      return cnst ? self_ref_kind::const_rvalue : self_ref_kind::rvalue;
    auto amp = self_part.rfind('&');
    bool rval = amp != std::string_view::npos && amp > 0 &&
                self_part[amp - 1] == '&';
    if (rval)
      return cnst ? self_ref_kind::const_rvalue : self_ref_kind::rvalue;
    if (amp != std::string_view::npos)
      return cnst ? self_ref_kind::const_lvalue : self_ref_kind::lvalue;
    return self_ref_kind::const_lvalue;
  } catch (std::meta::exception const&) {
    return self_ref_kind::const_lvalue;
  }
}

consteval self_ref_kind self_param_ref_kind(std::meta::info method) {
  auto ty = self_param_type_of(method);
  if (ty == std::meta::info{})
    return self_ref_from_display(method);
  bool rval = std::meta::is_rvalue_reference_type(ty);
  bool cnst = std::meta::is_const_type(std::meta::remove_reference(ty));
  if (rval && cnst)
    return self_ref_kind::const_rvalue;
  if (rval)
    return self_ref_kind::rvalue;
  if (std::meta::is_lvalue_reference_type(ty))
    return cnst ? self_ref_kind::const_lvalue : self_ref_kind::lvalue;
  auto bare = std::meta::remove_cvref(ty);
  if (std::meta::has_template_arguments(bare) &&
      std::meta::template_of(bare) == ^^mirror) {
    auto args = std::meta::template_arguments_of(bare);
    if (!args.empty() && std::meta::is_rvalue_reference_type(args[0])) {
      bool tcnst =
          std::meta::is_const_type(std::meta::remove_reference(args[0]));
      return tcnst ? self_ref_kind::const_rvalue : self_ref_kind::rvalue;
    }
  }
  return self_ref_kind::const_lvalue;
}

consteval bool is_const_self_method(std::meta::info method) {
  auto k = self_param_ref_kind(method);
  return k == self_ref_kind::const_lvalue || k == self_ref_kind::const_rvalue;
}

// Move construct overload: self is rvalue-qualified -- either a parameter
// rvalue-ref (auto&& / mirror<T>&&) or mirror<T&&> (T carries &&). Copy uses
// mirror<T> const& / auto const&. Name stays op_construct.
consteval bool is_rvalue_self_method(std::meta::info method) {
  auto k = self_param_ref_kind(method);
  return k == self_ref_kind::rvalue || k == self_ref_kind::const_rvalue;
}

// Declaration probe (signature_decl_of): rmirror<T> / T&& without a body.
template <std::meta::info Method>
consteval self_ref_kind self_param_ref_kind_of() {
  if constexpr (!std::meta::is_function_template(Method))
    return self_param_ref_kind(Method);
  else {
    constexpr auto decl = signature_decl_of<Method>();
    if constexpr (decl == std::meta::info{})
      return self_ref_from_display(Method);
    else
      return self_param_ref_kind(decl);
  }
}

template <std::meta::info Method>
consteval bool is_rvalue_self_method_of() {
  auto k = self_param_ref_kind_of<Method>();
  return k == self_ref_kind::rvalue || k == self_ref_kind::const_rvalue;
}

// Erased dyn slot drops the self/`auto` parameter (found by type==signature_self
// on a signature_self instantiation, else index 0).
consteval std::meta::info erased_sig(std::meta::info inst) {
  std::vector<std::meta::info> args;
  args.push_back(std::meta::return_type_of(inst));
  auto params = std::meta::parameters_of(inst);
  std::size_t self_i = 0;
  bool found = false;
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (std::meta::remove_cvref(std::meta::type_of(params[i])) ==
        ^^signature_self) {
      self_i = i;
      found = true;
      break;
    }
  }
  if (!found)
    self_i = 0;
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i == self_i)
      continue;
    args.push_back(std::meta::type_of(params[i]));
  }
  return std::meta::substitute(^^erased_fn, args);
}

template <template <class> class Trait, typename T>
consteval std::meta::info impl_self_info() {
  int rank = 0;
  if constexpr (std::meta::is_complete_type(^^Trait<T>)) {
    for (auto method : trait_methods(^^Trait<T>)) {
      if (std::meta::is_function_template(method))
        continue;
      auto params = std::meta::parameters_of(method);
      if (params.empty())
        continue;
      auto ty = std::meta::remove_cvref(std::meta::type_of(params[0]));
      int c = 0;
      if (ty == ^^mirror<T> || ty == ^^mirror<const T>)
        c = 1;
      else if (std::meta::has_template_arguments(ty) &&
               std::meta::template_of(ty) == ^^mirror) {
        auto args = std::meta::template_arguments_of(ty);
        if (args.size() >= 2 && (args[0] == ^^T) && (args[1] != ^^void))
          c = 2;
      }
      if (c > rank)
        rank = c;
    }
  }
  if (rank == 2)
    return ^^identity_mirror<T>;
  return ^^mirror<T>;
}

// Mirror agreement read with the impl Self: the method body is written for
// that convention, so a substitute is valid even for value-returning bodies
// (the ann_probe_self gate only covers void returns).
template <template <class> class Trait, typename As>
consteval bool section_allows_mirror_source_as() {
  constexpr auto section = ^^Trait<void>;
  auto on_trait =
      ann_choice_of(section, ^^ann::mirror_tag, ^^ann::no_mirror_tag);
  auto methods = trait_methods(section);
  bool saw = false;
  for (auto method : methods) {
    if (std::meta::has_identifier(method) &&
        is_lifetime_dyn_op_name(std::meta::identifier_of(method)))
      continue;
    saw = true;
    ann_choice on_method = ann_choice::unset;
    if (!std::meta::is_function_template(method))
      on_method =
          ann_choice_of(method, ^^ann::mirror_tag, ^^ann::no_mirror_tag);
    else {
      auto inst = instantiate(method, ann_probe_self_for<Trait, As>(method));
      if (inst != std::meta::info{})
        on_method =
            ann_choice_of(inst, ^^ann::mirror_tag, ^^ann::no_mirror_tag);
    }
    if (on_method == ann_choice::unset)
      on_method = on_trait;
    if (on_method != ann_choice::yes)
      return false;
  }
  if (!saw)
    return on_trait == ann_choice::yes;
  return true;
}

// As-known variant of layout_allows_unsourced_mirror (for iface / dyn-bind sites
// where the concrete type is at hand).
template <template <class> class Trait, typename As, std::size_t I>
consteval bool section_allows_mirror_source_at() {
  constexpr auto sec = sections_only_of(^^Trait<void>)[I];
  constexpr auto tmpl = std::meta::template_of(sec);
  constexpr auto check =
      std::meta::substitute(^^section_allows_mirror_source_as, {tmpl, ^^As});
  return [:check:]();
}

template <template <class> class Trait, typename As, std::size_t... I>
consteval bool layout_allows_mirror_source_as_pack(std::index_sequence<I...>) {
  return (section_allows_mirror_source_at<Trait, As, I>() && ...);
}

template <template <class> class Trait, typename As>
consteval bool layout_allows_mirror_source_as() {
  constexpr std::size_t n = sections_only_of(^^Trait<void>).size();
  if constexpr (n == 0)
    return false;
  else
    return layout_allows_mirror_source_as_pack<Trait, As>(
        std::make_index_sequence<n>{});
}

// Conversion explicitness with the impl Self substituted (body is valid for
// it). Used at iface carrier generation, where the object type is at hand.
template <template <class> class Trait, typename As>
consteval bool convert_is_explicit_as(std::meta::info member) {
  if (std::meta::has_identifier(member) &&
      is_special_op_name(std::meta::identifier_of(member),
                         special_op::assign))
    return false;
  if (!std::meta::is_function_template(member)) {
    if (std::meta::is_conversion_function(member))
      return std::meta::is_explicit(member);
    return ann_enabled(member, ^^ann::expl_tag, ^^ann::no_expl_tag, false);
  }
  auto on_decl = ann_choice_of(member, ^^ann::expl_tag, ^^ann::no_expl_tag);
  if (on_decl != ann_choice::unset)
    return on_decl == ann_choice::yes;
  auto inst = instantiate(member, ann_probe_self_for<Trait, As>(member));
  if (inst == std::meta::info{})
    return false;
  return ann_choice_of(inst, ^^ann::expl_tag, ^^ann::no_expl_tag) ==
         ann_choice::yes;
}

template <template <class> class Trait, typename T>
consteval bool impl_self_is_identity_mirror() {
  return impl_self_info<Trait, T>() == ^^identity_mirror<T>;
}

template <typename V, bool = is_mirror_like_v<std::remove_reference_t<V>>>
struct deduce_iface {
  using type = std::remove_reference_t<V>;
};

template <typename V>
struct deduce_iface<V, true> {
  using type = mirror_target_t<std::remove_reference_t<V>>;
};

template <typename V>
using deduce_iface_t = typename deduce_iface<V>::type;

// Destroy the source object (Src / concrete Storage), never a bare mirror value.
template <typename Storage>
void destroy_src(void* ptr) {
  using Raw = std::remove_cvref_t<Storage>;
  if constexpr (is_sourced_mirror_v<Raw>) {
    std::destroy_at(mirror_src_ptr(*static_cast<Raw*>(ptr)));
  } else {
    static_assert(!is_mirror_v<Raw>, "dyn destroy needs a source object");
    std::destroy_at(static_cast<Raw*>(ptr));
  }
}

// Fixed vt[-1] thunk: always ~Storage / Src. Not a Trait method.
template <typename Storage>
void vt_src_destroy(void* ptr) {
  destroy_src<Storage>(ptr);
}

// One address for every trivial destroy. Callers can compare the slot to this.
inline void vt_destroy_nop(void*) noexcept {}

template <typename Storage>
consteval bool vt_destroy_is_nop() {
  using Raw = std::remove_cvref_t<Storage>;
  if constexpr (is_sourced_mirror_v<Raw>)
    return std::is_trivially_destructible_v<
        std::remove_cv_t<mirror_src_t<Raw>>>;
  else
    return std::is_trivially_destructible_v<Raw>;
}

// Every physical table, including c_dyn: vt[-1] is ~Src.
// A trivial ~Src shares vt_destroy_nop (one address), not a per-type thunk.
template <bool MaskNonConst, typename Storage>
inline vt_fn vt_fixed_destroy_slot() {
  (void)sizeof(MaskNonConst);
  if constexpr (vt_destroy_is_nop<Storage>())
    return as_vt_fn(static_cast<vt_destroy_fn>(&vt_destroy_nop));
  else
    return as_vt_fn(static_cast<vt_destroy_fn>(&vt_src_destroy<Storage>));
}

template <typename Spec, std::meta::info Impl>
decltype(auto) call_impl_member(auto&& self, auto&&... args) {
  using Self = std::remove_cvref_t<decltype(self)>;
  if constexpr (std::meta::is_function_template(Impl)) {
    constexpr auto inst = std::meta::substitute(Impl, {^^Self});
    if constexpr (std::meta::is_static_member(Impl))
      return [:inst:](self, std::forward<decltype(args)>(args)...);
    else {
      Spec object{};
      return object.[:inst:](self, std::forward<decltype(args)>(args)...);
    }
  } else if constexpr (std::meta::is_static_member(Impl)) {
    return [:Impl:](self, std::forward<decltype(args)>(args)...);
  } else {
    Spec object{};
    return object.[:Impl:](self, std::forward<decltype(args)>(args)...);
  }
}

consteval bool is_construct_or_assign_special(std::meta::info method) {
  if (!std::meta::has_identifier(method))
    return false;
  auto name = std::meta::identifier_of(method);
  return is_special_op_name(name, special_op::construct) ||
         is_special_op_name(name, special_op::assign);
}

template <typename Self>
  requires(is_sourced_mirror_v<Self>)
decltype(auto) mirror_src_ref(Self&& self) {
  return src_of(self);
}

// from_impl construct/assign: capability of Src (AsMove selects ctor/assign kind).
template <typename Self, std::meta::info Method, bool AsMove>
consteval bool src_has_special_member_impl() {
  if constexpr (!is_sourced_mirror_v<Self> ||
                !is_construct_or_assign_special(Method))
    return false;
  else {
    using Src = mirror_src_t<Self>;
    constexpr auto name = std::meta::identifier_of(Method);
    if constexpr (is_special_op_name(name, special_op::construct)) {
      constexpr auto kind = self_param_ref_kind(Method);
      if constexpr (kind == self_ref_kind::lvalue)
        return std::is_constructible_v<Src, Src&>;
      else if constexpr (kind == self_ref_kind::rvalue)
        return std::is_constructible_v<Src, Src&&>;
      else if constexpr (kind == self_ref_kind::const_rvalue)
        return std::is_constructible_v<Src, Src const&&>;
      else
        return std::is_constructible_v<Src, Src const&>;
    } else if constexpr (is_special_op_name(name, special_op::assign)) {
      if constexpr (AsMove)
        return std::is_assignable_v<Src&, Src&&>;
      else
        return std::is_assignable_v<Src&, Src const&>;
    } else {
      return false;
    }
  }
}

// Same-named callable on Src (member or static); arity = sizeof...(Args).
template <typename Src>
consteval std::meta::info find_src_method(std::string_view name,
                                          std::size_t arity) {
  for (auto member : std::meta::members_of(^^Src, unchecked())) {
    if (!std::meta::has_identifier(member) ||
        std::meta::identifier_of(member) != name)
      continue;
    if (std::meta::is_function(member)) {
      auto params = std::meta::parameters_of(member);
      // non-static: implicit object not in parameters_of on some impls;
      // count explicit params only.
      std::size_t n = params.size();
      if (std::meta::is_static_member(member)) {
        if (n == arity)
          return member;
      } else if (n == arity) {
        return member;
      }
    }
  }
  return std::meta::info{};
}

template <typename Src>
consteval bool src_has_named_method(std::string_view name, std::size_t arity) {
  return find_src_method<Src>(name, arity) != std::meta::info{};
}

// Call-arg arity of a Trait method (excludes the self/`auto` parameter).
consteval std::size_t trait_method_arg_arity(std::meta::info method) {
  auto inst = instantiate(method, ^^signature_self);
  if (inst == std::meta::info{}) {
    int n = display_param_count(method);
    if (n > 0)
      return static_cast<std::size_t>(n - 1);
    if (!std::meta::is_function(method))
      return 0;
    auto params = std::meta::parameters_of(method);
    return params.empty() ? 0 : params.size() - 1;
  }
  auto params = std::meta::parameters_of(inst);
  return params.empty() ? 0 : params.size() - 1;
}

template <std::meta::info A, std::meta::info B>
consteval bool extras_decl_match() {
  constexpr auto da = signature_decl_of<A>();
  constexpr auto db = signature_decl_of<B>();
  if (da == std::meta::info{} || db == std::meta::info{})
    return false;
  std::size_t sa = self_param_index(A);
  std::size_t sb = self_param_index(B);
  auto pa = std::meta::parameters_of(da);
  auto pb = std::meta::parameters_of(db);
  std::size_t ia = 0, ib = 0;
  while (ia < pa.size() && ib < pb.size()) {
    if (ia == sa) {
      ++ia;
      continue;
    }
    if (ib == sb) {
      ++ib;
      continue;
    }
    auto ta = std::meta::remove_cvref(std::meta::type_of(pa[ia]));
    auto tb = std::meta::remove_cvref(std::meta::type_of(pb[ib]));
    if (ta != tb)
      return false;
    ++ia;
    ++ib;
  }
  for (; ia < pa.size(); ++ia)
    if (ia != sa)
      return false;
  for (; ib < pb.size(); ++ib)
    if (ib != sb)
      return false;
  return true;
}

template <template <class> class Trait, typename As, std::meta::info Method,
          std::size_t I>
consteval std::meta::info trait_impl_hit() {
  constexpr auto m = trait_methods(^^Trait<As>)[I];
  if (!std::meta::has_identifier(m) ||
      std::meta::identifier_of(m) != std::meta::identifier_of(Method))
    return std::meta::info{};
  if (self_param_index(m) != self_param_index(Method))
    return std::meta::info{};
  if (!extras_decl_match<Method, m>())
    return std::meta::info{};
  return m;
}

template <template <class> class Trait, typename As, std::meta::info Method,
          std::size_t... I>
consteval std::meta::info trait_impl_unrolled(std::index_sequence<I...>) {
  std::meta::info hits[] = {trait_impl_hit<Trait, As, Method, I>()...};
  std::meta::info found{};
  int n = 0;
  for (auto h : hits) {
    if (h == std::meta::info{})
      continue;
    ++n;
    found = h;
  }
  return n == 1 ? found : std::meta::info{};
}

// Trait<void> method -> Trait<As> impl. Extra types from the declaration probe.
template <template <class> class Trait, typename As, std::meta::info Method>
consteval std::meta::info trait_impl_of() {
  if constexpr (!std::meta::is_complete_type(^^Trait<As>)) {
    // Const observation (c_dyn / const iface): Trait<const X> is rarely
    // specialized; reuse the unqualified Trait<X> impl. A complete
    // Trait<const X> specialization always wins (checked above).
    if constexpr (std::is_const_v<std::remove_reference_t<As>>)
      return trait_impl_of<Trait, std::remove_cv_t<As>, Method>();
    else
      return std::meta::info{};
  } else {
    constexpr auto n = trait_methods(^^Trait<As>).size();
    if constexpr (n == 0)
      return std::meta::info{};
    else
      return trait_impl_unrolled<Trait, As, Method>(
          std::make_index_sequence<n>{});
  }
}

#if 0
template <template <class> class Trait, typename As>
consteval std::meta::info trait_impl_of_unused(std::meta::info method) {
  if (!std::meta::is_complete_type(^^Trait<As>))
    return std::meta::info{};
  auto named =
      find_methods_named(^^Trait<As>, std::meta::identifier_of(method));
  if (named.empty())
    return std::meta::info{};

  auto want_n = method_nonself_arity(method);
  std::size_t want_self = self_param_index(method);
  std::vector<std::meta::info> by_arity;
  for (auto m : named) {
    if (self_param_index(m) != want_self)
      continue;
    if (method_nonself_arity(m) == want_n)
      by_arity.push_back(m);
  }
  if (by_arity.empty())
    return std::meta::info{};
  if (by_arity.size() == 1) {
    // Still require non-self param types to match (reverse vs forward).
    auto want_inst = instantiate(method, ^^signature_self);
    auto got_inst = instantiate(by_arity[0], ^^signature_self);
    if (want_inst == std::meta::info{} || got_inst == std::meta::info{})
      return by_arity[0];
    auto want_params = std::meta::parameters_of(want_inst);
    auto got_params = std::meta::parameters_of(got_inst);
    std::size_t got_self = self_param_index(by_arity[0]);
    std::size_t wi = 0, gi = 0;
    while (wi < want_params.size() && gi < got_params.size()) {
      if (wi == want_self) {
        ++wi;
        continue;
      }
      if (gi == got_self) {
        ++gi;
        continue;
      }
      auto wt = std::meta::remove_cvref(std::meta::type_of(want_params[wi]));
      auto gt = std::meta::remove_cvref(std::meta::type_of(got_params[gi]));
      if (wt != gt)
        return std::meta::info{};
      ++wi;
      ++gi;
    }
    return by_arity[0];
  }

  auto want_inst = instantiate(method, ^^signature_self);
  if (want_inst == std::meta::info{})
    return std::meta::info{};
  auto want_params = std::meta::parameters_of(want_inst);

  for (auto m : by_arity) {
    auto got_inst = instantiate(m, ^^signature_self);
    if (got_inst == std::meta::info{})
      continue;
    auto got_params = std::meta::parameters_of(got_inst);
    std::size_t got_self = self_param_index(m);
    bool ok = true;
    std::size_t wi = 0, gi = 0;
    while (wi < want_params.size() && gi < got_params.size()) {
      if (wi == want_self) {
        ++wi;
        continue;
      }
      if (gi == got_self) {
        ++gi;
        continue;
      }
      auto wt = std::meta::remove_cvref(std::meta::type_of(want_params[wi]));
      auto gt = std::meta::remove_cvref(std::meta::type_of(got_params[gi]));
      if (wt != gt) {
        ok = false;
        break;
      }
      ++wi;
      ++gi;
    }
    for (; wi < want_params.size(); ++wi) {
      if (wi != want_self) {
        ok = false;
        break;
      }
    }
    for (; gi < got_params.size(); ++gi) {
      if (gi != got_self) {
        ok = false;
        break;
      }
    }
    if (ok)
      return m;
  }
  return std::meta::info{};
}
#endif

// Trait<void> [[=asf_ann::imple]] non-op -> same-named Src member.
// op_* is empty here; invoke_imple_op does that lookup.
template <template <class> class Trait, typename As, typename Src>
consteval std::meta::info src_impl_of(std::meta::info method) {
  if constexpr (std::is_void_v<Src>) {
    return std::meta::info{};
  } else {
    if (!method_prefers_imple_as<Trait, As>(method) ||
        is_imple_op_name(std::meta::identifier_of(method)))
      return std::meta::info{};
    return find_src_method<Src>(std::meta::identifier_of(method),
                                trait_method_arg_arity(method));
  }
}

template <typename Src, std::meta::info Method>
decltype(auto) call_impl_method(Src&& src, auto&&... args) {
  if constexpr (std::meta::is_static_member(Method)) {
    return [:Method:](std::forward<decltype(args)>(args)...);
  } else {
    return std::forward<Src>(src).[:Method:](
        std::forward<decltype(args)>(args)...);
  }
}

template <typename Src>
decltype(auto) assign_src(Src&& dst, auto&& rhs) {
  return (std::forward<Src>(dst) = std::forward<decltype(rhs)>(rhs));
}

// Constructor with the Trait method's parameter list. Self is passed as the
// reference the declaration names (auto&& is an rvalue). Other arguments
// stay in order.
template <std::size_t SelfIdx, self_ref_kind Kind, class Src, class Self,
          class... Args>
Src construct_like(Self&& self, Args&&... args) {
  constexpr std::size_t n = sizeof...(Args) + 1;
  static_assert(SelfIdx < n, "self parameter index out of range");
  auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
  return [&]<std::size_t... I>(std::index_sequence<I...>) -> Src {
    auto&& pick = [&]<std::size_t J>() -> decltype(auto) {
      if constexpr (J == SelfIdx) {
        if constexpr (Kind == self_ref_kind::rvalue)
          return static_cast<Src&&>(self);
        else if constexpr (Kind == self_ref_kind::const_rvalue)
          return static_cast<const Src&&>(self);
        else if constexpr (Kind == self_ref_kind::lvalue)
          return static_cast<Src&>(self);
        else
          return static_cast<const Src&>(self);
      } else {
        return std::get<(J < SelfIdx ? J : J - 1)>(std::move(tup));
      }
    };
    return Src(pick.template operator()<I>()...);
  }(std::make_index_sequence<n>{});
}

// Call with `self` inserted at SelfIdx; `args` are the non-self parameters.
template <std::meta::info Inst, std::size_t SelfIdx, bool AsMember, class Obj,
          class Self, class Tuple, std::size_t... I>
decltype(auto) apply_self_at(Obj&& obj, Self&& self, Tuple&& tup,
                             std::index_sequence<I...>) {
  auto&& pick = [&]<std::size_t J>() -> decltype(auto) {
    if constexpr (J == SelfIdx)
      return std::forward<Self>(self);
    else
      return std::get<(J < SelfIdx ? J : J - 1)>(std::forward<Tuple>(tup));
  };
  if constexpr (AsMember)
    return std::forward<Obj>(obj).[:Inst:](pick.template operator()<I>()...);
  else
    return [:Inst:](pick.template operator()<I>()...);
}

template <std::meta::info Inst, std::size_t SelfIdx, bool AsMember = false,
          class Obj = std::nullptr_t, class Self, class... Args>
decltype(auto) call_inst_with_self(Obj&& obj, Self&& self, Args&&... args) {
  constexpr std::size_t n = sizeof...(Args) + 1;
  static_assert(SelfIdx < n, "self parameter index out of range");
  auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
  // Move the forward_as_tuple so get<> yields rvalue refs (&& params stay &&).
  return apply_self_at<Inst, SelfIdx, AsMember>(
      std::forward<Obj>(obj), std::forward<Self>(self), std::move(tup),
      std::make_index_sequence<n>{});
}

template <std::size_t SelfIdx, class Bin, class Self, class... Args>
decltype(auto) imple_binary(Bin&& bin, Self&& self, Args&&... args) {
  static_assert(sizeof...(Args) == 1, "binary imple op needs one other operand");
  static_assert(SelfIdx <= 1, "binary imple op self must be parameter 0 or 1");
  if constexpr (SelfIdx == 0)
    return std::forward<Bin>(bin)(std::forward<Self>(self),
                                  std::forward<Args>(args)...);
  else
    return std::forward<Bin>(bin)(std::forward<Args>(args)...,
                                  std::forward<Self>(self));
}

template <class Un, class Self, class... Args>
decltype(auto) imple_unary(Un&& un, Self&& self, Args&&...) {
  static_assert(sizeof...(Args) == 0, "unary imple op takes only self");
  return std::forward<Un>(un)(std::forward<Self>(self));
}

// op_* imple. Not a name lookup: construct / assign / convert / catalog
// operator. Self stays at its declared index. auto&& is an rvalue (construct).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
// Kind is also an NTTP so GCC's reflection-info encoding (which collapses
// same-named function templates) still produces distinct assembler symbols
// for copy vs move op_construct.
template <std::meta::info Method, std::size_t SelfIdx, self_ref_kind Kind,
          class Obj, class... Args>
[[gnu::weak]] inline decltype(auto)
invoke_imple_op(Obj&& obj, Args&&... args) {
  constexpr auto name = std::meta::identifier_of(Method);
  if constexpr (is_special_op_name(name, special_op::construct)) {
    using Impl = std::remove_cvref_t<Obj>;
    return construct_like<SelfIdx, Kind, Impl>(
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (is_special_op_name(name, special_op::assign)) {
    return assign_src(std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (is_special_op_name(name, special_op::convert)) {
    static_assert(sizeof...(Args) == 0, "op_convert imple takes only self");
    constexpr auto inst = signature_decl_of<Method>();
    static_assert(inst != std::meta::info{},
                  "op_convert imple needs a concrete return type");
    using To = [:std::meta::return_type_of(inst):];
    return static_cast<To>(std::forward<Obj>(obj));
  } else if constexpr (name == "op_add") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) + std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_sub") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) - std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_neg") {
    return imple_unary(
        [](auto&& a) -> decltype(auto) { return -std::forward<decltype(a)>(a); },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_mul") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) * std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_div") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) / std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_mod") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) % std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_xor") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) ^ std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_bitand") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) & std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_bitor") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) | std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_compl") {
    return imple_unary(
        [](auto&& a) -> decltype(auto) { return ~std::forward<decltype(a)>(a); },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_not") {
    return imple_unary(
        [](auto&& a) -> decltype(auto) { return !std::forward<decltype(a)>(a); },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_lt") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) < std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_gt") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) > std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_iadd") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) += std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_isub") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) -= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_imul") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) *= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_idiv") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) /= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_imod") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) %= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_ixor") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) ^= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_ibitand") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) &= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_ibitor") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) |= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_lshift") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) << std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_rshift") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) >> std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_ilshift") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) <<= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_irshift") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) >>= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_eq") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) == std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_ne") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) != std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_le") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) <= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_ge") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) >= std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_cmp") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) <=> std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_and") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) && std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_or") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a) || std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_inc") {
    if constexpr (sizeof...(Args) == 0)
      return ++std::forward<Obj>(obj);
    else {
      static_assert(sizeof...(Args) == 1, "op_inc is prefix or postfix");
      return std::forward<Obj>(obj)++;
    }
  } else if constexpr (name == "op_dec") {
    if constexpr (sizeof...(Args) == 0)
      return --std::forward<Obj>(obj);
    else {
      static_assert(sizeof...(Args) == 1, "op_dec is prefix or postfix");
      return std::forward<Obj>(obj)--;
    }
  } else if constexpr (name == "op_index") {
    static_assert(SelfIdx == 0, "op_index imple self is the object");
    return std::forward<Obj>(obj)[std::forward<Args>(args)...];
  } else if constexpr (name == "op_call") {
    static_assert(SelfIdx == 0, "op_call imple self is the object");
    return std::forward<Obj>(obj)(std::forward<Args>(args)...);
  } else if constexpr (name == "op_comma") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return (std::forward<decltype(a)>(a), std::forward<decltype(b)>(b));
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else if constexpr (name == "op_ptr_to_mem") {
    return imple_binary<SelfIdx>(
        [](auto&& a, auto&& b) -> decltype(auto) {
          return std::forward<decltype(a)>(a)->*std::forward<decltype(b)>(b);
        },
        std::forward<Obj>(obj), std::forward<Args>(args)...);
  } else {
    static_assert(sizeof(Obj) == 0, "imple op has no special lookup");
  }
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Declaration cvref: auto& / auto const& / auto&& on the object at ptr.
// A stored mirror applies that cvref to T (c_mirror / rmirror), not to the
// handle. The impl either accepts the presented value or it does not.
template <self_ref_kind K>
constexpr decltype(auto) as_decl_self(auto&& x) {
  using Raw = std::remove_cvref_t<decltype(x)>;
  if constexpr (is_mirror_v<Raw>) {
    using View = mirror_target_t<Raw>;
    using Obj = std::remove_reference_t<View>;
    using To = std::conditional_t<
        K == self_ref_kind::const_rvalue, std::add_const_t<Obj>&&,
        std::conditional_t<
            K == self_ref_kind::rvalue, Obj&&,
            std::conditional_t<K == self_ref_kind::const_lvalue,
                               std::add_const_t<Obj>, Obj>>>;
    constexpr bool already =
        std::same_as<std::remove_reference_t<View>,
                     std::remove_reference_t<To>> &&
        std::is_rvalue_reference_v<View> == std::is_rvalue_reference_v<To>;
    if constexpr (already) {
      // Keep handle cvref. Raw is remove_cvref; static_cast<Raw&> would
      // drop const on c_mirror / const mirror<const T>.
      return std::forward<decltype(x)>(x);
    } else if constexpr (is_sourced_mirror_v<Raw>)
      return mirror<To, mirror_src_t<Raw>>(x);
    else
      return mirror<To>(x);
  } else if constexpr (K == self_ref_kind::const_lvalue)
    return static_cast<const Raw&>(x);
  else if constexpr (K == self_ref_kind::lvalue)
    return static_cast<Raw&>(x);
  else if constexpr (K == self_ref_kind::const_rvalue)
    return static_cast<const Raw&&>(x);
  else
    return static_cast<Raw&&>(x);
}

// Construct Self is the object (Src). Already-built dyn uses the proxy /
// mirror. A sourced mirror unwraps; cvref follows the declaration
// (rmirror -> &&, mirror / const T& -> const&).
template <self_ref_kind K>
constexpr decltype(auto) as_construct_self(auto&& x) {
  using Raw = std::remove_cvref_t<decltype(x)>;
  if constexpr (is_sourced_mirror_v<Raw>)
    return as_decl_self<K>(src_of(x));
  else
    return as_decl_self<K>(std::forward<decltype(x)>(x));
}

// GCC mangles std::meta::info of same-named overloads as one NTTP. An
// integer index among the declaring Trait's methods splits assembler symbols
// (same pattern as Kind on invoke_imple_op).
consteval std::size_t method_ord(std::meta::info method) {
  if (method == std::meta::info{})
    return static_cast<std::size_t>(-1);
  auto parent = std::meta::parent_of(method);
  if (parent == std::meta::info{})
    return static_cast<std::size_t>(-1);
  auto ms = trait_methods(parent);
  for (std::size_t i = 0; i < ms.size(); ++i) {
    if (ms[i] == method)
      return i;
  }
  return static_cast<std::size_t>(-1);
}

// Impl / SrcImpl are wired at proxy/vtable build (trait_impl_of / src_impl_of).
// Non-op imple uses that member. op_* goes through invoke_imple_op.
template <template <class> class Trait, typename As, std::meta::info Method,
          std::meta::info Impl, std::meta::info SrcImpl = std::meta::info{},
          bool AsMove = false, std::size_t Overload = method_ord(Method)>
decltype(auto) invoke_resolved(auto&& self, auto&&... args) {
  (void)Overload;
  using Self = std::remove_cvref_t<decltype(self)>;
  constexpr auto name = std::meta::identifier_of(Method);
  constexpr std::size_t self_i = self_param_index(Method);
  if constexpr (is_special_op_name(name, special_op::construct))
    check_construct_return_decl<Method>();

  // Trait<As> impl -> imple (op special, else same name) -> Trait<void> default.
  if constexpr (Impl != std::meta::info{}) {
    return call_impl_member<Trait<As>, Impl>(
        self, std::forward<decltype(args)>(args)...);
  } else if constexpr (method_prefers_imple_as<Trait, As>(Method)) {
    if constexpr (is_sourced_mirror_v<Self>) {
      if constexpr (is_imple_op_name(name)) {
        if constexpr (is_construct_or_assign_special(Method) &&
                      (!is_special_op_name(name, special_op::construct) ||
                       sizeof...(args) == 0)) {
          static_assert(
              src_has_special_member_impl<Self, Method, AsMove>(),
              "[[=asf_ann::imple]] special op needs matching Src ctor/assign");
        }
        return invoke_imple_op<Method, self_i, self_param_ref_kind(Method)>(
            mirror_src_ref(self), std::forward<decltype(args)>(args)...);
      } else {
        static_assert(SrcImpl != std::meta::info{},
                      "[[=asf_ann::imple]] needs a same-named method on Src");
        return call_impl_method<decltype(mirror_src_ref(self)), SrcImpl>(
            mirror_src_ref(self), std::forward<decltype(args)>(args)...);
      }
    } else if constexpr (!is_mirror_v<Self>) {
      // No ann::mirror: self is the object. Do not build a mirror.
      if constexpr (is_imple_op_name(name)) {
        return invoke_imple_op<Method, self_i, self_param_ref_kind(Method)>(
            self, std::forward<decltype(args)>(args)...);
      } else {
        static_assert(SrcImpl != std::meta::info{},
                      "[[=asf_ann::imple]] needs a same-named method on the "
                      "object");
        return call_impl_method<decltype(self), SrcImpl>(
            self, std::forward<decltype(args)>(args)...);
      }
    } else {
      static_assert(
          false,
          "[[=asf_ann::imple]] needs a sourced mirror (dyn, or iface with Src)");
    }
  } else {
    constexpr auto inst = instantiate(Method, ^^Self);
    static_assert(inst != std::meta::info{},
                  "this Interface method has no default; specialize "
                  "Interface<T>, mark [[=asf_ann::imple]], or implement on Src");
    using Defining = [:std::meta::parent_of(Method):];
    if constexpr (std::meta::is_static_member(Method) ||
                  std::meta::is_static_member(inst)) {
      return call_inst_with_self<inst, self_i, false>(
          nullptr, std::forward<decltype(self)>(self),
          std::forward<decltype(args)>(args)...);
    } else {
      Defining base{};
      return call_inst_with_self<inst, self_i, true>(
          base, std::forward<decltype(self)>(self),
          std::forward<decltype(args)>(args)...);
    }
  }
}

template <template <class> class Trait, typename As, std::meta::info Method,
          std::meta::info Impl, std::meta::info SrcImpl = std::meta::info{},
          bool AsMove = false, std::size_t Overload = method_ord(Method)>
decltype(auto) invoke_method(auto&& self, auto&&... args) {
  // Const was erased on the reused impl/ptr. The const surface (c_iface /
  // As = const T) must reject non-const methods here, before the thunk.
  if constexpr (std::is_const_v<std::remove_reference_t<As>>)
    static_assert(is_const_self_method(Method),
                  "const interface cannot call a non-const Interface method");
  if constexpr (std::meta::has_identifier(Method) &&
                is_special_op_name(std::meta::identifier_of(Method),
                                   special_op::construct)) {
    return invoke_resolved<Trait, As, Method, Impl, SrcImpl, AsMove, Overload>(
        as_construct_self<self_param_ref_kind_of<Method>()>(
            std::forward<decltype(self)>(self)),
        std::forward<decltype(args)>(args)...);
  } else {
    return invoke_resolved<Trait, As, Method, Impl, SrcImpl, AsMove, Overload>(
        as_decl_self<self_param_ref_kind_of<Method>()>(
            std::forward<decltype(self)>(self)),
        std::forward<decltype(args)>(args)...);
  }
}

template <template <class> class Trait, typename As, typename Src,
          std::meta::info Method, bool AsMove = false,
          std::size_t Overload = method_ord(Method)>
decltype(auto) invoke_wired(auto&& self, auto&&... args) {
  constexpr auto impl = trait_impl_of<Trait, As, Method>();
  // imple members are looked up on the object: Src for sourced binds, else As.
  using SrcObj = std::conditional_t<std::is_void_v<Src>,
                                    std::remove_cvref_t<As>, Src>;
  constexpr auto srcimpl = src_impl_of<Trait, As, SrcObj>(Method);
  return invoke_method<Trait, As, Method, impl, srcimpl, AsMove, Overload>(
      std::forward<decltype(self)>(self),
      std::forward<decltype(args)>(args)...);
}

template <typename R, typename... Args>
template <template <class> class Trait, typename Storage, typename As,
          std::size_t I>
R erased_fn<R, Args...>::call(void* ptr, vt_ptr vt, Args... args) {
  (void)vt;
  using Self = [:impl_self_info<Trait, As>():];
  using IdentityView = identity_mirror<As>;

  constexpr auto method = dyn_methods(^^Trait<void>)[I];
  // op_construct does not follow Trait [[=mirror]]: self is the object
  // (or Src of a sourced mirror), never a mirror.
  constexpr bool lifetime_op =
      std::meta::has_identifier(method) &&
      is_lifetime_dyn_op_name(std::meta::identifier_of(method));
  // Concrete Storage is Src; sourced mirror unwraps; unsourced mirror -> no Src.
  using SrcKey = std::conditional_t<
      is_sourced_mirror_v<Storage>, mirror_src_t<Storage>,
      std::conditional_t<is_mirror_v<Storage>, void, Storage>>;
  if constexpr (is_sourced_mirror_v<Storage> &&
                (lifetime_op ||
                 !section_allows_mirror_source_as<Trait, As>())) {
    // Non-mirror section, or lifetime op: self is the object (Src).
    using Src = mirror_src_t<Storage>;
    auto&& src = src_of(*static_cast<Storage*>(ptr));
    if constexpr (std::is_void_v<R>) {
      invoke_wired<Trait, As, Src, method>(
          std::forward<decltype(src)>(src), std::forward<Args>(args)...);
    } else {
      R result = invoke_wired<Trait, As, Src, method>(
          std::forward<decltype(src)>(src), std::forward<Args>(args)...);
      return result;
    }
  } else {
    // ptr -> declaration cvref (in invoke_method). Mirror impls construct
    // from that cvref or fail.
    if constexpr (!lifetime_op && std::is_same_v<Self, IdentityView>)
      static_assert(std::is_same_v<Storage, IdentityView>,
                    "Interface methods that take identity_mirror<T> "
                    "(mirror<T, T>) need Storage = identity_mirror<T>");
    if constexpr (std::is_void_v<R>) {
      invoke_wired<Trait, As, SrcKey, method>(
          *static_cast<Storage*>(ptr), std::forward<Args>(args)...);
    } else {
      R result = invoke_wired<Trait, As, SrcKey, method>(
          *static_cast<Storage*>(ptr), std::forward<Args>(args)...);
      return result;
    }
  }
}

template <template <class> class Trait>
consteval std::size_t dyn_count() {
  return dyn_methods(^^Trait<void>).size();
}

// First flat index of Trait's own method section inside Final's layout
// (for sub_dyn isolated slice). npos if Trait has no method slots there.
template <template <class> class Final, template <class> class Trait>
consteval std::size_t section_slice_offset() {
  auto flat = flatten_layout<Final>();
  constexpr auto target = ^^Trait<void>;
  for (std::size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].kind == slot_kind::method && flat[i].trait_void == target)
      return i;
  }
  return static_cast<std::size_t>(-1);
}

// Offset used by sub_dyn: section start, or 0 when Trait has no dyn methods.
template <template <class> class Final, template <class> class Trait>
consteval std::size_t sub_slice_offset() {
  auto o = section_slice_offset<Final, Trait>();
  if (o != static_cast<std::size_t>(-1))
    return o;
  return 0;
}

template <template <class> class Final, template <class> class Trait>
consteval bool can_sub_slice() {
  return is_trait_base_of<Trait, Final>() &&
         (section_slice_offset<Final, Trait>() != static_cast<std::size_t>(-1) ||
          dyn_count<Trait>() == 0);
}

// Index of Method within Trait's own dyn_methods (section-local, not full flat).
template <template <class> class Trait>
consteval std::size_t own_dyn_method_index(std::meta::info method) {
  auto methods = dyn_methods(^^Trait<void>);
  for (std::size_t i = 0; i < methods.size(); ++i) {
    if (methods[i] == method)
      return i;
  }
  return static_cast<std::size_t>(-1);
}

template <template <class> class Trait>
consteval std::size_t flat_slot_count() {
  return flatten_layout<Trait>().size();
}

template <template <class> class Trait>
consteval std::size_t dyn_index(std::string_view name) {
  // Own section only -- same name on a base must not steal the slot.
  auto flat = flatten_layout<Trait>();
  constexpr auto self = ^^Trait<void>;
  for (std::size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].kind != slot_kind::method || flat[i].trait_void != self)
      continue;
    auto methods = dyn_methods(flat[i].trait_void);
    if (std::meta::identifier_of(methods[flat[i].method_index]) == name)
      return i;
  }
  return static_cast<std::size_t>(-1);
}

// Slot of View's own method `name` inside Layout's flat table (no vt switch).
template <template <class> class Layout, template <class> class View>
consteval std::size_t dyn_index_in_layout(std::string_view name) {
  auto flat = flatten_layout<Layout>();
  constexpr auto view = ^^View<void>;
  for (std::size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].kind != slot_kind::method || flat[i].trait_void != view)
      continue;
    auto methods = dyn_methods(flat[i].trait_void);
    if (std::meta::identifier_of(methods[flat[i].method_index]) == name)
      return i;
  }
  return static_cast<std::size_t>(-1);
}

// Slot of a specific View method reflection inside Layout's flat table.
template <template <class> class Layout, template <class> class View>
consteval std::size_t dyn_index_of_method_in_layout(std::meta::info method) {
  auto flat = flatten_layout<Layout>();
  constexpr auto view = ^^View<void>;
  for (std::size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].kind != slot_kind::method || flat[i].trait_void != view)
      continue;
    auto methods = dyn_methods(flat[i].trait_void);
    if (methods[flat[i].method_index] == method)
      return i;
  }
  return static_cast<std::size_t>(-1);
}

// Flat index of a specific method reflection (same name overloads need this).
template <template <class> class Trait>
consteval std::size_t dyn_index_of_method(std::meta::info method) {
  auto flat = flatten_layout<Trait>();
  constexpr auto self = ^^Trait<void>;
  for (std::size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].kind != slot_kind::method || flat[i].trait_void != self)
      continue;
    auto methods = dyn_methods(flat[i].trait_void);
    if (methods[flat[i].method_index] == method)
      return i;
  }
  return static_cast<std::size_t>(-1);
}

template <template <class> class Trait, typename To, special_op Op>
consteval std::size_t dyn_special_op_index() {
  auto method = find_special_op_method_on<^^Trait<void>, To, Op>();
  if (method == std::meta::info{})
    return static_cast<std::size_t>(-1);
  return dyn_index_of_method<Trait>(method);
}

template <template <class> class Trait, typename To>
consteval std::size_t dyn_convert_index() {
  return dyn_special_op_index<Trait, To, special_op::convert>();
}

template <template <class> class Trait>
consteval bool trait_has_dyn_construct() {
  for (auto m : dyn_methods(^^Trait<void>)) {
    if (std::meta::has_identifier(m) &&
        is_special_op_name(std::meta::identifier_of(m), special_op::construct))
      return true;
  }
  return false;
}

// Exact self category for cache_ops matching Trait; no fallback to the other.
template <template <class> class Trait, bool Rvalue>
consteval bool trait_has_dyn_construct_as() {
  for (auto m : dyn_methods(^^Trait<void>)) {
    if (!std::meta::has_identifier(m) ||
        !is_special_op_name(std::meta::identifier_of(m), special_op::construct))
      continue;
    if (is_rvalue_self_method(m) == Rvalue)
      return true;
  }
  return false;
}
template <template <class> class Trait>
consteval bool trait_has_dyn_copy_construct() {
  return trait_has_dyn_construct_as<Trait, false>();
}
template <template <class> class Trait>
consteval bool trait_has_dyn_move_construct() {
  return trait_has_dyn_construct_as<Trait, true>();
}

template <std::meta::info Method, class... Extra>
consteval bool construct_decl_extras_match() {
  if constexpr (!std::meta::is_function_template(Method)) {
    return method_args_match_types<Extra...>(Method);
  } else if constexpr (has_placeholder_return<Method>()) {
    return sizeof...(Extra) == 0;
  } else {
    constexpr int n = signature_arity<Method>::value;
    if constexpr (n <= 0)
      return false;
    else {
      using MP = decltype(signature_mp<Method, signature_self>(
          std::make_index_sequence<static_cast<std::size_t>(n)>{}));
      return mp_extras_of<MP>::template match<Extra...>();
    }
  }
}

// Matching op_construct method. Extra types come from the declaration
// (`-> std::remove_cvref_t<decltype(self)>` / named T). Dummy Self is
// substituted only into the signature. PreferRvalue: rvalue-self (rproxy).
template <template <class> class Trait, bool PreferRvalue, std::size_t I,
          class... Extra>
consteval std::meta::info dyn_construct_scan(std::meta::info fallback = {}) {
  constexpr auto n = dyn_count<Trait>();
  if constexpr (I >= n)
    return fallback;
  else {
    constexpr auto m = dyn_methods(^^Trait<void>)[I];
    if constexpr (std::meta::has_identifier(m) &&
                  is_special_op_name(std::meta::identifier_of(m),
                                     special_op::construct) &&
                  construct_decl_extras_match<m, Extra...>()) {
      if constexpr (is_rvalue_self_method_of<m>() == PreferRvalue)
        return m;
      return dyn_construct_scan<Trait, PreferRvalue, I + 1, Extra...>(
          fallback == std::meta::info{} ? m : fallback);
    } else {
      return dyn_construct_scan<Trait, PreferRvalue, I + 1, Extra...>(
          fallback);
    }
  }
}

template <template <class> class Trait, bool PreferRvalue, class... Extra>
consteval std::meta::info dyn_construct_method() {
  return dyn_construct_scan<Trait, PreferRvalue, 0, Extra...>();
}

template <template <class> class Trait, bool PreferRvalue, class... Extra>
consteval std::size_t dyn_construct_slot_index() {
  auto m = dyn_construct_method<Trait, PreferRvalue, Extra...>();
  if (m == std::meta::info{})
    return static_cast<std::size_t>(-1);
  return dyn_index_of_method<Trait>(m);
}

// Const=true: c_dyn (const-self methods only). Const=false: dyn.
template <template <class> class Trait, typename Key, special_op Op,
          bool Const>
consteval bool dyn_like_has_special_op() {
  auto method = find_special_op_method_on<^^Trait<void>, Key, Op>();
  if (method == std::meta::info{})
    return false;
  // Dyn * assign: concrete RHS only (no Src/auto wildcard fallback).
  if (Op == special_op::assign && is_impl_keyed_assign(method))
    return false;
  if (Const && !is_const_self_method(method))
    return false;
  return dyn_index_of_method<Trait>(method) != static_cast<std::size_t>(-1);
}

template <template <class> class Trait, typename To>
consteval bool dyn_has_convert() {
  return dyn_like_has_special_op<Trait, To, special_op::convert, false>();
}

template <template <class> class Trait, typename To>
consteval bool dyn_has_construct() {
  return dyn_like_has_special_op<Trait, To, special_op::construct, false>();
}

template <template <class> class Trait, typename From>
consteval bool dyn_has_assign() {
  return dyn_like_has_special_op<Trait, From, special_op::assign, false>();
}

template <template <class> class Trait, typename To>
consteval bool c_dyn_has_convert() {
  return dyn_like_has_special_op<Trait, To, special_op::convert, true>();
}

template <template <class> class Trait, typename To>
consteval bool c_dyn_has_construct() {
  return dyn_like_has_special_op<Trait, To, special_op::construct, true>();
}

template <template <class> class Trait, std::size_t I>
consteval slot_kind flat_kind_at() {
  return flatten_layout<Trait>()[I].kind;
}

template <template <class> class Trait, std::size_t I>
consteval std::meta::info flat_trait_at() {
  return flatten_layout<Trait>()[I].trait_void;
}

template <template <class> class Trait, std::size_t I>
consteval std::size_t flat_method_index_at() {
  return flatten_layout<Trait>()[I].method_index;
}

template <template <class> class Trait, std::size_t I>
consteval std::meta::info flat_method_at() {
  auto flat = flatten_layout<Trait>();
  return dyn_methods(flat[I].trait_void)[flat[I].method_index];
}

template <template <class> class Trait, std::size_t Index>
consteval std::meta::info dyn_method_at() {
  constexpr auto flat_n = flat_slot_count<Trait>();
  static_assert(Index < flat_n, "dyn method index out of range");
  if constexpr (Index < flat_n)
    return flat_method_at<Trait, Index>();
  else
    return std::meta::info{};
}

// Dyn construct: void(dst, extras...) -> placement-new Storage from invoke
// result. Self is Src, never a mirror (only an already-built dyn uses the
// proxy). Instantiating construct uses that Src so [[=imple]] is visible.
// imple -> Src constructor (const& copy, && / auto&& move). Else Trait body.
template <template <class> class Trait, typename Storage, typename As,
          std::size_t I, bool AsMove, class... Extra>
void erased_construct_into(void* ptr, vt_ptr vt, void* dst, Extra... extra) {
  (void)vt;
  constexpr auto method = dyn_methods(^^Trait<void>)[I];
  using Obj = std::remove_reference_t<As>;
  using Src = std::conditional_t<is_sourced_mirror_v<Storage>,
                                 mirror_src_t<Storage>, Storage>;
  auto* store = static_cast<Storage*>(ptr);
  if constexpr (is_sourced_mirror_v<Storage>) {
    if constexpr (AsMove && !std::is_const_v<Obj>) {
      new (dst) Storage(invoke_wired<Trait, Src, Src, method, true>(
          std::move(src_of(*store)), std::forward<Extra>(extra)...));
    } else {
      new (dst) Storage(invoke_wired<Trait, Src, Src, method, false>(
          src_of(*store), std::forward<Extra>(extra)...));
    }
  } else if constexpr (AsMove && !std::is_const_v<Obj>) {
    new (dst) Storage(invoke_wired<Trait, Src, Src, method, true>(
        std::move(*store), std::forward<Extra>(extra)...));
  } else {
    new (dst) Storage(invoke_wired<Trait, Src, Src, method, false>(
        *store, std::forward<Extra>(extra)...));
  }
}

// Thunk type for one op_construct overload: dst plus the declaration's
// extra parameters (self is already erased). Empty Extra is copy/move.
// Lvalue/rvalue is a template on call, not a function argument.
template <class... Extra>
struct construct_erased_fn {
  template <template <class> class Trait, typename Storage, typename As,
            std::size_t I, bool AsMove>
  static void call(void* ptr, vt_ptr vt, void* dst, Extra... extra) {
    erased_construct_into<Trait, Storage, As, I, AsMove>(
        ptr, vt, dst, std::forward<Extra>(extra)...);
  }

  static void stub(void*, vt_ptr, void*, Extra...) { std::unreachable(); }

  static void dispatch(vt_fn slot, void* ptr, vt_ptr vt, void* dst,
                       std::type_identity_t<Extra>... extra) {
    from_vt_fn<void(void*, vt_ptr, void*, Extra...)>(slot)(
        ptr, vt, dst, std::forward<Extra>(extra)...);
  }
};

// construct_erased_fn<declared extra types...> from an instantiated method.
consteval std::meta::info construct_erased_sig(std::meta::info inst) {
  std::vector<std::meta::info> args;
  auto params = std::meta::parameters_of(inst);
  std::size_t self_i = 0;
  bool found = false;
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (std::meta::remove_cvref(std::meta::type_of(params[i])) ==
        ^^signature_self) {
      self_i = i;
      found = true;
      break;
    }
  }
  if (!found)
    self_i = 0;
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i == self_i)
      continue;
    args.push_back(std::meta::type_of(params[i]));
  }
  if (args.empty())
    return ^^construct_erased_fn<>;
  return std::meta::substitute(^^construct_erased_fn, args);
}

// Written return is checked by check_construct_return_decl. Dyn forbids
// a placeholder return, so Src is never substituted into the body.
template <typename Src>
consteval void check_construct_return_src(std::meta::info method) {
  (void)^^Src;
  (void)method;
}

// Declaration must return remove_cvref(self). Write
// `-> std::remove_cvref_t<decltype(self)>` (or `T` when self is `T`).
// Dummy Self is only substituted into that signature. Extra-arg construct
// cannot use a placeholder `auto` return (that would instantiate the body).
template <std::meta::info Method>
consteval void check_construct_return_decl() {
  if constexpr (has_placeholder_return<Method>()) {
    if (method_nonself_arity(Method) != 0)
      throw std::meta::exception(
          "op_construct extras require a trailing return "
          "std::remove_cvref_t<decltype(self)>",
          Method, std::source_location::current());
    return;
  }
  constexpr auto decl = signature_decl_of<Method>();
  if (decl == std::meta::info{})
    throw std::meta::exception(
        "op_construct extras require a trailing return "
        "std::remove_cvref_t<decltype(self)>",
        Method, std::source_location::current());
  std::meta::info raw{};
  try {
    raw = std::meta::return_type_of(decl);
  } catch (std::meta::exception const&) {
    throw std::meta::exception(
        "op_construct must return remove_cvref of the first parameter",
        Method, std::source_location::current());
  }
  if (std::meta::is_reference_type(raw) ||
      std::meta::dealias(std::meta::remove_cvref(raw)) != ^^signature_self)
    throw std::meta::exception(
        "op_construct must return remove_cvref of the first parameter; "
        "write -> std::remove_cvref_t<decltype(self)>",
        Method, std::source_location::current());
}

template <std::meta::info Method>
consteval std::meta::info construct_slot_sig() {
  check_construct_return_decl<Method>();
  constexpr auto decl = signature_decl_of<Method>();
  if (decl != std::meta::info{})
    return construct_erased_sig(decl);
  return ^^construct_erased_fn<>;
}

// Method slot count (flat). When Trait has op_construct,
// [n]=sizeof/alignof Storage (allocation). Teardown is vt[-1] (~Storage).
template <template <class> class Trait>
consteval std::size_t dyn_method_slot_count() {
  return flat_slot_count<Trait>();
}

template <template <class> class Trait>
inline std::size_t dyn_storage_size(vt_ptr vt)
  requires(trait_has_dyn_construct<Trait>()) {
  return storage_size_from_slot(vt[dyn_method_slot_count<Trait>()]);
}

template <template <class> class Trait>
inline std::size_t dyn_storage_align(vt_ptr vt)
  requires(trait_has_dyn_construct<Trait>()) {
  return storage_align_from_slot(vt[dyn_method_slot_count<Trait>()]);
}

template <template <class> class Trait>
inline std::size_t dyn_storage_align_log(vt_ptr vt)
  requires(trait_has_dyn_construct<Trait>()) {
  return storage_align_log_from_slot(vt[dyn_method_slot_count<Trait>()]);
}

// Erased construct thunk from the matching 0-extra op_construct method slot
// (copy/move). AsRvalue selects the rvalue-self overload (rproxy); the
// function pointer is only (ptr, vt, dst).
template <template <class> class Trait, bool AsRvalue>
inline vt_construct_fn dyn_construct_of(vt_ptr vt)
  requires(trait_has_dyn_construct<Trait>()) {
  constexpr auto idx = dyn_construct_slot_index<Trait, AsRvalue>();
  return from_vt_fn<void(void*, vt_ptr, void*)>(vt[idx]);
}

// Deep construct into a sourced-mirror Storage is only defined when the
// method yields a mirror (it rebinds the original source). An object result
// would alias a temporary that dies with the call: the slot is stubbed
// (observing binds still work; constructing traps like a masked method).
template <typename Storage, std::meta::info Method>
consteval bool sourced_object_construct() {
  if constexpr (!is_sourced_mirror_v<Storage>)
    return false;
  else if constexpr (method_prefers_imple(Method))
    return true;  // imple constructs the object (Src), never a mirror
  else {
    constexpr auto d = signature_decl_of<Method>();
    if constexpr (d == std::meta::info{})
      return true;
    else {
      auto r = std::meta::remove_cvref(std::meta::return_type_of(d));
      return !(std::meta::has_template_arguments(r) &&
               std::meta::template_of(r) == ^^mirror);
    }
  }
}

// Call the op_construct overload matching Extra... (empty Extra is copy/move).
// AsRvalue selects the slot; the erased pointer is (ptr, vt, dst, extras...).
template <template <class> class Trait, bool AsRvalue, class... Extra>
inline void dyn_construct_into(vt_ptr vt, void* ptr, void* dst,
                               Extra&&... extra)
  requires(trait_has_dyn_construct<Trait>()) {
  constexpr auto method = dyn_construct_method<Trait, AsRvalue, Extra...>();
  static_assert(method != std::meta::info{},
                "no op_construct overload matches these arguments");
  constexpr auto idx = dyn_index_of_method<Trait>(method);
  constexpr auto decl = signature_decl_of<method>();
  if constexpr (decl != std::meta::info{}) {
    using Ctor = [:construct_erased_sig(decl):];
    Ctor::dispatch(vt[idx], ptr, vt, dst, std::forward<Extra>(extra)...);
  } else {
    using Ctor = construct_erased_fn<Extra...>;
    Ctor::dispatch(vt[idx], ptr, vt, dst, std::forward<Extra>(extra)...);
  }
}

template <template <class> class Trait, typename Storage, typename As,
          std::size_t I>
vt_fn make_slot() {
  constexpr auto method = dyn_methods(^^Trait<void>)[I];
  // Construct (ann::imple or custom Trait default): always void(dst) ->
  // placement-new Storage from invoke result. Size is a separate vt slot.
  if constexpr (is_special_op_name(std::meta::identifier_of(method),
                                   special_op::construct)) {
    check_construct_return_decl<method>();
    using Ctor = [:construct_slot_sig<method>():];
    constexpr bool AsMove = is_rvalue_self_method_of<method>();
    if constexpr (sourced_object_construct<Storage, method>())
      return as_vt_fn(&Ctor::stub);
    return as_vt_fn(&Ctor::template call<Trait, Storage, As, I, AsMove>);
  } else if constexpr (method_prefers_imple(method)) {
    constexpr auto decl = signature_decl_of<method>();
    static_assert(decl != std::meta::info{},
                  "cannot determine dyn method signature");
    using Erased = [:erased_sig(decl):];
    return as_vt_fn(&Erased::template call<Trait, Storage, As, I>);
  } else {
    constexpr auto impl_inst = []() consteval {
      constexpr auto impl = trait_impl_of<Trait, As, method>();
      if constexpr (impl != std::meta::info{}) {
        if constexpr (std::meta::is_function_template(impl))
          return std::meta::substitute(impl, {impl_self_info<Trait, As>()});
        else
          return impl;
      }
      return instantiate(method, impl_self_info<Trait, As>());
    }();
    static_assert(impl_inst != std::meta::info{},
                  "dyn method needs a default or Interface<T> specialization");
    constexpr auto decl = signature_decl_of<method>();
    static_assert(decl != std::meta::info{},
                  "cannot determine dyn method signature");
    using Erased = [:erased_sig(decl):];
    return as_vt_fn(&Erased::template call<Trait, Storage, As, I>);
  }
}

// false: full dyn table; true: non-const self methods -> Erased::stub (c_dyn).
// make_slot must live in an else branch: a trailing call after if constexpr
// return is still instantiated even when that return is taken.
template <bool MaskNonConst, template <class> class Trait, typename Storage,
          typename As, std::size_t I>
vt_fn make_slot_ptr() {
  if constexpr (MaskNonConst) {
    constexpr auto method = dyn_methods(^^Trait<void>)[I];
    if constexpr (!is_const_self_method(method)) {
      if constexpr (is_special_op_name(std::meta::identifier_of(method),
                                       special_op::construct)) {
        using Ctor = [:construct_slot_sig<method>():];
        return as_vt_fn(&Ctor::stub);
      } else {
        constexpr auto inst = signature_decl_of<method>();
        static_assert(inst != std::meta::info{},
                      "cannot hide a dyn method with an unknown signature");
        using Erased = [:erased_sig(inst):];
        return as_vt_fn(&Erased::stub);
      }
    } else {
      return make_slot<Trait, Storage, As, I>();
    }
  } else {
    return make_slot<Trait, Storage, As, I>();
  }
}

// Tables keyed by (Mask, Final, Trait, Storage, As). Final is the dyn bind
// root (Trait of a Trait<T> `final` bind); Trait is the physical table owner
// (root or cover). Redirect(I9->I8) -> vtable<...,I9,I8,...>; same cover under
// another final is a distinct specialization. meta.find: switch-style
// id -> one of meta.tables[0..x) (distinct physical vts).
template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
vt_ptr redirect_cover();

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As>
vt_fn redirect_slot_ptr();

template <bool MaskNonConst, template <class> class Final,
          template <class> class Trait, typename Storage, typename As,
          std::size_t I>
vt_fn make_flat_slot() {
  if constexpr (flat_kind_at<Trait, I>() == slot_kind::redirect) {
    constexpr auto tmpl = std::meta::template_of(flat_trait_at<Trait, I>());
    constexpr auto helper = std::meta::substitute(
        ^^redirect_slot_ptr,
        {std::meta::reflect_constant(MaskNonConst), ^^Final, tmpl, ^^Storage,
         ^^As});
    return [:helper:]();
  } else {
    // Sourced mirror Storage: mirror sections specialize on view As (T);
    // non-mirror sections on Src. op_construct and vt[-1] destroy always use
    // Src (they do not follow the Trait mirror tag).
    constexpr auto section = flat_trait_at<Trait, I>();
    constexpr auto tmpl = std::meta::template_of(section);
    constexpr auto mi = flat_method_index_at<Trait, I>();
    constexpr auto life_method = dyn_methods(section)[mi];
    constexpr bool lifetime_slot =
        std::meta::has_identifier(life_method) &&
        is_lifetime_dyn_op_name(std::meta::identifier_of(life_method));
    constexpr auto mirror_section_check = std::meta::substitute(
        ^^section_allows_mirror_source_as, {tmpl, ^^As});
    if constexpr (is_sourced_mirror_v<Storage> &&
                  (lifetime_slot || ![:mirror_section_check:]())) {
      using SlotAs = mirror_src_t<Storage>;
      constexpr auto helper = std::meta::substitute(
          ^^make_slot_ptr,
          {std::meta::reflect_constant(MaskNonConst), tmpl, ^^Storage, ^^SlotAs,
           std::meta::reflect_constant(mi)});
      return [:helper:]();
    } else {
      constexpr auto helper = std::meta::substitute(
          ^^make_slot_ptr,
          {std::meta::reflect_constant(MaskNonConst), tmpl, ^^Storage, ^^As,
           std::meta::reflect_constant(mi)});
      return [:helper:]();
    }
  }
}

}  // namespace detail

ASF_END_NAMESPACE
