#include "asf_check.hpp"
#include "test_bind.hpp"
#include "cast.hpp"

#include <memory>
#include <string>
#include <utility>

namespace cache_deref_tests_detail {

template <typename T = void>
struct Stringable;

template <>
struct ASF_TEST_TRAIT_MIRROR
Stringable<void> {
  std::string to_string(auto const& self);
  template <typename T>
  [[=asf_ann::imple]] T op_construct(ASF_TEST_COPYM(T) self) {
#if ASF_TEST_VIA_MIRROR
    return asf::value_of(self);
#else
    return self;
#endif
  }
  template <typename T>
  [[=asf_ann::imple]] T op_construct(ASF_TEST_RM(T) self) {
#if ASF_TEST_VIA_MIRROR
    return asf::value_of(self);
#else
    return std::move(self);
#endif
  }
};

struct Widget {
  int id = 0;
  std::string tag;
};

// User-defined adopt wrapper (same protocol + opt-in asf_adopt_proxy marker).
template <class Ptr>
class manual_cache_adopt {
  static_assert(
      requires(Ptr& p) {
        typename std::pointer_traits<Ptr>::element_type;
        { *p } -> std::convertible_to<
            typename std::pointer_traits<Ptr>::element_type&>;
      },
      "manual_cache_adopt requires a dereferenceable Ptr with pointer_traits");

 public:
  using ptr_type = Ptr;
  using pointer_type = Ptr;
  using element_type = typename std::pointer_traits<Ptr>::element_type;
  using asf_adopt_proxy = void;

  manual_cache_adopt() = default;
  explicit manual_cache_adopt(Ptr p) noexcept(
      std::is_nothrow_move_constructible_v<Ptr>)
      : ptr_(std::move(p)) {}

  Ptr& adopt_storage() noexcept { return ptr_; }
  Ptr const& adopt_storage() const noexcept { return ptr_; }

  static decltype(auto) pointee(Ptr const& storage) noexcept { return *storage; }

 private:
  Ptr ptr_{};
};

template <>
struct Stringable<Widget> final {
  static std::string to_string(ASF_TEST_CM(Widget) self) {
    return self.tag + ":" + std::to_string(self.id);
  }
};

}  // namespace cache_deref_tests_detail

int main() {
  using namespace cache_deref_tests_detail;
  using UPtr = std::unique_ptr<Widget>;
  using SPtr = std::shared_ptr<Widget>;
  constexpr std::size_t up_sz = sizeof(UPtr);
  constexpr std::size_t up_al = alignof(UPtr);
  constexpr std::size_t sp_sz = sizeof(SPtr);
  constexpr std::size_t sp_al = alignof(SPtr);

  static_assert(std::same_as<
                    asf::cache_storage_ops<UPtr>,
                    asf::unique_pointer_ops>);
  static_assert(std::same_as<
                    asf::cache_storage_ops<SPtr>,
                    asf::shared_pointer_ops>);
  static_assert(asf::cache_storage_size_v<UPtr> == up_sz);
  static_assert(std::same_as<decltype(asf::cache_storage_spec<UPtr>::ops()),
                             asf::unique_pointer_ops>);

  // cache_ptr Spec + cache_deref<Ptr>: fits, method deref, move-only shallow ok.
  {
    using Spec = asf::cache_ptr<up_sz, up_al>;
    asf::cache_deref wrap{std::make_unique<Widget>(Widget{701, asf_test::no_sso("U")})};
    static_assert(asf::detail::has_asf_adopt_proxy_v<decltype(wrap)>);
    static_assert(asf::detail::is_cache_adopt_wrapper_v<decltype(wrap)>);
    ASF_CHECK_EQ(asf::cache_deref<UPtr>::pointee(wrap.adopt_storage()).tag,
                 asf_test::no_sso("U"));
    ASF_CHECK_EQ(asf::cache_deref<UPtr>::pointee(wrap.adopt_storage()).id, 701);
    auto h = asf::to_dyn<Stringable, Spec>(std::move(wrap));
    ASF_CHECK(static_cast<bool>(h));
    ASF_CHECK_EQ(h->to_string(), asf_test::no_sso("U") + ":701");
  }

  // cache_deref alone: default Spec = sizeof/alignof(Ptr).
  {
    auto h = asf::to_dyn<Stringable>(asf::cache_deref{
        std::make_unique<Widget>(Widget{702, asf_test::no_sso("W")})});
    ASF_CHECK(static_cast<bool>(h));
    ASF_CHECK_EQ(h->to_string(), asf_test::no_sso("W") + ":702");
  }

  // External adopt wrapper (manual_cache_adopt): same to_dyn path as cache_deref.
  {
    static_assert(asf::detail::has_asf_adopt_proxy_v<manual_cache_adopt<UPtr>>);
    auto h = asf::to_dyn<Stringable>(manual_cache_adopt<UPtr>{
        std::make_unique<Widget>(Widget{708, asf_test::no_sso("E")})});
    ASF_CHECK(static_cast<bool>(h));
    ASF_CHECK_EQ(h->to_string(), asf_test::no_sso("E") + ":708");
  }

  // Larger cache_ptr Spec + cache_deref.
  {
    using Spec = asf::cache_ptr<up_sz + 8, up_al>;
    auto h = asf::to_dyn<Stringable, Spec>(asf::cache_deref{
        std::make_unique<Widget>(Widget{703, asf_test::no_sso("X")})});
    ASF_CHECK(static_cast<bool>(h));
    ASF_CHECK_EQ(h->to_string(), asf_test::no_sso("X") + ":703");
  }

  // shared_ptr wrap; VT distinct from value-in-cache path.
  {
    using Spec = asf::cache_ptr<sp_sz, sp_al>;
    auto sp = std::make_shared<Widget>(Widget{705, asf_test::no_sso("S")});
    auto hp = asf::to_dyn<Stringable, Spec>(asf::cache_deref{sp});
    ASF_CHECK(static_cast<bool>(hp));
    ASF_CHECK_EQ(hp->to_string(), asf_test::no_sso("S") + ":705");

    Widget val{705, asf_test::no_sso("S")};
    auto hc = asf::to_dyn<Stringable, asf::cache_ptr>(val);
    ASF_CHECK(static_cast<bool>(hc));
    ASF_CHECK_EQ(hc->to_string(), asf_test::no_sso("S") + ":705");
    ASF_CHECK_NE(asf::vtable_of(hp), asf::vtable_of(hc));
  }

  // Move of proxy-wrap cache_dyn (Trait move -> Ptr move).
  {
    using Spec = asf::cache_ptr<up_sz, up_al>;
    auto h = asf::to_dyn<Stringable, Spec>(asf::cache_deref{
        std::make_unique<Widget>(Widget{706, asf_test::no_sso("M")})});
    auto h2 = std::move(h);
    ASF_CHECK(static_cast<bool>(h2));
    ASF_CHECK_EQ(h2->to_string(), asf_test::no_sso("M") + ":706");
  }

  // to_dyn<Trait, cache_ptr>(wrap) template-template form.
  {
    auto h = asf::to_dyn<Stringable, asf::cache_ptr>(asf::cache_deref{
        std::make_unique<Widget>(Widget{707, asf_test::no_sso("T")})});
    ASF_CHECK(static_cast<bool>(h));
    ASF_CHECK_EQ(h->to_string(), asf_test::no_sso("T") + ":707");
  }

  return asf_test::g_failures;
}
