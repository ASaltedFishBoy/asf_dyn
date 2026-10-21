#include "asf_check.hpp"
#include "mirror.hpp"

#include <cstdio>
#include <string>
#include <type_traits>

struct Widget {
  int w{};
  int h{};
  std::string name;
};

namespace asf {

template <>
class mirror<Widget> : public mirror_base<Widget> {
 public:
  using mirror_base<Widget>::mirror_base;
  constexpr mirror(mirror const&) = default;

  constexpr mirror& operator=(mirror const& other) {
    return detail::rebind_assign(*this, other);
  }
  template <typename V>
    requires(is_mirror_like_v<V> &&
             !std::same_as<std::remove_cvref_t<V>, mirror> &&
             std::constructible_from<mirror, V const&>)
  constexpr mirror& operator=(V const& other) {
    return detail::rebind_assign(*this, other);
  }

  int area() const { return w * h; }

  // Same name users are likely to pick. Must not hide or replace materialize.
  int value() const { return -1; }
};

}  // namespace asf

int main() {
  Widget widget{.w = 3, .h = 4, .name = asf_test::no_sso("box")};
  asf::mirror<Widget> m{widget};

  static_assert(asf::is_mirror_v<decltype(m)>);
  static_assert(!asf::is_mirror_v<asf::mirror_base<Widget>>);

  if (m.area() != 12) {
    std::puts("FAIL: specialized area()");
    return 1;
  }
  if (m.value() != -1) {
    std::puts("FAIL: specialized value() not visible");
    return 1;
  }

  auto rebuilt = asf::value_of(m);
  if (rebuilt.w != 3 || rebuilt.h != 4 || rebuilt.name != asf_test::no_sso("box")) {
    std::puts("FAIL: value_of");
    return 1;
  }

  Widget other{.w = 1, .h = 1, .name = asf_test::no_sso("old")};
  asf::mirror<Widget> dst{other};
  auto assigned = asf::assign_mirror(dst, widget);
  static_assert(!std::is_reference_v<decltype(assigned)>);
  if (other.w != 3 || other.h != 4 || other.name != asf_test::no_sso("box") ||
      dst.value() != -1 || assigned.value() != -1) {
    std::puts("FAIL: assign_mirror");
    return 1;
  }

  Widget moved_from{.w = 9, .h = 8, .name = asf_test::no_sso("mv")};
  Widget moved_to{.w = 0, .h = 0, .name = asf_test::no_sso("xx")};
  asf::mirror<Widget> mdst{moved_to};
  asf::assign_mirror(mdst, std::move(moved_from));
  if (moved_to.w != 9 || moved_to.h != 8 ||
      moved_to.name != asf_test::no_sso("mv") || !moved_from.name.empty()) {
    std::puts("FAIL: assign_mirror move");
    return 1;
  }

  auto sm = asf::to_mirror(widget);
  static_assert(asf::is_sourced_mirror_v<decltype(sm)>);
  static_assert(std::is_base_of_v<asf::mirror<Widget>, decltype(sm)>);
  if (sm.area() != 12 || sm.value() != -1) {
    std::puts("FAIL: mirror<T> specialization did not pass to mirror<T, Src>");
    return 1;
  }
  if (asf::value_of(sm).name != asf_test::no_sso("box")) {
    std::puts("FAIL: sourced value_of");
    return 1;
  }

  struct Label {
    std::string name;
    int h{};
    int w{};
    int extra{};
  };
  Label label{asf_test::no_sso("tag"), 4, 3, 9};
  auto named = asf::bind_mirror<Widget>(label);
  static_assert(std::is_same_v<decltype(named), asf::mirror<Widget, Label>>);
  if (named.area() != 12 || named.name != asf_test::no_sso("tag") ||
      named.value() != -1) {
    std::puts("FAIL: bind_mirror by name");
    return 1;
  }
  named.w = 8;
  asf::mirror<Widget, Label> const& named_c = named;
  if (label.w != 8 || &asf::src_of(named) != &label ||
      &asf::src_of(named_c) != &label) {
    std::puts("FAIL: bind_mirror aliases");
    return 1;
  }
  auto const_view = asf::to_mirror<Widget const>(widget);
  static_assert(std::is_same_v<decltype(asf::src_of(const_view)), Widget const&>);
  if (&asf::src_of(const_view) != &widget) {
    std::puts("FAIL: const T src_of");
    return 1;
  }

  auto self = asf::bind_mirror(widget);
  auto self_t = asf::bind_mirror<Widget>(widget);
  static_assert(std::is_same_v<decltype(self), asf::mirror<Widget, Widget>>);
  static_assert(std::is_same_v<decltype(self_t), decltype(sm)>);

  static_assert(!std::constructible_from<asf::mirror<Widget>, Label&>);
  static_assert(!std::is_default_constructible_v<asf::mirror<Widget>>);
  static_assert(!std::is_default_constructible_v<asf::mirror<Widget, Widget>>);

  // Sourced conversion explicitness follows the unsourced mirror<T> one.
  struct OneField {
    int x;
  };
  struct ConvertsToOneField {
    OneField b;
    operator OneField&() & { return b; }
  };
  // Implicit object conversion: both sides implicit.
  static_assert(
      std::convertible_to<ConvertsToOneField&, asf::mirror<OneField>>);
  static_assert(
      std::convertible_to<ConvertsToOneField&,
                          asf::mirror<OneField, ConvertsToOneField>>);
  // Explicit-only (single-field pack edge): both sides explicit.
  static_assert(std::constructible_from<asf::mirror<OneField>, int&>);
  static_assert(!std::convertible_to<int&, asf::mirror<OneField>>);
  static_assert(std::constructible_from<asf::mirror<OneField, int>, int&>);
  static_assert(!std::convertible_to<int&, asf::mirror<OneField, int>>);

  struct HasPtr {
    int ptr{7};
    std::string name;
  };
  HasPtr hp{.ptr = 4, .name = asf_test::no_sso("p")};
  auto hpm = asf::to_mirror(hp);
  if (hpm.ptr != 4 || &asf::src_of(hpm) != &hp) {
    std::puts("FAIL: sourced Src* must not hide a field named ptr");
    return 1;
  }

  std::puts("ok");
  return 0;
}
