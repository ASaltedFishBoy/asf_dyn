# Trait

A Trait is ASF's unit of abstraction: methods callable on `iface` / `dyn`.

## Shape

```cpp
template <typename T = void>
struct Trait;

template <>
struct Trait<void> : Base0<void>, Base1<void> {  // optional interface MI
  void foo(auto& self);
  auto bar(auto const& self) -> int { return 0; }  // default body
};

template <>
struct Trait<MyType> final {
  void foo(MyType& self) { /* ... */ }
};
```

| Specialization | Role |
| --- | --- |
| `Trait<void>` | Method list. May inherit other `Base<void>` |
| `Trait<T>` | Implementation for `T`. The `auto` self is usually `T&` / `const T&`. With `[[=mirror]]` it is `mirror<T>` / `c_mirror<T>` (`auto&` / `auto const&`) |
| `Trait<void> final` | Every `Trait<As>` of that Trait is also treated as final |
| `Trait<As> final` | Final only for that `As` (the usual dyn leaf) |

Implementation requirements: a non-static member on `Trait<T>` is invoked on a fresh `Trait<T>{}` per call, so `Trait<T>` must be default-constructible and is effectively stateless (any member state is discarded between calls); use `static` methods or stateless bodies.

`iface` does **not** require `final`.

**`dyn` pointer / mirror bind (intermediate leaves on by default):** the vtable is injected by a final leaf; a non-final intermediate Trait only ADL-fetches that table and **does not define it**. So you must explicitly instantiate `instantiate_dyn` or bind a covering **final leaf** first (or as well), or the link fails. If an ancestor is already final, binding a derived Trait is forbidden (`static_assert`). Full rules: [dyn.md](dyn.md)#pointer--mirror-bind-and-final. Macro and multi-final UB: [features.md](features.md)#asf_has_dyn_intermediate.

## Methods and the vtable

- Default: the method is a **dyn vtable slot** (like a virtual function).
- **Dyn slot**: `self` must be the first parameter, and there is at most one deduced template parameter (that is self).

Interface-property annotations may sit on the **type** (`Trait<void>`) or on a **method** (the interface). On the type, every method declared there inherits that annotation. On a method, the annotation overrides the type. Writing both the yes and no forms of a pair on the same declaration is ill-formed. Pairs: `[[=expl]]`/`[[=no_expl]]`, `[[=imple]]`/`[[=no_imple]]`, `[[=mirror]]`/`[[=no_mirror]]`. (`static` on a method is a declaration property, not an annotation.) See [annotations.md](annotations.md).

- A second deduced parameter cannot be dynamically dispatched (compile error); mark that overload `static` for iface only.
- `static`: not a dyn slot; the method is `iface` only (there is no opt-back-in).
- A `dyn` / `iface` handle exposes only **methods declared on this Trait**.
- The related-Trait view is the member `as<To>()` (`dyn`: same-table / upcast; `iface`: either way on the inheritance chain).
- The call surface is `proxy<To>()` / `rproxy<To>()`; the free functions are `get_proxy<To>`, `get_rproxy<To>`, and `get_forward_proxy<To>`. The pairs are in [cast.md](cast.md).

## Special method names

| Name | Meaning |
| --- | --- |
| *(destroy)* | Not a Trait method. Every vtable has destroy at `vt[-1]` (`~Src` / `~Storage`). A trivial destructor shares one empty function pointer. Handle `destroy()` calls that slot |
| `op_construct` | Erased construction. Returns `auto` or a concrete type (not `void`). `[[=imple]]` does not look this up by name: it selects the constructor with the declared parameters (`auto&&` is an rvalue). Overloads are ordinary dyn slots; dispatch is by arguments only (including `self` value category) |
| `op_assign*` | `(*h) = x` for a concrete RHS (not Src / `auto`) |
| `op_convert*` | `To(*h)`. `[[=expl]]` makes it explicit |

## Operators

Method names on the Trait. The proxy exposes the C++ operator in this table. Do not use `friend`. `self` may be any parameter. A dyn slot requires `self` first and at most one deduced template parameter. A later `self`, or a second deduced parameter, is iface only: mark that overload `static` and match by parameter position. A unary operator has only the Trait overload. dyn has no reversed operators.

| Name | Operator | Shape |
| --- | --- | --- |
| `op_add` | `+` | binary |
| `op_sub` | `-` | binary |
| `op_neg` | `-` | unary |
| `op_mul` | `*` | binary |
| `op_div` | `/` | binary |
| `op_mod` | `%` | binary |
| `op_xor` | `^` | binary |
| `op_bitand` | `&` | binary only. Unary `&` is not a Trait method |
| `op_bitor` | <code>&#124;</code> | binary |
| `op_compl` | `~` | unary |
| `op_not` | `!` | unary |
| `op_lt` | `<` | binary |
| `op_gt` | `>` | binary |
| `op_iadd` | `+=` | binary |
| `op_isub` | `-=` | binary |
| `op_imul` | `*=` | binary |
| `op_idiv` | `/=` | binary |
| `op_imod` | `%=` | binary |
| `op_ixor` | `^=` | binary |
| `op_ibitand` | `&=` | binary |
| `op_ibitor` | <code>&#124;=</code> | binary |
| `op_lshift` | `<<` | binary |
| `op_rshift` | `>>` | binary |
| `op_ilshift` | `<<=` | binary |
| `op_irshift` | `>>=` | binary |
| `op_eq` | `==` | binary |
| `op_ne` | `!=` | binary |
| `op_le` | `<=` | binary |
| `op_ge` | `>=` | binary |
| `op_cmp` | `<=>` | binary |
| `op_and` | `&&` | binary |
| `op_or` | <code>&#124;&#124;</code> | binary |
| `op_inc` | `++` | no other parameter: prefix; one `int` parameter: postfix |
| `op_dec` | `--` | no other parameter: prefix; one `int` parameter: postfix |
| `op_index` | `[]` | `self` is the indexed object |
| `op_call` | `()` | `self` is the called object; the other parameters are the arguments |
| `op_comma` | `,` | binary |
| `op_ptr_to_mem` | `->*` | binary |

`op_construct*`, `op_assign*`, and `op_convert*` are not in this table.

## Declarations on the proxy

The Trait only names methods (`foo`, `op_add`). Matching C++ operators, conversions, and assignments are generated on the proxy from `*h`, `proxy()`, and `rproxy()` on `dyn` / `c_dyn` / `iface` (see [proxy](#proxy)). Handles do not mix in Trait operators.

Each method name is enabled on its own. No `op_add` on the Trait means no `operator+` on the proxy. The name can exist and still be omitted when the surface condition fails (for example a const proxy while the method is not const-self). The per-surface conditions are the tables below.

Proxies of `sub_dyn`, `c_sub_dyn`, `dyn_view`, and `c_dyn_view` still get same-named method members. They do not get catalog operators, and they do not get conversions or assignments from `op_convert*` / `op_assign*`.

A non-`op_*` Trait method is a same-named member function on the proxy. Its parameters are the Trait parameters with `self` removed. It is present when:

- mutable `dyn` proxy: the method is a dyn slot (`self` first)
- mutable `iface` proxy: `Trait<void>` has the name
- `c_dyn` proxy: self is const, and the mutable `dyn` condition above holds
- proxy of `iface<Trait, const T>` / `iface<Trait, mirror<const T, Src>>`: `Trait<void>` has the name and self is const

`op_construct*`, `op_assign*`, and `op_convert*` are not named members. The last two become operators in the proxy subsection.

### proxy

`proxy_t` and `rproxy_t` from `*h`, `proxy()`, and `rproxy()` on `dyn`, `c_dyn`, and `iface`.

| Surface | When that operator exists |
| --- | --- |
| mutable `dyn` proxy | the method is a dyn slot (`self` first) |
| mutable `iface` proxy | `Trait<void>` has the name |
| `c_dyn` proxy | self is const, and the mutable `dyn` condition above holds |
| proxy of `iface<Trait, const T>` or `iface<Trait, mirror<const T, Src>>` | `Trait<void>` has the name and self is const |

Most operators map to one method name. `operator-` is the exception: unary `operator-()` calls `op_neg`; binary `operator-(rhs)` calls `op_sub`. If either name meets the table above, the proxy gets both `operator-` overloads.

Binary members. Each name has a non-const overload and a const overload. Arguments are forwarded to the Trait method. `self` is the handle stored in the proxy. Operator tokens are in [Operators](#operators).

```cpp
decltype(auto) operator+(auto&&... args);
decltype(auto) operator+(auto&&... args) const;
```

`op_bitand` takes one right-hand operand:

```cpp
decltype(auto) operator&(auto&& rhs);
decltype(auto) operator&(auto&& rhs) const;
```

Unary. Names: `op_neg`, `op_compl`, `op_not`.

```cpp
decltype(auto) operator-();
decltype(auto) operator-() const;
decltype(auto) operator~();
decltype(auto) operator~() const;
decltype(auto) operator!();
decltype(auto) operator!() const;
```

`op_inc` and `op_dec`. No argument is prefix. One argument is postfix; the proxy passes `0`.

```cpp
decltype(auto) operator++();
decltype(auto) operator++(int);
decltype(auto) operator++() const;
decltype(auto) operator++(int) const;
decltype(auto) operator--();
decltype(auto) operator--(int);
decltype(auto) operator--() const;
decltype(auto) operator--(int) const;
```

`op_index` and `op_call`. `self` is the proxy handle.

```cpp
decltype(auto) operator[](auto&&... args);
decltype(auto) operator[](auto&&... args) const;
decltype(auto) operator()(auto&&... args);
decltype(auto) operator()(auto&&... args) const;
```

`op_convert*` is on the proxy carrier, one declaration per overload. `To` is that overload's return type.

| Condition | Declaration |
| --- | --- |
| concrete return `To`, not `[[=expl]]` | `operator To() const` |
| concrete return `To`, and `[[=expl]]` | `explicit operator To() const` |
| `[[=imple]]` | `template <typename To> explicit(E) operator To() const`. `E` is true when the overload is `[[=expl]]`. `To` cannot be self, `mirror`, or `identity_mirror` |

`op_assign*` is on the same carrier. It is never explicit.

| Condition | Declaration |
| --- | --- |
| the RHS key is a concrete type `From` | `decltype(auto) operator=(From rhs)` |
| `[[=imple]]`, or the RHS is `auto` | `template <typename From> decltype(auto) operator=(From&& rhs)` |

Several `operator=` overloads are one overload set (`using`). `op_construct*` generates no operator. A dyn proxy has no reversed free operator.

Reversed (`4 + (*h)`). Condition: that name has self index 1. **iface proxy only**. Same binary names as the catalog (`op_add`, `op_comma`, …). `U` after `remove_cvref` is not a handle.

```cpp
template <typename U>
friend decltype(auto) operator+(U const& a, proxy& b);
template <typename U>
friend decltype(auto) operator+(U const& a, proxy const& b);
```

Copy and move of a proxy copy the observer stored in it. `construct(proxy)` copies the object. `construct(rproxy)` moves the object.

## Inheritance and layout (summary)

- Layout is an ancestor **grid-merge of sections**. The leaf table contains every ancestor method.
- The **first-declared** base is the primary base: sections merge in declaration order (primary's sections first, then the other bases' new sections, then self). Declaration order therefore decides which ancestors are section prefixes (free same-`vt` upcasts) and which need Redirect/cover.
- A **cover** is the layout “as if that Trait were final”. It is not necessarily a prefix slice of the real leaf table.
- `ASF_HAS_DYN_MULTI_VT=1`: ancestors that are not a section prefix get a Redirect→cover, so every base path can be stored.
- Details: [multi_vt.md](multi_vt.md). The flag: [features.md](features.md)#asf_has_dyn_multi_vt.

## See also

- [annotations.md](annotations.md)
- [iface.md](iface.md) / [dyn.md](dyn.md)
- Examples: `examples/*/01_static.cpp`, `examples/*/05_mi_cast.cpp`, `tests/general/iface_inherit.cpp`
