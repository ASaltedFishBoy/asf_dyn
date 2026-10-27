// asf_dyn vs microsoft/proxy benchmark (out-of-tree).
// Scenario mirrors microsoft/proxy's proxy_operation_benchmark.cpp:
//   100 template type series x N objects, Invocation + Relocation,
//   per Small (int) and Large (5 pointers + int) payloads.
#include "cast.hpp"

#include <proxy/proxy.h>

#include <any>
#include <chrono>
#include <cstdint>
#include <memory>
#include <print>
#include <utility>
#include <vector>

namespace {

constexpr int kDataSize = 200000;
constexpr int kTypeSeries = 100;

// ---------- payloads (member Fun for the direct/any rows) ----------
template <int N>
struct Small {
  explicit Small(int s) noexcept : seed(s) {}
  int Fun() const noexcept { return seed ^ (N + 1); }
  int seed;
};
template <int N>
struct Large {
  explicit Large(int s) noexcept : seed(s) {}
  int Fun() const noexcept { return seed ^ (N + 1); }
  void* padding[5]{};
  int seed;
};

// ---------- asf Trait ----------
template <typename T = void>
struct Ops;
template <>
struct Ops<void> {
  int Fun(auto const& self);
};
template <int N>
struct Ops<Small<N>> final {
  static int Fun(Small<N> const& self) { return self.Fun(); }
};
template <int N>
struct Ops<Large<N>> final {
  static int Fun(Large<N> const& self) { return self.Fun(); }
};

// ---------- virtual ----------
struct VBase {
  virtual int Fun() const = 0;
  virtual ~VBase() = default;
};
template <int N>
struct VSmall : VBase {
  explicit VSmall(int s) : seed(s) {}
  int Fun() const override { return seed ^ (N + 1); }
  int seed;
};
template <int N>
struct VLarge : VBase {
  explicit VLarge(int s) : seed(s) {}
  int Fun() const override { return seed ^ (N + 1); }
  void* padding[5]{};
  int seed;
};

// ---------- microsoft/proxy facade (mirrors their benchmark) ----------
PRO_DEF_MEM_DISPATCH(MemFun, Fun);
struct Facade : pro::facade_builder
                    ::add_convention<MemFun, int() const>
                    ::add_skill<pro::skills::as_view>
                    ::build {};

// ---------- data generation (mirrors FillTestData) ----------
template <int V>
struct IntConstant {};

template <int From, class Vec, class F>
void Fill(Vec& data, F&& gen) {
  if constexpr (From < kTypeSeries) {
    for (int i = From; i < kDataSize; i += kTypeSeries)
      data[i] = gen(IntConstant<From>{}, i);
    Fill<From + 1>(data, std::forward<F>(gen));
  }
}

template <class F>
auto Generate(F&& gen) {
  std::vector<decltype(gen(IntConstant<0>{}, 0))> result(kDataSize);
  Fill<0>(result, std::forward<F>(gen));
  return result;
}

template <template <int> class Impl, std::size_t... K>
auto make_arenas(std::index_sequence<K...>) {
  return std::tuple<std::vector<Impl<K>>...>{};
}

struct Timer {
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
  double ms() const {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0)
        .count();
  }
};

std::int64_t sink = 0;

template <typename Vec>
void run_invoke(const char* name, Vec const& data) {
  std::int64_t acc = 0;
  Timer t;
  for (auto const& h : data)
    acc += [&] { if constexpr (requires { h->Fun(); }) return h->Fun();
                 else return h.Fun(); }();
  sink += acc;
  double ms = t.ms();
  std::println("  invoke {:<34} {:>9.3f} ms  ({:.3f} ns/call)", name, ms,
               ms * 1e6 / kDataSize);
}

template <typename Vec>
void run_relocate(const char* name, Vec& data) {
  Vec buffer(data.size());
  Timer t;
  for (int round = 0; round < 25; ++round) {
    for (std::size_t i = 0; i < data.size(); ++i)
      buffer[i] = std::move(data[i]);
    std::swap(data, buffer);
  }
  sink += buffer.size();
  double ms = t.ms();
  std::println("  relocate {:<32} {:>9.3f} ms  ({:.3f} ns/move)", name, ms,
               ms * 1e6 / (25.0 * kDataSize));
}

// Same-shell SBO as microsoft/proxy: 16 B payload. Small fits inline; Large
// stores unique_ptr in that buffer (cache_deref + unique_pointer_ops).
using SboSpec = asf::cache_ptr<16, 8, asf::unique_pointer_ops>;

template <template <int> class Impl, template <int> class VImpl,
          class CacheSpec>
void run_size(const char* label) {
  std::println("-- {} --", label);

  // Owning rows: measure then drop so the next Generate is not sitting on
  // 200k live handles (cache48 build especially polluted sbo Large invoke).
  std::println(
      "  handle sizes: proxy {} B, unique_dyn {} B, sbo {} B, "
      "cache_dyn {} B, observer {} B, proxy_view {} B",
      sizeof(pro::proxy<Facade>),
      sizeof(asf::dyn<Ops, std::unique_ptr<void, asf::detail::new_delete>>),
      sizeof(asf::dyn<Ops, SboSpec>), sizeof(asf::dyn<Ops, CacheSpec>),
      sizeof(asf::dyn<Ops>), sizeof(pro::proxy_view<Facade>));

  {
    auto proxies = Generate([]<int N>(IntConstant<N>, int seed) {
      return pro::make_proxy<Facade, Impl<N>>(seed);
    });
    run_invoke("pro::proxy (owning)", proxies);
    run_relocate("pro::proxy (owning)", proxies);
  }

  {
    auto owned_asf = Generate([]<int N>(IntConstant<N>, int seed) {
      return asf::to_dyn<Ops, std::unique_ptr>(Impl<N>{seed});
    });
    run_invoke("asf unique_dyn (owning)", owned_asf);
    run_relocate("asf unique_dyn (owning)", owned_asf);
  }

  // Cos proxy SBO: fixed 16 B shell; inline value or unique_ptr-in-cache.
  {
    auto sbo_asf = Generate([]<int N>(IntConstant<N>, int seed) {
      if constexpr (sizeof(Impl<N>) <= SboSpec::size &&
                    alignof(Impl<N>) <= SboSpec::align) {
        return asf::to_dyn<Ops, SboSpec>(Impl<N>{seed});
      } else {
        return asf::to_dyn<Ops, SboSpec>(
            asf::cache_deref{std::make_unique<Impl<N>>(seed)});
      }
    });
    run_invoke("asf sbo (cache/uptr-in-cache)", sbo_asf);
    run_relocate("asf sbo (cache/uptr-in-cache)", sbo_asf);
  }

  // cache_dyn: object travels in-handle. Small ~proxy inline (16 B); Large
  // uses a cache that fits (48 B) -- size is per-Ptr, not facade-capped.
  {
    auto cached_asf = Generate([]<int N>(IntConstant<N>, int seed) {
      return asf::to_dyn<Ops, CacheSpec>(Impl<N>{seed});
    });
    run_invoke("asf cache_dyn (in-handle)", cached_asf);
    run_relocate("asf cache_dyn (in-handle)", cached_asf);
  }

  auto uniq = Generate([]<int N>(IntConstant<N>, int seed) {
    return std::unique_ptr<VBase>{new VImpl<N>(seed)};
  });
  run_invoke("virtual (unique_ptr)", uniq);
  run_relocate("virtual (unique_ptr)", uniq);

  auto anys = Generate(
      []<int N>(IntConstant<N>, int seed) { return std::any{Impl<N>{seed}}; });
  run_relocate("std::any", anys);

  // Observer handles: one arena per type series (raw objects live there);
  // dyn / proxy_view / raw-ptr rows point into them.
  auto arenas = make_arenas<Impl>(std::make_index_sequence<kTypeSeries>{});

  std::vector<asf::dyn<Ops>> dyn_obs;
  std::vector<pro::proxy_view<Facade>> views;
  dyn_obs.reserve(kDataSize);
  views.reserve(kDataSize);
  int series = 0;
  std::apply(
      [&](auto&... arena) {
        (void)series;
        (([&] {
           ++series;
           arena.reserve(kDataSize / kTypeSeries);
           for (int i = series - 1; i < kDataSize; i += kTypeSeries) {
             auto& o = arena.emplace_back(i);
             dyn_obs.emplace_back(&o);
             views.emplace_back(&o);
           }
         }()),
         ...);
      },
      arenas);

  run_invoke("asf::dyn (observer)", dyn_obs);
  run_relocate("asf::dyn (observer)", dyn_obs);
  run_invoke("pro::proxy_view", views);
  run_relocate("pro::proxy_view", views);

  std::vector<VBase*> rawptrs;
  rawptrs.reserve(uniq.size());
  for (auto& u : uniq)
    rawptrs.push_back(u.get());
  run_invoke("virtual (raw ptr)", rawptrs);

  // Reference rows: direct + statically-bound iface on one object.
  {
    auto one = Impl<0>{7};
    std::int64_t acc = 0;
    Timer t;
    for (int i = 0; i < kDataSize; ++i)
      acc += one.Fun();
    sink += acc;
    std::println("  invoke {:<34} {:>9.3f} ms  ({:.3f} ns/call)", "direct",
                 t.ms(), t.ms() * 1e6 / kDataSize);
    asf::iface<Ops, Impl<0>> ifc{one};
    acc = 0;
    Timer t2;
    for (int i = 0; i < kDataSize; ++i)
      acc += ifc->Fun();
    sink += acc;
    std::println("  invoke {:<34} {:>9.3f} ms  ({:.3f} ns/call)", "asf::iface",
                 t2.ms(), t2.ms() * 1e6 / kDataSize);
  }
}

}  // namespace

int main() {
  run_size<Small, VSmall, asf::cache_ptr<16, 8>>("Small (int seed)");
  run_size<Large, VLarge, asf::cache_ptr<48, 8>>("Large (5 ptrs + int)");
  std::println("sink = {}", sink);
  return 0;
}
