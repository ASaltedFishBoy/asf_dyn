#include "asf_check.hpp"
#include "test_bind.hpp"
#include "cast.hpp"

#include <cstdio>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#if __has_include(<memory_resource>)
#include <memory_resource>
#define ASF_PROXY_CREATION_HAS_PMR 1
#else
#define ASF_PROXY_CREATION_HAS_PMR 0
#endif

namespace dyn_creation_tests_detail {

template <typename T = void>
struct Stringable;

template <>
struct ASF_TEST_TRAIT_MIRROR
Stringable<void> {
  std::string to_string(auto const& self);
  // Copy vs move by self param type (not name): mirror<T> vs rmirror<T>.
  // Abbreviated auto overloads collide in invoke mangling. Return T; body
  // uses value_of (Src identity or reflective field materialize).
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

struct Session {
  std::string id;
  std::string label;
  Session(int n, std::string_view lab)
      : id(asf_test::no_sso(std::to_string(n))),
        label(asf_test::no_sso(lab)) {}
  Session(std::string id_in, std::string label_in)
      : id(std::move(id_in)), label(std::move(label_in)) {}
};

struct CountingAllocState {
  static inline std::size_t allocate_count{};
  static inline std::size_t deallocate_count{};
};

template <class T>
struct CountingAlloc {
  using value_type = T;

  CountingAlloc() noexcept = default;
  template <class U>
  CountingAlloc(CountingAlloc<U> const&) noexcept {}

  T* allocate(std::size_t n) {
    ++CountingAllocState::allocate_count;
    std::allocator<T> stda{};
    return std::allocator_traits<std::allocator<T>>::allocate(stda, n);
  }

  void deallocate(T* p, std::size_t n) {
    ++CountingAllocState::deallocate_count;
    std::allocator<T> stda{};
    std::allocator_traits<std::allocator<T>>::deallocate(stda, p, n);
  }

  template <class U>
  struct rebind {
    using other = CountingAlloc<U>;
  };

  friend bool operator==(CountingAlloc const&, CountingAlloc const&) noexcept {
    return true;
  }
};

template <>
struct Stringable<Session> final {
  static std::string to_string(ASF_TEST_CM(Session) self) {
    return self.label + " " + self.id;
  }
};

template <typename T = void>
struct Relabel;

template <>
struct Relabel<void> {
  std::string to_string(auto const& self);
  template <typename T>
  [[=asf_ann::imple]] T op_construct(const T& self) {
    return self;
  }
  template <typename T>
  T op_construct(const T& self, std::string tag) {
    T c = self;
    if constexpr (requires { c.label = tag; })
      c.label = std::move(tag);
    return c;
  }
  template <typename T>
  T op_construct(T&& self, std::string tag) {
    T c = std::move(self);
    if constexpr (requires { c.label = tag; })
      c.label = std::move(tag);
    return c;
  }
  template <typename T>
  T op_construct(const T& self, int n) {
    T c = self;
    if constexpr (requires { c.label = std::to_string(n); })
      c.label = asf_test::no_sso(std::to_string(n));
    return c;
  }
};

template <>
struct Relabel<Session> final {
  static std::string to_string(const Session& self) {
    return self.label + " " + self.id;
  }
};

// Not std::unique_ptr. Proxy-destroy: dyn runs vt[-1] (~Src) before reset();
// the pointer then frees storage only.
struct FakeBlock {
  static inline std::size_t nbytes{};
  static inline int allocs{};
  static inline int frees{};
};

template <class T>
struct FakeAlloc {
  using value_type = T;

  FakeAlloc() noexcept = default;
  template <class U>
  FakeAlloc(FakeAlloc<U> const&) noexcept {}

  T* allocate(std::size_t n) {
    ++FakeBlock::allocs;
    FakeBlock::nbytes = n * sizeof(T);
    std::allocator<T> stda{};
    return std::allocator_traits<std::allocator<T>>::allocate(stda, n);
  }

  void deallocate(T* p, std::size_t n) noexcept {
    ++FakeBlock::frees;
    FakeBlock::nbytes = 0;
    std::allocator<T> stda{};
    std::allocator_traits<std::allocator<T>>::deallocate(stda, p, n);
  }

  template <class U>
  struct rebind {
    using other = FakeAlloc<U>;
  };

  friend bool operator==(FakeAlloc const&, FakeAlloc const&) noexcept {
    return true;
  }
};

struct Box {
  std::string id;
  std::string label;
  static inline int dtor_count{};
  Box(int n, std::string_view lab)
      : id(asf_test::no_sso(std::to_string(n))),
        label(asf_test::no_sso(lab)) {}
  ~Box() { ++dtor_count; }
};

template <class T, class Alloc = FakeAlloc<T>>
class fake_uptr {
 public:
  using element_type = T;
  using allocator_type = Alloc;

  fake_uptr() noexcept = default;
  fake_uptr(std::nullptr_t) noexcept {}
  explicit fake_uptr(void* p) noexcept : p_(p) {}
  fake_uptr(fake_uptr const&) = delete;
  fake_uptr& operator=(fake_uptr const&) = delete;
  fake_uptr(fake_uptr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  fake_uptr& operator=(fake_uptr&& o) noexcept {
    if (this != &o) {
      reset();
      p_ = o.p_;
      o.p_ = nullptr;
    }
    return *this;
  }
  ~fake_uptr() { reset(); }

  void* get() const noexcept { return p_; }

  void* release() noexcept { return std::exchange(p_, nullptr); }

  std::default_delete<void> get_deleter() const noexcept { return {}; }

  void reset(void* p = nullptr) noexcept {
    if (p_) {
      FakeAlloc<std::byte>{}.deallocate(static_cast<std::byte*>(p_),
                                        FakeBlock::nbytes);
    }
    p_ = p;
  }

  explicit operator bool() const noexcept { return p_ != nullptr; }

 private:
  void* p_{};
};

// std::unique_ptr whose deleter stores the vt destroy pointer. Named
// explicitly when the target Ptr stores the destroy function itself.
struct vt_slot_delete {
  void (*destroy)(void*) = nullptr;

  vt_slot_delete() noexcept = default;
  vt_slot_delete(void (*fn)(void*)) noexcept : destroy(fn) {}

  void operator()(void* p) const noexcept {
    if (!p)
      return;
    if (destroy)
      destroy(p);
    FakeAlloc<std::byte>{}.deallocate(static_cast<std::byte*>(p),
                                      FakeBlock::nbytes);
  }
};

template <class T, class Alloc = FakeAlloc<T>>
using bridged_uptr = std::unique_ptr<T, vt_slot_delete>;

}  // namespace dyn_creation_tests_detail

namespace asf {
template <class T, class Alloc>
struct pointer_trait<dyn_creation_tests_detail::fake_uptr<T, Alloc>> {
  using kind = pointer_vt_destroy_tag;
};
template <>
struct pointer_trait<std::unique_ptr<
    void, dyn_creation_tests_detail::vt_slot_delete>> {
  using kind = pointer_deleter_tag;
};
}  // namespace asf

namespace dyn_creation_tests_detail {

template <typename T = void>
struct Boxable;

template <>
struct ASF_TEST_TRAIT_MIRROR
Boxable<void> {
  std::string to_string(auto const& self);
  template <typename T>
  [[=asf_ann::imple]] T op_construct(const T& self) {
    return self;
  }
};

template <>
struct Boxable<Box> final {
  static std::string to_string(ASF_TEST_CM(Box) self) {
    return self.label + " " + self.id;
  }
};

#if ASF_TEST_VIA_MIRROR
static_assert(asf::mirror_like<asf::mirror<Session>>);
static_assert(asf::mirror_like<asf::mirror<Session, Session>>);
static_assert(asf::mirror_like<asf::mirror<Session> const&>);
static_assert(!asf::mirror_like<Session>);
static_assert(!asf::sourced_mirror<asf::mirror<Session>>);
static_assert(asf::sourced_mirror<asf::mirror<Session, Session>>);
static_assert(asf::sourced_mirror<asf::mirror<Session, Session>&&>);
static_assert(!asf::sourced_mirror<Session>);

template <typename T = void>
struct NoMirrorBind;
template <>
struct NoMirrorBind<void> {
  void foo(auto&) {}
};

template <typename T = void>
struct MirrorOnTrait;
template <>
struct ASF_TEST_TRAIT_MIRROR
MirrorOnTrait<void> {
  void foo(auto&) {}
};

template <typename T = void>
struct MirrorOnMethods;
template <>
struct MirrorOnMethods<void> {
  [[=asf_ann::mirror]] void foo(auto&) {}
};

static_assert(!asf::detail::layout_allows_unsourced_mirror<NoMirrorBind>());
static_assert(asf::detail::layout_allows_unsourced_mirror<MirrorOnTrait>());
static_assert(asf::detail::layout_allows_unsourced_mirror<MirrorOnMethods>());
// Sourced mirror binds without [[=mirror]] (Storage=Src, like T*).
static_assert(std::constructible_from<asf::dyn<NoMirrorBind>,
                                      asf::mirror<Session, Session>&>);
static_assert(std::constructible_from<asf::dyn<MirrorOnTrait>,
                                      asf::mirror<Session, Session>&>);
// Mirror Arg -> iface: bind ungated (no full-layout [[=mirror]] required).
static_assert(std::constructible_from<
              asf::iface<NoMirrorBind, asf::mirror<Session, Session>>,
              asf::mirror<Session, Session>&>);
static_assert(std::constructible_from<
              asf::iface<NoMirrorBind, asf::mirror<Session>>,
              asf::mirror<Session>&>);

// Destroy is always vt[-1] (~Src). view≠Src may still use dyn.
struct SessionAlias {
  Session& s;
  operator asf::mirror<Session>() { return asf::mirror<Session>{s}; }
};

template <typename T = void>
struct UniformLifeNamed;
template <>
struct ASF_TEST_TRAIT_MIRROR
UniformLifeNamed<void> {
  void who(auto const&) {}
};

static_assert(
    asf::detail::sourced_dyn_view_mismatch_ok<UniformLifeNamed>());
static_assert(std::constructible_from<asf::dyn<UniformLifeNamed>,
                                      asf::mirror<Session, Session>&>);
static_assert(std::constructible_from<asf::dyn<UniformLifeNamed>,
                                      asf::mirror<Session, SessionAlias>&>);

// Entirely non-mirror layout: view≠Src may still use dyn.
template <typename T = void>
struct NonMirrorLife;
template <>
struct NonMirrorLife<void> {
  void who(auto const&) {}
};

static_assert(
    asf::detail::sourced_dyn_view_mismatch_ok<NonMirrorLife>());
static_assert(std::constructible_from<asf::dyn<NonMirrorLife>,
                                      asf::mirror<Session, SessionAlias>&>);

// Conversion-operator explicitness carries to the sourced mirror: a
// non-explicit operator mirror<T>() keeps the sourced conversion implicit;
// an explicit one keeps it explicit. The dyn mirror interface accepts the
// explicitly built mirror all the same.
static_assert(std::convertible_to<SessionAlias&, asf::mirror<Session>>);
static_assert(
    std::convertible_to<SessionAlias&, asf::mirror<Session, SessionAlias>>);

struct ExplicitSessionAlias {
  Session& s;
  explicit operator asf::mirror<Session>() {
    return asf::mirror<Session>{s};
  }
};

static_assert(
    std::constructible_from<asf::mirror<Session>, ExplicitSessionAlias&>);
static_assert(
    !std::convertible_to<ExplicitSessionAlias&, asf::mirror<Session>>);
static_assert(
    std::constructible_from<asf::mirror<Session, ExplicitSessionAlias>,
                            ExplicitSessionAlias&>);
static_assert(
    !std::convertible_to<ExplicitSessionAlias&,
                         asf::mirror<Session, ExplicitSessionAlias>>);
static_assert(
    std::constructible_from<asf::dyn<NonMirrorLife>,
                            asf::mirror<Session, ExplicitSessionAlias>&>);
static_assert(
    std::constructible_from<asf::dyn<Stringable>,
                            asf::mirror<Session, ExplicitSessionAlias>&>);
#endif


// Trait-level annotations (not mirror-source): no_dyn / expl / imple on Trait<void>.
template <typename T = void>
struct NodynOnTrait;
template <>
struct NodynOnTrait<void> {
  static void foo(auto&) {}
};

template <typename T = void>
struct ExOnTrait;
template <>
struct [[=asf_ann::expl]]
ExOnTrait<void> {
  int op_convert(auto&) { return 0; }
};

template <typename T = void>
struct ImplOnTrait;
template <>
struct [[=asf_ann::imple]]
ImplOnTrait<void> {
  auto op_construct(auto& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

static_assert(!asf::detail::is_dyn_slot(asf::detail::find_method(
    ^^NodynOnTrait<void>, "foo")));
static_assert(asf::detail::convert_is_explicit(asf::detail::find_method(
    ^^ExOnTrait<void>, "op_convert")));
static_assert(asf::detail::method_prefers_imple(asf::detail::find_method(
    ^^ImplOnTrait<void>, "op_construct")));
// invoke_resolved uses the As-aware probe; Trait-level [[=imple]] must
// still win (fallback to declaring Trait, same as method_prefers_imple).
static_assert(asf::detail::method_prefers_imple_as<ImplOnTrait, Session>(
    asf::detail::find_method(^^ImplOnTrait<void>, "op_construct")));

template <typename T = void>
struct CallMut;
template <typename T = void>
struct CallConst;

struct CallImpl {
  int value{};
  int extra{};
  std::string payload{asf_test::no_sso("call")};
};

template <>
struct ASF_TEST_TRAIT_MIRROR
CallMut<void> {
  int op_call(auto& self) {
    if constexpr (requires { self.value; })
      return self.value;
    else
      return 0;
  }
};
template <>
struct ASF_TEST_TRAIT_MIRROR
CallConst<void> {
  int op_call(auto const& self) {
    if constexpr (requires { self.extra; })
      return self.extra;
    else
      return 0;
  }
};

template <>
struct CallMut<CallImpl> final {};
template <>
struct CallConst<CallImpl> final {};

void TestMakeDynObserved() {
  CallImpl c{.value = 0, .extra = 1};
  auto p = asf_test::to_dyn<CallMut>(c);
  ASF_CHECK_EQ((*p)(), 0);
  ASF_CHECK_EQ((*asf_test::to_c_dyn<CallConst>(c))(), 1);
}

#if ASF_TEST_VIA_MIRROR
void TestCDynObserved_Impl() {
  // As = const Session reuses the unqualified Stringable<Session> impl.
  Session s{101, "CDynObs"};
  asf::c_dyn<Stringable> cd{&s};
  ASF_CHECK_EQ(cd->to_string(), asf_test::session_text("CDynObs", 101));
}

void TestCIfaceObserved_Impl() {
  // iface<const T> reuses the unqualified impl via the same const-As path.
  Session s{103, "CIfaceObs"};
  asf::iface<Stringable, const Session> ci{s};
  ASF_CHECK_EQ(ci->to_string(), asf_test::session_text("CIfaceObs", 103));
}

void TestToCIface() {
  Session s{105, "ToCIface"};
  auto ci = asf::to_c_iface<Stringable>(s);
  static_assert(
      std::is_same_v<decltype(ci), asf::iface<Stringable, const Session>>);
  ASF_CHECK_EQ(ci->to_string(), asf_test::session_text("ToCIface", 105));
  auto mi = asf::to_c_iface<Stringable>(asf::to_mirror(s));
  static_assert(std::is_same_v<
                decltype(mi),
                asf::iface<Stringable, asf::mirror<const Session, Session>>>);
  ASF_CHECK_EQ(mi->to_string(), asf_test::session_text("ToCIface", 105));
  auto ui = asf::to_c_iface<Stringable>(asf::to_pure_mirror(s));
  // Unsourced pure mirror stays a mirror Arg (const View); not peeled to
  // plain const Session.
  static_assert(std::is_same_v<
                decltype(ui),
                asf::iface<Stringable, asf::mirror<const Session>>>);
  ASF_CHECK_EQ(ui->to_string(), asf_test::session_text("ToCIface", 105));
}
#endif


#if ASF_TEST_VIA_MIRROR
// Bare `auto` return (no trailing type): declaration facts (self cv/ref)
// come from the display-string fallback, not signature_decl_of.
struct BareBox {
  int num{};
  std::string label{asf_test::no_sso("BareAuto")};
};

template <typename T = void>
struct BareAuto;
template <>
struct BareAuto<void> {
  static auto who(auto const& self) { return self.label; }
  static auto eat(auto&& self) { return self.num + 100; }
  static auto bump(auto& self) { return ++self.num; }
  static constexpr auto tagged(auto const& self) { return self.num + 5; }
  static decltype(auto) what(auto const& self) { return (self.num + 3); }
};

void TestBareAutoReturn_DisplayFallback() {
  BareBox b{140};
  asf::iface<BareAuto, BareBox> i{b};
  ASF_CHECK_EQ(i->who(), asf_test::no_sso("BareAuto"));
  ASF_CHECK_EQ(i->eat(), 240);    // auto:6&&  (rvalue self)
  ASF_CHECK_EQ(i->bump(), 141);   // auto:6&
  ASF_CHECK_EQ(i->tagged(), 146); // static constexpr prefix
  ASF_CHECK_EQ(i->what(), 144);   // decltype(auto) placeholder
}

void TestSourcedMirror_ExplicitConversionOp_Dyn() {
  Session s{102, "ExplAlias"};
  ExplicitSessionAlias ea{s};
  // Explicit-only conversion: direct-initialization builds the sourced
  // mirror; copy-initialization is ill-formed (asserted above).
  asf::mirror<Session, ExplicitSessionAlias> em{ea};
  ASF_CHECK(&asf::src_of(em) == &ea);
  asf::dyn<Stringable> d{em};
  ASF_CHECK_EQ(d->to_string(), asf_test::session_text("ExplAlias", 102));
  asf::c_dyn<Stringable> cd{em};
  ASF_CHECK_EQ(cd->to_string(), asf_test::session_text("ExplAlias", 102));
}

// Sourced: [[=mirror]] chooses presented Self; non-mirror peels to Src.
// Unsourced: only [[=mirror]] methods are callable.
template <typename T = void>
struct PresentSelf;
template <>
struct PresentSelf<void> {
  int as_obj(auto& self) {
    using R = std::remove_cvref_t<decltype(self)>;
    if constexpr (asf::is_mirror_v<R>)
      return 801;
    else
      return 802;
  }
  [[=asf_ann::mirror]] int as_mir(auto& self) {
    using R = std::remove_cvref_t<decltype(self)>;
    if constexpr (asf::is_mirror_v<R>)
      return 803;
    else
      return 804;
  }
};

void TestMirrorIface_PresentSelf() {
  Session s{106, "PresentSelf"};
  auto sm = asf::to_mirror(s);
  auto si = asf::to_iface<PresentSelf>(sm);
  static_assert(std::is_same_v<
                decltype(si),
                asf::iface<PresentSelf, asf::mirror<Session, Session>>>);
  ASF_CHECK_EQ(si->as_obj(), 802); // peel to Src
  ASF_CHECK_EQ(si->as_mir(), 803); // present mirror

  auto um = asf::to_pure_mirror(s);
  auto ui = asf::to_iface<PresentSelf>(um);
  static_assert(std::is_same_v<
                decltype(ui), asf::iface<PresentSelf, asf::mirror<Session>>>);
  ASF_CHECK_EQ(ui->as_mir(), 803); // present mirror; as_obj is ill-formed
}
#endif


void TestMakeDyn_Unique_FromValue() {
  Session s{1, "Session"};
  auto obs = asf_test::to_dyn<Stringable>(s);
  auto p = asf::construct<std::unique_ptr>(*obs);
  static_assert(std::same_as<decltype(p), asf::unique_dyn<Stringable>>);
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("Session", 1));
  auto q = asf::to_dyn<Stringable, std::unique_ptr>(s);
  static_assert(std::same_as<decltype(q), asf::unique_dyn<Stringable>>);
  ASF_CHECK_EQ(q->to_string(), asf_test::session_text("Session", 1));
  CountingAllocState::allocate_count = 0;
  auto r = asf::to_dyn<Stringable, std::unique_ptr>(CountingAlloc<void>{}, s);
  ASF_CHECK(CountingAllocState::allocate_count >= 1u);
  ASF_CHECK_EQ(r->to_string(), asf_test::session_text("Session", 1));
}

void TestMakeDynShared_FromValue() {
  Session s{1, "Session"};
  auto obs = asf_test::to_dyn<Stringable>(s);
  auto p = asf::construct<std::shared_ptr>(*obs);
  static_assert(std::same_as<decltype(p), asf::shared_dyn<Stringable>>);
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("Session", 1));
  auto p2 = p;
  ASF_CHECK(p == p2);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("Session", 1));
  CountingAllocState::allocate_count = 0;
  auto q = asf::construct<std::shared_ptr>(CountingAlloc<void>{}, *obs);
  static_assert(std::same_as<decltype(q), asf::shared_dyn<Stringable>>);
  ASF_CHECK_EQ(q->to_string(), asf_test::session_text("Session", 1));
  ASF_CHECK(CountingAllocState::allocate_count >= 1u);
}

void TestStdWeakPtrCompatibility() {
  Session s{1, "Session"};
  auto obs = asf_test::to_dyn<Stringable>(s);
  auto p1 = asf::construct<std::shared_ptr>(*obs);
  asf::dyn<Stringable, std::weak_ptr<void>> p2{p1};
  ASF_CHECK(static_cast<bool>(p1));
  ASF_CHECK(static_cast<bool>(p2));
  auto p3 = p2.lock();
  ASF_CHECK(static_cast<bool>(p3));
  ASF_CHECK_EQ(p3->to_string(), asf_test::session_text("Session", 1));
  p3.reset();
  p1.reset();
  auto p4 = p2.lock();
  ASF_CHECK(!p4);
  ASF_CHECK(p2.expired());
}

void TestExternalSharedPtr() {
  int del = 0;
  auto ext = std::shared_ptr<Session>(new Session{9, "Ext"},
                                      [&](Session* p) {
                                        ++del;
                                        delete p;
                                      });
  auto d = asf::to_dyn<Stringable>(ext);
  static_assert(std::same_as<decltype(d), asf::shared_dyn<Stringable>>);
  ASF_CHECK_EQ(d->to_string(), asf_test::session_text("Ext", 9));
  d.reset();
  ASF_CHECK_EQ(del, 0);
  ext.reset();
  ASF_CHECK_EQ(del, 1);
}

void TestConstructCopy_IndependentStorage() {
  Session s{3, "Ada"};
  auto obs = asf_test::to_dyn<Stringable>(s);
  auto clone = asf::construct(*obs);
  static_assert(std::same_as<decltype(clone), asf::dyn<Stringable>>);
  s.label = asf_test::no_sso("Zed");
  ASF_CHECK_EQ(clone->to_string(), asf_test::session_text("Ada", 3));
}

void TestConstruct_ExtraArgs() {
  Session s{4, "Alloc"};
  asf::dyn<Relabel> obs{&s};
  auto copy = asf::construct(*obs, asf_test::no_sso("Retag"));
  ASF_CHECK_EQ(copy->to_string(), asf_test::session_text("Retag", 4));
  ASF_CHECK_EQ(s.label, asf_test::no_sso("Alloc"));
  auto numbered = asf::construct(*obs, 9);
  ASF_CHECK_EQ(numbered->to_string(), asf_test::session_text("9", 4));
  ASF_CHECK_EQ(s.label, asf_test::no_sso("Alloc"));
  CountingAllocState::allocate_count = 0;
  auto owned = asf::construct<std::unique_ptr>(CountingAlloc<void>{}, *obs,
                                               asf_test::no_sso("Owned"));
  ASF_CHECK_EQ(owned->to_string(), asf_test::session_text("Owned", 4));
  ASF_CHECK(CountingAllocState::allocate_count >= 1u);
  {
    Session mv{4, "Alloc"};
    asf::dyn<Relabel> src{&mv};
    auto moved = asf::construct(src.rproxy(), asf_test::no_sso("Retag"));
    ASF_CHECK_EQ(moved->to_string(), asf_test::session_text("Retag", 4));
    ASF_CHECK(mv.label.empty());
  }
  {
    Session mv{4, "Alloc"};
    asf::dyn<Relabel> src{&mv};
    CountingAllocState::allocate_count = 0;
    auto moved = asf::construct<std::unique_ptr>(
        CountingAlloc<void>{}, src.rproxy(), asf_test::no_sso("Moved"));
    ASF_CHECK_EQ(moved->to_string(), asf_test::session_text("Moved", 4));
    ASF_CHECK(mv.label.empty());
    ASF_CHECK(CountingAllocState::allocate_count >= 1u);
  }
}

void TestAllocateDyn_DirectAllocator_FromValue() {
  CountingAllocState::allocate_count = 0;
  CountingAllocState::deallocate_count = 0;
  Session s{4, "Alloc"};
  auto obs = asf_test::to_dyn<Stringable>(s);
  auto p = asf::construct<std::unique_ptr>(CountingAlloc<void>{}, *obs);
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("Alloc", 4));
  ASF_CHECK(CountingAllocState::allocate_count >= 1u);
  p.reset();
  ASF_CHECK(CountingAllocState::deallocate_count >= 1u);
}

void TestAllocateDyn_DirectAllocator_Lifetime_Move() {
  Session s{5, "MoveAlloc"};
  auto obs = asf_test::to_dyn<Stringable>(s);
  auto p1 = asf::construct<std::unique_ptr>(CountingAlloc<void>{}, *obs);
  ASF_CHECK_EQ(p1->to_string(), asf_test::session_text("MoveAlloc", 5));
  auto p2 = std::move(p1);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK(!p1);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("MoveAlloc", 5));
}

void TestConstructMove_ViaStarDyn() {
  {
    Session s{6, "ConstructMove"};
    auto obs = asf_test::to_dyn<Stringable>(s);
    auto view = *obs;
    asf::detail::dyn_rproxy rp(view);
    asf::detail::dyn_proxy back(rp);
    ASF_CHECK_EQ(back.to_string(), asf_test::session_text("ConstructMove", 6));
  }
  {
    Session s{6, "ConstructMove"};
    auto obs = asf_test::to_dyn<Stringable>(s);
    auto h = asf::construct(obs.rproxy());
    ASF_CHECK_EQ(h->to_string(), asf_test::session_text("ConstructMove", 6));
  }
}

void TestMakeDynShared_Lifetime_Copy() {
  Session s{7, "SharedCopy"};
  auto obs = asf_test::to_dyn<Stringable>(s);
  auto p1 = asf::construct<std::shared_ptr>(*obs);
  auto p2 = p1;
  ASF_CHECK(p1 == p2);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("SharedCopy", 7));
}

void TestMakeDynShared_Lifetime_Move() {
  Session s{8, "SharedMove"};
  auto obs = asf_test::to_dyn<Stringable>(s);
  auto p1 = asf::construct<std::shared_ptr>(*obs);
  auto p2 = std::move(p1);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK(!p1);
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("SharedMove", 8));
}

#if ASF_PROXY_CREATION_HAS_PMR
void TestAllocateDyn_IndirectAllocator_FromValue() {
  std::pmr::unsynchronized_pool_resource pool;
  (void)pool;
  Session s{9, "Pmr"};
  auto obs = asf_test::to_dyn<Stringable>(s);
  auto p = asf::construct<std::unique_ptr>(
      std::pmr::polymorphic_allocator<std::byte>{}, *obs);
  ASF_CHECK(static_cast<bool>(p));
  ASF_CHECK_EQ(p->to_string(), asf_test::session_text("Pmr", 9));
}
#endif

#if 1
using CacheSession = asf::cache_dyn<
    Stringable, sizeof(Session), alignof(Session),
    asf::cache_ops_from_trait_or_t<Stringable, Session>>;

void TestCacheDyn_FromValue() {
  CountingAllocState::allocate_count = 0;
  CountingAllocState::deallocate_count = 0;
  Session s{10, "Cache"};
  auto d = asf::to_dyn<Stringable, asf::cache_ptr>(s);
  static_assert(std::same_as<decltype(d), CacheSession>);
  ASF_CHECK(static_cast<bool>(d));
  ASF_CHECK_EQ(d->to_string(), asf_test::session_text("Cache", 10));
  ASF_CHECK_EQ(CountingAllocState::allocate_count, std::size_t{0});
  ASF_CHECK_EQ(CountingAllocState::deallocate_count, std::size_t{0});
  d.reset();
  ASF_CHECK_EQ(CountingAllocState::deallocate_count, std::size_t{0});
}

void TestCacheDyn_Move() {
  Session s{11, "CacheMove"};
  auto d = asf::to_dyn<Stringable, asf::cache_ptr>(s);
  ASF_CHECK(static_cast<bool>(d));
  ASF_CHECK_EQ(d->to_string(), asf_test::session_text("CacheMove", 11));
  auto p2 = std::move(d);
  ASF_CHECK(static_cast<bool>(p2));
  ASF_CHECK_EQ(p2->to_string(), asf_test::session_text("CacheMove", 11));
}

void TestCacheDyn_ConstructMove() {
  Session s{12, "CacheCMove"};
  auto d = asf::to_dyn<Stringable, asf::cache_ptr>(std::move(s));
  static_assert(std::same_as<decltype(d), CacheSession>);
  ASF_CHECK(static_cast<bool>(d));
  ASF_CHECK_EQ(d->to_string(), asf_test::session_text("CacheCMove", 12));
}

void TestCacheDyn_TooSmall() {
  Session s{13, "Tiny"};
  auto d = asf::to_dyn<Stringable, asf::cache_ptr<1, 1>>(s);
  ASF_CHECK(!d);
}

// Trivial payload on purpose: this is the memcpy path. A Trait with no
// op_construct leaves copy/move as op_trivial; destroy becomes op_from_trait
// because every table has vt[-1].
template <typename T = void>
struct NumView;
template <>
struct NumView<void> {
  int value(auto const& self);
};

struct TrivialNum {
  int n{};
};

template <>
struct NumView<TrivialNum> final {
  static int value(TrivialNum const& self) { return self.n; }
};

void TestCacheTrivialCopyMove() {
  using Ops = asf::cache_ops_of<TrivialNum>;
  static_assert(std::same_as<Ops,
                             asf::cache_ops<asf::op_trivial, asf::op_trivial,
                                            asf::op_trivial, asf::op_trivial,
                                            asf::op_trivial>>);

  using DynOps = asf::cache_ops_from_trait_or_t<NumView, TrivialNum>;
  using DynPtr = asf::cache_ptr<sizeof(TrivialNum), alignof(TrivialNum), DynOps>;
  auto d = asf::to_dyn<NumView, asf::cache_ptr>(TrivialNum{11});
  static_assert(std::same_as<decltype(d), asf::dyn<NumView, DynPtr>>);
  ASF_CHECK(static_cast<bool>(d));
  ASF_CHECK_EQ(d->value(), 11);

  auto copied = d;
  ASF_CHECK_EQ(copied->value(), 11);
  static_cast<TrivialNum*>(d.get().get())->n = 12;
  ASF_CHECK_EQ(d->value(), 12);
  ASF_CHECK_EQ(copied->value(), 11);

  asf::dyn<NumView, DynPtr> assigned;
  auto d2 = asf::to_dyn<NumView, asf::cache_ptr>(TrivialNum{3});
  assigned = d2;
  ASF_CHECK_EQ(assigned->value(), 3);
  static_cast<TrivialNum*>(d2.get().get())->n = 4;
  ASF_CHECK_EQ(assigned->value(), 3);

  auto moved = std::move(copied);
  ASF_CHECK(!copied);
  ASF_CHECK_EQ(moved->value(), 11);

  assigned = std::move(moved);
  ASF_CHECK(!moved);
  ASF_CHECK_EQ(assigned->value(), 11);
}

#if ASF_TEST_VIA_MIRROR
void TestCacheDyn_FromMirror() {
  Session s{14, "MirrorCache"};
  auto m = asf::to_mirror<Session>(s);
  auto d = asf::to_dyn<Stringable, asf::cache_ptr>(m);
  ASF_CHECK(static_cast<bool>(d));
  ASF_CHECK_EQ(d->to_string(), asf_test::session_text("MirrorCache", 14));
}
#endif


#endif

// --- SBO / inplace matrix: cache_dyn vs unique_dyn ---
// WithSBO / Inplace  -> cache_dyn (explicit Size/Align, no heap)
// WithoutSBO         -> unique_dyn (heap uptr)
// No inplace ctor / initializer_list factory; copy of owning handles is
// move-only -- "Lifetime_Copy" uses a second construct() clone.

enum class LifetimeOpKind { Value, Copy, Move, Destroy };

struct LifetimeOp {
  int id{};
  LifetimeOpKind kind{};
  friend bool operator==(LifetimeOp const&, LifetimeOp const&) = default;
};

struct LifetimeTracker {
  std::vector<LifetimeOp> ops;
  int max_id = 0;

  int allocate_id(LifetimeOpKind kind) {
    ops.push_back(LifetimeOp{++max_id, kind});
    return max_id;
  }
};

struct TrackedSession {
  int id{};
  LifetimeTracker* host{};
  std::string payload{asf_test::no_sso("tracked")};

  explicit TrackedSession(LifetimeTracker* h)
      : id(h->allocate_id(LifetimeOpKind::Value)), host(h) {}
  TrackedSession(TrackedSession const& o)
      : id(o.host->allocate_id(LifetimeOpKind::Copy)), host(o.host),
        payload(o.payload) {}
  TrackedSession(TrackedSession&& o) noexcept
      : id(o.host->allocate_id(LifetimeOpKind::Move)), host(o.host),
        payload(std::move(o.payload)) {}
  ~TrackedSession() {
    if (host)
      host->ops.push_back(LifetimeOp{id, LifetimeOpKind::Destroy});
  }
};

template <>
struct Stringable<TrackedSession> final {
  static std::string to_string(ASF_TEST_CM(TrackedSession) self) {
    return "Session " + std::to_string(self.id);
  }
};

#if 1
using CacheTracked = asf::cache_dyn<
    Stringable, sizeof(TrackedSession), alignof(TrackedSession),
    asf::cache_ops_from_trait<Stringable>>;

// Also covers former Inplace_FromValue (same cache_dyn path).
void TestMakeDyn_WithSBO_FromValue() {
  LifetimeTracker tracker;
  TrackedSession session{&tracker};
  ASF_CHECK_EQ(tracker.ops.size(), std::size_t{1});
  ASF_CHECK(tracker.ops[0].kind == LifetimeOpKind::Value);
  {
    auto d = asf::to_dyn<Stringable, asf::cache_ptr>(session);
    static_assert(std::same_as<decltype(d), CacheTracked>);
    ASF_CHECK(static_cast<bool>(d));
    ASF_CHECK_EQ(d->to_string(), "Session 2");
    ASF_CHECK_EQ(tracker.ops.size(), std::size_t{2});
    ASF_CHECK(tracker.ops[1].kind == LifetimeOpKind::Copy);
  }
  ASF_CHECK_EQ(tracker.ops.size(), std::size_t{3});
  ASF_CHECK(tracker.ops[2].kind == LifetimeOpKind::Destroy);
  ASF_CHECK_EQ(tracker.ops[2].id, 2);
}

// Also covers former Inplace_Lifetime_Move (same cache_dyn path).
void TestMakeDyn_WithSBO_Lifetime_Move() {
  LifetimeTracker tracker;
  TrackedSession session{&tracker};
  {
    auto d = asf::to_dyn<Stringable, asf::cache_ptr>(session);
    ASF_CHECK(static_cast<bool>(d));
    ASF_CHECK_EQ(d->to_string(), "Session 2");
    auto p2 = std::move(d);
    ASF_CHECK(static_cast<bool>(p2));
    // construct_move into dest + destroy moved-from cache storage
    ASF_CHECK(tracker.ops.size() >= std::size_t{4});
    ASF_CHECK(tracker.ops[2].kind == LifetimeOpKind::Move);
    ASF_CHECK(tracker.ops[3].kind == LifetimeOpKind::Destroy);
    ASF_CHECK_EQ(tracker.ops[3].id, 2);
    ASF_CHECK_EQ(p2->to_string(),
                 "Session " + std::to_string(tracker.ops[2].id));
  }
}

// cache_dyn copy follows its ops; this test clones with a second construct().
void TestMakeDyn_WithSBO_Lifetime_Copy() {
  LifetimeTracker tracker;
  TrackedSession session{&tracker};
  {
    auto d1 = asf::to_dyn<Stringable, asf::cache_ptr>(session);
    auto d2 = asf::to_dyn<Stringable, asf::cache_ptr>(session);
    ASF_CHECK(static_cast<bool>(d1));
    ASF_CHECK(static_cast<bool>(d2));
    ASF_CHECK_EQ(d1->to_string(), "Session 2");
    ASF_CHECK_EQ(d2->to_string(), "Session 3");
    ASF_CHECK(tracker.ops[1].kind == LifetimeOpKind::Copy);
    ASF_CHECK(tracker.ops[2].kind == LifetimeOpKind::Copy);
  }
}

#endif

// Inplace_* is the same as WithSBO (cache_dyn); not run separately.

// WithoutSBO_* -> heap unique_dyn
void TestMakeDyn_WithoutSBO_FromValue() {
  LifetimeTracker tracker;
  TrackedSession session{&tracker};
  {
    auto obs = asf_test::to_dyn<Stringable>(session);
    auto p = asf::construct<std::unique_ptr>(*obs);
    static_assert(std::same_as<decltype(p), asf::unique_dyn<Stringable>>);
    ASF_CHECK(static_cast<bool>(p));
    ASF_CHECK_EQ(p->to_string(), "Session 2");
    ASF_CHECK(tracker.ops[1].kind == LifetimeOpKind::Copy);
  }
  ASF_CHECK(tracker.ops.back().kind == LifetimeOpKind::Destroy);
  ASF_CHECK_EQ(tracker.ops.back().id, 2);
}

void TestMakeDyn_WithoutSBO_Lifetime_Move() {
  LifetimeTracker tracker;
  TrackedSession session{&tracker};
  {
    auto obs = asf_test::to_dyn<Stringable>(session);
    auto p1 = asf::construct<std::unique_ptr>(*obs);
    ASF_CHECK_EQ(p1->to_string(), "Session 2");
    auto p2 = std::move(p1);
    ASF_CHECK(!p1);
    ASF_CHECK(static_cast<bool>(p2));
    // uptr steals pointer -- no Session move/destroy on move of handle
    ASF_CHECK_EQ(p2->to_string(), "Session 2");
    std::size_t destroys = 0;
    for (auto const& op : tracker.ops)
      if (op.kind == LifetimeOpKind::Destroy)
        ++destroys;
    ASF_CHECK_EQ(destroys, std::size_t{0});
  }
  ASF_CHECK(tracker.ops.back().kind == LifetimeOpKind::Destroy);
  ASF_CHECK_EQ(tracker.ops.back().id, 2);
}

void TestMakeDyn_WithoutSBO_Lifetime_Copy() {
  LifetimeTracker tracker;
  TrackedSession session{&tracker};
  {
    auto obs = asf_test::to_dyn<Stringable>(session);
    auto p1 = asf::construct<std::unique_ptr>(*obs);
    auto p2 = asf::construct<std::unique_ptr>(*obs);
    ASF_CHECK_EQ(p1->to_string(), "Session 2");
    ASF_CHECK_EQ(p2->to_string(), "Session 3");
    ASF_CHECK(tracker.ops[1].kind == LifetimeOpKind::Copy);
    ASF_CHECK(tracker.ops[2].kind == LifetimeOpKind::Copy);
  }
}

// construct(*dyn) = deep copy; construct(rproxy) = deep move (rvalue overload).
void TestConstructDeepMove_ViaRproxy() {
  {
    LifetimeTracker tracker;
    TrackedSession session{&tracker};
    auto obs = asf_test::to_dyn<Stringable>(session);
    auto clone = asf::construct(*obs);
    static_assert(std::same_as<decltype(clone), asf::dyn<Stringable>>);
    ASF_CHECK(tracker.ops[1].kind == LifetimeOpKind::Copy);
    ASF_CHECK_EQ(clone->to_string(), "Session 2");
  }
  {
    LifetimeTracker tracker;
    TrackedSession session{&tracker};
    auto obs = asf_test::to_dyn<Stringable>(session);
    auto moved = asf::construct(obs.rproxy());
    static_assert(std::same_as<decltype(moved), asf::dyn<Stringable>>);
    ASF_CHECK(static_cast<bool>(moved));
    ASF_CHECK(tracker.ops[1].kind == LifetimeOpKind::Move);
    ASF_CHECK_EQ(moved->to_string(),
                 "Session " + std::to_string(tracker.ops[1].id));
  }
  {
    LifetimeTracker tracker;
    TrackedSession session{&tracker};
    auto obs = asf_test::to_dyn<Stringable>(session);
    auto view = *obs;
    auto moved = asf::construct(asf::detail::dyn_rproxy(view));
    ASF_CHECK(tracker.ops[1].kind == LifetimeOpKind::Move);
    ASF_CHECK_EQ(moved->to_string(),
                 "Session " + std::to_string(tracker.ops[1].id));
  }
  {
    LifetimeTracker tracker;
    TrackedSession session{&tracker};
    auto obs = asf_test::to_dyn<Stringable>(session);
    auto owned = asf::construct<std::unique_ptr>(*obs);
    static_assert(
        std::same_as<decltype(owned), asf::unique_dyn<Stringable>>);
    ASF_CHECK(tracker.ops[1].kind == LifetimeOpKind::Copy);
    auto moved = asf::construct(owned.rproxy());
    static_assert(
        std::same_as<decltype(moved), asf::unique_dyn<Stringable>>);
    ASF_CHECK(tracker.ops[2].kind == LifetimeOpKind::Move);
    ASF_CHECK_EQ(moved->to_string(),
                 "Session " + std::to_string(tracker.ops[2].id));
  }
}

#if ASF_TEST_VIA_MIRROR
void TestValueOf_CopyAndMove() {
  {
    Session s{3, "Ada"};
    asf::mirror<Session> bare{s};
    auto copied = asf::value_of(bare);
    ASF_CHECK_EQ(copied.id, asf_test::no_sso("3"));
    ASF_CHECK_EQ(copied.label, asf_test::no_sso("Ada"));
    s.label = asf_test::no_sso("Zed");
    ASF_CHECK_EQ(copied.label, asf_test::no_sso("Ada"));
    auto again = asf::value_of(std::move(bare));
    ASF_CHECK_EQ(again.label, asf_test::no_sso("Zed"));
    ASF_CHECK_EQ(s.label, asf_test::no_sso("Zed"));
  }
  {
    Session s{4, "Move"};
    asf::rmirror<Session> bare{std::move(s)};
    auto moved = asf::value_of(bare);
    ASF_CHECK_EQ(moved.id, asf_test::no_sso("4"));
    ASF_CHECK_EQ(moved.label, asf_test::no_sso("Move"));
    ASF_CHECK(s.label.empty());
  }
  {
    LifetimeTracker tracker;
    TrackedSession session{&tracker};
    asf::mirror<TrackedSession, TrackedSession> held{session};
    auto copied = asf::value_of(held);
    ASF_CHECK(tracker.ops.back().kind == LifetimeOpKind::Copy);
    ASF_CHECK_EQ(copied.id, tracker.ops.back().id);
    auto copied2 = asf::value_of(std::move(held));
    ASF_CHECK(tracker.ops.back().kind == LifetimeOpKind::Copy);
    ASF_CHECK_EQ(copied2.id, tracker.ops.back().id);
  }
  {
    LifetimeTracker tracker;
    TrackedSession session{&tracker};
    asf::mirror<TrackedSession, TrackedSession> held{session};
    asf::rmirror<TrackedSession, TrackedSession> rm{held};
    auto moved = asf::value_of(rm);
    ASF_CHECK(tracker.ops.back().kind == LifetimeOpKind::Move);
    auto moved2 = asf::value_of(std::move(rm));
    ASF_CHECK(tracker.ops.back().kind == LifetimeOpKind::Move);
  }
}

void TestAssignMirror_CopyAndMove() {
  {
    Session from{1, "Ada"};
    Session to{2, "Bob"};
    auto dst = asf::to_mirror(to);
    asf::assign_mirror(dst, from);
    ASF_CHECK_EQ(to.label, asf_test::no_sso("Ada"));
    ASF_CHECK_EQ(from.label, asf_test::no_sso("Ada"));
  }
  {
    Session from{1, "Ada"};
    Session to{2, "Bob"};
    auto dst = asf::to_mirror(to);
    asf::assign_mirror(dst, std::move(from));
    ASF_CHECK_EQ(to.label, asf_test::no_sso("Ada"));
    ASF_CHECK(from.label.empty());
  }
  {
    Session from{1, "Ada"};
    Session to{2, "Bob"};
    asf::mirror<Session&> dst{to};
    asf::assign_mirror(dst, from);
    ASF_CHECK_EQ(to.label, asf_test::no_sso("Ada"));
    ASF_CHECK_EQ(from.label, asf_test::no_sso("Ada"));
  }
  {
    Session from{1, "Ada"};
    Session to{2, "Bob"};
    Session const& cfrom = from;
    auto dst = asf::to_mirror(to);
    asf::assign_mirror(dst, cfrom);
    ASF_CHECK_EQ(to.label, asf_test::no_sso("Ada"));
    ASF_CHECK_EQ(from.label, asf_test::no_sso("Ada"));
  }
  {
    Session from{1, "Ada"};
    Session to{2, "Bob"};
    auto dst = asf::to_mirror(to);
    asf::c_mirror<Session> src{from};
    asf::assign_mirror(dst, src);
    ASF_CHECK_EQ(to.label, asf_test::no_sso("Ada"));
    ASF_CHECK_EQ(from.label, asf_test::no_sso("Ada"));
  }
  {
    Session from{1, "Ada"};
    Session to{2, "Bob"};
    auto dst = asf::to_mirror(to);
    asf::rmirror<Session> src{std::move(from)};
    asf::assign_mirror(dst, src);
    ASF_CHECK_EQ(to.label, asf_test::no_sso("Ada"));
    ASF_CHECK(from.label.empty());
  }
}

void TestMirrorAssign_Rebind() {
  Session a{1, "Ada"};
  Session b{2, "Bob"};
  auto ma = asf::to_mirror(a);
  auto mb = asf::to_mirror(b);
  mb = ma;
  ASF_CHECK_EQ(mb.label, asf_test::no_sso("Ada"));
  mb.label = asf_test::no_sso("Zed");
  ASF_CHECK_EQ(a.label, asf_test::no_sso("Zed"));
  ASF_CHECK_EQ(b.label, asf_test::no_sso("Bob"));
  ma = ma;
  ASF_CHECK_EQ(ma.label, asf_test::no_sso("Zed"));

  Session c{3, "Cara"};
  auto mc = asf::to_mirror(c);
  asf::assign_mirror(mc, a);
  ASF_CHECK_EQ(c.label, asf_test::no_sso("Zed"));

  asf::mirror<Session, Session> sm{b};
  asf::c_mirror<Session, Session> cm{a};
  cm = sm;
  ASF_CHECK_EQ(asf::src_of(cm).label, asf_test::no_sso("Bob"));

  asf::rmirror<Session, Session> rm{std::move(a)};
  rm = sm;
  ASF_CHECK_EQ(asf::src_of(rm).label, asf_test::no_sso("Bob"));
}
#endif

void TestFakeUptr_NotLibraryUptr() {
  Box::dtor_count = 0;
  FakeBlock::allocs = 0;
  FakeBlock::frees = 0;
  FakeBlock::nbytes = 0;
  Box box{7, "Fake"};
  asf::dyn<Boxable> obs{&box};
  {
    auto owned = asf::construct<fake_uptr>(*obs);
    static_assert(std::same_as<decltype(owned), asf::dyn<Boxable, fake_uptr<void>>>);
    static_assert(!asf::detail::is_default_uptr_v<fake_uptr<void>>);
    ASF_CHECK(static_cast<bool>(owned));
    ASF_CHECK_EQ(owned->to_string(), asf_test::session_text("Fake", 7));
    ASF_CHECK(FakeBlock::allocs >= 1);
    auto moved = std::move(owned);
    ASF_CHECK(!owned);
    ASF_CHECK(static_cast<bool>(moved));
    ASF_CHECK_EQ(moved->to_string(), asf_test::session_text("Fake", 7));
  }
  ASF_CHECK(Box::dtor_count >= 1);
  ASF_CHECK_EQ(FakeBlock::frees, 1);
  ASF_CHECK_EQ(box.label, asf_test::no_sso("Fake"));
}

template <typename T = void>
struct Quiet;
template <>
struct ASF_TEST_TRAIT_MIRROR
Quiet<void> {
  std::string name(auto const&);
};
template <>
struct Quiet<Box> {
  static std::string name(ASF_TEST_CM(Box) self) { return self.label; }
};

template <typename T = void>
struct Loud;
template <>
struct ASF_TEST_TRAIT_MIRROR
Loud<void> : Quiet<void> {
  std::string name(auto const&);
  template <typename T>
  [[=asf_ann::imple]] T op_construct(const T& self) {
    return self;
  }
};

template <>
struct Loud<Box> final {
  static std::string name(ASF_TEST_CM(Box) self) { return self.label; }
};

static_assert(asf::detail::pointer_has_deleter_v<
              std::unique_ptr<void, asf::dyn_default_delete>>);
static_assert(asf::detail::dyn_ptr_uses_typed_deleter<
              std::unique_ptr<void, asf::dyn_default_delete>>());
static_assert(std::constructible_from<
              asf::dyn<Quiet, std::unique_ptr<void, asf::dyn_default_delete>>,
              asf::unique_dyn<Quiet>&&>);
static_assert(asf::detail::dyn_ptr_uses_typed_deleter<bridged_uptr<void>>());
static_assert(asf::detail::dyn_ptr_proxy_handoff_ok<
              bridged_uptr<void>,
              std::unique_ptr<void, asf::detail::new_delete>>());
static_assert(std::constructible_from<asf::dyn<Quiet, bridged_uptr<void>>,
                                      asf::unique_dyn<Quiet>&&>);

void TestBridgeDeleter_ExplicitPtr() {
  Box::dtor_count = 0;
  FakeBlock::allocs = 0;
  FakeBlock::frees = 0;
  FakeBlock::nbytes = 0;
  Box box{3, "Bridge"};
  asf::dyn<Loud> obs{&box};
  auto owned = asf::construct<fake_uptr>(*obs);
  {
    auto mid = asf::to_dyn<Quiet, bridged_uptr>(std::move(owned));
    static_assert(
        std::same_as<decltype(mid), asf::dyn<Quiet, bridged_uptr<void>>>);
    ASF_CHECK(static_cast<bool>(mid));
    ASF_CHECK_EQ(mid->name(), asf_test::no_sso("Bridge"));
  }
  ASF_CHECK(Box::dtor_count >= 1);
  ASF_CHECK_EQ(FakeBlock::frees, 1);
}

void TestBridgeDeleter_LibraryUptr() {
  Box::dtor_count = 0;
  FakeBlock::allocs = 0;
  FakeBlock::frees = 0;
  FakeBlock::nbytes = 0;
  Box box{9, "LibUptr"};
  asf::dyn<Loud> obs{&box};
  auto owned = asf::construct<std::unique_ptr>(FakeAlloc<void>{}, *obs);
  static_assert(std::same_as<
      decltype(owned),
      asf::dyn<Loud, std::unique_ptr<void, asf::alloc_delete<FakeAlloc<void>>>>>);
  static_assert(std::same_as<
      decltype(asf::trait_cast<Quiet>(std::declval<decltype(owned)>())),
      asf::dyn<Quiet, std::unique_ptr<void, asf::alloc_delete<FakeAlloc<void>>>>>);
  static_assert(asf::detail::is_default_uptr_v<
                std::unique_ptr<void, asf::alloc_delete<FakeAlloc<void>>>>);
  {
    auto mid = asf::to_dyn<Quiet, bridged_uptr>(std::move(owned));
    static_assert(
        std::same_as<decltype(mid), asf::dyn<Quiet, bridged_uptr<void>>>);
    ASF_CHECK(static_cast<bool>(mid));
    ASF_CHECK_EQ(mid->name(), asf_test::no_sso("LibUptr"));
  }
  ASF_CHECK(Box::dtor_count >= 1);
  ASF_CHECK_EQ(FakeBlock::frees, 1);
}

#if !ASF_TEST_VIA_MIRROR
void TestSharedFromQualifiedUptr() {
  Box::dtor_count = 0;
  Box box{1, "Qual"};
  asf::dyn<Loud> obs{&box};
  auto owned = asf::construct<std::unique_ptr>(*obs);
  void* raw = owned.ptr();
  asf::shared_dyn<Loud> shared{std::move(owned)};
  ASF_CHECK(static_cast<bool>(shared));
  ASF_CHECK(!owned);
  ASF_CHECK(shared.ptr() == raw);
  ASF_CHECK_EQ(shared->name(), asf_test::no_sso("Qual"));
  shared.reset();
  ASF_CHECK(Box::dtor_count >= 1);
}

void TestReleaseToPtrAndObs() {
  Box::dtor_count = 0;
  Box box{1, "Rel"};
  asf::dyn<Loud> obs{&box};
  auto owned = asf::construct<std::unique_ptr>(*obs);
  void* raw = owned.ptr();
  auto parts = owned.release();
  static_assert(std::same_as<decltype(parts.first),
                             std::unique_ptr<void, asf::detail::new_delete>>);
  static_assert(std::same_as<decltype(parts.second), asf::dyn<Loud>>);
  ASF_CHECK(!owned);
  ASF_CHECK(parts.first.get() == raw);
  ASF_CHECK(parts.second.ptr() == raw);
  ASF_CHECK_EQ(parts.second->name(), asf_test::no_sso("Rel"));
  ASF_CHECK_EQ(Box::dtor_count, 0);
  parts.second.destroy();
  ASF_CHECK_EQ(Box::dtor_count, 1);
  parts.first.reset();
  ASF_CHECK_EQ(Box::dtor_count, 1);
  parts.second.reset();
  ASF_CHECK_EQ(Box::dtor_count, 1);

  asf::dyn<Loud> view{&box};
  auto raw_parts = view.release();
  ASF_CHECK(!view);
  ASF_CHECK(raw_parts.first == &box);
  ASF_CHECK(raw_parts.second.ptr() == &box);
  ASF_CHECK_EQ(raw_parts.second->name(), asf_test::no_sso("Rel"));
  raw_parts.second.reset();
  ASF_CHECK_EQ(Box::dtor_count, 1);
}
#endif

}  // namespace dyn_creation_tests_detail


int main() {
  std::fprintf(stdout, "mirror/dyn_creation_tests\n");
  ASF_RUN(dyn_creation_tests_detail::TestMakeDynObserved);
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestCDynObserved_Impl);
#endif
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestCIfaceObserved_Impl);
#endif
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestToCIface);
#endif
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestBareAutoReturn_DisplayFallback);
#endif
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestSourcedMirror_ExplicitConversionOp_Dyn);
  ASF_RUN(dyn_creation_tests_detail::TestMirrorIface_PresentSelf);
#endif
  ASF_RUN(dyn_creation_tests_detail::TestMakeDyn_Unique_FromValue);
  ASF_RUN(dyn_creation_tests_detail::TestMakeDynShared_FromValue);
  ASF_RUN(dyn_creation_tests_detail::TestStdWeakPtrCompatibility);
  ASF_RUN(dyn_creation_tests_detail::TestExternalSharedPtr);
#if !ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestSharedFromQualifiedUptr);
  ASF_RUN(dyn_creation_tests_detail::TestReleaseToPtrAndObs);
#endif
  ASF_RUN(dyn_creation_tests_detail::TestConstructCopy_IndependentStorage);
  ASF_RUN(dyn_creation_tests_detail::TestAllocateDyn_DirectAllocator_FromValue);
  ASF_RUN(dyn_creation_tests_detail::TestConstruct_ExtraArgs);
  ASF_RUN(dyn_creation_tests_detail::TestAllocateDyn_DirectAllocator_Lifetime_Move);
  ASF_RUN(dyn_creation_tests_detail::TestConstructMove_ViaStarDyn);
  ASF_RUN(dyn_creation_tests_detail::TestMakeDynShared_Lifetime_Copy);
  ASF_RUN(dyn_creation_tests_detail::TestMakeDynShared_Lifetime_Move);
#if ASF_PROXY_CREATION_HAS_PMR
  ASF_RUN(
      dyn_creation_tests_detail::TestAllocateDyn_IndirectAllocator_FromValue);
#else
  std::fprintf(stdout, "  SKIP TestAllocateDyn_IndirectAllocator_FromValue "
                       "(no <memory_resource>)\n");
#endif
#if 1
  ASF_RUN(dyn_creation_tests_detail::TestCacheDyn_FromValue);
  ASF_RUN(dyn_creation_tests_detail::TestCacheDyn_Move);
  ASF_RUN(dyn_creation_tests_detail::TestCacheDyn_ConstructMove);
  ASF_RUN(dyn_creation_tests_detail::TestCacheDyn_TooSmall);
  ASF_RUN(dyn_creation_tests_detail::TestCacheTrivialCopyMove);
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestCacheDyn_FromMirror);
#endif
  ASF_RUN(dyn_creation_tests_detail::TestMakeDyn_WithSBO_FromValue);
  ASF_RUN(dyn_creation_tests_detail::TestMakeDyn_WithSBO_Lifetime_Move);
  ASF_RUN(dyn_creation_tests_detail::TestMakeDyn_WithSBO_Lifetime_Copy);
#endif
  ASF_RUN(dyn_creation_tests_detail::TestMakeDyn_WithoutSBO_FromValue);
  ASF_RUN(dyn_creation_tests_detail::TestMakeDyn_WithoutSBO_Lifetime_Move);
  ASF_RUN(dyn_creation_tests_detail::TestMakeDyn_WithoutSBO_Lifetime_Copy);
  ASF_RUN(dyn_creation_tests_detail::TestConstructDeepMove_ViaRproxy);
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestValueOf_CopyAndMove);
#endif
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestAssignMirror_CopyAndMove);
#endif
#if ASF_TEST_VIA_MIRROR
  ASF_RUN(dyn_creation_tests_detail::TestMirrorAssign_Rebind);
#endif
  ASF_RUN(dyn_creation_tests_detail::TestFakeUptr_NotLibraryUptr);
  ASF_RUN(dyn_creation_tests_detail::TestBridgeDeleter_ExplicitPtr);
  ASF_RUN(dyn_creation_tests_detail::TestBridgeDeleter_LibraryUptr);
  return asf_test::g_failures ? 1 : 0;
}
