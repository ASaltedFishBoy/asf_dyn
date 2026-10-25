#include "asf_check.hpp"
#include "test_bind.hpp"
#include "cast.hpp"

#include <memory>
#include <string>
#include <utility>

namespace proxy_wrap_tests_detail {

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

template <>
struct Stringable<Widget> final {
  static std::string to_string(ASF_TEST_CM(Widget) self) {
    return self.tag + ":" + std::to_string(self.id);
  }
};

}  // namespace proxy_wrap_tests_detail

int main() {
  using namespace proxy_wrap_tests_detail;
  using UPtr = std::unique_ptr<Widget>;
  using SPtr = std::shared_ptr<Widget>;
  constexpr std::size_t up_sz = sizeof(UPtr);
  constexpr std::size_t up_al = alignof(UPtr);
  constexpr std::size_t sp_sz = sizeof(SPtr);
  constexpr std::size_t sp_al = alignof(SPtr);

  // cache_ptr Spec + proxy_wrap<Ptr>: fits, method deref, move-only shallow ok.
  {
    using Spec = asf::cache_ptr<up_sz, up_al>;
    auto h = asf::to_dyn<Stringable, Spec>(asf::proxy_wrap{
        std::make_unique<Widget>(Widget{701, asf_test::no_sso("U")})});
    ASF_CHECK(static_cast<bool>(h));
    ASF_CHECK_EQ(h->to_string(), asf_test::no_sso("U") + ":701");
  }

  // proxy_wrap alone: default Spec = sizeof/alignof(Ptr).
  {
    auto h = asf::to_dyn<Stringable>(asf::proxy_wrap{
        std::make_unique<Widget>(Widget{702, asf_test::no_sso("W")})});
    ASF_CHECK(static_cast<bool>(h));
    ASF_CHECK_EQ(h->to_string(), asf_test::no_sso("W") + ":702");
  }

  // Larger cache_ptr Spec + proxy_wrap.
  {
    using Spec = asf::cache_ptr<up_sz + 8, up_al>;
    auto h = asf::to_dyn<Stringable, Spec>(asf::proxy_wrap{
        std::make_unique<Widget>(Widget{703, asf_test::no_sso("X")})});
    ASF_CHECK(static_cast<bool>(h));
    ASF_CHECK_EQ(h->to_string(), asf_test::no_sso("X") + ":703");
  }

  // shared_ptr wrap; VT distinct from value-in-cache path.
  {
    using Spec = asf::cache_ptr<sp_sz, sp_al>;
    auto sp = std::make_shared<Widget>(Widget{705, asf_test::no_sso("S")});
    auto hp = asf::to_dyn<Stringable, Spec>(asf::proxy_wrap{sp});
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
    auto h = asf::to_dyn<Stringable, Spec>(asf::proxy_wrap{
        std::make_unique<Widget>(Widget{706, asf_test::no_sso("M")})});
    auto h2 = std::move(h);
    ASF_CHECK(static_cast<bool>(h2));
    ASF_CHECK_EQ(h2->to_string(), asf_test::no_sso("M") + ":706");
  }

  // to_dyn<Trait, cache_ptr>(wrap) template-template form.
  {
    auto h = asf::to_dyn<Stringable, asf::cache_ptr>(asf::proxy_wrap{
        std::make_unique<Widget>(Widget{707, asf_test::no_sso("T")})});
    ASF_CHECK(static_cast<bool>(h));
    ASF_CHECK_EQ(h->to_string(), asf_test::no_sso("T") + ":707");
  }

  return asf_test::g_failures;
}
