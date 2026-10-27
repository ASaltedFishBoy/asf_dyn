# cast and construction helpers

Header: `cast.hpp`.

Call form is left-associative: `to_dyn<I>(&obj)->method()` needs no extra parentheses.

`as<To>()` is a member only. dyn: `dyn_view<Layout, To>` / `c_dyn_view<Layout, To>` (same `vt`). iface: Self rewrap on the inheritance chain (up or down). See [dyn.md](dyn.md), [iface.md](iface.md).

## `trait_cast` / `c_trait_cast`

```cpp
template <template <class> class Trait>
inline constexpr trait_cast_t<Trait> trait_cast;

template <template <class> class Trait>
inline constexpr c_trait_cast_t<Trait> c_trait_cast;
```

```cpp
template <template <class> class From, class Arg>
iface<Trait, Arg> trait_cast(iface<From, Arg>);

template <template <class> class From, class Ptr>
dyn<Trait, Ptr> trait_cast(dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
dyn<Trait, Ptr> trait_cast(dyn<From, Ptr>&&);

template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> trait_cast(c_dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> trait_cast(c_dyn<From, Ptr>&&);
```

iface: Trait related either way; target `Self` constructible from the source `Self`. Same as `h.trait_cast<Trait>()` / the converting constructor. Pointer-semantic: one overload; handle const / value category is ignored.

dyn family: upcast only. `Ptr` from the source. Lvalue copies, rvalue moves.

```cpp
template <template <class> class From, class Arg>
c_iface<Trait, Arg> c_trait_cast(iface<From, Arg>);

template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> c_trait_cast(dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> c_trait_cast(dyn<From, Ptr>&&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> c_trait_cast(c_dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> c_trait_cast(c_dyn<From, Ptr>&&);
```

Objects and user mirrors use `to_iface` / `iface`, not `trait_cast`.

## `to_dyn` / `to_c_dyn`

```cpp
template <template <class> class Trait, class Spec = void>
inline constexpr to_dyn_t<Trait, Spec> to_dyn;

template <template <class> class Trait>
inline constexpr to_c_dyn_t<Trait> to_c_dyn;
```

Not a dyn-to-dyn upcast (use `trait_cast`). Not a naked object (pass `T*` or a sourced mirror).

```cpp
template <class P>
/* see below */ to_dyn(P&& p);
```

`P` is a bindable pointer; the result follows the pointer family: `dyn<Trait>` (`void*`) from `T*`; `dyn<Trait, std::unique_ptr<void, D'>>` from `unique_ptr`; `dyn<Trait, std::shared_ptr<void>>` from `shared_ptr`; `dyn<Trait, std::weak_ptr<void>>` from `weak_ptr`. Keeps the deleter. `vt` from Trait + pointee.

Accepted `unique_ptr` deleters: `std::default_delete`, `alloc_delete`, `dyn_default_delete`, and any custom `D` invocable with `T*` (adopted **deleter-kind**: `D` carries the full teardown itself; no `vt[-1]` is attached). See [pointers.md](pointers.md).

```cpp
template <class T, class Src>
dyn<Trait> to_dyn(mirror<T, Src> const& m);
```

```cpp
template <template <class> class From, class Arg>
dyn<Trait> to_dyn(iface<From, Arg> handle);
```

When `Arg&` can construct `dyn<Trait>`. `iface<Trait, Person>` cannot. When a sourced mirror can, that iface can.

```cpp
template <class U>
cache_dyn<Trait, sizeof(U), alignof(U), /* Trait-erased ops */>
to_dyn<Trait, cache_ptr>(U&& object);

template <class Ptr>
cache_dyn<Trait, Size, Align, /* erased ops for Ptr */>
to_dyn<Trait, cache_ptr<Size, Align, Ops>>(cache_deref<Ptr> w);

template <class Ptr>
cache_dyn<Trait, sizeof(Ptr), alignof(Ptr), /* ... */>
to_dyn<Trait>(cache_deref<Ptr> w);

template <class U>
unique_dyn<Trait> to_dyn<Trait, std::unique_ptr>(U&& object);
template <class Alloc, class U>
dyn<Trait, std::unique_ptr<void, alloc_delete<Alloc>>>
to_dyn<Trait, std::unique_ptr>(Alloc alloc, U&& object);
```

Value (or sourced-mirror Src) is forward-constructed into the final storage. No intermediate dyn. Cache Spec fit (`sizeof` / `alignof` vs Size/Align) is a compile error. `Alloc` is an optional first argument. `std::allocator` stays the default `unique_dyn` (`::operator delete`), not `alloc_delete`.

Types with **`using asf_adopt_proxy = void;`** (or any same-named member; **presence only**) and the adopt-wrapper protocol in [pointers.md](pointers.md)#cache_deref — including **`cache_deref<Ptr>`** — hold a dereferenceable `Ptr`. Pass them to `to_dyn` with a `cache_ptr` Spec (or omit Spec to use `cache_ptr_for_storage<Ptr>`). The Ptr is placed into the cache buffer; Spec fit is a compile error. The VT is keyed on `Ptr` with `As` = pointee: methods dereference; deep construct / destroy stay on `Ptr`. `meta.type` reports the pointee. If cache ops cover away copy and/or move, the matching deep `op_construct` slots stay empty.

```cpp
template <template <class...> class Ptr, template <class> class From, class SrcP>
dyn<Trait, Ptr<void>> to_dyn(dyn<From, SrcP>&& handle);
```

`Ptr<void>` must be deleter-kind. Shallow handoff: the object stays in the source allocation; the target is `P(raw, d)` (`d` destroys then frees). Not a deep clone. Example: `examples/en/07_pointer_trait.cpp`. See [pointers.md](pointers.md).

`to_c_dyn<Trait>(…)` is the same acceptance, result is `c_dyn`.

Pointer/mirror bind of a non-final intermediate: [dyn.md](dyn.md)#pointer--mirror-bind-and-final.

```cpp
template <template <class> class Trait, class As>
struct instantiate_dyn;
```

## `to_sub_dyn` / `to_c_sub_dyn`

```cpp
template <template <class> class Trait>
inline constexpr to_sub_dyn_t<Trait> to_sub_dyn;

template <template <class> class Trait>
inline constexpr to_c_sub_dyn_t<Trait> to_c_sub_dyn;
```

```cpp
template <template <class> class Final, class Ptr>
sub_dyn<Trait, Ptr> to_sub_dyn(dyn<Final, Ptr> const&);
template <template <class> class Final, class Ptr>
sub_dyn<Trait, Ptr> to_sub_dyn(dyn<Final, Ptr>&&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_sub_dyn(c_dyn<Final, Ptr> const&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_sub_dyn(c_dyn<Final, Ptr>&&);

template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(dyn<Final, Ptr> const&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(dyn<Final, Ptr>&&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(c_dyn<Final, Ptr> const&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(c_dyn<Final, Ptr>&&);
template <class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(sub_dyn<Trait, Ptr> const&);
template <class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(sub_dyn<Trait, Ptr>&&);
```

`Trait` must be a section of `Final`. Same as `sub_dyn<Trait, Ptr>(d)` / `c_sub_dyn<Trait, Ptr>(d)`.

## `get_proxy` / `get_rproxy` / `get_forward_proxy`

```cpp
template <template <class> class To>
inline constexpr get_proxy_t<To> get_proxy;

template <template <class> class To>
inline constexpr get_rproxy_t<To> get_rproxy;

template <template <class> class To>
inline constexpr get_forward_proxy_t<To> get_forward_proxy;
```

`To` is a Trait base of the handle's Trait.

```cpp
template <template <class> class From, class Ptr>
auto get_proxy(dyn<From, Ptr> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Ptr>
auto get_proxy(c_dyn<From, Ptr> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Arg>
auto get_proxy(iface<From, Arg> const& h) -> decltype(h.template proxy<To>());

template <template <class> class From, class Ptr>
auto get_rproxy(dyn<From, Ptr> const& h) -> decltype(h.template rproxy<To>());
template <template <class> class From, class Ptr>
auto get_rproxy(c_dyn<From, Ptr> const& h) -> decltype(h.template rproxy<To>());
template <template <class> class From, class Arg>
auto get_rproxy(iface<From, Arg> const& h) -> decltype(h.template rproxy<To>());

template <template <class> class From, class Ptr>
auto get_forward_proxy(dyn<From, Ptr> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Ptr>
auto get_forward_proxy(dyn<From, Ptr>&& h) -> decltype(std::move(h).template rproxy<To>());
template <template <class> class From, class Ptr>
auto get_forward_proxy(c_dyn<From, Ptr> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Ptr>
auto get_forward_proxy(c_dyn<From, Ptr>&& h) -> decltype(std::move(h).template rproxy<To>());
template <template <class> class From, class Arg>
auto get_forward_proxy(iface<From, Arg> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Arg>
auto get_forward_proxy(iface<From, Arg>&& h) -> decltype(std::move(h).template rproxy<To>());
```

`get_proxy` / `get_rproxy` take `const&`. `get_forward_proxy` is lvalue `proxy<To>()`, rvalue `rproxy<To>()`. Same for `c_dyn` / `dyn_view`. This Trait uses the handle's own proxy; a base Trait uses that view's proxy.

## `construct`

Deep-construct an already-erased object. Requires dyn `op_construct` on the Trait. Teardown is always `vt[-1]`. `op_construct` dispatches by arguments, not only copy/move. Extra function arguments after `*dyn` / `rproxy` go to that overload. `Alloc` is an optional first argument: if the first argument is the proxy, there is no allocator; if the second is the proxy, the first is the allocator. `OutPtr` retargets `Ptr` only as a side effect of that deep construct; it is not a standalone operation. The source must be live; `construct` does not empty-check (`!h` is the caller's job).

Deep-constructing from a sourced-mirror dyn requires `op_construct` to return a `mirror` (it rebinds the original source). An object result would alias a temporary that dies with the call, so that slot is emitted as a stub and constructing traps.

```cpp
template <template <class> class Trait, class SrcP, class... Extra>
dyn<Trait, SrcP> construct(/* *dyn or rproxy */, Extra&&... extra);
```

Same `Ptr` as the source. `construct(d.rproxy())` selects the rvalue `op_construct` when the source is not const. `construct(*cd)` always selects the lvalue overload.

```cpp
template <template <class...> class OutPtr, template <class> class Trait, class SrcP, class... Extra>
dyn<Trait, /* erased OutPtr<void, …> */> construct(/* *dyn or rproxy */, Extra&&... extra);

template <template <class...> class OutPtr, class Alloc, template <class> class Trait, class... Extra>
auto construct(Alloc alloc, /* *dyn or rproxy */, Extra&&... extra);
```

`OutPtr` is `std::unique_ptr` and there is no `alloc`: `unique_dyn<Trait>`.

`OutPtr` is `std::unique_ptr` and there is `Alloc alloc` as the first argument: `dyn<Trait, std::unique_ptr<void, alloc_delete<Alloc>>>`. `std::allocator` stays `unique_dyn`. Other `OutPtr` types keep their type; `alloc` is only used for the allocation.

`OutPtr` is `cache_ptr`: `cache_dyn<Trait, …>` with Trait-deduced `cache_ops`. Empty `vt` if the erased source's storage does not fit (size/align from the vt).

A custom `OutPtr` (for example `fake_uptr`): `dyn<Trait, OutPtr<void>>`.

Prefer `to_dyn<Trait, std::unique_ptr>(value)` (optional `alloc` first) to own a value directly.

## `unsafe_downcast` / `dyn_cast`

```cpp
template <template <class> class Trait>
inline constexpr unsafe_downcast_t<Trait> unsafe_downcast;

template <template <class> class From, class Ptr>
dyn<Trait, Ptr> unsafe_downcast(dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
dyn<Trait, Ptr> unsafe_downcast(dyn<From, Ptr>&&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> unsafe_downcast(c_dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> unsafe_downcast(c_dyn<From, Ptr>&&);
```

Free function only. Same ptr, same `vt`. Misuse is UB. After a Redirect upcast the handle may hold a cover `vt`; naming a more-derived Trait is then UB. [multi_vt.md](multi_vt.md). For `cache_dyn`, the target chain must not add copy/move `op_construct` the source chain lacked.

`#if ASF_HAS_DYN_RTTI` (experimental; guaranteed only to the creation node and above it, or exact with `ASF_HAS_DYN_INTERMEDIATE=OFF`, [features.md](features.md)):

```cpp
template <template <class> class Trait>
inline constexpr dyn_cast_t<Trait> dyn_cast;

template <template <class> class From, class Ptr>
/* dyn<Trait, Ptr> or empty */ dyn_cast(dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
/* dyn<Trait, Ptr> or empty */ dyn_cast(dyn<From, Ptr>&&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> dyn_cast(c_dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> dyn_cast(c_dyn<From, Ptr>&&);

template <template <class> class Trait, class Ptr>
const std::type_info& dyn_typeid(dyn<Trait, Ptr> const& h);
```

Static upcast when possible, otherwise meta lookup. Empty only when the id is missing.

## See also

- [dyn.md](dyn.md)
- [iface.md](iface.md)
- [features.md](features.md)
- [multi_vt.md](multi_vt.md)
