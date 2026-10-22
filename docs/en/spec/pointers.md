# Pointers and ownership (dyn Ptr)

Header: `dyn.hpp` (via `interface.hpp`).

`dyn` / `sub_dyn` describe erased storage through the complete pointer type `Ptr` (`void*`, `std::unique_ptr<void, Deleter>`, …). Handle aliases: [dyn.md](dyn.md). Construction helpers: [cast.md](cast.md).

Every physical vtable has destroy at `vt[-1]`: always `~Src` / `~Storage`. When `~Src` is trivial, every such slot is the same empty function, `vt_destroy_nop`.

The library does not probe `get_deleter()`. Disposal is `pointer_trait<P>::kind` only.

## Tags and `pointer_trait`

```cpp
struct pointer_vt_destroy_tag {
  explicit pointer_vt_destroy_tag() = default;
};
struct pointer_deleter_tag {
  explicit pointer_deleter_tag() = default;
};
inline constexpr pointer_vt_destroy_tag pointer_vt_destroy{};
inline constexpr pointer_deleter_tag pointer_deleter{};

enum class pointer_kind { observer, vt_destroy, deleter };

template <class P>
struct pointer_trait {};
```

A missing specialization, or a missing / unrecognized `kind`, is the observer.

```cpp
template <class P>
struct pointer_trait {
  using kind = /* pointer_vt_destroy_tag | pointer_deleter_tag */;
  static void release(/* Handle& */);  // optional; library passes *this
};
```

- observer (default): release does not run `destroy`.
- `pointer_vt_destroy_tag`: release runs `destroy`, then the pointer drops storage. Optional `release(Handle&)` replaces that default. On `sub_dyn`, a vt_destroy `Ptr` is allowed only when that call is valid.
- `pointer_deleter_tag`: dropping the handle does not run `destroy`. Building this `P` from a vt_destroy source is `P(raw, d)` (`d` destroys then frees).

A custom deleter with no specialization stays an observer. A complete compilable example is `examples/en/07_pointer_trait.cpp`.

Library specializations are on the standard pointer types themselves, not only after erasure to `void`:

```cpp
template <class T, class U>
struct pointer_trait<std::unique_ptr<T, std::default_delete<U>>> {
  using kind = pointer_vt_destroy_tag;
};
template <class T, class Alloc>
struct pointer_trait<std::unique_ptr<T, alloc_delete<Alloc>>> {
  using kind = pointer_vt_destroy_tag;
};
template <class T, class Free>
struct pointer_trait<std::unique_ptr<T, dyn_adopt_delete<Free>>> {
  using kind = pointer_deleter_tag;
};
template <class T>
struct pointer_trait<std::shared_ptr<T>> {
  using kind = pointer_deleter_tag;
};
template <std::size_t Size, std::size_t Align, class Ops>
struct pointer_trait<cache_ptr<Size, Align, Ops>> {
  using kind = /* Ops::destroy_type:
                  op_unset            -> absent (observer)
                  otherwise           -> pointer_vt_destroy_tag */;
};
```

`std::weak_ptr<T>` is not specialized (observer). `void*` is not a class type and is not specialized.

Passing a deleter is a shallow handoff, not a deep clone: the object stays in the allocation the source already owns. Same type: move. A vt_destroy source `release()`s the raw pointer and dest is `P(raw, d)`. `d` runs `vt[-1]` then frees. Dest's own deleter type is `d` when that type can be built from the destroy function. Else the default source deleter is not forwarded: `d` is `dyn_default_delete` (the default-Free `dyn_adopt_delete`: `::operator delete`). Any other source deleter type is passed into `dyn_adopt_delete<Free>` as `d`. `vt[-1]` is not dest's deleter.

## `alloc_delete`

```cpp
template <class Alloc = std::allocator<std::byte>>
struct alloc_delete {
  using allocator_type = Alloc;
  Alloc alloc{};
  std::size_t size = 0;

  alloc_delete() noexcept;
  explicit alloc_delete(std::size_t n) noexcept;
  alloc_delete(Alloc a, std::size_t n) noexcept;
  void operator()(void* p) const noexcept;
};
```

The handle runs `vt[-1]` first. This deleter only deallocates the sized block through `allocator_traits`. Not used when `Alloc` is `std::allocator` (that path stays `unique_dyn` / `::operator delete`).

## `dyn_adopt_delete` / `dyn_default_delete`

```cpp
template <class Free = /* detail::new_delete */>
struct dyn_adopt_delete {
  void (*destroy)(void*) = nullptr;

  dyn_adopt_delete() noexcept;
  dyn_adopt_delete(void (*fn)(void*), Free = {}) noexcept;
  void operator()(void* p) const noexcept;
};

using dyn_default_delete = dyn_adopt_delete<>;
```

`operator()` runs `destroy` (from `vt[-1]`) then `Free`. `Free` is a private base, so an empty `Free` costs nothing (EBO) and the default form is a single pointer. The default `Free` is `detail::new_delete`: the void-safe stand-in for `std::default_delete<void>` (`::operator delete`; the std type itself cannot be called on `void*`). `dyn_default_delete` names that default-Free form: it is the dest `d` when the source storage deleter is the default and dest has no `deleter_type` built from the destroy function. `unique_ptr<T, dyn_adopt_delete<Free>>` is deleter-kind; adopting one into a dyn (re)wires `destroy` to `vt[-1]`, so `~T` always runs before `Free` even when the source deleter's `destroy` was null.

## `cache_ops` / `cache_ptr`

```cpp
struct op_trivial {};       // bytes, or no payload destructor
struct op_unsupported {};   // that special member is deleted
struct op_unset {};         // not chosen yet
struct op_from_trait {      // erased cache_dyn form: deep vt op as that slot
  explicit op_from_trait() = default;
};

template <class T>
struct op_copy_of {
  void operator()(void* dst, void const* src) const;
};
template <class T>
struct op_move_of {
  void operator()(void* dst, void* src) const;
};
template <class T>
struct op_destroy_of {
  void operator()(void* p) const;
};
template <class T>
struct op_assign_copy_of {
  void operator()(void* dst, void const* src) const;
};
template <class T>
struct op_assign_move_of {
  void operator()(void* dst, void* src) const;
};

template <class Destroy = op_unset, class Copy = op_unset,
          class Move = op_unset, class CopyAssign = op_unset,
          class MoveAssign = op_unset>
struct cache_ops {
  using destroy_type = Destroy;
  using copy_type = Copy;
  using move_type = Move;
  using copy_assign_type = CopyAssign;
  using move_assign_type = MoveAssign;
};
```

`Destroy` cannot be `op_unsupported` (a compile error); copy and move may be stripped with it. Default of each slot is `op_unset`. A non-tag slot is a callable with that signature. The library provides the `op_*_of<T>` catalog above; a user type with the same call operator is also fine. `op_from_trait` is the erased `cache_dyn` form: the deep vt op (`op_construct` / `vt[-1]`) is applied as that shallow slot; the Ptr stores neither a type nor a function pointer for it.

A slot that is not one of the tags `op_trivial` / `op_unsupported` / `op_unset` / `op_from_trait` is a callable op carried by the type itself (invoked in place, no per-object state). Erasure for a `cache_dyn` keeps such an op riding along in the type the same way (still no stored function pointer).

A requested op may only weaken what `T` can do, or be stripped with `op_unsupported` (copy/move only).

Relocation of a cache payload picks the first applicable tier: `op_trivial` (bytes), the typed assigner into an already-live destination, and finally the slot's own construct + destroy. Only assign has a substitute: when no assign op is live, assigning into an occupied cache simply destroys and re-constructs.

Putting into a `cache_dyn` checks ops, then erases: each operation the Trait inheritance chain can perform becomes `op_from_trait`; otherwise a usable typed op rides along in the type (still no stored function pointer), or stays `op_trivial`. The two assign slots erase independently of the Trait: `op_unset` / `op_from_trait` degrade to `op_unset`, `op_trivial` stays, anything else rides along in the type. If the chain has no lifetime slots, a `cache_ptr` that itself can copy, move, and destroy may still enter. Downcast checks the Trait chain only when `cache_ops` does not supply lifetime itself.

A custom op may additionally overload on the participating public handles (any dyn handle flavor, deduced). Wherever a fully-formed handle exists at the call site, the handle form is tried first; the `void*` form is the baseline every op must provide. A construct op receives the source handle only (its destination is raw storage, not yet a handle); an assign op receives both. Destroy is offered a live handle everywhere except the final destructor, which has none left. An op callable only with handles is not live at the storage level (tier liveness is a storage-level property); for assign that just means the destroy + construct substitute runs.

A `cache_ptr` converts to another `cache_ptr` only when `Size` and `Align` both stay or grow; a shrink is a compile error. The payload then provably fits (it lived in the smaller-or-equal source buffer), so the transfer needs no runtime fit check. The same growth rule holds for the typed -> erased conversion (each slot transferred by its own op) and for erased `cache_dyn` -> `cache_dyn` conversion (a vt construct into the bigger buffer, when the chain provides that construct).

```cpp
template <std::size_t Size, std::size_t Align = alignof(std::max_align_t),
          class Ops = cache_ops<>>
class cache_ptr;

template <class T, class Ops = cache_ops<>>
using cache_ptr_for = cache_ptr<sizeof(T), alignof(T), Ops>;
```

The buffer travels with the object. `cache_ptr` is not relocatable by pointer steal; dyn move relocates through `op_construct`.

```cpp
using element_type = void;
using ops_type = Ops;
using cache_ptr_tag = void;
static constexpr std::size_t size = Size;
static constexpr std::size_t align = Align;
static constexpr std::size_t cache_capacity = Size;
static constexpr std::size_t cache_alignment = Align;

cache_ptr() noexcept;
cache_ptr(std::nullptr_t) noexcept;
cache_ptr(cache_ptr const&);            // per Ops
cache_ptr(cache_ptr&&);
cache_ptr& operator=(cache_ptr const&);
cache_ptr& operator=(cache_ptr&&);
~cache_ptr() = default;

// Grow-only conversions (S1 <= Size && A1 <= Align, else compile error):
template <std::size_t S1, std::size_t A1>
cache_ptr(cache_ptr<S1, A1, Ops> const&);
template <std::size_t S1, std::size_t A1>
cache_ptr(cache_ptr<S1, A1, Ops>&&);
template <std::size_t S1, std::size_t A1, class FromOps>
  // Ops is the erased form of FromOps
cache_ptr(cache_ptr<S1, A1, FromOps> const&);
template <std::size_t S1, std::size_t A1, class FromOps>
cache_ptr(cache_ptr<S1, A1, FromOps>&&);

void* get() const noexcept;
```

A `cache_ptr` tracks no occupancy: `get()` always returns the buffer and the ptr level has no empty/`nullptr` state or boolean query. Inside a `dyn`, emptiness is judged from the vtable (`vt_ == nullptr`). With no occupancy flag to store, a `cache_dyn` is exactly `Size + sizeof(vt)` bytes (16 B buffer -> 24 B handle).

## Ptrs the library provides

```cpp
// void*                                                 observer; default Ptr
// std::unique_ptr<void, alloc_delete<Alloc>>            vt_destroy
// std::unique_ptr<void, dyn_default_delete>            deleter
// std::unique_ptr<void, dyn_adopt_delete<Free>>         deleter
template <template <class> class Trait, class Deleter = std::default_delete<void>>
using unique_dyn = dyn<Trait, std::unique_ptr<void, /* default unique_dyn deleter */>>;
                                                          // vt_destroy; ::operator delete
template <template <class> class Trait>
using shared_dyn = dyn<Trait, std::shared_ptr<void>>;     // deleter
template <template <class> class Trait>
using weak_dyn = dyn<Trait, std::weak_ptr<void>>;         // observer
template <template <class> class Trait, std::size_t Size,
          std::size_t Align = alignof(std::max_align_t), class Ops = cache_ops<>>
using cache_dyn = dyn<Trait, cache_ptr<Size, Align, Ops>>;
```

`unique_dyn<Trait>` and `unique_dyn<Trait, std::default_delete<void>>` are the same type.

## Extra arguments at creation

`Alloc` is not a template parameter of `to_dyn` / `construct`. It is a function argument forwarded into Ptr construction. Signatures: [cast.md](cast.md).

```cpp
unique_dyn<Trait> to_dyn<Trait, std::unique_ptr>(U&& object);
dyn<Trait, std::unique_ptr<void, alloc_delete<Alloc>>>
to_dyn<Trait, std::unique_ptr>(Alloc alloc, U&& object);

unique_dyn<Trait> construct<std::unique_ptr>(/* *dyn */, Extra&&... extra);
dyn<Trait, std::unique_ptr<void, alloc_delete<Alloc>>>
construct<std::unique_ptr>(Alloc alloc, /* *dyn */, Extra&&... extra);

shared_dyn<Trait> construct<std::shared_ptr>(/* *dyn */, Extra&&... extra);
shared_dyn<Trait> construct<std::shared_ptr>(Alloc alloc, /* *dyn */, Extra&&... extra);
```

`construct(*d)` with no template arguments returns `dyn<Trait, SrcP>` (same `Ptr` as the source). `Alloc` is an optional first argument, detected by the position of `*dyn` / `rproxy`. Remaining arguments after the proxy dispatch to `op_construct`. `std::allocator` stays the default `unique_dyn` (`::operator delete`), not `alloc_delete`.

## Creation

The source must be able to become the target `Ptr` (or be a sourced mirror / an existing handle).

- Smart pointers and custom paths never call `::operator new/delete` directly. They use `allocator_traits`. Storage release for the default `unique_ptr` (`unique_dyn`) is `::operator delete`.
- A deep-cloned `shared_ptr` uses the given allocator (default `std::allocator`). An extra destroy function pointer is stored with the allocation.
- `to_dyn<Trait>(T*|unique_ptr|shared_ptr|weak_ptr)`: adopt as the matching `Ptr` and keep the deleter. Acceptance is by `pointer_kind` principle, not by type name: `T*` observes; `unique_ptr` with `default_delete` / `alloc_delete` is vt_destroy; `shared_ptr` / `weak_ptr` (deleter travels in the control block) adopt directly; `unique_ptr<T, dyn_default_delete>` adopts deleter-kind; `unique_ptr<T, D>` with any other custom `D` invocable with `T*` adopts **deleter-kind** through a `T`-typed wrapper (no `vt[-1]` attached; `D` itself is expected to run `~T` and free).
- Value to `unique_ptr`: `to_dyn<Trait, std::unique_ptr>(value)` — forward-construct into the final storage (no intermediate dyn). Optional allocator first: `to_dyn<Trait, std::unique_ptr>(alloc, value)`.
- Deep-construct an already-erased handle: `construct<Ptr>(*dyn, extra...)` / `construct<Ptr>(d.rproxy(), extra...)`. Optional allocator first: `construct<std::unique_ptr>(alloc, *dyn, extra...)`.

A sourced mirror whose view differs from `Src` still uses ordinary `dyn`. Destroy is always `vt[-1]` (`~Src`). See [mirror.md](mirror.md).

## Handle conversion

Trait upcast (includes `From == Trait`) and `SrcP` -> this `Ptr` together. Same `Ptr`: copy/move (or cache via vt). Otherwise this `Ptr` is constructed from that value category of `SrcP`. Explicitness follows the pointer types (`weak_ptr` from `shared_ptr` is implicit). This is not `construct` (deep clone). When the dest `Ptr` is deleter-kind and the source is vt_destroy, the handoff is as above. Same `Ptr` is `trait_cast<Trait>(o)`.

```cpp
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP const&, Ptr>)
dyn(dyn<From, SrcP> const& src);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP &&, Ptr>)
dyn(dyn<From, SrcP>&& src);
```

```cpp
std::pair<Ptr, dyn<Trait, void*>> release();
```

Lvalue. Not `cache_ptr` / `weak_ptr`. Moves `Ptr` out; `dyn<Trait, void*>` keeps the same `vt` and observes. The source handle is empty.

## See also

- [dyn.md](dyn.md)
- [cast.md](cast.md) (`construct`)
- [ref.md](ref.md) (sealed references; a different layer from dyn Ptr)
