// asf module interface unit.
//
// The library sources stay in include/ as regular headers (textual #include
// keeps working unchanged). This unit textual-includes them in purview with
// ASF_MODULE_INTERFACE defined, which
//   - expands ASF_EXPORT to `export` (ASF_BEGIN_NAMESPACE / the asf_ann /
//     asf_dyn aliases), exporting namespace asf (API under asf::dyn) at
//     namespace granularity, and
//   - makes the headers skip their own <...> includes; the std dependencies
//     are textually included in the global module fragment below instead.
//
// Consumers must make the std declarations reachable at the points of
// instantiation (templates + consteval reflection are evaluated in the
// importing TU): include "asf/std.hpp" after `import asf;` (or the
// individual std headers). Do NOT mix with std header units on GCC:
// imported header units collide with later textual includes (redefinition).
module;

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

export module asf;

#define ASF_MODULE_INTERFACE
#include "interface.hpp"
#include "cast.hpp"
