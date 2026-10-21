// asf::dyn_cast (Trait RTTI). Requires ASF_HAS_DYN_RTTI=1.

#include "asf_check.hpp"
#include "mirror_bind.hpp"
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
struct [[=asf_ann::mirror]]
SomeMinimalTrait<void> {
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
struct [[=asf_ann::mirror]]
I1<void> {
  const char* m1(auto const& /*self*/) { return "m1"; }
};
template <>
struct [[=asf_ann::mirror]]
I2<void> : I1<void> {
  const char* m2(auto const& /*self*/) { return "m2"; }
};
template <>
struct [[=asf_ann::mirror]]
I3<void> : I2<void> {
  const char* m3(auto const& /*self*/) { return "m3"; }
};
template <>
struct [[=asf_ann::mirror]]
Unrelated<void> {
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
  auto leaf = asf_test::to_dyn_m<I3>(o);
  asf::dyn<I1> base = leaf;
  auto back = asf::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(back));
  ASF_CHECK_STREQ(back->m3(), "m3");
}

void TestDynCast_Cross_Succeed() {
  Obj o{};
  auto leaf = asf_test::to_dyn_m<I3>(o);
  asf::dyn<I1> base = leaf;
  auto mid = asf::dyn_cast<I2>(base);
  ASF_CHECK(static_cast<bool>(mid));
  ASF_CHECK_STREQ(mid->m2(), "m2");
}

void TestDynCast_Fail() {
  Obj o{};
  auto leaf = asf_test::to_dyn_m<I3>(o);
  asf::dyn<I1> base = leaf;
  auto miss = asf::dyn_cast<Unrelated>(base);
  ASF_CHECK(!miss);
}

void TestTryCast() {
  Obj o{};
  auto leaf = asf_test::to_dyn_m<I3>(o);
  asf::dyn<I1> base = leaf;
  auto ok = asf::dyn_cast<I3>(base);
  ASF_CHECK(static_cast<bool>(ok));
  auto bad = asf::dyn_cast<Unrelated>(base);
  ASF_CHECK(!bad);
}

void TestTypeid() {
  TypeTag a{17};
  auto p = asf_test::to_dyn_m<SomeMinimalTrait>(a);
  ASF_CHECK(asf::dyn_typeid(p) == typeid(TypeTag));
}

// The recommended way to expose a mirror through dyn: a plain dyn method
// whose impl names the concrete mirror view (no feature macro needed).
struct MirrorBox {
  int num{};
  std::string payload{asf_test::no_sso("mirror")};
};

template <typename T = void>
struct MirrorReadable;

template <>
struct [[=asf_ann::mirror]]
MirrorReadable<void> {
  int read(auto const& self);
};

template <>
struct MirrorReadable<MirrorBox> final {
  static int read(asf::c_mirror<MirrorBox> self) { return self.num; }
};

void TestMirrorMethod_Ref() {
  MirrorBox box{42};
  auto p = asf_test::to_dyn_m<MirrorReadable>(box);
  ASF_CHECK_EQ(p->read(), 42);
}

}  // namespace dyn_rtti_tests_detail

int main() {
  std::fprintf(stdout, "mirror/dyn_rtti_tests\n");
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Up_Succeed);
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Cross_Succeed);
  ASF_RUN(dyn_rtti_tests_detail::TestDynCast_Fail);
  ASF_RUN(dyn_rtti_tests_detail::TestTryCast);
  ASF_RUN(dyn_rtti_tests_detail::TestTypeid);
  ASF_RUN(dyn_rtti_tests_detail::TestMirrorMethod_Ref);
  return asf_test::g_failures ? 1 : 0;
}
