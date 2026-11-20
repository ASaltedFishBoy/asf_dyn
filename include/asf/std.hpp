#pragma once

// std reachability umbrella for module consumers.
//
// `import asf;` exports templates + consteval reflection machinery that is
// instantiated/evaluated in the importing TU. Every name the machinery
// references must be reachable at the point of instantiation; the module
// deliberately does not re-export the std declarations (GCC header units
// collide with textual includes). Include this header once after
// `import asf;` (or include the individual std headers yourself). It is a
// plain textual header: safe in both module and non-module TUs.
//
// Keep in sync with the global module fragment of src/asf.cppm and the
// ASF_MODULE_INTERFACE-guarded include lists of the public headers.

#include <bit>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <meta>
#include <new>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>
