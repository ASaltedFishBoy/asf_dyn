// Non-owning asf::dyn / view of owning unique_dyn.

#include "asf_check.hpp"
#include "mirror_bind.hpp"
#include "cast.hpp"

#include <string>

namespace dyn_view_tests_detail {

struct IntBox {
  int num{};
  std::string payload{asf_test::no_sso("intbox")};
};

template <typename T = void>
struct MutStringable;

template <>
struct [[=asf_ann::mirror]]
MutStringable<void> {
  void op_iadd(auto& self, int n);
  std::string to_string(auto const& self);
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

template <>
struct MutStringable<IntBox> final {
  static void op_iadd(asf::mirror<IntBox> self, int n) { self.num += n; }
  static std::string to_string(asf::c_mirror<IntBox> self) {
    return std::to_string(self.num);
  }
};

template <typename T = void>
struct CallMut;
template <typename T = void>
struct CallConst;

struct CallImpl {
  int num{};
  int extra{};
  std::string payload{asf_test::no_sso("call")};
};

template <>
struct [[=asf_ann::mirror]]
CallMut<void> {
  int op_call(auto& self) {
    if constexpr (requires { self.num; })
      return 0;
    else
      return -1;
  }
};
template <>
struct [[=asf_ann::mirror]]
CallConst<void> {
  int op_call(auto const& self) {
    if constexpr (requires { self.extra; })
      return 1;
    else
      return -1;
  }
};

template <>
struct CallMut<CallImpl> final {};
template <>
struct CallConst<CallImpl> final {};

template <typename T = void>
struct BaseToString;

template <>
struct [[=asf_ann::mirror]]
BaseToString<void> {
  std::string to_string(auto const& self) {
    if constexpr (requires {
                    { self.num + 0 } -> std::convertible_to<int>;
                  })
      return std::to_string(self.num + 0);
    else {
      (void)self;
      return {};
    }
  }
};

template <typename T = void>
struct DerivedMut;

template <>
struct [[=asf_ann::mirror]]
DerivedMut<void> : BaseToString<void> {
  void op_iadd(auto& self, int n) {
    if constexpr (requires { self.num += 0; })
      self.num += n;
    else
      (void)self, (void)n;
  }
};

template <>
struct DerivedMut<IntBox> final {};

void TestViewOfNull() {
  asf::dyn<MutStringable> p1;
  asf::dyn<MutStringable> p2 = p1;
  ASF_CHECK(!p2);
}

void TestViewIndependentUse() {
  IntBox a{123};
  auto p = asf_test::to_dyn_m<MutStringable>(a);
  (*p) += 3;
  ASF_CHECK_EQ(p->to_string(), "126");
  ASF_CHECK_EQ(a.num, 126);
}

void TestViewOfOwning() {
  IntBox src{123};
  auto _m_src = asf::to_mirror(src);
  asf::dyn<MutStringable> obs{_m_src};
  auto p1 = asf::construct<std::unique_ptr>(*obs);
  asf::dyn<MutStringable> p2 =
      asf::to_dyn<MutStringable>(static_cast<IntBox*>(p1.ptr()));
  ASF_CHECK(static_cast<bool>(p1));
  ASF_CHECK(static_cast<bool>(p2));
  (*p2) += 3;
  ASF_CHECK_EQ(p1->to_string(), "126");
  p1.reset();
}

void TestViewOfNonOwning() {
  IntBox a{123};
  auto p1 = asf_test::to_dyn_m<MutStringable>(a);
  auto p2 = p1;
  ASF_CHECK(static_cast<bool>(p1));
  ASF_CHECK(static_cast<bool>(p2));
  (*p2) += 3;
  ASF_CHECK_EQ(p1->to_string(), "126");
  p1.reset();
  ASF_CHECK_EQ(p2->to_string(), "126");
  ASF_CHECK_EQ(a.num, 126);
}

void TestOverloadShadowing() {
  CallImpl impl{};
  auto p1 = asf_test::to_dyn_m<CallMut>(impl);
  auto p2 = asf_test::to_c_dyn_m<CallConst>(impl);
  ASF_CHECK_EQ((*p1)(), 0);
  ASF_CHECK_EQ((*p2)(), 1);
}

void TestSubstitution_FromNull() {
  asf::dyn<DerivedMut> p1;
  asf::dyn<BaseToString> p3 = p1;
  ASF_CHECK(!p1);
  ASF_CHECK(!p3);
}

void TestSubstitution_FromValue() {
  IntBox v{123};
  auto p1 = asf_test::to_dyn_m<DerivedMut>(v);
  asf::dyn<BaseToString> p3 = p1;
  ASF_CHECK_EQ(p1.as<BaseToString>()->to_string(), "123");
  ASF_CHECK_EQ(p3->to_string(), "123");
}

}  // namespace dyn_view_tests_detail

int main() {
  std::fprintf(stdout, "dyn_view_tests\n");
  ASF_RUN(dyn_view_tests_detail::TestViewOfNull);
  ASF_RUN(dyn_view_tests_detail::TestViewIndependentUse);
  ASF_RUN(dyn_view_tests_detail::TestViewOfOwning);
  ASF_RUN(dyn_view_tests_detail::TestViewOfNonOwning);
  ASF_RUN(dyn_view_tests_detail::TestOverloadShadowing);
  ASF_RUN(dyn_view_tests_detail::TestSubstitution_FromNull);
  ASF_RUN(dyn_view_tests_detail::TestSubstitution_FromValue);
  return asf_test::g_failures ? 1 : 0;
}
