#pragma once

// Minimal check helpers for proxy-like suites (no gtest dependency).
// Tests return non-zero from main on first failure.

#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace asf_test {

// GCC treats a missing member inside requires { x.fn() } as a hard error.
// These detectors stay in the immediate context.
template <class T, class = void>
inline constexpr bool detect_size = false;
template <class T>
inline constexpr bool detect_size<
    T, std::void_t<decltype(std::declval<T const&>().size())>> = true;

template <class T, class = void>
inline constexpr bool detect_align = false;
template <class T>
inline constexpr bool detect_align<
    T, std::void_t<decltype(std::declval<T const&>().align())>> = true;

template <class T, class = void>
inline constexpr bool detect_destroy = false;
template <class T>
inline constexpr bool detect_destroy<
    T, std::void_t<decltype(std::declval<T&>().destroy())>> = true;

template <class T, class = void>
inline constexpr bool detect_typeid_of = false;
template <class T>
inline constexpr bool detect_typeid_of<
    T, std::void_t<decltype(std::declval<T const&>().typeid_of())>> = true;

// Longer than libstdc++ SSO (15). Payload strings in tests go through this
// so a bitwise copy is a heap double-free, not a silent success.
inline constexpr char kNoSso[] = "0123456789abcdef";

inline std::string no_sso(std::string_view tag) {
  std::string s;
  s.reserve(tag.size() + 1 + sizeof(kNoSso) - 1);
  s.append(tag);
  s.push_back('#');
  s.append(kNoSso);
  return s;
}

inline std::string session_text(std::string_view label, int id) {
  return no_sso(label) + " " + no_sso(std::to_string(id));
}

inline int g_failures = 0;

inline void fail(std::string_view expr, std::string_view file, int line) {
  std::fprintf(stderr, "CHECK failed: %.*s (%.*s:%d)\n",
               static_cast<int>(expr.size()), expr.data(),
               static_cast<int>(file.size()), file.data(), line);
  ++g_failures;
}

}  // namespace asf_test

#define ASF_CHECK(expr)                                                       \
  do {                                                                        \
    if (!(expr))                                                              \
      ::asf_test::fail(#expr, __FILE__, __LINE__);                            \
  } while (0)

#define ASF_CHECK_EQ(a, b) ASF_CHECK((a) == (b))
#define ASF_CHECK_NE(a, b) ASF_CHECK((a) != (b))
#define ASF_CHECK_STREQ(a, b) ASF_CHECK(std::string_view(a) == (b))

#define ASF_RUN(fn)                                                           \
  do {                                                                        \
    const int before = ::asf_test::g_failures;                                \
    fn();                                                                     \
    if (::asf_test::g_failures == before)                                     \
      std::fprintf(stdout, "  OK  %s\n", #fn);                                \
    else                                                                      \
      std::fprintf(stdout, "FAIL  %s\n", #fn);                                \
  } while (0)
