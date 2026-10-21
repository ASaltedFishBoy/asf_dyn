# ref / cref

Header: `ref.hpp`. Not a Trait handle. A different layer from dyn `Ptr` ([pointers.md](pointers.md)).

## Role

`ref<P>` / `cref<P>` seal a pointer-semantic `P` so it is used as a **reference**: no construction or assignment from a bare `P` or from `nullptr`. Adopt with `as_ref` / `as_cref`. No null check; the type does not own lifetime.

const is on the pointee view (`*` / `->` / `()`), not on `P`. A non-const `cref` object can still reseat the stored `P` through `get()`.

## Pointer-semantic `P`

Every declaration below requires `P` to be pointer-semantic: from a `P const&` one can obtain a pointer to a **non-`void`** object. A raw pointer, or a smart-pointer-style type with `.get()` returning such a pointer.

## Types

```cpp
template <class P>
class cref;

template <class P>
using ref = /* cref<P> if P's pointee is const; otherwise the mutable ref of P */;
```

`cref<P>` is always present. `*` / `->` / `()` see a const pointee (like `*const T*`).

`ref<P>` when the pointee of `P` is not const: mutable view of that pointee.

`ref<P>` when the pointee of `P` is const: the same type as `cref<P>` (`std::same_as`).

```cpp
template <class P>
constexpr ref<P> as_ref(P p) noexcept(std::is_nothrow_move_constructible_v<P>);

template <class P>
constexpr cref<P> as_cref(P p) noexcept(std::is_nothrow_move_constructible_v<P>);
```

`as_ref` returns `ref<P>` (so a const pointee yields `cref<P>`). `as_cref` always returns `cref<P>`.

## Construction

On mutable `ref<P>` and on `cref<P>`:

```cpp
ref() = delete;
ref(std::nullptr_t) = delete;
ref& operator=(std::nullptr_t) = delete;
ref(P const&) = delete;
ref(P&&) = delete;
ref& operator=(P const&) = delete;
ref& operator=(P&&) = delete;

ref(ref const&) = default;
ref(ref&&) = default;
ref& operator=(ref const&) = default;
ref& operator=(ref&&) = default;
```

The same list on `cref` (replace `ref` with `cref`).

`cref<P>` also converts from a mutable `ref<P>` when that specialization exists (pointee of `P` is not const):

```cpp
constexpr cref(ref<P> const&) noexcept;
constexpr cref(ref<P>&&) noexcept;
```

Implicit. The other direction does not exist.

## Members

Present on mutable `ref<P>` and on `cref<P>`.

```cpp
using pointer = P;
static constexpr bool is_const = /* false on mutable ref, true on cref */;
```

Stored `P` (reseat / observe the pointer object; does **not** const-qualify the pointee):

```cpp
constexpr P& get() noexcept;                    // non-const handle
constexpr P const& get() const noexcept;        // const handle
constexpr operator P&() noexcept;               // non-const handle
constexpr operator P const&() const noexcept;   // const handle
```

Pointee view. On `cref`, the pointee is const. On mutable `ref`, it is not.

```cpp
constexpr decltype(auto) operator*() const;     // pointer obtained from P can be dereferenced
constexpr auto operator->() const noexcept;     // that pointer is a raw pointer
template <class... Args>
constexpr decltype(auto) operator()(Args&&... args) const;
                                                // the (const, if cref) pointee is callable with Args
```

Equality compares the raw addresses obtained from `P`, not `P`'s own `==`. Same `P`, either view. No declared `!=`, `<=>`, `operator bool`, or compare to `nullptr`. C++20 may rewrite `!=` from `==`.

```cpp
constexpr bool operator==(ref<P> const& a, ref<P> const& b) noexcept;
constexpr bool operator==(cref<P> const& a, cref<P> const& b) noexcept;
```

When a mutable `ref<P>` exists, `ref == cref` and `cref == ref` of that `P` are also present.

## Not provided

- Default / null handle
- Construction or assignment from a bare `P` (use `as_ref` / `as_cref`)
- Lifetime or null checks
- Trait methods, a vtable, `dyn` / `iface`

## See also

- [pointers.md](pointers.md) — `void*` and the other dyn Ptrs
- [dyn.md](dyn.md)
