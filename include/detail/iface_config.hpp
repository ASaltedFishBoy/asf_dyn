#pragma once

// Feature macros + runtime-visible flags for the iface/dyn stack.
//   ASF_HAS_DYN_RTTI         0 (default): no meta / dyn_cast (experimental when
//     on: dyn_cast over-accepts downcasts; see docs/*/spec/features.md)
//   ASF_HAS_DYN_INTERMEDIATE 1 (default): to_dyn from non-final via ADL friend;
//     only dyn final injects (same-TU STMP first wins; multi-TU ODR/COMDAT, no
//     weak). See docs/en/spec/features.md. 0 => final-only object-bind.
//   ASF_HAS_DYN_MULTI_VT     1 (default): Redirect/cover multi-vtable; dyn->dyn
//     full-path upcast (any Trait base). 0 => sections-only layout; upcast is
//     section-prefix / same physical vt only. Mutually exclusive with
//     ASF_HAS_DYN_SINGLE_INHERIT (CMake FATAL_ERROR; #error if both are 1).
//   ASF_HAS_DYN_SINGLE_INHERIT  0 (default): Trait MI allowed. 1 => dyn
//     generation static_asserts SI among live dyn bases (truncate/truncated
//     edges allowed). Mutex with ASF_HAS_DYN_MULTI_VT. Not an extra upcast
//     switch; MULTI_VT-off is still section-prefix / same vt.
//
// ABI version (semver triple) + feature bitstring. Encoded in mangled names
// via inline namespace ASF_ABI_NAMESPACE so a .dll/.so built against one ASF
// version / option mix does not COMDAT-merge slots / static vt data / erased
// thunks with another loaded in the same process. Bump ASF_VERSION_* (and the
// vMAJOR_MINOR_PATCH token below) when breaking the dyn/vtable symbol ABI.
// Source still spells asf::dyn, asf::detail::... (inline ns is transparent).

#ifndef ASF_VERSION_MAJOR
#define ASF_VERSION_MAJOR 0
#endif
#ifndef ASF_VERSION_MINOR
#define ASF_VERSION_MINOR 1
#endif
#ifndef ASF_VERSION_PATCH
#define ASF_VERSION_PATCH 4
#endif

#define ASF_VERSION                                                            \
  (ASF_VERSION_MAJOR * 10000 + ASF_VERSION_MINOR * 100 + ASF_VERSION_PATCH)

#ifndef ASF_VERSION_STRING
#define ASF_VERSION_STRING "0.1.4"
#endif

#ifndef ASF_HAS_DYN_RTTI
#define ASF_HAS_DYN_RTTI 0
#endif
#ifndef ASF_HAS_DYN_INTERMEDIATE
#define ASF_HAS_DYN_INTERMEDIATE 1
#endif
#ifndef ASF_HAS_DYN_MULTI_VT
#define ASF_HAS_DYN_MULTI_VT 1
#endif
#ifndef ASF_HAS_DYN_SINGLE_INHERIT
#define ASF_HAS_DYN_SINGLE_INHERIT 0
#endif

#if ASF_HAS_DYN_SINGLE_INHERIT && ASF_HAS_DYN_MULTI_VT
#error "ASF_HAS_DYN_SINGLE_INHERIT and ASF_HAS_DYN_MULTI_VT are mutually " \
       "exclusive; set ASF_HAS_DYN_MULTI_VT=0 when enabling " \
       "ASF_HAS_DYN_SINGLE_INHERIT"
#endif

#define ASF_PP_CAT2(a, b) a##b
#define ASF_PP_CAT(a, b) ASF_PP_CAT2(a, b)
#define ASF_PP_STRINGIZE2(x) #x
#define ASF_PP_STRINGIZE(x) ASF_PP_STRINGIZE2(x)

// Function entry alignment. The align-tag packing in destroy pointers
// needs the low bits of the tagged thunks free. GCC/Clang only: no
// portable per-function alignment on MSVC (that path must carry align
// separately; the tagging assert will catch it).
#if defined(__GNUC__) || defined(__clang__)
#define ASF_FN_ALIGN(n) __attribute__((aligned(n)))
#else
#define ASF_FN_ALIGN(n)
#endif

// Binary feature tag (MSB->LSB): RTTI, INTERMEDIATE, MULTI_VT,
// SINGLE_INHERIT. Pasted as ..._bXXXX into the inline ABI namespace.
#define ASF_ABI_FEATURE_BITS                                                   \
  ASF_PP_CAT(                                                                  \
      ASF_PP_CAT(ASF_PP_CAT(ASF_HAS_DYN_RTTI, ASF_HAS_DYN_INTERMEDIATE),       \
                 ASF_HAS_DYN_MULTI_VT),                                        \
      ASF_HAS_DYN_SINGLE_INHERIT)

// Keep the v0_1_4 token in sync with ASF_VERSION_* when bumping.
#ifndef ASF_ABI_NAMESPACE
#define ASF_ABI_NAMESPACE ASF_PP_CAT(v0_1_4_b, ASF_ABI_FEATURE_BITS)
#endif

#define ASF_BEGIN_NAMESPACE                                                    \
  namespace asf {                                                               \
  inline namespace ASF_ABI_NAMESPACE {

#define ASF_END_NAMESPACE                                                      \
  } /* inline ASF_ABI_NAMESPACE */                                             \
  } /* namespace asf */

ASF_BEGIN_NAMESPACE

inline constexpr int version_major = ASF_VERSION_MAJOR;
inline constexpr int version_minor = ASF_VERSION_MINOR;
inline constexpr int version_patch = ASF_VERSION_PATCH;
inline constexpr int version = ASF_VERSION;

inline constexpr bool dyn_rtti_v = ASF_HAS_DYN_RTTI != 0;
inline constexpr bool dyn_intermediate_v = ASF_HAS_DYN_INTERMEDIATE != 0;
inline constexpr bool dyn_multi_vt_v = ASF_HAS_DYN_MULTI_VT != 0;
inline constexpr bool dyn_single_inherit_v = ASF_HAS_DYN_SINGLE_INHERIT != 0;

// Same bit order as ASF_ABI_FEATURE_BITS / the `_bXXXX` namespace suffix.
inline constexpr unsigned dyn_feature_bits =
    (dyn_rtti_v ? 1u << 3 : 0u) | (dyn_intermediate_v ? 1u << 2 : 0u) |
    (dyn_multi_vt_v ? 1u << 1 : 0u) |
    (dyn_single_inherit_v ? 1u << 0 : 0u);

inline constexpr char const* abi_namespace_name =
    ASF_PP_STRINGIZE(ASF_ABI_NAMESPACE);

ASF_END_NAMESPACE
