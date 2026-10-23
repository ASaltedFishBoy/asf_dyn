
#include "asf_check.hpp"
#include "test_bind.hpp"
#include "cast.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace dyn_invocation_tests_detail {

template <typename T = void>
struct Callable;

template <>
struct ASF_TEST_TRAIT_MIRROR
Callable<void> {
  int op_call(auto& self, std::string s, std::vector<int> v);
};

struct ForwardFn {
  std::string* arg1_received{};
  std::vector<int>* arg2_received{};
  int expected_result{};
};

template <>
struct Callable<ForwardFn> final {
  static int op_call(ASF_TEST_M(ForwardFn) self, std::string s,
                     std::vector<int> v) {
    *self.arg1_received = std::move(s);
    *self.arg2_received = std::move(v);
    return self.expected_result;
  }
};

template <typename T = void>
struct VoidCallable;

template <>
struct ASF_TEST_TRAIT_MIRROR
VoidCallable<void> {
  void op_call(auto& self);
};

struct ThrowFn {
  const char* msg{};
};

template <>
struct VoidCallable<ThrowFn> final {
  static void op_call(ASF_TEST_M(ThrowFn) self) {
    throw std::runtime_error{self.msg};
  }
};

template <typename T = void>
struct Iterable;

template <>
struct ASF_TEST_TRAIT_MIRROR
Iterable<void> {
  std::size_t size(auto const& self);
  void for_each(auto& self, void (*fn)(int&));
};

struct IntList {
  std::vector<int> data;
};

template <>
struct Iterable<IntList> final {
  static std::size_t size(ASF_TEST_CM(IntList) self) {
    return self.data.size();
  }
  static void for_each(ASF_TEST_M(IntList) self, void (*fn)(int&)) {
    for (int& x : self.data)
      fn(x);
  }
};

template <typename T = void>
struct AtDict;

template <>
struct ASF_TEST_TRAIT_MIRROR
AtDict<void> {
  std::string at(auto const& self, int i);
};

struct StrVec {
  std::vector<std::string> data;
};

template <>
struct AtDict<StrVec> final {
  static std::string at(ASF_TEST_CM(StrVec) self, int i) {
    return self.data.at(static_cast<std::size_t>(i));
  }
};

void TestArgumentForwarding() {
  std::string arg1 = asf_test::no_sso("My string");
  std::vector<int> arg2 = {1, 2, 3};
  std::vector<int> arg2_copy = arg2;
  std::string arg1_received;
  std::vector<int> arg2_received;
  int expected_result = 456;
  ForwardFn f{&arg1_received, &arg2_received, expected_result};
  auto p = asf_test::to_dyn<Callable>(f);
  int result = (*p)(arg1, std::move(arg2));
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(arg1_received, arg1);
  ASF_CHECK(arg2.empty());
  ASF_CHECK_EQ(arg2_received, arg2_copy);
  ASF_CHECK_EQ(result, expected_result);
}

void TestThrow() {
  const char* expected_error_message = "My exception";
  ThrowFn f{expected_error_message};
  auto p = asf_test::to_dyn<VoidCallable>(f);
  bool exception_thrown = false;
  try {
    (*p)();
  } catch (const std::runtime_error& e) {
    exception_thrown = true;
    ASF_CHECK_STREQ(e.what(), expected_error_message);
  }
  ASF_CHECK(exception_thrown);
  ASF_CHECK(static_cast<bool>(p));
}

void TestMultipleDispatches_Unique() {
  IntList l{{1, 2, 3}};
  auto p = asf_test::to_dyn<Iterable>(l);
  ASF_CHECK_EQ(p->size(), std::size_t{3});
  static int sum;
  sum = 0;
  p->for_each(+[](int& x) { sum += x; });
  ASF_CHECK_EQ(sum, 6);
}

void TestMemberDispatch() {
  StrVec container1{{asf_test::no_sso("hello"), asf_test::no_sso("world"),
                     asf_test::no_sso("!")}};
  auto p = asf_test::to_dyn<AtDict>(container1);
  ASF_CHECK_EQ(p->at(0), asf_test::no_sso("hello"));
}

struct OverloadRecorder {
  int last_tag{};
  int last_i{};
  double last_d{};
  std::string last_s;
  std::string last_owned;
};

template <typename T = void>
struct OverloadCallable;

template <>
struct ASF_TEST_TRAIT_MIRROR
OverloadCallable<void> {
  void op_call(auto& self, int x);
  void op_call(auto& self, double x);
  void op_call(auto& self, const char* s);
  void op_call(auto& self, char* s);
  void op_call(auto& self, std::string s, int n);
};

template <>
struct OverloadCallable<OverloadRecorder> final {
  static void op_call(ASF_TEST_M(OverloadRecorder) self, int x) {
    self.last_tag = 1;
    self.last_i = x;
  }
  static void op_call(ASF_TEST_M(OverloadRecorder) self, double x) {
    self.last_tag = 2;
    self.last_d = x;
  }
  static void op_call(ASF_TEST_M(OverloadRecorder) self, const char* s) {
    self.last_tag = 3;
    self.last_s = s;
  }
  static void op_call(ASF_TEST_M(OverloadRecorder) self, char* s) {
    self.last_tag = 4;
    self.last_s = s ? s : "";
  }
  static void op_call(ASF_TEST_M(OverloadRecorder) self, std::string s, int n) {
    self.last_tag = 5;
    self.last_owned = std::move(s);
    self.last_i = n;
  }
};

void TestOverloadResolution_Member() {
  OverloadRecorder rec{};
  auto p = asf_test::to_dyn<OverloadCallable>(rec);
  (*p)(42);
  ASF_CHECK_EQ(rec.last_tag, 1);
  ASF_CHECK_EQ(rec.last_i, 42);
  (*p)(3.14);
  ASF_CHECK_EQ(rec.last_tag, 2);
  ASF_CHECK_EQ(rec.last_d, 3.14);
  (*p)(static_cast<const char*>(asf_test::kNoSso));
  ASF_CHECK_EQ(rec.last_tag, 3);
  ASF_CHECK_STREQ(rec.last_s, asf_test::kNoSso);
  char mut[] = "mutable-payload-x";
  (*p)(mut);
  ASF_CHECK_EQ(rec.last_tag, 4);
  ASF_CHECK_STREQ(rec.last_s, "mutable-payload-x");
  (*p)(asf_test::no_sso("own"), 7);
  ASF_CHECK_EQ(rec.last_tag, 5);
  ASF_CHECK_EQ(rec.last_owned, asf_test::no_sso("own"));
  ASF_CHECK_EQ(rec.last_i, 7);
}

void TestMultipleDispatches_Duplicated() {
  // trait_methods dedups same name; Iterable still exposes size + for_each once.
  IntList l{{4, 5, 6}};
  auto p = asf_test::to_dyn<Iterable>(l);
  ASF_CHECK_EQ(p->size(), std::size_t{3});
  static int sum;
  sum = 0;
  p->for_each(+[](int& x) { sum += x; });
  ASF_CHECK_EQ(sum, 15);
}

struct IntBox {
  int num{};
  std::string payload{asf_test::no_sso("intbox")};
};

template <typename T = void>
struct IntStringable;

template <>
struct ASF_TEST_TRAIT_MIRROR
IntStringable<void> {
  std::string to_string(auto const& self);
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

template <>
struct IntStringable<IntBox> final {
  static std::string to_string(ASF_TEST_CM(IntBox) self) {
    return "int:" + std::to_string(self.num);
  }
};

void TestObserverDispatch() {
  auto sp = std::make_shared<IntBox>(IntBox{99});
  auto owned = asf::to_dyn<IntStringable>(sp);
  ASF_CHECK_EQ(owned->to_string(), "int:99");
  asf::dyn<IntStringable, std::weak_ptr<void>> weak{owned};
  ASF_CHECK(static_cast<bool>(weak));
  {
    auto locked = weak.lock();
    ASF_CHECK(static_cast<bool>(locked));
    ASF_CHECK_EQ(locked->to_string(), "int:99");
  }
  owned.reset();
  sp.reset();
  ASF_CHECK(weak.expired());
  ASF_CHECK(!weak.lock());
}

// imple order: Trait<T> -> imple(T) -> Trait<void>. Unique tags so a wrong
// layer cannot hide behind a matching value.
inline constexpr int k_imple_trait_t = 5101;
inline constexpr int k_imple_from_t = 5102;
inline constexpr int k_imple_default = 5103;
inline constexpr int k_no_imple_default = 5104;
inline constexpr int k_trait_level_from_t = 5105;
inline constexpr int k_imple_over_int = 5106;
inline constexpr int k_imple_over_cstr = 5107;
inline constexpr int k_skip_src_default = 5108;
inline constexpr int k_skip_src_unused = 5199;

template <typename T = void>
struct ImpleOrder;

// Trait<void> final: un-specialized T still emits a vt (no empty Trait<T>).
template <>
struct ASF_TEST_TRAIT_MIRROR
ImpleOrder<void> final {
  [[=asf_ann::imple]] int tag(auto const&) { return k_imple_default; }
};

struct ImpleOrderBoth {
  int tag() const { return k_imple_from_t; }
};

template <>
struct ImpleOrder<ImpleOrderBoth> {
  static int tag(ASF_TEST_CM(ImpleOrderBoth)) { return k_imple_trait_t; }
};

struct ImpleOrderSrcOnly {
  int tag() const { return k_imple_from_t; }
};

template <typename T = void>
struct ImpleSkipSrc;

template <>
struct ASF_TEST_TRAIT_MIRROR
ImpleSkipSrc<void> final {
  // Not imple: object's tag must not be selected.
  int tag(auto const&) { return k_skip_src_default; }
};

struct ImpleSkipSrcObj {
  int tag() const { return k_skip_src_unused; }
};

template <typename T = void>
struct ImpleTraitLevel;

template <>
struct [[=asf_ann::imple]] ASF_TEST_TRAIT_MIRROR
ImpleTraitLevel<void> final {
  int a(auto const&) { return k_imple_default; }
  [[=asf_ann::no_imple]] int b(auto const&) { return k_no_imple_default; }
};

struct ImpleTraitLevelObj {
  int a() const { return k_trait_level_from_t; }
  int b() const { return k_skip_src_unused; }
};

template <typename T = void>
struct ImpleOverload;

template <>
struct ASF_TEST_TRAIT_MIRROR
ImpleOverload<void> final {
  [[=asf_ann::imple]] int pick(auto const&, int) { return k_imple_default; }
  [[=asf_ann::imple]] int pick(auto const&, const char*) {
    return k_imple_default;
  }
};

struct ImpleOverloadObj {
  int pick(int) const { return k_imple_over_int; }
  int pick(const char*) const { return k_imple_over_cstr; }
};

void TestImpleOrder_TraitTWins() {
  ImpleOrderBoth o{};
  auto p = asf_test::to_dyn<ImpleOrder>(o);
  ASF_CHECK_EQ(p->tag(), k_imple_trait_t);
  auto i = asf_test::to_iface<ImpleOrder>(o);
  ASF_CHECK_EQ(i->tag(), k_imple_trait_t);
}

void TestImpleOrder_ImpleWins() {
  ImpleOrderSrcOnly o{};
  auto p = asf_test::to_dyn<ImpleOrder>(o);
  ASF_CHECK_EQ(p->tag(), k_imple_from_t);
  auto i = asf_test::to_iface<ImpleOrder>(o);
  ASF_CHECK_EQ(i->tag(), k_imple_from_t);
}

void TestImpleOrder_DefaultWithoutImple() {
  ImpleSkipSrcObj o{};
  auto p = asf_test::to_dyn<ImpleSkipSrc>(o);
  ASF_CHECK_EQ(p->tag(), k_skip_src_default);
  auto i = asf_test::to_iface<ImpleSkipSrc>(o);
  ASF_CHECK_EQ(i->tag(), k_skip_src_default);
}

void TestImpleOrder_TraitLevelAndNoImple() {
  ImpleTraitLevelObj o{};
  auto p = asf_test::to_dyn<ImpleTraitLevel>(o);
  ASF_CHECK_EQ(p->a(), k_trait_level_from_t);
  ASF_CHECK_EQ(p->b(), k_no_imple_default);
  auto i = asf_test::to_iface<ImpleTraitLevel>(o);
  ASF_CHECK_EQ(i->a(), k_trait_level_from_t);
  ASF_CHECK_EQ(i->b(), k_no_imple_default);
}

void TestImpleOrder_OverloadOnT() {
  ImpleOverloadObj o{};
  auto p = asf_test::to_dyn<ImpleOverload>(o);
  ASF_CHECK_EQ(p->pick(0), k_imple_over_int);
  ASF_CHECK_EQ(p->pick("x"), k_imple_over_cstr);
  auto i = asf_test::to_iface<ImpleOverload>(o);
  ASF_CHECK_EQ(i->pick(1), k_imple_over_int);
  ASF_CHECK_EQ(i->pick("y"), k_imple_over_cstr);
}

// imple object-member const / ref-qual vs Trait self: const must match;
// lvalue self matches unqualified or `&`; rvalue self needs `&&`. Mismatch
// falls through to Trait<void> (unique tags).
inline constexpr int k_qual_default = 5110;
inline constexpr int k_qual_unqual = 5111;
inline constexpr int k_qual_lref = 5112;
inline constexpr int k_qual_rref = 5113;
inline constexpr int k_qual_const = 5114;
inline constexpr int k_qual_const_lref = 5115;
inline constexpr int k_qual_const_rref = 5116;

template <typename T = void>
struct ImpleQualL;

template <>
struct ASF_TEST_TRAIT_MIRROR
ImpleQualL<void> final {
  [[=asf_ann::imple]] int tag(auto&) { return k_qual_default; }
};

struct ImpleQualUnqual {
  int tag() { return k_qual_unqual; }
};

struct ImpleQualLRef {
  int tag() & { return k_qual_lref; }
};

struct ImpleQualRRefOnly {
  int tag() && { return k_qual_rref; }
};

struct ImpleQualConstOnly {
  int tag() const { return k_qual_const; }
};

template <typename T = void>
struct ImpleQualC;

template <>
struct ASF_TEST_TRAIT_MIRROR
ImpleQualC<void> final {
  [[=asf_ann::imple]] int tag(auto const&) { return k_qual_default; }
};

struct ImpleQualMutOnly {
  int tag() { return k_qual_unqual; }
};

struct ImpleQualConstLRef {
  int tag() const& { return k_qual_const_lref; }
};

template <typename T = void>
struct ImpleQualR;

template <>
struct ASF_TEST_TRAIT_MIRROR
ImpleQualR<void> final {
  [[=asf_ann::imple]] int tag(auto&&) { return k_qual_default; }
};

struct ImpleQualRRef {
  int tag() && { return k_qual_rref; }
};

struct ImpleQualLRefOnly {
  int tag() & { return k_qual_lref; }
};

template <typename T = void>
struct ImpleQualCR;

template <>
struct ASF_TEST_TRAIT_MIRROR
ImpleQualCR<void> final {
  [[=asf_ann::imple]] int tag(auto const&&) { return k_qual_default; }
};

struct ImpleQualConstRRef {
  int tag() const&& { return k_qual_const_rref; }
};

void TestImpleQual_LvalueUnqual() {
  ImpleQualUnqual o{};
  auto p = asf_test::to_dyn<ImpleQualL>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_unqual);
  auto i = asf_test::to_iface<ImpleQualL>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_unqual);
}

void TestImpleQual_LvalueRefQual() {
  ImpleQualLRef o{};
  auto p = asf_test::to_dyn<ImpleQualL>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_lref);
  auto i = asf_test::to_iface<ImpleQualL>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_lref);
}

void TestImpleQual_LvalueRejectsRvalueMem() {
  ImpleQualRRefOnly o{};
  auto p = asf_test::to_dyn<ImpleQualL>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_default);
  auto i = asf_test::to_iface<ImpleQualL>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_default);
}

void TestImpleQual_LvalueRejectsConstMem() {
  ImpleQualConstOnly o{};
  auto p = asf_test::to_dyn<ImpleQualL>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_default);
  auto i = asf_test::to_iface<ImpleQualL>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_default);
}

void TestImpleQual_ConstMatch() {
  ImpleQualConstOnly o{};
  auto p = asf_test::to_dyn<ImpleQualC>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_const);
  auto i = asf_test::to_iface<ImpleQualC>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_const);
}

void TestImpleQual_ConstLRefMatch() {
  ImpleQualConstLRef o{};
  auto p = asf_test::to_dyn<ImpleQualC>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_const_lref);
  auto i = asf_test::to_iface<ImpleQualC>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_const_lref);
}

void TestImpleQual_ConstRejectsMutMem() {
  ImpleQualMutOnly o{};
  auto p = asf_test::to_dyn<ImpleQualC>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_default);
  auto i = asf_test::to_iface<ImpleQualC>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_default);
}

void TestImpleQual_RvalueRefQual() {
  ImpleQualRRef o{};
  auto p = asf_test::to_dyn<ImpleQualR>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_rref);
  auto i = asf_test::to_iface<ImpleQualR>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_rref);
}

void TestImpleQual_RvalueRejectsLvalueMem() {
  ImpleQualLRefOnly o{};
  auto p = asf_test::to_dyn<ImpleQualR>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_default);
  auto i = asf_test::to_iface<ImpleQualR>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_default);
}

void TestImpleQual_ConstRvalueMatch() {
  ImpleQualConstRRef o{};
  auto p = asf_test::to_dyn<ImpleQualCR>(o);
  ASF_CHECK_EQ(p->tag(), k_qual_const_rref);
  auto i = asf_test::to_iface<ImpleQualCR>(o);
  ASF_CHECK_EQ(i->tag(), k_qual_const_rref);
}

}  // namespace dyn_invocation_tests_detail

int main() {
  std::fprintf(stdout, "dyn_invocation_tests\n");
  ASF_RUN(dyn_invocation_tests_detail::TestArgumentForwarding);
  ASF_RUN(dyn_invocation_tests_detail::TestThrow);
  ASF_RUN(dyn_invocation_tests_detail::TestMultipleDispatches_Unique);
  ASF_RUN(dyn_invocation_tests_detail::TestMemberDispatch);
  ASF_RUN(dyn_invocation_tests_detail::TestOverloadResolution_Member);
  ASF_RUN(dyn_invocation_tests_detail::TestMultipleDispatches_Duplicated);
  ASF_RUN(dyn_invocation_tests_detail::TestObserverDispatch);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleOrder_TraitTWins);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleOrder_ImpleWins);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleOrder_DefaultWithoutImple);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleOrder_TraitLevelAndNoImple);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleOrder_OverloadOnT);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_LvalueUnqual);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_LvalueRefQual);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_LvalueRejectsRvalueMem);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_LvalueRejectsConstMem);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_ConstMatch);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_ConstLRefMatch);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_ConstRejectsMutMem);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_RvalueRefQual);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_RvalueRejectsLvalueMem);
  ASF_RUN(dyn_invocation_tests_detail::TestImpleQual_ConstRvalueMatch);
  return asf_test::g_failures ? 1 : 0;
}
