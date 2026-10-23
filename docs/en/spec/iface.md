# iface

Header: `iface.hpp`.

## Role

`iface<Trait, T>` is a Trait handle that **keeps the concrete type**. Calls use the implementation bind. There is no type erasure.

`iface<Trait, T>`: `Self` is `T&`, or the Trait's impl mirror when the layout is `[[=mirror]]`.

`iface<Trait, const T>`: `const T&`, or that impl mirror with a const view.

`iface<Trait, mirror<T, Src>>`: `Self` is `mirror<T, Src>`.

`iface<Trait, mirror<const T, Src>>`: `Self` is `mirror<const T, Src>`.

The concrete `Self` for a plain object:

| Form | Non-mirror Trait | `[[=mirror]]` layout |
| --- | --- | --- |
| `iface<Trait, T>` | `T&` | `mirror<T>`; `identity_mirror<T>` (= `mirror<T, T>`) when some impl method of `Trait<T>` takes a `mirror<T, T>`-shaped self |
| `iface<Trait, const T>` | `const T&` | `mirror<const T>`; `mirror<const T, T>` on the same condition |

Storage follows that: a reference `Self` (`T&` / `const T&`) means the handle stores a pointer to the object; a mirror `Self` is stored by value.

`c_iface<Trait, T>`: `iface<Trait, const T>`.

`c_iface<Trait, mirror<View, Src>>`: `iface<Trait, mirror<const View, Src>>`.

```cpp
template <template <class> class Trait, typename T>
class iface;

template <template <class> class Trait, typename T>
using c_iface = /* const-self of T, or of the mirror view */;
```

`Arg` below is the second template argument of the handle (`T` / `const T` / `mirror<…>`).

## Construction

```cpp
template <class V>
explicit iface(V&& object);
```

`Self` must be constructible from `V`. `V` must not be this handle type. For a plain object the argument must be an lvalue (the handle stores its address). Binding from a `mirror` (sourced or unsourced) is ungated: every method stays callable; `[[=mirror]]` only chooses the presented Self at each call (see [annotations.md](annotations.md), [mirror.md](mirror.md)).

A plain-object `iface` stores `T*` / `const T*` and is rebindable by assignment. An `iface` is **always bound**: there is no null state, no default constructor, and no `operator bool` / null comparison. (Contrast: `dyn` is pointer semantics — nullable, default-constructible, with `operator bool`.) `==` is identity by address, not a null check. A mirror-backed `iface` keeps its mirror value: sourced mirrors also compare through `src_of` / the private `Src*` identity; `mirror` itself is not default-constructible.

```cpp
iface(iface const&) = default;
iface& operator=(iface const&) = default;

template <template <class> class From, typename Arg>
iface(iface<From, Arg> o);
template <template <class> class From, typename Arg>
iface& operator=(iface<From, Arg> const& o);
```

Trait related either way. This handle's `Self` must be constructible from `Arg&` or `Arg const&`. Keeps `T` / `Src`. Same-Trait mutable → const-self is this ctor: `const T` / `mirror<const T, Src>` is constructible from the mutable `Arg`.

```cpp
template <template <class> class Trait, class V>
constexpr auto to_iface(V&& object);
```

Factory: it picks the second template argument. `iface` cannot (`Trait` plus `T` / `const T` / `mirror<…>`). After that, construction is the `explicit iface(V&&)` above (`Self` from `V`).

`V` is a sourced `mirror<View, Src>`: `iface<Trait, mirror<View, Src>>`.

`V` is unsourced mirror-like: `iface<Trait, mirror<View>>` (bind ungated; `[[=mirror]]` at each call).

Otherwise: `iface<Trait, std::remove_reference_t<V>>` (`const Person&` → `iface<Trait, const Person>`).

```cpp
template <template <class> class Trait, class V>
constexpr auto to_c_iface(V&& object);
```

Const-view counterpart of `to_iface` (symmetric with `to_c_dyn` next to `to_dyn`): a plain object gives `iface<Trait, const T>`; a sourced `mirror<View, Src>` gives `iface<Trait, mirror<const View, Src>>`; unsourced mirror-like gives `iface<Trait, mirror<const View>>`.

No `iface` form has a default constructor; every `iface` is bound at construction.

## Declarations — mutable `iface`

`iface<Trait, T>` and `iface<Trait, mirror<T, Src>>`.

```cpp
using proxy_t = /* this handle's proxy */;
using rproxy_t = /* this handle's rproxy */;
using c_proxy_t = /* const-self proxy of this handle */;
```

Sourced: `using mirror_t` is the held `mirror<T, Src>`.

```cpp
proxy_t proxy() const;
rproxy_t rproxy() const;
c_proxy_t c_proxy() const;
proxy_t operator->() const;
proxy_t operator*() const;

template <template <class> class To>  // To is a Trait base of this Trait
auto proxy() const -> typename /* To-shaped iface */::proxy_t;
template <template <class> class To>
auto rproxy() const -> typename /* To-shaped iface */::rproxy_t;
template <template <class> class To>
auto c_proxy() const -> typename /* To-shaped iface */::c_proxy_t;
```

`To`-shaped means `iface<To, T>` or `iface<To, mirror<T, Src>>`. Same-Trait `proxy<To>()` / `rproxy<To>()` / `c_proxy<To>()` is this handle's `proxy_t` / `rproxy_t` / `c_proxy_t`.

```cpp
template <template <class> class To>  // To is on this Trait's inheritance chain
iface<To, T> as() const;                       // unsourced
template <template <class> class To>
iface<To, mirror<T, Src>> as() const;          // sourced

template <template <class> class To>  // Trait related either way; target Self constructible
iface<To, T> trait_cast() const;
template <template <class> class To>
iface<To, mirror<T, Src>> trait_cast() const;

template <template <class> class To>
iface<To, const T> c_trait_cast() const;
template <template <class> class To>
iface<To, mirror<const T, Src>> c_trait_cast() const;

explicit operator c_iface<Trait, T>() const;                 // unsourced, same Trait
explicit operator iface<Trait, mirror<const T, Src>>() const;  // sourced, same Trait

Self get() const;
```

Plain object and sourced:

```cpp
bool operator==(iface const& a, iface const& b) noexcept;  // identity by address

template <template <class> class Trait2, typename View, typename Src2>
bool operator==(iface const& a,
                iface<Trait2, mirror<View, Src2>> const& b) noexcept;
```

On a plain object `==` compares the stored pointer; on a sourced mirror it compares `src_of` / the private `Src*` identity. Cross-Trait `==` requires a comparable sourced pair (related Traits, same mirror class, related `Src`). No declared `!=` (C++20 may rewrite it). No `<=>`.

Named methods, convert, construct, and assign are not on the iface handle. Same path as dyn: `*h` / `h->` ([dyn.md](dyn.md), [traits.md](traits.md)#proxy).

Handle `==` is identity (sourced `src_of`). Trait operators are on `*h` only: [traits.md](traits.md)#proxy.

## Declarations — const-self `iface`

`iface<Trait, const T>`, `iface<Trait, mirror<const T, Src>>`, and `c_iface<…>`.

```cpp
using proxy_t = /* const-self proxy of this handle */;
using rproxy_t = /* const-self rproxy of this handle */;
using c_proxy_t = proxy_t;

proxy_t proxy() const;
rproxy_t rproxy() const;
c_proxy_t c_proxy() const;            // same as proxy()
proxy_t operator->() const;
proxy_t operator*() const;

template <template <class> class To>  // To is a Trait base
auto proxy() const -> typename /* To-shaped const-self iface */::proxy_t;
template <template <class> class To>
auto rproxy() const -> typename /* To-shaped const-self iface */::rproxy_t;
template <template <class> class To>
auto c_proxy() const -> typename /* To-shaped const-self iface */::c_proxy_t;

template <template <class> class To>  // To is on this Trait's inheritance chain
iface<To, const T> as() const;
template <template <class> class To>
iface<To, mirror<const T, Src>> as() const;

template <template <class> class To>
iface<To, const T> trait_cast() const;
template <template <class> class To>
iface<To, mirror<const T, Src>> trait_cast() const;

template <template <class> class To>
iface<To, const T> c_trait_cast() const;
template <template <class> class To>
iface<To, mirror<const T, Src>> c_trait_cast() const;

Self get() const;
```

`==` for plain objects and sourced mirrors is the same shape as on mutable, with this `Self`. Named methods / convert / construct / assign: `*h` / `h->`, same as mutable.

## Free functions (same behavior)

```cpp
template <template <class> class To, template <class> class From, typename Arg>
auto get_proxy(iface<From, Arg> const& h)
    -> decltype(h.template proxy<To>());

template <template <class> class To, template <class> class From, typename Arg>
auto get_rproxy(iface<From, Arg> const& h)
    -> decltype(h.template rproxy<To>());

template <template <class> class To, template <class> class From, typename Arg>
auto get_forward_proxy(iface<From, Arg> const& h)
    -> decltype(h.template proxy<To>());
template <template <class> class To, template <class> class From, typename Arg>
auto get_forward_proxy(iface<From, Arg>&& h)
    -> decltype(std::move(h).template rproxy<To>());

template <template <class> class To, template <class> class From, typename Arg>
iface<To, Arg> trait_cast(iface<From, Arg> const&);

template <template <class> class To, template <class> class From, typename Arg>
c_iface<To, Arg> c_trait_cast(iface<From, Arg> const&);
```

`get_proxy` / `get_rproxy` take `const&`. `get_forward_proxy` is lvalue `proxy<To>()`, rvalue `rproxy<To>()`. `To` must be a Trait base for the proxy helpers. `trait_cast` / `c_trait_cast` require Trait related either way and a constructible target `Self`. Full table: [cast.md](cast.md).

## With dyn

`to_dyn` / `to_c_dyn` accept `iface<From, Arg>` when `Arg` itself can become the destination `dyn` / `c_dyn`. A plain object cannot `to_dyn`, and neither can `iface<Trait, Person>`. When `mirror<View, Src>` can, `iface<Trait, mirror<View, Src>>` can. Dyn-to-dyn upcast is `trait_cast`, not `to_dyn`. See [cast.md](cast.md), [dyn.md](dyn.md).

## See also

- [traits.md](traits.md)
- [mirror.md](mirror.md)
- [annotations.md](annotations.md)
- Examples: `examples/*/01_static.cpp`
