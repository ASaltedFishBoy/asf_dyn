#pragma once

// Dual-mode library entry point.
//
// Module mode (ASF_USE_MODULE=1, what the CMake build uses): import the
// feature-matching module variant, then pull the std reachability umbrella
// (templates + consteval reflection are instantiated in the importing TU;
// the std declarations must be reachable there).
//
// Header mode: plain textual includes, exactly like including "cast.hpp".
//
// This header must be included before any other declaration in the TU in
// module mode (import declarations precede everything else).

#ifdef ASF_USE_MODULE
// Textual std umbrella FIRST: GCC dedups a textual declaration against the
// same declaration in the module's GMF only when the textual one comes
// first (textual-after-import redeclares and conflicts). This also means
// asf.hpp must be the first #include of the TU in module mode.
#  include "asf/std.hpp"
// Module name = this TU's effective (RTTI, INTERMEDIATE) feature pair; the
// CMake module variants are built with exactly these combinations.
#  if defined(ASF_HAS_DYN_RTTI) && ASF_HAS_DYN_RTTI
#    if defined(ASF_HAS_DYN_INTERMEDIATE) && !ASF_HAS_DYN_INTERMEDIATE
import asf_rtti_noint;
#    else
import asf_rtti;
#    endif
#  elif defined(ASF_HAS_DYN_INTERMEDIATE) && !ASF_HAS_DYN_INTERMEDIATE
import asf_noint;
#  else
import asf;
#  endif
#else
#  include "cast.hpp"
#endif
