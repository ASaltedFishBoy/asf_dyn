// One Trait method name per op (fn cannot overload same name).
// Bodies live in Trait<IntBox>; Trait<void> may also have default bodies
// that access members (declaration probe, no dummy Self).

#include "asf_check.hpp"
#include "test_bind.hpp"
#include "cast.hpp"

#include <compare>
#include <vector>

namespace dyn_dispatch_tests_detail {

struct IntBox {
  int num{};
  std::string payload{asf_test::no_sso("intbox")};
};

template <typename T = void>
struct OpPlus;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpPlus<void> {
  int op_add(auto const& self, int val);
};
template <>
struct OpPlus<IntBox> final {
  static int op_add(ASF_TEST_CM(IntBox) self, int val) {
    return self.num + val;
  }
};

template <typename T = void>
struct OpMinus;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpMinus<void> {
  int op_neg(auto const& self);
  int op_sub(auto const& self, int val);
};
template <>
struct OpMinus<IntBox> final {
  static int op_neg(ASF_TEST_CM(IntBox) self) { return -self.num; }
  static int op_sub(ASF_TEST_CM(IntBox) self, int val) {
    return self.num - val;
  }
};

template <typename T = void>
struct OpMul;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpMul<void> {
  int op_mul(auto const& self, int val);
};
template <>
struct OpMul<IntBox> final {
  static int op_mul(ASF_TEST_CM(IntBox) self, int val) {
    return self.num * val;
  }
};

template <typename T = void>
struct OpDiv;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpDiv<void> {
  int op_div(auto const& self, int val);
};
template <>
struct OpDiv<IntBox> final {
  static int op_div(ASF_TEST_CM(IntBox) self, int val) {
    return self.num / val;
  }
};

template <typename T = void>
struct OpMod;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpMod<void> {
  int op_mod(auto const& self, int val);
};
template <>
struct OpMod<IntBox> final {
  static int op_mod(ASF_TEST_CM(IntBox) self, int val) {
    return self.num % val;
  }
};

template <typename T = void>
struct OpInc;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpInc<void> {
  int op_inc(auto& self) {
    if constexpr (requires { self.num += 0; })
      return ++self.num;
    else {
      (void)self;
      return 0;
    }
  }
  int op_inc(auto& self, int) {
    if constexpr (requires { self.num += 0; })
      return self.num++;
    else {
      (void)self;
      return 0;
    }
  }
};
template <>
struct OpInc<IntBox> final {};

template <typename T = void>
struct OpDec;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpDec<void> {
  int op_dec(auto& self) {
    if constexpr (requires { self.num += 0; })
      return --self.num;
    else {
      (void)self;
      return 0;
    }
  }
  int op_dec(auto& self, int) {
    if constexpr (requires { self.num += 0; })
      return self.num--;
    else {
      (void)self;
      return 0;
    }
  }
};
template <>
struct OpDec<IntBox> final {};

template <typename T = void>
struct OpEq;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpEq<void> {
  bool op_eq(auto const& self, int val);
  bool op_ne(auto const& self, int val);
  bool op_gt(auto const& self, int val);
  bool op_lt(auto const& self, int val);
  bool op_ge(auto const& self, int val);
  bool op_le(auto const& self, int val);
  std::strong_ordering op_cmp(auto const& self, int val);
};
template <>
struct OpEq<IntBox> final {
  static bool op_eq(ASF_TEST_CM(IntBox) self, int val) {
    return self.num == val;
  }
  static bool op_ne(ASF_TEST_CM(IntBox) self, int val) {
    return self.num != val;
  }
  static bool op_gt(ASF_TEST_CM(IntBox) self, int val) {
    return self.num > val;
  }
  static bool op_lt(ASF_TEST_CM(IntBox) self, int val) {
    return self.num < val;
  }
  static bool op_ge(ASF_TEST_CM(IntBox) self, int val) {
    return self.num >= val;
  }
  static bool op_le(ASF_TEST_CM(IntBox) self, int val) {
    return self.num <= val;
  }
  static std::strong_ordering op_cmp(ASF_TEST_CM(IntBox) self, int val) {
    return self.num <=> val;
  }
};

template <typename T = void>
struct OpLogic;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpLogic<void> {
  bool op_not(auto const& self);
  bool op_and(auto const& self, int val);
  bool op_or(auto const& self, int val);
};
template <>
struct OpLogic<IntBox> final {
  static bool op_not(ASF_TEST_CM(IntBox) self) { return !self.num; }
  static bool op_and(ASF_TEST_CM(IntBox) self, int val) {
    return self.num && val;
  }
  static bool op_or(ASF_TEST_CM(IntBox) self, int val) {
    return self.num || val;
  }
};

template <typename T = void>
struct OpBit;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpBit<void> {
  int op_compl(auto const& self);
  int op_bitand(auto const& self, int val);
  int op_bitor(auto const& self, int val);
  int op_xor(auto const& self, int val);
  int op_lshift(auto const& self, int val);
  int op_rshift(auto const& self, int val);
};
template <>
struct OpBit<IntBox> final {
  static int op_compl(ASF_TEST_CM(IntBox) self) { return ~self.num; }
  static int op_bitand(ASF_TEST_CM(IntBox) self, int val) {
    return self.num & val;
  }
  static int op_bitor(ASF_TEST_CM(IntBox) self, int val) {
    return self.num | val;
  }
  static int op_xor(ASF_TEST_CM(IntBox) self, int val) {
    return self.num ^ val;
  }
  static int op_lshift(ASF_TEST_CM(IntBox) self, int val) {
    return self.num << val;
  }
  static int op_rshift(ASF_TEST_CM(IntBox) self, int val) {
    return self.num >> val;
  }
};

template <typename T = void>
struct OpAssign;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpAssign<void> {
  void op_iadd(auto& self, int val);
  void op_isub(auto& self, int val);
  void op_imul(auto& self, int val);
  void op_idiv(auto& self, int val);
  void op_imod(auto& self, int val);
  void op_ibitand(auto& self, int val);
  void op_ibitor(auto& self, int val);
  void op_ixor(auto& self, int val);
  void op_ilshift(auto& self, int val);
  void op_irshift(auto& self, int val);
};
template <>
struct OpAssign<IntBox> final {
  static void op_iadd(ASF_TEST_M(IntBox) self, int val) { self.num += val; }
  static void op_isub(ASF_TEST_M(IntBox) self, int val) { self.num -= val; }
  static void op_imul(ASF_TEST_M(IntBox) self, int val) { self.num *= val; }
  static void op_idiv(ASF_TEST_M(IntBox) self, int val) { self.num /= val; }
  static void op_imod(ASF_TEST_M(IntBox) self, int val) { self.num %= val; }
  static void op_ibitand(ASF_TEST_M(IntBox) self, int val) {
    self.num &= val;
  }
  static void op_ibitor(ASF_TEST_M(IntBox) self, int val) {
    self.num |= val;
  }
  static void op_ixor(ASF_TEST_M(IntBox) self, int val) { self.num ^= val; }
  static void op_ilshift(ASF_TEST_M(IntBox) self, int val) {
    self.num <<= val;
  }
  static void op_irshift(ASF_TEST_M(IntBox) self, int val) {
    self.num >>= val;
  }
};

template <typename T = void>
struct OpCall;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpCall<void> {
  int op_call(auto const& self, int val);
};
template <>
struct OpCall<IntBox> final {
  static int op_call(ASF_TEST_CM(IntBox) self, int val) {
    return self.num + val;
  }
};

template <typename T = void>
struct OpIndex;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpIndex<void> {
  int op_index(auto const& self, int i);
  int op_index(auto const& self, int i, int j);
};
template <>
struct OpIndex<IntBox> final {
  static int op_index(ASF_TEST_CM(IntBox) self, int i) {
    return self.num + i;
  }
  static int op_index(ASF_TEST_CM(IntBox) self, int i, int j) {
    return self.num + i + j;
  }
};

template <typename T = void>
struct OpConvert;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpConvert<void> {
  [[=asf_ann::expl]] int op_convert(auto const& self);
};
template <>
struct OpConvert<IntBox> final {
  static int op_convert(ASF_TEST_CM(IntBox) self) { return self.num; }
};

template <typename T = void>
struct OpComma;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpComma<void> {
  int value_of(auto const& self);
  int op_comma(auto const& self, int v);
  static int op_comma(int v, auto const& self) {
    if constexpr (requires { self.num; })
      return v * self.num;
    else {
      (void)self;
      return v;
    }
  }
};
template <>
struct OpComma<IntBox> final {
  static int value_of(ASF_TEST_CM(IntBox) self) { return self.num; }
  static int op_comma(ASF_TEST_CM(IntBox) self, int v) {
    return self.num + v;
  }
};

struct Fields {
  int a{};
  int b{};
  int c{};
  std::string payload{asf_test::no_sso("fields")};
};

template <typename T = void>
struct OpPtrToMem;
template <>
struct ASF_TEST_TRAIT_MIRROR
OpPtrToMem<void> {
  int scale(auto const& self);
  int& op_ptr_to_mem(auto& self, int Fields::* ptm);
  static int op_ptr_to_mem(int v, auto const& self) {
    if constexpr (requires { self.a; })
      return v * self.a;
    else {
      (void)self;
      return v;
    }
  }
};
template <>
struct OpPtrToMem<Fields> final {
  static int scale(ASF_TEST_CM(Fields) self) { return self.a; }
  static int& op_ptr_to_mem(ASF_TEST_M(Fields) self, int Fields::* ptm) {
    if (ptm == &Fields::a)
      return self.a;
    if (ptm == &Fields::b)
      return self.b;
    return self.c;
  }
};

void TestOpPlus() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpPlus>(v);
  ASF_CHECK_EQ(*p + 2, 14);
}

void TestOpMinus() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpMinus>(v);
  ASF_CHECK_EQ(-*p, -12);
  ASF_CHECK_EQ(*p - 2, 10);
}

void TestOpAsterisk() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpMul>(v);
  ASF_CHECK_EQ(*p * 2, 24);
}

void TestOpSlash() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpDiv>(v);
  ASF_CHECK_EQ(*p / 2, 6);
}

void TestOpModulo() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpMod>(v);
  ASF_CHECK_EQ(*p % 5, 2);
}

void TestOpIncrement() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpInc>(v);
  ASF_CHECK_EQ(++*p, 13);
  ASF_CHECK_EQ((*p)++, 13);
  ASF_CHECK_EQ(v.num, 14);
}

void TestOpDecrement() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpDec>(v);
  ASF_CHECK_EQ(--*p, 11);
  ASF_CHECK_EQ((*p)--, 11);
  ASF_CHECK_EQ(v.num, 10);
}

void TestOpEqualTo() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpEq>(v);
  ASF_CHECK(*p == 12);
  ASF_CHECK(!(*p == 11));
}

void TestOpNotEqualTo() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpEq>(v);
  ASF_CHECK(!(*p != 12));
  ASF_CHECK(*p != 11);
}

void TestOpGreaterThan() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpEq>(v);
  ASF_CHECK(*p > 2);
  ASF_CHECK(!(*p > 20));
}

void TestOpLessThan() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpEq>(v);
  ASF_CHECK(*p < 20);
  ASF_CHECK(!(*p < 2));
}

void TestOpGreaterThanOrEqualTo() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpEq>(v);
  ASF_CHECK(*p >= 12);
  ASF_CHECK(!(*p >= 20));
}

void TestOpLessThanOrEqualTo() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpEq>(v);
  ASF_CHECK(*p <= 12);
  ASF_CHECK(!(*p <= 2));
}

void TestOpSpaceship() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpEq>(v);
  ASF_CHECK((*p <=> 12) == 0);
  ASF_CHECK((*p <=> 11) > 0);
}

void TestOpLogicalNot() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpLogic>(v);
  ASF_CHECK(!(!*p));
  IntBox z{0};
  auto pz = asf_test::to_dyn<OpLogic>(z);
  ASF_CHECK(!*pz);
}

void TestOpLogicalAnd() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpLogic>(v);
  ASF_CHECK(*p && 1);
  ASF_CHECK(!(*p && 0));
}

void TestOpLogicalOr() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpLogic>(v);
  ASF_CHECK(*p || 0);
  IntBox z{0};
  auto pz = asf_test::to_dyn<OpLogic>(z);
  ASF_CHECK(*pz || 1);
}

void TestOpTilde() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpBit>(v);
  ASF_CHECK_EQ(~*p, ~12);
}

void TestOpAmpersand() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpBit>(v);
  ASF_CHECK_EQ(*p & 8, 8);
}

void TestOpPipe() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpBit>(v);
  ASF_CHECK_EQ(*p | 16, 28);
}

void TestOpCaret() {
  IntBox v{16};
  auto p = asf_test::to_dyn<OpBit>(v);
  ASF_CHECK_EQ(*p ^ 1, 17);
}

void TestOpLeftShift() {
  IntBox v{26};
  auto p = asf_test::to_dyn<OpBit>(v);
  ASF_CHECK_EQ(*p << 1, 52);
}

void TestOpRightShift() {
  IntBox v{36};
  auto p = asf_test::to_dyn<OpBit>(v);
  ASF_CHECK_EQ(*p >> 1, 18);
}

void TestOpAssignmentAddition() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p += 3;
  ASF_CHECK_EQ(v.num, 15);
}

void TestOpAssignmentSubtraction() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p -= 5;
  ASF_CHECK_EQ(v.num, 7);
}

void TestOpAssignmentMultiplication() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p *= 3;
  ASF_CHECK_EQ(v.num, 36);
}

void TestOpAssignmentDivision() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p /= 4;
  ASF_CHECK_EQ(v.num, 3);
}

void TestOpAssignmentModulus() {
  IntBox v{19};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p %= 5;
  ASF_CHECK_EQ(v.num, 4);
}

void TestOpAssignmentBitwiseAnd() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p &= 3;
  ASF_CHECK_EQ(v.num, 0);
}

void TestOpAssignmentBitwiseOr() {
  IntBox v{18};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p |= 1;
  ASF_CHECK_EQ(v.num, 19);
}

void TestOpAssignmentBitwiseXor() {
  IntBox v{22};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p ^= 1;
  ASF_CHECK_EQ(v.num, 23);
}

void TestOpAssignmentLeftShift() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p <<= 2;
  ASF_CHECK_EQ(v.num, 48);
}

void TestOpAssignmentRightShift() {
  IntBox v{44};
  auto p = asf_test::to_dyn<OpAssign>(v);
  *p >>= 3;
  ASF_CHECK_EQ(v.num, 5);
}

void TestOpParentheses() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpCall>(v);
  ASF_CHECK_EQ((*p)(13), 25);
}

void TestOpBrackets() {
  IntBox v{13};
  auto p = asf_test::to_dyn<OpIndex>(v);
  ASF_CHECK_EQ((*p)[3], 16);
}

void TestOpBrackets_Multidimensional() {
  IntBox v{13};
  auto p = asf_test::to_dyn<OpIndex>(v);
  ASF_CHECK_EQ(((*p)[3, 4]), 20);
}

void TestIndirectConversion() {
  IntBox v{12};
  auto p = asf_test::to_dyn<OpConvert>(v);
  ASF_CHECK_EQ(int(*p), 12);
}

void TestImplicitConversion() {
  // Conversion is annotated [[=asf_ann::expl]] (explicit); same surface as
  // IndirectConversion. Implicit-only conversion is not a library skill yet.
  IntBox v{12};
  auto p = asf_test::to_dyn<OpConvert>(v);
  ASF_CHECK_EQ(int(*p), 12);
}

void TestOpComma() {
  IntBox v{3};
  auto p = asf_test::to_dyn<OpComma>(v);
  ASF_CHECK_EQ(((*p), 6), 9);
}

void TestOpPtrToMem() {
  Fields v{};
  auto p = asf_test::to_dyn<OpPtrToMem>(v);
  std::vector<int Fields::*> fields{&Fields::a, &Fields::b, &Fields::c};
  for (int i = 0; i < 3; ++i)
    (*p)->*fields[static_cast<std::size_t>(i)] = i + 1;
  ASF_CHECK_EQ(v.a, 1);
  ASF_CHECK_EQ(v.b, 2);
  ASF_CHECK_EQ(v.c, 3);
}

void TestRhsOpComma() {
  IntBox v{3};
  auto p = asf::to_iface<OpComma>(v);
  ASF_CHECK_EQ((7, *p), 21);
}

void TestRhsOpPtrToMem() {
  Fields v{.a = 11};
  auto p = asf::to_iface<OpPtrToMem>(v);
  ASF_CHECK_EQ(2->*(*p), 22);
}

}  // namespace dyn_dispatch_tests_detail

int main() {
  std::fprintf(stdout, "dyn_dispatch_tests\n");
  ASF_RUN(dyn_dispatch_tests_detail::TestOpPlus);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpMinus);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAsterisk);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpSlash);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpModulo);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpIncrement);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpDecrement);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpEqualTo);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpNotEqualTo);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpGreaterThan);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpLessThan);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpGreaterThanOrEqualTo);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpLessThanOrEqualTo);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpSpaceship);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpLogicalNot);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpLogicalAnd);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpLogicalOr);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpTilde);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAmpersand);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpPipe);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpCaret);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpLeftShift);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpRightShift);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentAddition);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentSubtraction);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentMultiplication);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentDivision);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentModulus);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentBitwiseAnd);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentBitwiseOr);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentBitwiseXor);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentLeftShift);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpAssignmentRightShift);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpParentheses);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpBrackets);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpBrackets_Multidimensional);
  ASF_RUN(dyn_dispatch_tests_detail::TestIndirectConversion);
  ASF_RUN(dyn_dispatch_tests_detail::TestImplicitConversion);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpComma);
  ASF_RUN(dyn_dispatch_tests_detail::TestOpPtrToMem);
  ASF_RUN(dyn_dispatch_tests_detail::TestRhsOpComma);
  ASF_RUN(dyn_dispatch_tests_detail::TestRhsOpPtrToMem);
  return asf_test::g_failures ? 1 : 0;
}
