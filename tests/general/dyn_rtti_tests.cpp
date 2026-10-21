// asf::dyn_cast (Trait RTTI). Requires ASF_HAS_DYN_RTTI=1.

#include "asf_check.hpp"
#include "cast.hpp"

#include <typeinfo>

namespace dyn_rtti_tests_detail {

struct TypeTag {
  int n{};
  std::string payload{asf_test::no_sso("tag")};
};

template <typename T = void>
struct SomeMinimalTrait;

template <>
struct SomeMinimalTrait<void> {
  void mark(auto const& self) { (void)self; }
};

template <>
struct SomeMinimalTrait<TypeTag> final {};

template <typename T = void>
struct I1;
template <typename T = void>
struct I2;
template <typename T = void>
struct I3;
template <typename T = void>
struct Unrelated;

template <>
struct I1<void> {
  const char* m1(auto const& /*self*/) { return "m1"; }
};
template <>
struct I2<void> : I1<void> {
  const char* m2(auto const& /*self*/) { return "m2"; }
};
template <>
struct I3<void> : I2<void> {
  const char* m3(auto const& /*self*/) { return "m3"; }
};
template <>
struct Unrelated<void> {
  const char* mu(auto const& /*self*/) { return "mu"; }
};

struct Obj {
  int tag{};
  std::string payload{asf_test::no_sso("obj")};
};

template <>
struct I3<Obj> final {};
template <>
struct Unrelated<Obj> final {};

void TestDynCast_Up_Succeed() {
  Obj o{};
  auto leaf = asf::to_dyn<I3>(&o);
  asf::dyn<I1> base = leaf;
  auto back = asf::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(back));
  ASF_CHECK_STREQ(back->m3(), "m3");
}

void TestDynCast_Cross_Succeed() {
  Obj o{};
  auto leaf = asf::to_dyn<I3>(&o);
  asf::dyn<I1> base = leaf;
  auto mid = asf::dyn_cast<I2>(base);
  ASF_CHECK(static_cast<bool>(mid));
  ASF_CHECK_STREQ(mid->m2(), "m2");
}

void TestDynCast_Fail() {
  Obj o{};
  auto leaf = asf::to_dyn<I3>(&o);
  asf::dyn<I1> base = leaf;
  auto miss = asf::dyn_cast<Unrelated>(base);
  ASF_CHECK(!miss);
}

void TestTryCast() {
  Obj o{};
  auto leaf = asf::to_dyn<I3>(&o);
  asf::dyn<I1> base = leaf;
  auto ok = asf::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(ok));
  auto bad = asf::dyn_cast<Unrelated>(base);
  ASF_CHECK(!bad);
}

void TestTypeid() {
  TypeTag a{17};
  auto p = asf::to_dyn<SomeMinimalTrait>(&a);
  ASF_CHECK(asf::dyn_typeid(p) == typeid(TypeTag));
}

}  // namespace dyn_rtti_tests_detail

int main() {
  std::fprintf(stdout, "dyn_rtti_tests\n");
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Up_Succeed);
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Cross_Succeed);
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Fail);
  ASF_RUN(dyn_rtti_tests_detail::TestTryCast);
  ASF_RUN(dyn_rtti_tests_detail::TestTypeid);
  return asf_test::g_failures ? 1 : 0;
}
