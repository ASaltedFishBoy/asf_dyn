// Handle lifetime for asf::dyn (void* / uptr). Skips SBO/inplace value poly.

#include "asf_check.hpp"
#include "mirror_bind.hpp"
#include "cast.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

struct lifetime_sub_del {
  void operator()(void* p) const noexcept { ::operator delete(p); }
};

namespace asf {
template <>
struct pointer_trait<std::unique_ptr<void, lifetime_sub_del>> {
  using kind = pointer_vt_destroy_tag;
  static void release(auto& h) { h.get().reset(); }
};
}

namespace dyn_lifetime_tests_detail {

template <typename T = void>
struct Stringable;

template <>
struct [[=asf_ann::mirror]]
Stringable<void> {
  std::string to_string(auto const& self);
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

struct Session {
  std::string id;
  std::string label;
  Session(int n, std::string_view lab)
      : id(asf_test::no_sso(std::to_string(n))),
        label(asf_test::no_sso(lab)) {}
};

template <>
struct Stringable<Session> final {
  static std::string to_string(asf::c_mirror<Session> self) {
    return self.label + " " + self.id;
  }
};

template <typename T = void>
struct Labeled;
template <>
struct [[=asf_ann::mirror]]
Labeled<void> {
  auto tag(auto const& self) -> std::string { return self.label; }
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  {
    (void)self.label;
    return self;
  }
};
template <>
struct Labeled<Session> final {};

template <typename T = void>
struct NoCtor;
template <>
struct NoCtor<void> {
  void ping(auto const&);
};

static_assert(asf_test::detect_size<asf::dyn<Stringable>>);
static_assert(asf_test::detect_align<asf::dyn<Stringable>>);
static_assert(!asf_test::detect_size<asf::dyn<NoCtor>>);
static_assert(!asf_test::detect_align<asf::dyn<NoCtor>>);
static_assert(std::is_destructible_v<asf::sub_dyn<Stringable>>);
static_assert(std::is_destructible_v<asf::unique_dyn<Stringable>>);
static_assert(!std::is_destructible_v<
              asf::sub_dyn<Stringable, std::unique_ptr<void>>>);
static_assert(std::is_destructible_v<
              asf::sub_dyn<Stringable, std::unique_ptr<void, lifetime_sub_del>>>);
static_assert(asf_test::detect_destroy<asf::dyn<Stringable>>);
static_assert(!asf_test::detect_destroy<asf::sub_dyn<Stringable>>);
#if ASF_HAS_DYN_RTTI
static_assert(asf_test::detect_typeid_of<asf::dyn<Stringable>>);
static_assert(!asf_test::detect_typeid_of<asf::sub_dyn<Stringable>>);
static_assert(!asf_test::detect_typeid_of<asf::c_sub_dyn<Stringable>>);
#endif

void TestDefaultBodyUsesMembers() {
  Session s{1, "lab"};
  auto p = asf::to_dyn<Labeled>(&s);
  ASF_CHECK_EQ(p->tag(), asf_test::no_sso("lab"));
  auto q = p;
  ASF_CHECK_EQ(q->tag(), asf_test::no_sso("lab"));
}

void TestDefaultConstruction() {
  asf::dyn<Stringable> p;
  ASF_CHECK(!p);
}

void TestNullConstruction_Nullptr() {
  asf::dyn<Stringable> p = nullptr;
  ASF_CHECK(!p);
}

void TestPolyConstruction_FromValue_NonOwning() {
  Session s{2, "Session"};
  auto p = asf_test::to_dyn_m<Stringable>(s);
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("Session", 2));
}

void TestCopyConstruction_FromValue() {
  Session s{2, "Session"};
  auto p1 = asf_test::to_dyn_m<Stringable>(s);
  auto p2 = p1;
  ASF_CHECK(static_cast<bool>(p1));
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK(p1 == p2);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("Session", 2));
}

void TestCopyConstruction_FromNull() {
  asf::dyn<Stringable> p1;
  auto p2 = p1;
  ASF_CHECK(!p1);
  ASF_CHECK(!p2);
}

void TestMoveConstruction_FromValue() {
  Session s{2, "Session"};
  auto p1 = asf_test::to_dyn_m<Stringable>(s);
  auto p2 = std::move(p1);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("Session", 2));
}

void TestMoveConstruction_FromNull() {
  asf::dyn<Stringable> p1;
  auto p2 = std::move(p1);
  ASF_CHECK(!p2);
}

void TestNullAssignment_FromNullptr_ToValue() {
  Session s{2, "Session"};
  auto p = asf_test::to_dyn_m<Stringable>(s);
  ASF_CHECK(static_cast<bool>(p));
  p = nullptr;
  ASF_CHECK(!p);
}

void TestNullAssignment_ToNull() {
  asf::dyn<Stringable> p;
  p = nullptr;
  ASF_CHECK(!p);
}

void TestCopyAssignment_FromValue_ToValue() {
  Session a{1, "A"};
  Session b{2, "B"};
  auto p1 = asf_test::to_dyn_m<Stringable>(a);
  auto p2 = asf_test::to_dyn_m<Stringable>(b);
  p2 = p1;
  ASF_CHECK(p1 == p2);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestCopyAssignment_FromValue_ToNull() {
  Session a{1, "A"};
  auto p1 = asf_test::to_dyn_m<Stringable>(a);
  asf::dyn<Stringable> p2;
  p2 = p1;
  ASF_CHECK(p1 == p2);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestCopyAssignment_FromNull_ToValue() {
  Session a{1, "A"};
  auto p1 = asf_test::to_dyn_m<Stringable>(a);
  asf::dyn<Stringable> p2;
  p1 = p2;
  ASF_CHECK(!p1);
}

void TestCopyAssignment_FromValue_ToSelf() {
  Session a{1, "A"};
  auto p = asf_test::to_dyn_m<Stringable>(a);
  p = p;
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("A", 1));
}

void TestMoveAssignment_FromValue_ToSelf() {
  Session a{1, "A"};
  auto p = asf_test::to_dyn_m<Stringable>(a);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
  p = std::move(p);
#pragma GCC diagnostic pop
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("A", 1));
}

void TestCopyAssignment_FromNull_ToNull() {
  asf::dyn<Stringable> p1;
  asf::dyn<Stringable> p2;
  p1 = p2;
  ASF_CHECK(!p1);
  ASF_CHECK(!p2);
}

void TestMoveAssignment_FromNull_ToNull() {
  asf::dyn<Stringable> p1;
  asf::dyn<Stringable> p2;
  p1 = std::move(p2);
  ASF_CHECK(!p1);
  ASF_CHECK(!p2);
}

void TestMoveAssignment_FromValue_ToValue() {
  Session a{1, "A"};
  Session b{2, "B"};
  auto p1 = asf_test::to_dyn_m<Stringable>(a);
  auto p2 = asf_test::to_dyn_m<Stringable>(b);
  p2 = std::move(p1);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestMoveAssignment_FromValue_ToNull() {
  Session a{1, "A"};
  auto p1 = asf_test::to_dyn_m<Stringable>(a);
  asf::dyn<Stringable> p2;
  p2 = std::move(p1);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestOperatorBool() {
  asf::dyn<Stringable> empty;
  ASF_CHECK(!empty);
  Session s{1, "S"};
  auto p = asf_test::to_dyn_m<Stringable>(s);
  ASF_CHECK(static_cast<bool>(p));
}

void TestEqualsToNullptr() {
  asf::dyn<Stringable> empty;
  ASF_CHECK(empty == nullptr);
  ASF_CHECK(nullptr == empty);
  Session s{1, "S"};
  auto p = asf_test::to_dyn_m<Stringable>(s);
  ASF_CHECK(p != nullptr);
  ASF_CHECK(nullptr != p);
}

void TestReset_FromValue() {
  Session s{1, "S"};
  auto p = asf_test::to_dyn_m<Stringable>(s);
  p.reset();
  ASF_CHECK(!p);
}

void TestReset_FromNull() {
  asf::dyn<Stringable> p;
  p.reset();
  ASF_CHECK(!p);
}

void TestSwap_Value_Value() {
  Session a{1, "A"};
  Session b{2, "B"};
  auto p1 = asf_test::to_dyn_m<Stringable>(a);
  auto p2 = asf_test::to_dyn_m<Stringable>(b);
  using std::swap;
  swap(p1, p2);
  ASF_CHECK_EQ(p1->to_string(), asf_test::session_text("B", 2));
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestSwap_Value_Null() {
  Session a{1, "A"};
  auto p1 = asf_test::to_dyn_m<Stringable>(a);
  asf::dyn<Stringable> p2;
  using std::swap;
  swap(p1, p2);
  ASF_CHECK(!p1);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestSwap_Value_Self() {
  Session a{1, "A"};
  auto p = asf_test::to_dyn_m<Stringable>(a);
  using std::swap;
  swap(p, p);
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("A", 1));
}

void TestSwap_Null_Value() {
  Session a{1, "A"};
  asf::dyn<Stringable> p1;
  auto p2 = asf_test::to_dyn_m<Stringable>(a);
  using std::swap;
  swap(p1, p2);
  ASF_CHECK(!p2);
  ASF_CHECK_EQ(p1->to_string(), asf_test::session_text("A", 1));
}

void TestSwap_Null_Null() {
  asf::dyn<Stringable> p1;
  asf::dyn<Stringable> p2;
  using std::swap;
  swap(p1, p2);
  ASF_CHECK(!p1);
  ASF_CHECK(!p2);
}

void TestHasValue() {
  asf::dyn<Stringable> empty;
  ASF_CHECK(!static_cast<bool>(empty));
  Session s{1, "S"};
  auto p = asf_test::to_dyn_m<Stringable>(s);
  ASF_CHECK(static_cast<bool>(p));
}

void TestSizeAlign() {
  asf::dyn<Stringable> empty;
  ASF_CHECK_EQ(empty.size(), 0);
  ASF_CHECK_EQ(empty.align(), 0);
  Session s{1, "S"};
  auto p = asf_test::to_dyn_m<Stringable>(s);
  ASF_CHECK_EQ(p.size(), sizeof(Session));
  ASF_CHECK_EQ(p.align(), alignof(Session));
  asf::c_dyn<Stringable> cp{p};
  ASF_CHECK_EQ(cp.size(), sizeof(Session));
  ASF_CHECK_EQ(cp.align(), alignof(Session));
}

void TestOwnedMove_Unique() {
  Session s{7, "Owned"};
  auto _m_s = asf::to_mirror(s);
  asf::dyn<Stringable> obs{_m_s};
  auto p1 = asf::construct<std::unique_ptr>(*obs);
  ASF_CHECK_EQ(p1->to_string(), asf_test::session_text("Owned", 7));
  auto p2 = std::move(p1);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK(!p1);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("Owned", 7));
}

void TestOwnedMove_Shared() {
  Session s{8, "SharedOwned"};
  auto _m_s = asf::to_mirror(s);
  asf::dyn<Stringable> obs{_m_s};
  auto p1 = asf::construct<std::shared_ptr>(*obs);
  ASF_CHECK_EQ(p1->to_string(), asf_test::session_text("SharedOwned", 8));
  auto p2 = std::move(p1);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK(!p1);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("SharedOwned", 8));
}

#if 1
void TestOwnedMove_Cache() {
  Session s{9, "CacheOwned"};
  auto d = asf::to_dyn<Stringable, asf::cache_ptr>(s);
  ASF_CHECK(static_cast<bool>(d));
  ASF_CHECK_EQ(d->to_string(), asf_test::session_text("CacheOwned", 9));
  auto p2 = std::move(d);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("CacheOwned", 9));
}
#endif

}  // namespace dyn_lifetime_tests_detail

int main() {
  std::fprintf(stdout, "dyn_lifetime_tests\n");
  ASF_RUN(dyn_lifetime_tests_detail::TestDefaultBodyUsesMembers);
  ASF_RUN(dyn_lifetime_tests_detail::TestDefaultConstruction);
  ASF_RUN(dyn_lifetime_tests_detail::TestNullConstruction_Nullptr);
  ASF_RUN(dyn_lifetime_tests_detail::TestPolyConstruction_FromValue_NonOwning);
  ASF_RUN(dyn_lifetime_tests_detail::TestCopyConstruction_FromValue);
  ASF_RUN(dyn_lifetime_tests_detail::TestCopyConstruction_FromNull);
  ASF_RUN(dyn_lifetime_tests_detail::TestMoveConstruction_FromValue);
  ASF_RUN(dyn_lifetime_tests_detail::TestMoveConstruction_FromNull);
  ASF_RUN(dyn_lifetime_tests_detail::TestNullAssignment_FromNullptr_ToValue);
  ASF_RUN(dyn_lifetime_tests_detail::TestNullAssignment_ToNull);
  ASF_RUN(dyn_lifetime_tests_detail::TestCopyAssignment_FromValue_ToValue);
  ASF_RUN(dyn_lifetime_tests_detail::TestCopyAssignment_FromValue_ToNull);
  ASF_RUN(dyn_lifetime_tests_detail::TestCopyAssignment_FromNull_ToValue);
  ASF_RUN(dyn_lifetime_tests_detail::TestCopyAssignment_FromValue_ToSelf);
  ASF_RUN(dyn_lifetime_tests_detail::TestMoveAssignment_FromValue_ToSelf);
  ASF_RUN(dyn_lifetime_tests_detail::TestCopyAssignment_FromNull_ToNull);
  ASF_RUN(dyn_lifetime_tests_detail::TestMoveAssignment_FromNull_ToNull);
  ASF_RUN(dyn_lifetime_tests_detail::TestMoveAssignment_FromValue_ToValue);
  ASF_RUN(dyn_lifetime_tests_detail::TestMoveAssignment_FromValue_ToNull);
  ASF_RUN(dyn_lifetime_tests_detail::TestOperatorBool);
  ASF_RUN(dyn_lifetime_tests_detail::TestEqualsToNullptr);
  ASF_RUN(dyn_lifetime_tests_detail::TestReset_FromValue);
  ASF_RUN(dyn_lifetime_tests_detail::TestReset_FromNull);
  ASF_RUN(dyn_lifetime_tests_detail::TestSwap_Value_Value);
  ASF_RUN(dyn_lifetime_tests_detail::TestSwap_Value_Null);
  ASF_RUN(dyn_lifetime_tests_detail::TestSwap_Value_Self);
  ASF_RUN(dyn_lifetime_tests_detail::TestSwap_Null_Value);
  ASF_RUN(dyn_lifetime_tests_detail::TestSwap_Null_Null);
  ASF_RUN(dyn_lifetime_tests_detail::TestHasValue);
  ASF_RUN(dyn_lifetime_tests_detail::TestSizeAlign);
  ASF_RUN(dyn_lifetime_tests_detail::TestOwnedMove_Unique);
  ASF_RUN(dyn_lifetime_tests_detail::TestOwnedMove_Shared);
#if 1
  ASF_RUN(dyn_lifetime_tests_detail::TestOwnedMove_Cache);
#endif
  return asf_test::g_failures ? 1 : 0;
}
