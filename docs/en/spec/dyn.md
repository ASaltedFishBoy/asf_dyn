# dyn-family handles

Header: `dyn.hpp` (via `interface.hpp`).

`dyn<Trait, Ptr = void*>` holds `{ complete erased Ptr, vt }`. Default `Ptr = void*` is non-owning. Casts keep the complete `Ptr` type unless a constructor says otherwise.

```cpp
template <template <class> class Trait, class Ptr = void*>
class dyn;

template <template <class> class Trait, class Ptr = void*>
class c_dyn;

template <template <class> class Trait, class Ptr = void*>
class sub_dyn;

template <template <class> class Trait, class Ptr = void*>
class c_sub_dyn;
```

```cpp
template <template <class> class Trait, class Deleter = std::default_delete<void>>
using unique_dyn = dyn<Trait, std::unique_ptr<void, /* default unique_dyn deleter */>>;
template <template <class> class Trait>
using shared_dyn = dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_dyn = dyn<Trait, std::weak_ptr<void>>;
template <template <class> class Trait, std::size_t Size,
          std::size_t Align = alignof(std::max_align_t), class Ops = cache_ops<>>
using cache_dyn = dyn<Trait, cache_ptr<Size, Align, Ops>>;

template <template <class> class Trait>
using shared_sub_dyn = sub_dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_sub_dyn = sub_dyn<Trait, std::weak_ptr<void>>;

template <template <class> class Trait>
using shared_c_dyn = c_dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_c_dyn = c_dyn<Trait, std::weak_ptr<void>>;
template <template <class> class Trait>
using shared_c_sub_dyn = c_sub_dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_c_sub_dyn = c_sub_dyn<Trait, std::weak_ptr<void>>;
```

`unique_dyn<Trait>` and `unique_dyn<Trait, std::default_delete<void>>` are the same type. Pointer kinds: [pointers.md](pointers.md). Construction helpers: [cast.md](cast.md).

## Construction — `dyn`

```cpp
dyn() noexcept;
dyn(std::nullptr_t) noexcept;
dyn(dyn const&) = default;
dyn(dyn&&) = default;
dyn& operator=(dyn const&) = default;
dyn& operator=(dyn&&) = default;

template <class U>
dyn(U* p);   // observation Ptr only; not void; not const U; not a naked object
```

`U*` bind requires a final leaf (or `ASF_HAS_DYN_INTERMEDIATE`). See [pointer / mirror bind](#pointer--mirror-bind-and-final).

```cpp
template <class T, class Src>
dyn(mirror<T, Src> const& v);   // sourced; not a heap Ptr; T is not const
```

Does not require every section to opt into `[[=mirror]]`. Rules: [mirror.md](mirror.md).

```cpp
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP const&, Ptr>)
dyn(dyn<From, SrcP> const& o);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP &&, Ptr>)
dyn(dyn<From, SrcP>&& o);
template <template <class> class From, class SrcP>
dyn& operator=(dyn<From, SrcP> const& o);
template <template <class> class From, class SrcP>
dyn& operator=(dyn<From, SrcP>&& o);
```

Trait upcast (includes `From == Trait`) and `SrcP` -> this `Ptr` together. Same `Ptr`: copy/move (or cache via vt). Otherwise this `Ptr` is constructed from that value category of `SrcP`. Explicitness follows the pointer types (`weak_ptr` from `shared_ptr` is implicit). This is not `construct`. Same `Ptr` is `trait_cast<Trait>(o)`. MULTI_VT may change `vt`. [multi_vt.md](multi_vt.md). Handle conversion: [pointers.md](pointers.md).

## Construction — `c_dyn`

```cpp
c_dyn() noexcept;
c_dyn(std::nullptr_t) noexcept;
c_dyn(c_dyn const&) = default;
c_dyn(c_dyn&&) = default;
c_dyn& operator=(c_dyn const&) = default;
c_dyn& operator=(c_dyn&&) = default;

template <class U>
c_dyn(U* p);   // observation Ptr; const pointee allowed

template <class T, class Src>
c_dyn(mirror<T, Src> const& v);

template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP const&, Ptr>)
c_dyn(c_dyn<From, SrcP> const& o);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP &&, Ptr>)
c_dyn(c_dyn<From, SrcP>&& o);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP const&, Ptr>)
c_dyn(dyn<From, SrcP> const& o);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP &&, Ptr>)
c_dyn(dyn<From, SrcP>&& o);
```

Same convert / bind rules as `dyn`, const-self slots.

## Declarations — `dyn<Trait, Ptr>`

Not `weak_ptr` unless noted. `proxy_t` / `rproxy_t` / `c_proxy_t` are this handle's proxy types (observer `void*` inside).

```cpp
using proxy_t = /* mutable Trait proxy */;
using rproxy_t = /* move-semantic Trait proxy */;
using c_proxy_t = /* const Trait proxy */;

proxy_t proxy() const;
rproxy_t rproxy() const;
c_proxy_t c_proxy() const;            // c_dyn: same as proxy()
proxy_t operator->() const;
proxy_t operator*() const;

template <template <class> class To>  // To is a Trait base of this Trait
auto proxy() const -> /* To-shaped handle's proxy_t */;
template <template <class> class To>
auto rproxy() const -> /* To-shaped handle's rproxy_t */;
template <template <class> class To>
auto c_proxy() const -> /* To-shaped handle's c_proxy_t */;
```

`To == Trait` is this handle's `proxy_t` / `rproxy_t` / `c_proxy_t`. Otherwise `as<To>().proxy()` / `rproxy()` / `c_proxy()`. On `c_dyn`, `c_proxy` is the same type as `proxy`.

```cpp
template <template <class> class To>  // To is a Trait base
dyn_view<Trait, To> as() const;       // dyn
template <template <class> class To>
c_dyn_view<Trait, To> as() const;     // c_dyn
```

Same `vt`, no convert. Storable upcast is the converting constructor / `trait_cast<To>(h)`, not `as`.

```cpp
Ptr& get() noexcept;
Ptr const& get() const noexcept;

void* raw() const noexcept;              // dyn
void const* raw() const noexcept;        // c_dyn

void reset();
void destroy();   // dyn; not shared_ptr / weak_ptr. Always vt[-1] (~Src)

std::size_t size() const noexcept
  requires(/* Trait has dyn op_construct */);  // empty handle -> 0
std::size_t align() const noexcept
  requires(/* Trait has dyn op_construct */);  // sizeof / alignof Storage, vt tail

explicit operator bool() const noexcept;
bool operator==(dyn const& d, std::nullptr_t) noexcept;
bool operator==(dyn const& a, dyn const& b) noexcept;

template <template <class> class Other>
bool operator==(dyn<Trait, Ptr> const& a, dyn<Other, Ptr> const& b) noexcept;
```

`operator bool` / `== nullptr`: non-null raw pointer, or `!expired()` on `weak_ptr`. Cross-Trait `==` compares `raw()`. No declared `!=` (C++20 may rewrite). No `<=>`.

When the Trait has a dyn `op_construct`, `size()` / `align()` are Storage `sizeof` / `alignof` from the vt tail after the method slots. Empty handle: 0. No `op_construct`: these members are absent.

```cpp
std::pair<Ptr, dyn<Trait, void*>> release();
```

Lvalue. Not `cache_ptr` / `weak_ptr`. Moves `Ptr` out; `dyn<Trait, void*>` keeps the same `vt` and observes. `*this` is empty.

```cpp
dyn<Trait, std::shared_ptr<void>> lock() const;  // weak_dyn only
bool expired() const noexcept;                   // weak_dyn only
```

Named methods, convert, construct, and assign are not on the dyn handle. Same as iface: `*h` / `h->` ([traits.md](traits.md)#proxy).

`#if ASF_HAS_DYN_RTTI`:

```cpp
const std::type_info& typeid_of() const noexcept;  // empty handle -> typeid(void)
```

Proxy operators: [traits.md](traits.md). dyn has no reverse friends. `*dyn` copy/move is shallow. Deep: `construct(*d)` / `construct(d.rproxy())`.

## Declarations — `sub_dyn` / `c_sub_dyn`

Section slice of one Trait on a leaf/final table. No second implicit upcast / `trait_cast`. Independent of `ASF_HAS_DYN_MULTI_VT`. Named methods only on the proxy; no catalog operators. `weak_ptr` has no `->` / `*`.

```cpp
sub_dyn() noexcept;
sub_dyn(std::nullptr_t) noexcept;
sub_dyn(sub_dyn const&) = default;
sub_dyn(sub_dyn&&) = default;
sub_dyn& operator=(sub_dyn const&) = default;
sub_dyn& operator=(sub_dyn&&) = default;

template <template <class> class Final>
explicit sub_dyn(dyn<Final, Ptr> const& handle) noexcept;
template <template <class> class Final>
explicit sub_dyn(dyn<Final, Ptr>&& handle) noexcept;

proxy_t operator->();
proxy_t operator*();

sub_dyn<Trait, std::shared_ptr<void>> lock() const;  // weak_sub_dyn

explicit c_sub_dyn(sub_dyn<Trait, Ptr> const& w) noexcept;
explicit c_sub_dyn(sub_dyn<Trait, Ptr>&& w) noexcept;
template <template <class> class Final>
explicit c_sub_dyn(dyn<Final, Ptr> const& handle) noexcept;
template <template <class> class Final>
explicit c_sub_dyn(dyn<Final, Ptr>&& handle) noexcept;
template <template <class> class Final>
explicit c_sub_dyn(c_dyn<Final, Ptr> const& handle) noexcept;
template <template <class> class Final>
explicit c_sub_dyn(c_dyn<Final, Ptr>&& handle) noexcept;

proxy_t operator->() const;
proxy_t operator*() const;
```

`get` / `ptr` / `operator bool` match `dyn` on the same `Ptr`. `reset` matches when the `Ptr` does not vt_destroy, or when `pointer_trait::release` accepts this handle. Named methods: `*h` / `h->`. Free: `to_sub_dyn` / `to_c_sub_dyn` in [cast.md](cast.md).

No negative prefix (`destroy`, `typeid_of`) on `sub_dyn`. Those slots sit at the table origin, and a section cannot know whether it sits there: even a Trait with no bases may be a sibling of another section. A vt_destroy `Ptr` is allowed only if `pointer_trait::release` accepts this handle.

## Pointer / mirror bind and `final`

From `T*` / sourced mirror (including `to_dyn`), the vtable comes from a **leaf**. With `ASF_HAS_DYN_INTERMEDIATE` (default, [features.md](features.md)):

- Final leaf (`Trait<void> final` or `Trait<As> final`): injects that leaf table.
- Non-final intermediate: only ADL-fetches; does not inject.

Required: explicit `instantiate_dyn<Trait, As>` of a covering final leaf, or a pointer/mirror bind (or other ODR-use) of that leaf. Else link failure.

Forbidden: a final ancestor forbids a derived Trait pointer/mirror bind (`static_assert`).

Several finals for the same base: same-TU first instantiated wins; cross-TU is internal UB.

`iface` does not require `final`.

```cpp
template <template <class> class Trait, class As>
struct instantiate_dyn;
```

Explicitly instantiate: `template struct asf::instantiate_dyn<Trait, As>;`.

## See also

- [pointers.md](pointers.md)
- [cast.md](cast.md)
- [iface.md](iface.md)
- [features.md](features.md)
- [multi_vt.md](multi_vt.md)
