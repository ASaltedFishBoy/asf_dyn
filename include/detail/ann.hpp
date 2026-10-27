#pragma once

#include "iface_config.hpp"

#include <cstddef>
#include <meta>
#include <string>
#include <string_view>

ASF_BEGIN_NAMESPACE

// Annotation markers live in asf::ann (alias asf_ann).
// Method pairs may sit on Trait<void> (applies to every method declared there)
// or on a single method. A method tag overrides the Trait tag.
// Yes and no forms are paired; both on the same declaration are ill-formed.
// Dyn slot opt-out is not an annotation: `static` on Trait<void> is iface-only.
//   expl / no_expl        conversion explicit or implicit (default no_expl)
//   imple / no_imple    skip the Trait body (default no_imple). Non-op:
//                      same-named member. op_*: ctor / operator / conversion
//   mirror / no_mirror  dyn/iface from a user mirror (default no_mirror)
// Layout-only (Trait<void> and/or base-specifier; not methods). Dyn /
// MULTI_VT only -- iface convert ignores these. Same root:
//   truncate   active: on Trait -> dyn cannot upcast further from it;
//              on base-specifier -> that MI branch is not a storeable dyn
//              upcast path (no MULTI_VT Redirect/cover for it)
//   truncated  passive: on Trait -> inheriting it acts as truncate on the
//              edge (dyn cannot upcast to this Interface)
namespace ann {
struct expl_tag {};
inline constexpr expl_tag expl{};
struct no_expl_tag {};
inline constexpr no_expl_tag no_expl{};

struct imple_tag {};
inline constexpr imple_tag imple{};
struct no_imple_tag {};
inline constexpr no_imple_tag no_imple{};

struct mirror_tag {};
inline constexpr mirror_tag mirror{};
struct no_mirror_tag {};
inline constexpr no_mirror_tag no_mirror{};

struct truncate_tag {};
inline constexpr truncate_tag truncate{};

struct truncated_tag {};
inline constexpr truncated_tag truncated{};
}  // namespace ann

// Internal wildcard key for auto / ann::imple (not a user return type).
struct imple_t {};

template <std::size_t N>
struct fixed_string {
  char data[N]{};

  constexpr fixed_string(const char (&s)[N]) {
    for (std::size_t i = 0; i < N; ++i)
      data[i] = s[i];
  }

  constexpr std::string_view view() const { return {data, N - 1}; }

  constexpr bool operator==(std::string_view other) const {
    return view() == other;
  }
};

template <std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;


inline constexpr auto unchecked = [] {
  return std::meta::access_context::unchecked();
};

ASF_END_NAMESPACE

namespace asf_ann = asf::ann;
