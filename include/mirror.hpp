#pragma once

#include "detail/ann.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <meta>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

ASF_BEGIN_NAMESPACE
namespace detail {

template <typename R, typename... Args>
struct erased_fn;

// Decay to the class used for field layout (not a pointer/handle typedef).
template <typename T>
using mirror_class = std::remove_cvref_t<T>;

// Live object -> mirror<T>: T as a reference is used as-is (T& / const T& /
// T&&). Non-reference T is T&.
template <typename T>
using mirror_object_t = std::conditional_t<std::is_reference_v<T>, T,
                                           std::add_lvalue_reference_t<T>>;

template <typename T, typename U>
concept converts_to_mirror_object = std::convertible_to<
    std::conditional_t<std::is_rvalue_reference_v<mirror_object_t<T>>, U&&,
                       U&>,
    mirror_object_t<T>>;

template <typename T, typename U>
constexpr decltype(auto) as_mirror_object(U&& obj) {
  using Bind = mirror_object_t<T>;
  if constexpr (std::is_rvalue_reference_v<Bind>)
    return static_cast<Bind>(std::forward<U>(obj));
  else
    return static_cast<Bind>(obj);
}

consteval std::meta::info find_field(std::meta::info cls,
                                     std::string_view name) {
  for (auto member : std::meta::nonstatic_data_members_of(cls, unchecked())) {
    if (std::meta::has_identifier(member) &&
        std::meta::identifier_of(member) == name)
      return member;
  }
  for (auto base : std::meta::bases_of(cls, unchecked())) {
    auto found = find_field(std::meta::type_of(base), name);
    if (found != std::meta::info{})
      return found;
  }
  return std::meta::info{};
}

template <typename T>
consteval std::size_t field_count() {
  return std::meta::nonstatic_data_members_of(^^mirror_class<T>, unchecked())
      .size();
}

template <typename T, std::size_t I>
consteval std::meta::info field_at() {
  return std::meta::nonstatic_data_members_of(^^mirror_class<T>, unchecked())[I];
}

template <typename T>
consteval std::meta::info mirror_member_type(std::meta::info member) {
  auto type = std::meta::type_of(member);
  if (std::meta::is_reference_type(type))
    return type;

  auto object = std::meta::remove_reference(^^T);
  if (std::meta::is_const_type(object) &&
      !std::meta::is_mutable_member(member))
    type = std::meta::add_const(type);
  if (std::meta::is_volatile_type(object))
    type = std::meta::add_volatile(type);
  if (std::meta::is_rvalue_reference_type(^^T))
    return std::meta::add_rvalue_reference(type);
  return std::meta::add_lvalue_reference(type);
}

template <typename T>
consteval std::vector<std::meta::info> mirror_specs() {
  std::vector<std::meta::info> specs;
  for (auto member :
       std::meta::nonstatic_data_members_of(^^mirror_class<T>, unchecked())) {
    specs.push_back(std::meta::data_member_spec(
        mirror_member_type<T>(member),
        {.name = std::string(std::meta::identifier_of(member))}));
  }
  return specs;
}

template <typename T>
struct mirror_box {
  struct data;
  consteval { std::meta::define_aggregate(^^data, mirror_specs<T>()); }
};

template <typename T, typename U>
consteval bool can_bind_fields() {
  using Src = mirror_class<U>;
  using Obj = std::remove_reference_t<U>;
  for (auto tmem :
       std::meta::nonstatic_data_members_of(^^mirror_class<T>, unchecked())) {
    auto umem = find_field(^^Src, std::meta::identifier_of(tmem));
    if (umem == std::meta::info{})
      return false;
    auto want = mirror_member_type<T>(tmem);
    auto src = std::meta::type_of(umem);
    if (!std::meta::is_reference_type(src)) {
      if (std::meta::is_const_type(^^Obj) &&
          !std::meta::is_mutable_member(umem))
        src = std::meta::add_const(src);
      if (std::meta::is_volatile_type(^^Obj))
        src = std::meta::add_volatile(src);
    }
    auto from = std::meta::is_rvalue_reference_type(want)
                    ? std::meta::add_rvalue_reference(src)
                    : std::meta::add_lvalue_reference(src);
    auto from_obj = std::meta::remove_reference(from);
    auto want_obj = std::meta::remove_reference(want);
    if (std::meta::is_const_type(from_obj) &&
        !std::meta::is_const_type(want_obj))
      return false;
    if (std::meta::is_volatile_type(from_obj) &&
        !std::meta::is_volatile_type(want_obj))
      return false;
    if (!std::meta::is_convertible_type(from, want))
      return false;
  }
  return true;
}

template <typename T, typename Obj, std::size_t I>
constexpr decltype(auto) bind_one(Obj&& obj) {
  constexpr auto tmem = field_at<T, I>();
  constexpr auto umem =
      find_field(^^mirror_class<Obj>, std::meta::identifier_of(tmem));
  static_assert(umem != std::meta::info{},
                "source type is missing a field required by mirror<T, TT>");
  constexpr auto mirror_ty = mirror_member_type<T>(tmem);
  if constexpr (std::meta::is_rvalue_reference_type(mirror_ty)) {
    return std::move(std::forward<Obj>(obj).[:umem:]);
  } else {
    auto& field = obj.[:umem:];
    return field;
  }
}

template <typename T, typename Obj, std::size_t... I>
constexpr typename mirror_box<T>::data assemble(Obj&& obj,
                                                std::index_sequence<I...>) {
  using Data = typename mirror_box<T>::data;
  return Data{bind_one<T, Obj, I>(std::forward<Obj>(obj))...};
}

// Field-pack ctor: each arg must convert to that field's mirror ref type.
template <typename T, typename Tuple, std::size_t... I>
consteval bool can_bind_field_pack_impl(std::index_sequence<I...>) {
  return (std::meta::is_convertible_type(
              ^^std::tuple_element_t<I, Tuple>,
              mirror_member_type<T>(field_at<T, I>())) &&
          ...);
}

template <typename T, typename... Refs>
consteval bool can_bind_field_pack() {
  if (sizeof...(Refs) != field_count<T>())
    return false;
  return can_bind_field_pack_impl<T, std::tuple<Refs...>>(
      std::index_sequence_for<Refs...>{});
}

template <typename Dst, typename Src>
constexpr Dst& rebind_assign(Dst& dst, Src const& src) {
  if constexpr (std::same_as<Dst, std::remove_cvref_t<Src>>) {
    if (std::addressof(dst) ==
        std::addressof(static_cast<Dst const&>(src)))
      return dst;
  }
  std::destroy_at(std::addressof(dst));
  std::construct_at(std::addressof(dst), src);
  return dst;
}

template <typename T, std::size_t I>
consteval std::meta::info data_field_at() {
  using Data = typename mirror_box<T>::data;
  return std::meta::nonstatic_data_members_of(^^Data, unchecked())[I];
}

// Field-wise assign. Copy/move follows the source stored refs / object category.
template <typename T, typename U, typename Dst, typename Src, std::size_t I>
constexpr void assign_field_at(Dst&& dst, Src const& src) {
  constexpr auto dmem = data_field_at<T, I>();
  constexpr auto smem = data_field_at<U, I>();
  auto const& src_data = static_cast<typename mirror_box<U>::data const&>(src);
  if constexpr (std::meta::is_rvalue_reference_type(std::meta::type_of(smem)))
    dst.[:dmem:] = std::move(src_data.[:smem:]);
  else
    dst.[:dmem:] = src_data.[:smem:];
}

template <typename T, typename U, typename Dst, typename Src, std::size_t... I>
constexpr void assign_fields(Dst&& dst, Src const& src,
                             std::index_sequence<I...>) {
  (assign_field_at<T, U, Dst, Src, I>(std::forward<Dst>(dst), src), ...);
}

template <typename T, typename Dst, typename Obj, std::size_t I>
constexpr void assign_field_from_object(Dst&& dst, Obj&& obj) {
  constexpr auto dmem = data_field_at<T, I>();
  constexpr auto omem = field_at<T, I>();
  dst.[:dmem:] = std::forward<Obj>(obj).[:omem:];
}

template <typename T, typename Dst, typename Obj, std::size_t... I>
constexpr void assign_fields_from_object(Dst&& dst, Obj&& obj,
                                         std::index_sequence<I...>) {
  (assign_field_from_object<T, Dst, Obj, I>(
       std::forward<Dst>(dst), std::forward<Obj>(obj)),
   ...);
}

// Rebuild mirror_class<T> from *stored* field refs (data member types), not
// by re-deriving ref kinds from primary T.
template <typename T, typename Data, std::size_t I>
constexpr decltype(auto) materialize_field(Data&& data) {
  constexpr auto dmem = data_field_at<T, I>();
  using Stored = [:std::meta::type_of(dmem):];
  return static_cast<Stored>(std::forward<Data>(data).[:dmem:]);
}

template <typename T, typename Data, std::size_t... I>
constexpr mirror_class<T> materialize_from_data(Data&& data,
                                                std::index_sequence<I...>) {
  return mirror_class<T>{materialize_field<T, Data, I>(
      std::forward<Data>(data))...};
}

template <typename T, typename Data>
constexpr mirror_class<T> materialize_from_data(Data&& data) {
  return materialize_from_data<T>(
      std::forward<Data>(data),
      std::make_index_sequence<field_count<T>()>{});
}

// Src viewed through T: cv and &/&& follow T (the mirror is only a view).
template <typename From, typename To>
using copy_cv_t = std::conditional_t<
    std::is_volatile_v<From>,
    std::add_volatile_t<std::conditional_t<
        std::is_const_v<From>, std::add_const_t<To>, To>>,
    std::conditional_t<std::is_const_v<From>, std::add_const_t<To>, To>>;

template <typename T, typename Src>
using src_ref_t = std::conditional_t<
    std::is_rvalue_reference_v<T>,
    copy_cv_t<std::remove_reference_t<T>, std::remove_reference_t<Src>>&&,
    copy_cv_t<std::remove_reference_t<T>, std::remove_reference_t<Src>>&>;

// Src* with T's cv. Different Src already instantiates separately; not void*.
template <typename T, typename Src>
using src_ptr_t =
    copy_cv_t<std::remove_reference_t<T>, std::remove_reference_t<Src>>*;

template <typename T, typename Src, typename U>
constexpr src_ptr_t<T, Src> source_ptr_of(U& obj) noexcept {
  return static_cast<src_ptr_t<T, Src>>(std::addressof(obj));
}

// Same identity, viewed through dest T (pointer cv follows T). Not a
// covariant Src* conversion.
template <typename T, typename Src, typename FromPtr>
constexpr src_ptr_t<T, Src> transform_src_ptr(FromPtr p) noexcept {
  return static_cast<src_ptr_t<T, Src>>(p);
}

}  // namespace detail

// Implementation base (public). Specialize / inherit via thin `mirror` shell;
// do not put user helpers on mirror_base -- put them on a specialized `mirror`.
template <typename T, typename Src = void>
class mirror_base;

// Structural field binding of T (optional Src identity). A replaceable
// reference: no default construct. Sourced identity is a private Src*
// (cv follows T), not a public void*. Assign is always field-wise.
// Default: thin shell over mirror_base; users may specialize and add helpers.
template <typename T, typename Src = void>
class mirror;

namespace detail {
template <typename T, typename Src>
  requires(!std::is_void_v<Src>)
constexpr src_ptr_t<T, Src> mirror_src_ptr(mirror<T, Src> const& m) noexcept;
}

template <typename T, typename Src>
  requires(!std::is_void_v<Src>)
constexpr detail::src_ref_t<T, Src> src_of(mirror<T, Src> const& m);

template <typename T, typename Src = void>
  requires(!std::is_rvalue_reference_v<T>)
constexpr detail::mirror_class<T> value_of(mirror<T, Src> const& m);

template <typename T, typename Src = void>
  requires(std::is_rvalue_reference_v<T>)
constexpr detail::mirror_class<T> value_of(mirror<T, Src>& m);

template <typename T, typename Src = void>
  requires(std::is_rvalue_reference_v<T>)
constexpr detail::mirror_class<T> value_of(mirror<T, Src>&& m);

template <class>
struct is_mirror : std::false_type {};

template <typename T, typename Src>
struct is_mirror<mirror<T, Src>> : std::true_type {};

template <class V>
inline constexpr bool is_mirror_v = is_mirror<std::remove_cvref_t<V>>::value;

template <class>
struct is_sourced_mirror : std::false_type {};

template <typename T, typename Src>
struct is_sourced_mirror<mirror<T, Src>>
    : std::bool_constant<!std::is_void_v<Src>> {};

template <class V>
inline constexpr bool is_sourced_mirror_v =
    is_sourced_mirror<std::remove_cvref_t<V>>::value;

template <class V>
concept sourced_mirror = is_sourced_mirror_v<V>;

template <class V>
struct mirror_src {
  using type = void;
};

template <typename T, typename Src>
struct mirror_src<mirror<T, Src>> {
  using type = Src;
};

template <class V>
using mirror_src_t = typename mirror_src<std::remove_cvref_t<V>>::type;

template <class V>
inline constexpr bool is_mirror_like_v = is_mirror_v<V>;

template <class V>
concept mirror_like = is_mirror_like_v<V>;

// Sourced mirror whose Src is T itself (mirror<T, T>). Contrast: mirror<T, U>.
template <typename T>
using identity_mirror = mirror<T, detail::mirror_class<T>>;

// Value-category aliases (same shape as dyn_rproxy / dyn_c_proxy / dyn_c_rproxy):
//   rmirror     mirror<T&&>       -- r joined to mirror
//   c_mirror    mirror<const T>
//   c_rmirror   mirror<const T&&>
template <typename T, typename Src = void>
using rmirror = mirror<T&&, Src>;
template <typename T, typename Src = void>
using c_mirror = mirror<const T, Src>;
template <typename T, typename Src = void>
using c_rmirror = mirror<const T&&, Src>;

template <class V>
struct mirror_target;

template <typename T, typename Src>
struct mirror_target<mirror<T, Src>> {
  using type = T;
};

template <class V>
using mirror_target_t = typename mirror_target<std::remove_cvref_t<V>>::type;

template <typename To, typename FromMirror>
concept mirror_qualifies_to =
    mirror_like<FromMirror> &&
    std::same_as<detail::mirror_class<mirror_target_t<FromMirror>>,
                 detail::mirror_class<To>> &&
    std::convertible_to<std::remove_reference_t<mirror_target_t<FromMirror>>&,
                        std::remove_reference_t<To>&>;

// Result type when binding U as field mirror of T (const from U propagates).
// Only used for T-compatible U (converts to that T); no cross-type name bind.
template <typename T, typename U>
struct bind_mirror_result {
  using Src = std::remove_reference_t<U>;
  using Base = std::remove_reference_t<T>;
  using Qualified =
      std::conditional_t<std::is_const_v<Src>, std::add_const_t<Base>, Base>;
  using type = std::conditional_t<
      std::is_rvalue_reference_v<T>, Qualified&&,
      std::conditional_t<std::is_lvalue_reference_v<T>, Qualified&, Qualified>>;
};

template <typename T, typename U>
using bind_mirror_t = mirror<typename bind_mirror_result<T, U>::type>;

namespace detail {

// Internal (vtable / dyn): U must convert to the T this mirror wants
// (T&& / const T& / T as T&), or construct identity_mirror. Not cross-type
// name binding; that is bind_mirror.
template <typename T, typename U>
  requires(!is_mirror_like_v<U>)
bind_mirror_t<T, U> object_mirror(U&& obj) {
  using R = typename bind_mirror_result<T, U>::type;
  if constexpr (converts_to_mirror_object<R, U>) {
    return mirror<R>(std::forward<U>(obj));
  } else if constexpr (
      !std::same_as<mirror_class<U>, mirror_class<R>> &&
      std::constructible_from<identity_mirror<R>, U>) {
    // Sourced mirror_base inherits mirror<R>, so the identity mirror is a mirror<R>.
    return mirror<R>(
        static_cast<mirror<R> const&>(identity_mirror<R>(std::forward<U>(obj))));
  } else {
    static_assert(std::is_void_v<U>,
                  "object_mirror<T> needs conversion to that T "
                  "(or identity_mirror)");
  }
}

}  // namespace detail

template <typename T>
class mirror_base<T, void> : public detail::mirror_box<T>::data {
  using data_type = typename detail::mirror_box<T>::data;

  explicit mirror_base(data_type const& data) : data_type(data) {}

  template <typename U, typename V>
    requires(!is_mirror_like_v<V>)
  friend bind_mirror_t<U, V> detail::object_mirror(V&&);

  mirror_base() = delete;

 public:
  template <typename U>
    requires(!is_mirror_like_v<U> &&
             detail::converts_to_mirror_object<T, U>)
  mirror_base(U&& obj)
      : data_type(detail::assemble<T>(
            detail::as_mirror_object<T>(std::forward<U>(obj)),
            std::make_index_sequence<detail::field_count<T>()>{})) {}

  // Explicit: same class, different T qualification (T / T& / T&& / const...).
  template <typename V>
    requires(mirror_qualifies_to<T, V> && !std::same_as<mirror_target_t<V>, T>)
  explicit mirror_base(V&& other)
      : data_type(detail::assemble<T>(
            std::forward<V>(other),
            std::make_index_sequence<detail::field_count<T>()>{})) {}

  mirror_base(const mirror_base&) = default;

  // Explicit field refs in T's declaration order (bare mirror only; no Src).
  template <typename... FieldRefs>
    requires(detail::can_bind_field_pack<T, FieldRefs...>())
  explicit mirror_base(FieldRefs&&... refs)
      : data_type{std::forward<FieldRefs>(refs)...} {}
};

namespace detail {

// Sourced mirror from an object whose class is Src.
// Bare mirror<T> is the base; do not name a particular Src (identity_mirror).
// Qualification converts (T <-> T&& / const...) are explicit; use
// constructible_from so those ctors still count.
// Explicit field-pack ctor on bare mirror must not enable Src.
template <typename T, typename Obj>
constexpr mirror<T> bare_from_names(Obj&& obj);

struct by_name_t {
  explicit by_name_t() = default;
};
inline constexpr by_name_t by_name{};

template <typename T, typename U>
concept can_source_mirror_from =
    !is_mirror_like_v<U> &&
    (converts_to_mirror_object<T, U> ||
     std::constructible_from<mirror<T>, U&>);

}  // namespace detail

// Inherit mirror<T> (the user-specializable shell), not mirror_base<T>, so a
// specialization of mirror<T> is a base of every mirror<T, Src>.
template <typename T, typename Src>
class mirror_base : public mirror<T> {
  using src_ptr_t = detail::src_ptr_t<T, Src>;
  // Private, not a T field name. Inheritance must not hide user members.
  src_ptr_t __asf_src;

  template <typename, typename>
  friend class mirror_base;
  template <typename U, typename S>
    requires(!std::is_void_v<S>)
  friend constexpr detail::src_ptr_t<U, S> detail::mirror_src_ptr(
      mirror<U, S> const&) noexcept;

  mirror_base() = delete;

 public:
  // The sourced conversion inherits the strength of the unsourced one:
  // implicit iff U& converts to mirror<T> implicitly (the object
  // conversion or a non-explicit conversion operator); explicit when
  // only an explicit mirror<T> ctor exists (e.g. the single-field pack
  // edge, or an explicit conversion operator).
  template <typename U>
    requires(std::same_as<detail::mirror_class<U>, detail::mirror_class<Src>> &&
             detail::can_source_mirror_from<T, U> &&
             std::convertible_to<U&, mirror<T>>)
  mirror_base(U&& obj)
      : mirror<T>(std::forward<U>(obj)),
        __asf_src(detail::source_ptr_of<T, Src>(obj)) {}

  template <typename U>
    requires(std::same_as<detail::mirror_class<U>, detail::mirror_class<Src>> &&
             detail::can_source_mirror_from<T, U> &&
             !std::convertible_to<U&, mirror<T>>)
  explicit mirror_base(U&& obj)
      : mirror<T>(std::forward<U>(obj)),
        __asf_src(detail::source_ptr_of<T, Src>(obj)) {}

  // Same field names. Tag keeps this off the converting ctor and to_mirror.
  template <typename U>
    requires(std::same_as<detail::mirror_class<U>, detail::mirror_class<Src>> &&
             detail::can_bind_fields<T, U>())
  mirror_base(detail::by_name_t, U&& obj)
      : mirror<T>(detail::bare_from_names<T>(std::forward<U>(obj))),
        __asf_src(detail::source_ptr_of<T, Src>(obj)) {}

  // Same target T: copy fields + Src identity.
  template <typename U, typename Other>
    requires(!std::is_void_v<Other> && std::same_as<U, T> &&
             std::same_as<detail::mirror_class<U>, detail::mirror_class<T>> &&
             std::convertible_to<std::remove_reference_t<U>&,
                                 std::remove_reference_t<T>&> &&
             std::convertible_to<detail::src_ptr_t<U, Other>, src_ptr_t>)
  mirror_base(mirror<U, Other> const& other)
      : mirror<T>(other),
        __asf_src(detail::transform_src_ptr<T, Src>(other.__asf_src)) {}

  // Explicit: same class, different T qualification (e.g. mirror<T> -> mirror<T&&>).
  // Src identity is transformed through dest T, not a covariant Src* conversion.
  template <typename U, typename Other>
    requires(!std::is_void_v<Other> && !std::same_as<U, T> &&
             std::same_as<detail::mirror_class<U>, detail::mirror_class<T>> &&
             std::convertible_to<std::remove_reference_t<U>&,
                                 std::remove_reference_t<T>&> &&
             std::convertible_to<detail::src_ptr_t<U, Other>, src_ptr_t>)
  explicit mirror_base(mirror<U, Other> const& other)
      : mirror<T>(other),
        __asf_src(detail::transform_src_ptr<T, Src>(other.__asf_src)) {}

  mirror_base(const mirror_base&) = default;
};

// Thin public shell: specialize this to add user helpers; keep mirror_base.
// mirror<T, Src>'s implementation base inherits mirror<T>, so those helpers
// are also available on sourced mirrors.
template <typename T, typename Src>
class mirror : public mirror_base<T, Src> {
 public:
  using mirror_base<T, Src>::mirror_base;
  mirror(mirror const&) = default;

  // Pointer rebind: destroy + copy-construct. Field write is assign_mirror.
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
};

namespace detail {

template <typename T, typename Obj, std::size_t... I>
constexpr mirror<T> bare_from_names(Obj&& obj, std::index_sequence<I...>) {
  return mirror<T>(bind_one<T, Obj, I>(std::forward<Obj>(obj))...);
}

template <typename T, typename Obj>
constexpr mirror<T> bare_from_names(Obj&& obj) {
  return bare_from_names<T>(std::forward<Obj>(obj),
                            std::make_index_sequence<field_count<T>()>{});
}

}  // namespace detail

namespace detail {
template <typename T, typename Src>
  requires(!std::is_void_v<Src>)
constexpr src_ptr_t<T, Src> mirror_src_ptr(mirror<T, Src> const& m) noexcept {
  return m.__asf_src;
}
}  // namespace detail

// Underlying source object (identity). cv-ref follows T, not whether m is const.
template <typename T, typename Src>
  requires(!std::is_void_v<Src>)
constexpr detail::src_ref_t<T, Src> src_of(mirror<T, Src> const& m) {
  using Ref = detail::src_ref_t<T, Src>;
  return static_cast<Ref>(*detail::mirror_src_ptr(m));
}

// Field-wise write through dst's bound refs. Does not change a sourced ptr.
// dest is T& or T (writable). src is T&& (move) / T const& (copy), unsourced
// c_mirror (copy), or unsourced rmirror (move). Copyable mirrors are passed
// by value; rmirror is const& (rvalue field refs are not copyable).
// Returns a copy of dst.
namespace detail {
template <typename DstT, typename ST, typename Obj>
constexpr mirror<DstT, ST> assign_mirror_obj(mirror<DstT, ST> dst, Obj&& obj) {
  using Data = typename mirror_box<DstT>::data;
  assign_fields_from_object<DstT>(
      static_cast<Data const&>(dst), std::forward<Obj>(obj),
      std::make_index_sequence<field_count<DstT>()>{});
  return dst;
}

template <typename DstT, typename ST, typename SrcView, typename Src>
constexpr mirror<DstT, ST> assign_mirror_view(mirror<DstT, ST> dst,
                                              Src const& src) {
  using Data = typename mirror_box<DstT>::data;
  assign_fields<DstT, SrcView>(
      static_cast<Data const&>(dst), src,
      std::make_index_sequence<field_count<DstT>()>{});
  return dst;
}
}  // namespace detail

template <typename T, typename ST>
  requires(!std::is_reference_v<T>)
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, T&& src) {
  return detail::assign_mirror_obj(std::move(dst), std::move(src));
}
template <typename T, typename ST>
  requires(!std::is_reference_v<T>)
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, T const& src) {
  return detail::assign_mirror_obj(std::move(dst), src);
}
template <typename T, typename ST>
  requires(!std::is_reference_v<T>)
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, c_mirror<T> src) {
  return detail::assign_mirror_view<T&, ST, const T>(std::move(dst),
                                                     std::move(src));
}
template <typename T, typename ST>
  requires(!std::is_reference_v<T>)
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst,
                                       rmirror<T> const& src) {
  return detail::assign_mirror_view<T&, ST, T&&>(std::move(dst), src);
}

template <typename T, typename ST>
  requires(!std::is_reference_v<T>)
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, T&& src) {
  return detail::assign_mirror_obj(std::move(dst), std::move(src));
}
template <typename T, typename ST>
  requires(!std::is_reference_v<T>)
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, T const& src) {
  return detail::assign_mirror_obj(std::move(dst), src);
}
template <typename T, typename ST>
  requires(!std::is_reference_v<T>)
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, c_mirror<T> src) {
  return detail::assign_mirror_view<T, ST, const T>(std::move(dst),
                                                    std::move(src));
}
template <typename T, typename ST>
  requires(!std::is_reference_v<T>)
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst,
                                      rmirror<T> const& src) {
  return detail::assign_mirror_view<T, ST, T&&>(std::move(dst), src);
}

// Rebuild T from stored field refs, or from Src* when Src is T itself.
// Copy/move follows T (T&& moves; otherwise copies), not whether m is an rvalue.
template <typename T, typename Src>
  requires(!std::is_rvalue_reference_v<T>)
constexpr detail::mirror_class<T> value_of(mirror<T, Src> const& m) {
  if constexpr (!std::is_void_v<Src> &&
                std::same_as<detail::mirror_class<T>,
                             detail::mirror_class<Src>>)
    return src_of(m);
  else {
    using Data = typename detail::mirror_box<T>::data;
    return detail::materialize_from_data<T>(static_cast<Data const&>(m));
  }
}

template <typename T, typename Src>
  requires(std::is_rvalue_reference_v<T>)
constexpr detail::mirror_class<T> value_of(mirror<T, Src>& m) {
  if constexpr (!std::is_void_v<Src> &&
                std::same_as<detail::mirror_class<T>,
                             detail::mirror_class<Src>>)
    return src_of(m);
  else {
    using Data = typename detail::mirror_box<T>::data;
    return detail::materialize_from_data<T>(static_cast<Data&&>(m));
  }
}

template <typename T, typename Src>
  requires(std::is_rvalue_reference_v<T>)
constexpr detail::mirror_class<T> value_of(mirror<T, Src>&& m) {
  return value_of(m);
}

template <typename U>
mirror(U&&) -> mirror<U&&>;

template <typename V, bool = is_mirror_like_v<std::remove_reference_t<V>>>
struct deduce_mirror {
  using type = std::remove_reference_t<V>;
};

template <typename V>
struct deduce_mirror<V, true> {
  using type = mirror_target_t<std::remove_reference_t<V>>;
};

template <typename V>
using deduce_mirror_t = typename deduce_mirror<V>::type;

// Object only (mirrors rejected).
//   to_mirror<T>(obj) -> mirror<T, remove_cvref_t<obj>>
//   to_mirror(obj)    -> mirror<T, T>, T = remove_reference_t<obj>
// Object converts to the T this mirror wants (T&& / const T& / T as T&),
// or a user conversion / identity_mirror.
// Cross-type same-name fields: bind_mirror.
template <typename T = void, typename V>
  requires(!is_mirror_like_v<V>)
constexpr auto to_mirror(V&& obj) {
  if constexpr (std::is_void_v<T>) {
    using Obj = std::remove_reference_t<V>;
    return mirror<Obj, Obj>(std::forward<V>(obj));
  } else {
    return mirror<T, std::remove_cvref_t<V>>(std::forward<V>(obj));
  }
}

// Object only (mirrors rejected). Same call shape as to_mirror.
//   bind_mirror<T>(obj) -> mirror<T, remove_cvref_t<obj>>
//   bind_mirror(obj)    -> mirror<T, T>, T = remove_reference_t<obj>
// Each field of T binds to the same-named field of obj.
// Obj need not convert to that mirror's T.
template <typename T = void, typename V>
  requires(!is_mirror_like_v<V> &&
           (std::is_void_v<T> || detail::can_bind_fields<T, V>()))
constexpr auto bind_mirror(V&& obj) {
  if constexpr (std::is_void_v<T>) {
    using Obj = std::remove_reference_t<V>;
    return mirror<Obj, Obj>(std::forward<V>(obj));
  } else {
    return mirror<T, std::remove_cvref_t<V>>(detail::by_name,
                                             std::forward<V>(obj));
  }
}

// Bare mirror<T> (no Src). Accepts an object or a mirror.
//   to_pure_mirror<T>(obj|mirror) -> mirror<T>
//   to_pure_mirror(obj)           -> mirror<remove_reference_t<obj>>
//   to_pure_mirror(mirror)        -> mirror<that mirror's T>
template <typename T = void, typename V>
constexpr auto to_pure_mirror(V&& v) {
  if constexpr (is_mirror_like_v<V>) {
    using FromT = mirror_target_t<std::remove_cvref_t<V>>;
    if constexpr (std::is_void_v<T>)
      return mirror<FromT>(std::forward<V>(v));
    else
      return mirror<T>(std::forward<V>(v));
  } else if constexpr (std::is_void_v<T>) {
    using Obj = std::remove_reference_t<V>;
    return mirror<Obj>(std::forward<V>(v));
  } else {
    return mirror<T>(std::forward<V>(v));
  }
}

ASF_END_NAMESPACE
