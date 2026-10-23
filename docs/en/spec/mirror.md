# mirror

Header: `mirror.hpp` (via `interface.hpp`).

`mirror<T>` is a reflection view of `T`'s non-static data members. Members are references, so a Trait method can treat `self` as a field bundle without `T` inheriting a base. Not pointer semantics.

```cpp
template <typename T, typename Src = void>
class mirror;

template <typename T, typename Src = void>
class mirror_base;
```

- `mirror<T>`: bare view. Construct from an object that converts to that `T` (`T&&` / `const T&` / non-reference `T` as `T&`), or a type convertible to that mirror.
- `mirror<T, Src>`: sourced. Also associated with source type `Src`.
- `mirror<T, T>`: the usual identity sourced form.

Constraints:

- mirror to mirror must not drop cv.
- Field reference qualification follows const / rvalue of `T`.
- A `mutable` member stays a mutable reference on `mirror<const T>`.
- `T` must be a class type (non-class types such as `int` cannot be mirrored).
- Bit-fields cannot be referenced; a class with bit-field members cannot be mirrored.
- Only **direct** non-static data members enter the mirror; base-class members are not part of `mirror<T>`. `bind_mirror` is the different interface for by-name binding, which does search bases (`to_mirror` accepts only the source and defined conversions; `bind_mirror` is name binding).

Example: `examples/*/06_mirror.cpp`.

## Traits and aliases

```cpp
template <class V>
inline constexpr bool is_mirror_v = /* V decays to mirror<T, Src> */;

template <class V>
inline constexpr bool is_sourced_mirror_v =
    /* V decays to mirror<T, Src> and Src is not void */;

template <class V>
concept mirror_like = is_mirror_v<V>;

template <class V>
concept sourced_mirror = is_sourced_mirror_v<V>;

template <class V>
using mirror_target_t = /* T of that mirror */;

template <class V>
using mirror_src_t = /* Src of that mirror, or void */;

template <typename T>
using identity_mirror = mirror<T, /* class of T */>;

template <typename T, typename Src = void>
using rmirror = mirror<T&&, Src>;
template <typename T, typename Src = void>
using c_mirror = mirror<const T, Src>;
template <typename T, typename Src = void>
using c_rmirror = mirror<const T&&, Src>;

template <typename To, typename FromMirror>
concept mirror_qualifies_to =
    /* FromMirror is a mirror of the same class as To, and its T& converts to To& */;
```

`is_mirror_v` / `mirror_like` recognize only `mirror<…>` (including cv/ref on the mirror type).

## Construction — bare `mirror<T>`

Implementation lives on `mirror_base<T>`. Default `mirror` is a thin shell:

```cpp
template <typename T, typename Src>
class mirror : public mirror_base<T, Src> {
 public:
  using mirror_base<T, Src>::mirror_base;
};
```

```cpp
template <typename U>
  requires(!mirror_like<U> && /* object converts to T if T is a ref, else T& */)
mirror_base(U&& obj);

template <typename V>
  requires(/* same class, different T qualification */)
explicit mirror_base(V&& other);

template <typename... FieldRefs>
  requires(/* one argument per field, each convertible to that field's mirror ref */)
explicit mirror_base(FieldRefs&&... refs);

mirror_base(const mirror_base&) = default;
mirror(mirror const&) = default;

mirror& operator=(mirror const& other);
template <typename V>
  requires(/* V is a mirror that can construct this */)
mirror& operator=(V const& other);

template <typename U>
mirror(U&&) -> mirror<U&&>;
```

`operator=` is pointer rebind: destroy, then copy-construct from the source mirror. It does not write fields. Field write is `assign_mirror`.

A user type can bind through a conversion operator:

```cpp
struct Alias {
  std::string name;
  operator asf::mirror<Person>() { return asf::mirror<Person>{name}; }
};
```

## Construction — sourced `mirror<T, Src>`

`mirror_base<T, Src>` inherits `mirror<T>`, so functions specialized on `mirror<T>` are also available on `mirror<T, Src>`.

```cpp
// no public pointer; identity is a private Src* (cv follows T)
// no default constructor (replaceable reference)

template <typename U>
  requires(/* U's class is Src, and U converts to mirror<T> implicitly */)
mirror_base(U&& obj);

template <typename U>
  requires(/* U's class is Src, but only an explicit mirror<T> ctor exists */)
explicit mirror_base(U&& obj);

template <typename U, typename Other>
  requires(/* same T, sourced Other */)
mirror_base(mirror<U, Other> const& other);

template <typename U, typename Other>
  requires(/* same class, different T qualification */)
explicit mirror_base(mirror<U, Other> const& other);

mirror_base(const mirror_base&) = default;
mirror(mirror const&) = default;

mirror& operator=(mirror const& other);
template <typename V>
  requires(/* V is a mirror that can construct this */)
mirror& operator=(V const& other);
```

`operator=` is the same pointer rebind (destroy + copy-construct). The `Src` identity is a private `Src*` (cv follows `T`), not a public `void*`. Conversion of identity goes through this `T`, not a covariant `Src*` conversion. The field is not named `ptr`, so it cannot hide a user member.

The sourced conversion keeps the strength of the unsourced one: implicit iff `U&` converts to `mirror<T>` implicitly; when only an explicit `mirror<T>` constructor exists (for example the single-field pack), the sourced constructor is explicit too.

## Free functions

```cpp
template <typename T = void, typename V>
  requires(!mirror_like<V>)
constexpr auto to_mirror(V&& obj);
```

Object only (not an existing mirror). The object must convert to the `T` this mirror wants: a reference `T` is used as-is (`T&` / `const T&` / `T&&`); a non-reference `T` is `T&`. A user conversion / `identity_mirror` also counts.

- `to_mirror<T>(obj)` → `mirror<T, std::remove_cvref_t<V>>`
- `to_mirror(obj)` → `mirror<Obj, Obj>`

```cpp
template <typename T = void, typename V>
  requires(!mirror_like<V> /* and T's fields exist on V by name */)
constexpr auto bind_mirror(V&& obj);
```

Same call shape as `to_mirror`. Each field of `T` binds to the same-named field of the object. The object need not convert to that mirror's `T`.

- `bind_mirror<T>(obj)` → `mirror<T, std::remove_cvref_t<V>>`
- `bind_mirror(obj)` → `mirror<Obj, Obj>`

```cpp
template <typename T = void, typename V>
constexpr auto to_pure_mirror(V&& v);
```

Bare `mirror<T>` (no `Src`). Object or an existing mirror.

- `to_pure_mirror<T>(obj|mirror)` → `mirror<T>`
- `to_pure_mirror(obj)` → `mirror<std::remove_reference_t<V>>`
- `to_pure_mirror(mirror)` → `mirror<that mirror's T>`

```cpp
template <typename T, typename Src>
  requires(!std::is_void_v<Src>)
constexpr /* Src& / const Src& / Src&& following T */
src_of(mirror<T, Src> const& m);
```

Sourced only. cv and ref follow `T`, not whether `m` is const (a const `mirror<Person, Person>` still yields `Person&`; `mirror<const Person, Person>` yields `const Person&`; `rmirror` yields `Src&&`).

```cpp
template <typename T, typename ST>
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, T&& src);
template <typename T, typename ST>
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, T const& src);
template <typename T, typename ST>
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, c_mirror<T> src);
template <typename T, typename ST>
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, rmirror<T> const& src);

template <typename T, typename ST>
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, T&& src);
template <typename T, typename ST>
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, T const& src);
template <typename T, typename ST>
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, c_mirror<T> src);
template <typename T, typename ST>
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, rmirror<T> const& src);
```

Write `src` into the object `dst` is bound to. Dest is `T&` or `T`. `src` is `T&&` (move) / `T const&` (copy), unsourced `c_mirror<T>` (copy), or unsourced `rmirror<T>` (move). Copyable mirror arguments are copies; `rmirror` is `const&`. Returns a copy of `dst`. Does not change a sourced dest's identity.

```cpp
template <typename T, typename Src = void>
constexpr /* decay of T */ value_of(mirror<T, Src> const& m);
```

Rebuild `T` from the stored field refs. When `Src` is `T`, take from `src_of`. Copy/move follows `T` (`T&&` moves, otherwise copies), not the mirror's value category.

## Customization: specialize `mirror`, inherit `mirror_base`

Field assembly, `assign_mirror`, `value`, and `src` live on the public `mirror_base<T[, Src]>`. `operator=` is pointer rebind on the default `mirror` shell. A specialization that replaces `mirror` should keep that rebind. To add functions, specialize `mirror` and inherit the matching `mirror_base` (do not edit `mirror_base` itself):

```cpp
template <>
class asf::mirror<Widget> : public asf::mirror_base<Widget> {
 public:
  using mirror_base<Widget>::mirror_base;
  int width() const { return w; }
};
```

`is_mirror_v` / `mirror_like` still recognize only `mirror<…>`.

## With iface / dyn

- `iface<Trait, T>`: Self comes from the implementation-side mirror (unsourced or sourced).
- `iface<Trait, mirror<T, Src>>`: `Self = mirror<T, Src>`.
- A user `mirror` with no Src binds to `iface` only (no `dyn` path). Mirror → `iface` (sourced or unsourced): bind is ungated and every method is callable; `[[=mirror]]` (method tag, else declaring Trait) only chooses the presented Self — the mirror, or the object (`Src`) / a materialized View.
- `dyn` from `mirror<T, Src>`: the whole layout need not opt into `mirror`. When view matches `Src`, Storage points at the object (mirror rebuilt per call); when view ≠ `Src` or `identity_mirror`, Storage points at that mirror (must outlive the dyn). Mirror sections use `Trait<T>`, non-mirror sections use `Trait<Src>`.
- On `dyn`, `op_construct` is not a mirror method: it does not follow the Trait `[[=mirror]]` tag. `self` is the object (`Src` when the mirror is sourced). Marking `op_construct` itself `[[=mirror]]` is ill-formed.
- Destroy is not a Trait method: every vtable has destroy at `vt[-1]` (`~Src`). A sourced mirror whose view differs from `Src` still uses ordinary `dyn`.

## See also

- [iface.md](iface.md)
- [dyn.md](dyn.md)
- [cast.md](cast.md)
