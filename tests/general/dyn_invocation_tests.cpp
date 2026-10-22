
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
  return asf_test::g_failures ? 1 : 0;
}
