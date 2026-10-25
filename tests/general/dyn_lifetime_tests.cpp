// Handle lifetime for asf::dyn (void* / uptr). Skips SBO/inplace value poly.

#include "asf_check.hpp"
#include "test_bind.hpp"
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
struct ASF_TEST_TRAIT_MIRROR
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
  static std::string to_string(ASF_TEST_CM(Session) self) {
    return self.label + " " + self.id;
  }
};

template <typename T = void>
struct Labeled;
template <>
struct ASF_TEST_TRAIT_MIRROR
Labeled<void> {
  auto tag(auto const& self) -> std::string { return self.label; }
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  {
    (void)self.label;
    return self;
  }
};
template <>
struct Labeled<Session> final {};

#if !ASF_TEST_VIA_MIRROR
// Trailing return is remove_cvref(self). Dummy signature probe does not
// instantiate the body; extras are real types. Body may use members.
// (general pass only: the mirror-self op_construct form is covered in
// dyn_creation_tests.)
template <typename T = void>
struct RelabelByParts;
template <>
struct RelabelByParts<void> {
  auto tag(auto const& self) -> std::string {
    return self.label + " " + self.id;
  }
  auto op_construct(auto const& self)

      -> std::remove_cvref_t<decltype(self)> {
    Session out{0, "x"};
    out.id = self.id;
    out.label = self.label;
    return out;
  }
  auto op_construct(auto const& self, std::string extra)

      -> std::remove_cvref_t<decltype(self)> {
    Session out{0, "x"};
    out.id = self.id;
    out.label = extra.empty() ? std::string(self.label) : extra;
    return out;
  }
  auto op_construct(auto const& self, int n)

      -> std::remove_cvref_t<decltype(self)> {
    Session out{n, "x"};
    out.id = asf_test::no_sso(std::to_string(n));
    out.label = self.label;
    return out;
  }
  auto op_construct(auto const& self, int n, std::string extra)

      -> std::remove_cvref_t<decltype(self)> {
    Session out{n, "x"};
    out.id = asf_test::no_sso(std::to_string(n));
    out.label = extra.empty() ? std::string(self.label) : extra;
    return out;
  }
};
template <>
struct RelabelByParts<Session> final {};
#endif

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

void TestDestroyThunk_Alignment() {
  // tag_destroy_align packs the align log into the destroy pointer's low
  // bits: the two vtable destroy thunks must stay max-aligned.
  ASF_CHECK((reinterpret_cast<std::uintptr_t>(
                 &asf::detail::vt_src_destroy<Session>) &
             asf::detail::dyn_align_tag_mask) == 0);
  ASF_CHECK((reinterpret_cast<std::uintptr_t>(&asf::detail::vt_destroy_nop) &
             asf::detail::dyn_align_tag_mask) == 0);
}

void TestDefaultBodyUsesMembers() {
  Session s{1, "lab"};
  auto p = asf::to_dyn<Labeled>(&s);
  ASF_CHECK_EQ(p->tag(), asf_test::no_sso("lab"));
  auto q = p;
  ASF_CHECK_EQ(q->tag(), asf_test::no_sso("lab"));
}

#if !ASF_TEST_VIA_MIRROR
void TestConstructExtrasUseMembers() {
  Session s{1, "lab"};
  auto p = asf_test::to_dyn<RelabelByParts>(s);
  ASF_CHECK_EQ(p->tag(), asf_test::session_text("lab", 1));
  auto by_str = asf::construct(*p, asf_test::no_sso("pre"));
  ASF_CHECK_EQ(by_str->tag(), asf_test::session_text("pre", 1));
  auto by_int = asf::construct(*p, 7);
  ASF_CHECK_EQ(by_int->tag(), asf_test::session_text("lab", 7));
  auto q = asf::construct(*p, 7, asf_test::no_sso("pre"));
  ASF_CHECK_EQ(q->tag(), asf_test::session_text("pre", 7));
  ASF_CHECK_EQ(p->tag(), asf_test::session_text("lab", 1));
  auto owned = asf::construct<std::unique_ptr>(*p, 8, asf_test::no_sso("own"));
  ASF_CHECK_EQ(owned->tag(), asf_test::session_text("own", 8));
  ASF_CHECK_EQ(p->tag(), asf_test::session_text("lab", 1));
}
#endif

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
  auto p = asf_test::to_dyn<Stringable>(s);
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("Session", 2));
}

void TestCopyConstruction_FromValue() {
  Session s{2, "Session"};
  auto p1 = asf_test::to_dyn<Stringable>(s);
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
  auto p1 = asf_test::to_dyn<Stringable>(s);
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
  auto p = asf_test::to_dyn<Stringable>(s);
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
  auto p1 = asf_test::to_dyn<Stringable>(a);
  auto p2 = asf_test::to_dyn<Stringable>(b);
  p2 = p1;
  ASF_CHECK(p1 == p2);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestCopyAssignment_FromValue_ToNull() {
  Session a{1, "A"};
  auto p1 = asf_test::to_dyn<Stringable>(a);
  asf::dyn<Stringable> p2;
  p2 = p1;
  ASF_CHECK(p1 == p2);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestCopyAssignment_FromNull_ToValue() {
  Session a{1, "A"};
  auto p1 = asf_test::to_dyn<Stringable>(a);
  asf::dyn<Stringable> p2;
  p1 = p2;
  ASF_CHECK(!p1);
}

void TestCopyAssignment_FromValue_ToSelf() {
  Session a{1, "A"};
  auto p = asf_test::to_dyn<Stringable>(a);
  p = p;
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("A", 1));
}

void TestMoveAssignment_FromValue_ToSelf() {
  Session a{1, "A"};
  auto p = asf_test::to_dyn<Stringable>(a);
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
  auto p1 = asf_test::to_dyn<Stringable>(a);
  auto p2 = asf_test::to_dyn<Stringable>(b);
  p2 = std::move(p1);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestMoveAssignment_FromValue_ToNull() {
  Session a{1, "A"};
  auto p1 = asf_test::to_dyn<Stringable>(a);
  asf::dyn<Stringable> p2;
  p2 = std::move(p1);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestOperatorBool() {
  asf::dyn<Stringable> empty;
  ASF_CHECK(!empty);
  Session s{1, "S"};
  auto p = asf_test::to_dyn<Stringable>(s);
  ASF_CHECK(static_cast<bool>(p));
}

void TestEqualsToNullptr() {
  asf::dyn<Stringable> empty;
  ASF_CHECK(empty == nullptr);
  ASF_CHECK(nullptr == empty);
  Session s{1, "S"};
  auto p = asf_test::to_dyn<Stringable>(s);
  ASF_CHECK(p != nullptr);
  ASF_CHECK(nullptr != p);
}

void TestReset_FromValue() {
  Session s{1, "S"};
  auto p = asf_test::to_dyn<Stringable>(s);
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
  auto p1 = asf_test::to_dyn<Stringable>(a);
  auto p2 = asf_test::to_dyn<Stringable>(b);
  using std::swap;
  swap(p1, p2);
  ASF_CHECK_EQ(p1->to_string(), asf_test::session_text("B", 2));
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestSwap_Value_Null() {
  Session a{1, "A"};
  auto p1 = asf_test::to_dyn<Stringable>(a);
  asf::dyn<Stringable> p2;
  using std::swap;
  swap(p1, p2);
  ASF_CHECK(!p1);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("A", 1));
}

void TestSwap_Value_Self() {
  Session a{1, "A"};
  auto p = asf_test::to_dyn<Stringable>(a);
  using std::swap;
  swap(p, p);
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("A", 1));
}

void TestSwap_Null_Value() {
  Session a{1, "A"};
  asf::dyn<Stringable> p1;
  auto p2 = asf_test::to_dyn<Stringable>(a);
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
  auto p = asf_test::to_dyn<Stringable>(s);
  ASF_CHECK(static_cast<bool>(p));
}

void TestSizeAlign() {
  asf::dyn<Stringable> empty;
  ASF_CHECK_EQ(empty.size(), 0);
  ASF_CHECK_EQ(empty.align(), 0);
  Session s{1, "S"};
  auto p = asf_test::to_dyn<Stringable>(s);
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

// --- Typed cache ops: unified handle/void* dispatch + grow-only ---------

struct Probe {
  static inline int dtor_count = 0;
  int v = 0;
  Probe(int x) : v(x) {}
  Probe(Probe const& o) : v(o.v) {}
  Probe(Probe&& o) noexcept : v(o.v) { o.v = -1; }
  Probe& operator=(Probe const& o) {
    v = o.v;
    return *this;
  }
  Probe& operator=(Probe&& o) noexcept {
    v = o.v;
    o.v = -1;
    return *this;
  }
  ~Probe() { ++dtor_count; }
};

// Which op form fired. The handle form is offered wherever a fully-formed
// public handle participates; the void* form is the baseline.
struct OpForms {
  static inline int copy_hdl = 0, copy_ptr = 0;
  static inline int move_hdl = 0, move_ptr = 0;
  static inline int destroy_hdl = 0, destroy_ptr = 0;
  static inline int cassign_hdl = 0, cassign_ptr = 0;
  static inline int massign_hdl = 0, massign_ptr = 0;
  static void reset() {
    copy_hdl = copy_ptr = 0;
    move_hdl = move_ptr = 0;
    destroy_hdl = destroy_ptr = 0;
    cassign_hdl = cassign_ptr = 0;
    massign_hdl = massign_ptr = 0;
  }
};

// Payload access for handle-form ops: the public handle accessor
// (dyn::get() yields the Ptr, cache_ptr::get() yields the payload).
template <class H>
Probe* probe_of(H& h) {
  if constexpr (requires { h.get().get(); })
    return static_cast<Probe*>(h.get().get());
  else
    return static_cast<Probe*>(h.get());
}

struct ProbeCopy {
  void operator()(void* d, void const* s) const {
    ++OpForms::copy_ptr;
    std::construct_at(static_cast<Probe*>(d), *static_cast<Probe const*>(s));
  }
  void operator()(void* d, auto const& sh) const {
    ++OpForms::copy_hdl;
    std::construct_at(static_cast<Probe*>(d), *probe_of(sh));
  }
};
struct ProbeMove {
  void operator()(void* d, void* s) const {
    ++OpForms::move_ptr;
    auto* sp = static_cast<Probe*>(s);
    std::construct_at(static_cast<Probe*>(d), std::move(*sp));
    std::destroy_at(sp);
  }
  void operator()(void* d, auto& sh) const {
    ++OpForms::move_hdl;
    auto* sp = probe_of(sh);
    std::construct_at(static_cast<Probe*>(d), std::move(*sp));
    std::destroy_at(sp);
  }
};
struct ProbeDestroy {
  void operator()(void* p) const {
    ++OpForms::destroy_ptr;
    std::destroy_at(static_cast<Probe*>(p));
  }
  void operator()(auto& h) const {
    ++OpForms::destroy_hdl;
    std::destroy_at(probe_of(h));
  }
};
struct ProbeCAssign {
  void operator()(void* d, void const* s) const {
    ++OpForms::cassign_ptr;
    *static_cast<Probe*>(d) = *static_cast<Probe const*>(s);
  }
  void operator()(auto& dh, auto const& sh) const {
    ++OpForms::cassign_hdl;
    *probe_of(dh) = *probe_of(sh);
  }
};
struct ProbeMAssign {
  void operator()(void* d, void* s) const {
    ++OpForms::massign_ptr;
    *static_cast<Probe*>(d) = std::move(*static_cast<Probe*>(s));
  }
  void operator()(auto& dh, auto& sh) const {
    ++OpForms::massign_hdl;
    *probe_of(dh) = std::move(*probe_of(sh));
  }
};

// Handle-form-only assigner: not invocable with void*, so the assign tier
// stays dead everywhere and destroy + construct substitutes for it.
struct ProbeCAssignHdlOnly {
  void operator()(auto& dh, auto const& sh) const {
    ++OpForms::cassign_hdl;
    *probe_of(dh) = *probe_of(sh);
  }
};

using ProbeOps =
    asf::pointer_ops<ProbeDestroy, ProbeCopy, ProbeMove, ProbeCAssign,
                   ProbeMAssign>;
using ProbeOpsNoAssign = asf::pointer_ops<ProbeDestroy, ProbeCopy, ProbeMove>;
using ProbeOpsHdlAssign =
    asf::pointer_ops<ProbeDestroy, ProbeCopy, ProbeMove, ProbeCAssignHdlOnly,
                   ProbeMAssign>;

// Ops is structural: any type with the slot aliases (and call operators /
// tags) works — not only asf::pointer_ops.
struct ProbeDuckOps {
  using destroy_type = ProbeDestroy;
  using copy_type = ProbeCopy;
  using move_type = ProbeMove;
};
static_assert(asf::is_pointer_ops_v<ProbeDuckOps>);
static_assert(asf::is_pointer_ops_v<ProbeOpsNoAssign>);
static_assert(std::is_same_v<asf::ops_copy_assign_t<ProbeDuckOps>,
                             asf::op_unset>);
static_assert(
    std::is_constructible_v<asf::cache_ptr<16, 8, ProbeDuckOps>,
                            asf::cache_ptr<16, 8, ProbeDuckOps> const&>);

// ProbeView has no op_construct: typed copy/move survive as op_modified
// (the Trait chain cannot construct), destroy is always vt[-1].
// Cache dyn fill does not need a Trait op_construct (direct placement).
template <typename T = void>
struct ProbeView;
template <>
struct ProbeView<void> {
  int value(auto const& self);
};
template <>
struct ProbeView<Probe> final {
  static int value(Probe const& self) { return self.v; }
};

// ProbeMake has op_construct: erased cache ops are all op_from_trait
// (needed for the erased grow-only cache_dyn -> cache_dyn conversion).
template <typename T = void>
struct ProbeMake;
template <>
struct ProbeMake<void> {
  int value(auto const& self);
  [[=asf_ann::imple]] auto op_construct(auto const& self)
      -> std::remove_cvref_t<decltype(self)> {
    return self;
  }
};
template <>
struct ProbeMake<Probe> final {
  static int value(Probe const& self) { return self.v; }
};

// Grow-only cache -> cache (compile-time; shrink is an error).
using ProbeNatOps = asf::pointer_ops_of<Probe>;
static_assert(
    std::is_constructible_v<asf::cache_ptr<32, 16, ProbeNatOps>,
                            asf::cache_ptr<16, 8, ProbeNatOps> const&>);
static_assert(std::is_constructible_v<asf::cache_ptr<32, 16, ProbeNatOps>,
                                      asf::cache_ptr<16, 8, ProbeNatOps>&&>);
static_assert(
    !std::is_constructible_v<asf::cache_ptr<16, 8, ProbeNatOps>,
                             asf::cache_ptr<32, 16, ProbeNatOps> const&>);
static_assert(
    !std::is_constructible_v<asf::cache_ptr<16, 8, ProbeNatOps>,
                             asf::cache_ptr<32, 16, ProbeNatOps>&&>);
// Align may not shrink even when size grows.
static_assert(
    !std::is_constructible_v<asf::cache_ptr<32, 4, ProbeNatOps>,
                             asf::cache_ptr<16, 8, ProbeNatOps> const&>);
using ProbeErasedOps = asf::pointer_ops_erase_typed<ProbeNatOps>;
static_assert(
    std::is_constructible_v<asf::cache_ptr<32, 16, ProbeErasedOps>,
                            asf::cache_ptr<16, 8, ProbeNatOps> const&>);
static_assert(
    !std::is_constructible_v<asf::cache_ptr<16, 8, ProbeErasedOps>,
                             asf::cache_ptr<32, 16, ProbeNatOps> const&>);

// Capability checks unwrap modified proxies (recursively); a fixed typed
// op is itself the callable.
static_assert(asf::cache_copy_live<ProbeCopy>());
static_assert(asf::cache_copy_live<asf::detail::op_modified<ProbeCopy>>());
static_assert(asf::cache_copy_live<
              asf::detail::op_modified<asf::detail::op_modified<ProbeCopy>>>());
static_assert(
    std::same_as<
        asf::cache_slot_op_t<
            asf::detail::op_modified<asf::detail::op_modified<ProbeCopy>>>,
        ProbeCopy>);
// Re-erasing an already-erased ops bundle keeps the slots usable.
using ProbeReErasedOps = asf::pointer_ops_erase_typed<ProbeErasedOps>;
static_assert(
    std::is_constructible_v<asf::cache_ptr<32, 16, ProbeReErasedOps>,
                            asf::cache_ptr<16, 8, ProbeErasedOps> const&>);

using ProbeSrcDyn = std::remove_cvref_t<decltype(
    asf::to_dyn<ProbeMake, asf::cache_ptr<16, 8>>(std::declval<Probe&&>()))>;
// Same erased ops as the source, bigger geometry: copy converts through the
// dyn-level vt construct (both sides op_from_trait for copy); move converts
// through the same-ops cache_ptr converting ctor (op_modified move op).
using ProbeBigDyn = asf::cache_dyn<ProbeMake, 32, 16,
                                   asf::pointer_ops_from_trait_or_t<ProbeMake,
                                                                  Probe>>;
static_assert(std::is_constructible_v<ProbeBigDyn, ProbeSrcDyn const&>);
static_assert(std::is_constructible_v<ProbeBigDyn, ProbeSrcDyn&&>);
using ProbeSmallDyn = asf::cache_dyn<ProbeMake, 8, 4,
                                     asf::pointer_ops_from_trait_or_t<ProbeMake,
                                                                    Probe>>;
static_assert(!std::is_constructible_v<ProbeSmallDyn, ProbeSrcDyn const&>);
static_assert(!std::is_constructible_v<ProbeSmallDyn, ProbeSrcDyn&&>);
using ProbeMisalDyn = asf::cache_dyn<ProbeMake, 32, 4,
                                     asf::pointer_ops_from_trait_or_t<ProbeMake,
                                                                    Probe>>;
static_assert(!std::is_constructible_v<ProbeMisalDyn, ProbeSrcDyn const&>);

void TestCacheOps_AssignHandleForms() {
  using Ptr16 = asf::cache_ptr<16, 8, ProbeOps>;
  auto a = asf::to_dyn<ProbeView, Ptr16>(Probe{61});
  auto b = asf::to_dyn<ProbeView, Ptr16>(Probe{62});
  OpForms::reset();
  b = a;
  ASF_CHECK_EQ(OpForms::cassign_hdl, 1);
  ASF_CHECK_EQ(OpForms::cassign_ptr, 0);
  ASF_CHECK_EQ(b->value(), 61);
  ASF_CHECK_EQ(a->value(), 61);
  b = std::move(a);
  ASF_CHECK_EQ(OpForms::massign_hdl, 1);
  ASF_CHECK_EQ(OpForms::massign_ptr, 0);
  ASF_CHECK_EQ(b->value(), 61);
  ASF_CHECK(!a);
}

// The two-overload dispatch is not assign-only: dyn copy/move construction
// also offers the source handle to the typed op.
void TestCacheOps_CopyMoveHandleForms() {
  using Ptr16 = asf::cache_ptr<16, 8, ProbeOps>;
  auto a = asf::to_dyn<ProbeView, Ptr16>(Probe{63});
  OpForms::reset();
  auto b = a;
  ASF_CHECK_EQ(OpForms::copy_hdl, 1);
  ASF_CHECK_EQ(OpForms::copy_ptr, 0);
  ASF_CHECK_EQ(b->value(), 63);
  ASF_CHECK_EQ(a->value(), 63);
  auto c = std::move(a);
  ASF_CHECK_EQ(OpForms::move_hdl, 1);
  ASF_CHECK_EQ(OpForms::move_ptr, 0);
  ASF_CHECK_EQ(c->value(), 63);
  ASF_CHECK(!a);
}

// Assign is special: it can always be substituted by destroy + construct.
void TestCacheOps_AssignFallback() {
  using Ptr16 = asf::cache_ptr<16, 8, ProbeOpsNoAssign>;
  auto a = asf::to_dyn<ProbeView, Ptr16>(Probe{71});
  auto b = asf::to_dyn<ProbeView, Ptr16>(Probe{72});
  Probe::dtor_count = 0;
  OpForms::reset();
  b = a;
  // No assign op: the old payload was destroyed (vt[-1]) and re-constructed
  // by the typed copy op (offered the source dyn handle).
  ASF_CHECK_EQ(Probe::dtor_count, 1);
  ASF_CHECK_EQ(OpForms::cassign_hdl, 0);
  ASF_CHECK_EQ(OpForms::copy_hdl, 1);
  ASF_CHECK_EQ(OpForms::copy_ptr, 0);
  ASF_CHECK_EQ(b->value(), 71);
  b = std::move(a);
  ASF_CHECK_EQ(OpForms::move_hdl, 1);
  ASF_CHECK_EQ(b->value(), 71);
  ASF_CHECK(!a);
}

// A handle-form-only assign op is not invocable with void*, so the assign
// tier stays dead even though a dyn handle exists at the call site:
// destroy + construct substitutes (liveness is a storage-level property).
void TestCacheOps_HandleOnlyAssignFallsBack() {
  using Ptr16 = asf::cache_ptr<16, 8, ProbeOpsHdlAssign>;
  auto a = asf::to_dyn<ProbeView, Ptr16>(Probe{91});
  auto b = asf::to_dyn<ProbeView, Ptr16>(Probe{92});
  Probe::dtor_count = 0;
  OpForms::reset();
  b = a;
  ASF_CHECK_EQ(OpForms::cassign_hdl, 0);
  ASF_CHECK_EQ(Probe::dtor_count, 1);
  ASF_CHECK_EQ(OpForms::copy_hdl, 1);
  ASF_CHECK_EQ(b->value(), 91);
}

// Erased cache_dyn -> cache_dyn, grow-only: payload provably fits, so the
// transfer is a vt construct into the bigger buffer (no runtime check).
void TestCacheDyn_GrowConversion() {
  auto small = asf::to_dyn<ProbeMake, asf::cache_ptr<16, 8>>(Probe{81});
  static_assert(std::same_as<decltype(small), ProbeSrcDyn>);
  ProbeBigDyn copied{small};
  ASF_CHECK(static_cast<bool>(copied));
  ASF_CHECK_EQ(copied->value(), 81);
  ASF_CHECK_EQ(small->value(), 81);
  ProbeBigDyn assigned;
  assigned = small;
  ASF_CHECK_EQ(assigned->value(), 81);

  ProbeBigDyn moved{std::move(small)};
  ASF_CHECK_EQ(moved->value(), 81);
  ASF_CHECK(!small);

  auto small2 = asf::to_dyn<ProbeMake, asf::cache_ptr<16, 8>>(Probe{82});
  ProbeBigDyn massigned;
  massigned = std::move(small2);
  ASF_CHECK_EQ(massigned->value(), 82);
  ASF_CHECK(!small2);
}

// --- Destroy definitions per Ptr flavor + shallow copy/move ------------

// Observation dyn (Ptr = void*): never destroys the observed object.
void TestDestroy_ObserverNeverDestroys() {
  Probe::dtor_count = 0;
  {
    Probe p{44};
    asf::dyn<ProbeView> obs{&p};
    obs.reset();
    ASF_CHECK_EQ(Probe::dtor_count, 0);
  }
  ASF_CHECK_EQ(Probe::dtor_count, 1);
}

// unique_ptr default (vt_destroy kind): vt[-1] runs ~Src on reset.
void TestDestroy_UniqueDefault_Vt() {
  Probe ps{45};
  auto obs = asf::to_dyn<ProbeMake>(&ps);
  Probe::dtor_count = 0;
  auto owned = asf::construct<std::unique_ptr>(*obs);
  ASF_CHECK_EQ(owned->value(), 45);
  owned.reset();
  ASF_CHECK_EQ(Probe::dtor_count, 1);
  ASF_CHECK(!owned);
}

// unique_ptr + dyn_adopt_delete<Free> (deleter-kind): the wired vt[-1]
// runs ~Src, then Free(p) deallocates. Both fire exactly once.
struct FreeCount {
  static inline int count = 0;
  void operator()(void* p) const noexcept {
    ++count;
    ::operator delete(p);
  }
};

void TestDestroy_AdoptDelete_VtThenFree() {
  FreeCount::count = 0;
  Probe::dtor_count = 0;
  std::unique_ptr<Probe, asf::dyn_adopt_delete<FreeCount>> src(
      new Probe{46}, asf::dyn_adopt_delete<FreeCount>{});
  auto d = asf::to_dyn<ProbeMake>(std::move(src));
  ASF_CHECK_EQ(d->value(), 46);
  d.reset();
  ASF_CHECK_EQ(Probe::dtor_count, 1);
  ASF_CHECK_EQ(FreeCount::count, 1);
  ASF_CHECK(!d);

  // Default-Free form (dyn_default_delete): vt[-1] teardown is wired too.
  Probe::dtor_count = 0;
  std::unique_ptr<Probe, asf::dyn_default_delete> src2(new Probe{47});
  auto d2 = asf::to_dyn<ProbeMake>(std::move(src2));
  ASF_CHECK_EQ(d2->value(), 47);
  d2.reset();
  ASF_CHECK_EQ(Probe::dtor_count, 1);
  ASF_CHECK(!d2);
}

// unique_ptr + custom deleter D (deleter-kind): D carries the full
// teardown; no vt[-1] is wired (would double-destroy).
struct ProbeDel {
  static inline int count = 0;
  void operator()(Probe* p) const {
    ++count;
    delete p;
  }
};

void TestDestroy_TypedDeleter_NoDoubleDestroy() {
  ProbeDel::count = 0;
  Probe::dtor_count = 0;
  std::unique_ptr<Probe, ProbeDel> src(new Probe{47});
  auto d = asf::to_dyn<ProbeMake>(std::move(src));
  ASF_CHECK_EQ(d->value(), 47);
  d.reset();
  ASF_CHECK_EQ(ProbeDel::count, 1);
  ASF_CHECK_EQ(Probe::dtor_count, 1);
}

// shared_ptr (deleter-kind control block): shallow copy shares the
// payload; the last reset destroys it.
void TestDestroy_SharedControlBlock_LastReset() {
  Probe ps{48};
  auto obs = asf::to_dyn<ProbeMake>(&ps);
  Probe::dtor_count = 0;
  auto p1 = asf::construct<std::shared_ptr>(*obs);
  auto p2 = p1;
  ASF_CHECK(p1.raw() == p2.raw());
  static_cast<Probe*>(p1.raw())->v = 49;
  ASF_CHECK_EQ(p2->value(), 49);
  p1.reset();
  ASF_CHECK_EQ(Probe::dtor_count, 0);
  p2.reset();
  ASF_CHECK_EQ(Probe::dtor_count, 1);
}

// Shallow moves hand the pointer over: the payload address is unchanged.
void TestShallowMove_UniqueKeepsAddress() {
  Probe ps{50};
  auto obs = asf::to_dyn<ProbeMake>(&ps);
  auto p1 = asf::construct<std::unique_ptr>(*obs);
  void* addr = p1.raw();
  auto p2 = std::move(p1);
  ASF_CHECK_EQ(p2.raw(), addr);
  ASF_CHECK(!p1);
  ASF_CHECK_EQ(p2->value(), 50);
}

void TestShallowMove_SharedKeepsAddress() {
  Probe ps{51};
  auto obs = asf::to_dyn<ProbeMake>(&ps);
  auto p1 = asf::construct<std::shared_ptr>(*obs);
  void* addr = p1.raw();
  auto p2 = std::move(p1);
  ASF_CHECK_EQ(p2.raw(), addr);
  ASF_CHECK(!p1);
  ASF_CHECK_EQ(p2->value(), 51);
}

// Shallow move with a deleter attached: the payload address is unchanged
// and the deleter travels with the handle (firing exactly once, at the
// final teardown).
void TestShallowMove_AdoptDeleteTravels() {
  FreeCount::count = 0;
  Probe::dtor_count = 0;
  std::unique_ptr<Probe, asf::dyn_adopt_delete<FreeCount>> src(
      new Probe{52}, asf::dyn_adopt_delete<FreeCount>{});
  auto d1 = asf::to_dyn<ProbeMake>(std::move(src));
  void* addr = d1.raw();
  auto d2 = std::move(d1);
  ASF_CHECK_EQ(d2.raw(), addr);
  ASF_CHECK(!d1);
  ASF_CHECK_EQ(FreeCount::count, 0);
  d2.reset();
  ASF_CHECK_EQ(Probe::dtor_count, 1);
  ASF_CHECK_EQ(FreeCount::count, 1);
}

void TestShallowMove_TypedDeleterTravels() {
  ProbeDel::count = 0;
  std::unique_ptr<Probe, ProbeDel> src(new Probe{53});
  auto d1 = asf::to_dyn<ProbeMake>(std::move(src));
  void* addr = d1.raw();
  auto d2 = std::move(d1);
  ASF_CHECK_EQ(d2.raw(), addr);
  ASF_CHECK(!d1);
  ASF_CHECK_EQ(ProbeDel::count, 0);
  d2.reset();
  ASF_CHECK_EQ(ProbeDel::count, 1);
}

// Deleter handoff: a non-default source storage deleter must be forwarded
// into the destination's dyn_adopt_delete as Free (it has output).
struct ProbeAllocState {
  static inline std::size_t dealloc = 0;
};
template <typename T>
struct ProbeAlloc {
  using value_type = T;
  ProbeAlloc() noexcept = default;
  template <typename U>
  ProbeAlloc(ProbeAlloc<U> const&) noexcept {}
  T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
  void deallocate(T* p, std::size_t n) {
    ++ProbeAllocState::dealloc;
    std::allocator<T>{}.deallocate(p, n);
  }
  template <typename U>
  struct rebind {
    using other = ProbeAlloc<U>;
  };
  friend bool operator==(ProbeAlloc const&, ProbeAlloc const&) noexcept {
    return true;
  }
};

void TestDeleterHandoff_NonDefaultForwarded() {
  auto d1 = asf::to_dyn<ProbeMake, std::unique_ptr>(ProbeAlloc<void>{},
                                                  Probe{54});
  // Reset after creation: the forwarded Probe{54} temporary has its own
  // destructor; only the payload teardown counts below.
  ProbeAllocState::dealloc = 0;
  Probe::dtor_count = 0;
  using DstPtr =
      std::unique_ptr<void,
                      asf::dyn_adopt_delete<
                          asf::alloc_delete<ProbeAlloc<void>>>>;
  asf::dyn<ProbeMake, DstPtr> d2{std::move(d1)};
  ASF_CHECK_EQ(d2->value(), 54);
  ASF_CHECK(!d1);
  d2.reset();
  ASF_CHECK_EQ(Probe::dtor_count, 1);
  ASF_CHECK(ProbeAllocState::dealloc >= std::size_t{1});
}

// Default source storage deleter is not forwarded: dest d is the
// default-Free dyn_default_delete.
void TestDeleterHandoff_DefaultNotForwarded() {
  Probe ps{55};
  auto obs = asf::to_dyn<ProbeMake>(&ps);
  Probe::dtor_count = 0;
  auto d1 = asf::construct<std::unique_ptr>(*obs);
  asf::dyn<ProbeMake, std::unique_ptr<void, asf::dyn_default_delete>> d2{
      std::move(d1)};
  ASF_CHECK_EQ(d2->value(), 55);
  d2.reset();
  ASF_CHECK_EQ(Probe::dtor_count, 1);
}

// Flavor copy/move shape at the type level. uptr: move only. sptr: shared
// shallow copy. cache: copy/move may be stripped (destroy may not).
// Observer (void*): trivially move-assignable -- vector relocate is plain
// memberwise stores, the same class as pro::proxy_view. (The move ctor is
// not formally trivial: the RAII dtor must run before member teardown, so
// it stays user-provided.)
static_assert(
    std::is_trivially_assignable_v<asf::dyn<ProbeView>&,
                                   asf::dyn<ProbeView>&&>);
static_assert(!std::is_copy_constructible_v<asf::unique_dyn<ProbeMake>>);
static_assert(std::is_move_constructible_v<asf::unique_dyn<ProbeMake>>);
static_assert(std::is_copy_constructible_v<asf::shared_dyn<ProbeMake>>);
static_assert(std::is_move_constructible_v<asf::shared_dyn<ProbeMake>>);
static_assert(std::is_copy_constructible_v<asf::dyn<ProbeView>>);
using StripCopyOps =
    asf::pointer_ops<asf::op_destroy_of<Probe>, asf::op_unsupported,
                   asf::op_move_of<Probe>>;
static_assert(
    !std::is_copy_constructible_v<asf::cache_ptr<16, 8, StripCopyOps>>);
static_assert(
    std::is_move_constructible_v<asf::cache_ptr<16, 8, StripCopyOps>>);
using StripMoveOps =
    asf::pointer_ops<asf::op_destroy_of<Probe>, asf::op_copy_of<Probe>,
                   asf::op_unsupported>;
static_assert(
    !std::is_move_constructible_v<asf::cache_ptr<16, 8, StripMoveOps>>);
static_assert(
    std::is_copy_constructible_v<asf::cache_ptr<16, 8, StripMoveOps>>);

}  // namespace dyn_lifetime_tests_detail

int main() {
  std::fprintf(stdout, "dyn_lifetime_tests\n");
  ASF_RUN(dyn_lifetime_tests_detail::TestDestroyThunk_Alignment);
  ASF_RUN(dyn_lifetime_tests_detail::TestDefaultBodyUsesMembers);
#if !ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_lifetime_tests_detail::TestConstructExtrasUseMembers);
#endif
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
  ASF_RUN(dyn_lifetime_tests_detail::TestCacheOps_AssignHandleForms);
  ASF_RUN(dyn_lifetime_tests_detail::TestCacheOps_CopyMoveHandleForms);
  ASF_RUN(dyn_lifetime_tests_detail::TestCacheOps_AssignFallback);
  ASF_RUN(dyn_lifetime_tests_detail::TestCacheOps_HandleOnlyAssignFallsBack);
  ASF_RUN(dyn_lifetime_tests_detail::TestCacheDyn_GrowConversion);
  ASF_RUN(dyn_lifetime_tests_detail::TestDestroy_ObserverNeverDestroys);
  ASF_RUN(dyn_lifetime_tests_detail::TestDestroy_UniqueDefault_Vt);
  ASF_RUN(dyn_lifetime_tests_detail::TestDestroy_AdoptDelete_VtThenFree);
  ASF_RUN(dyn_lifetime_tests_detail::TestDestroy_TypedDeleter_NoDoubleDestroy);
  ASF_RUN(dyn_lifetime_tests_detail::TestDestroy_SharedControlBlock_LastReset);
  ASF_RUN(dyn_lifetime_tests_detail::TestShallowMove_UniqueKeepsAddress);
  ASF_RUN(dyn_lifetime_tests_detail::TestShallowMove_SharedKeepsAddress);
  ASF_RUN(dyn_lifetime_tests_detail::TestShallowMove_AdoptDeleteTravels);
  ASF_RUN(dyn_lifetime_tests_detail::TestShallowMove_TypedDeleterTravels);
  ASF_RUN(dyn_lifetime_tests_detail::TestDeleterHandoff_NonDefaultForwarded);
  ASF_RUN(dyn_lifetime_tests_detail::TestDeleterHandoff_DefaultNotForwarded);
  return asf_test::g_failures ? 1 : 0;
}
