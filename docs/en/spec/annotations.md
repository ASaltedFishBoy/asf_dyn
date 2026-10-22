# Annotations

## Annotation namespace

```cpp
namespace asf::ann {
  /* expl/no_expl, imple/no_imple, mirror/no_mirror,
     truncate, truncated */
}
namespace asf_ann = asf::ann;
```

C++26 annotation syntax `[[=asf_ann::name]]`. Method pairs may sit on a **method** or on **`Trait<void>`**. On `Trait<void>`, every method declared there inherits that annotation. A method annotation overrides the Trait annotation. Putting both forms on the same declaration is ill-formed.

| Yes | No | Default | Meaning |
| --- | --- | --- | --- |
| `expl` | `no_expl` | `no_expl` | Conversion is explicit (`expl`) or implicit (`no_expl`, like `operator T()`) |
| `imple` | `no_imple` | `no_imple` | Skip the Trait body. A non-op method calls the same-named member. `op_*` is not looked up under that name: `op_construct` selects the constructor (`auto&&` is an rvalue), `op_assign` selects `operator=`, `op_convert` selects the conversion, and catalog names select the matching C++ operator. An object or a sourced Self both work |
| `mirror` | `no_mirror` | `no_mirror` | User `mirror` with no Src, bound to `iface` (unsourced mirrors do not bind to `dyn`): every method of every Trait in the layout must allow `mirror`. Binding `dyn` from a sourced `mirror<T, Src>` does not need the whole layout tagged: mirror sections use the `T` specialization, non-mirror sections use `Src`. `op_construct` does not inherit the Trait `mirror` tag; marking it `[[=mirror]]` is ill-formed. Destroy is always `vt[-1]` (`~Src`), not a Trait method (see [mirror.md](mirror.md)) |

## Dyn slots and `static` (not an annotation)

`static` is a C++ **declaration property** (the `static` keyword on a method), **not** a `[[=asf_ann::…]]` annotation. That is deliberate: method-level annotations on templates must be substituted to be read (GCC instantiates the body — see *Compiler limitation* below), while a declaration property is readable by reflection for free. So the dyn-slot opt-out is `static`, not an annotation.

Rule: a **non**-`static` method with `self` first and at most one deduced template parameter is a dyn slot; a `static` method is iface-only, with no opt-back-in. A later `self`, or a second deduced parameter, is also iface-only by shape (those never reach a dyn slot anyway).

Why no opt-back-in: a dyn cannot know which concrete `mirror<T>` view a `static` self expects, and routing the call through the vtable anyway would just be a dyn method with extra steps. If you need a mirror out of a dyn, write an ordinary dyn method whose impl names the concrete mirror view (e.g. takes `c_mirror<T>` as Self or returns `mirror<T>`).

## Compiler limitation (annotation reads on templates)

Reading an annotation on a function template requires substituting it (GCC instantiates the body), so method-level annotations on templates are read only where a concrete self is available: `[[=imple]]` and `[[=mirror]]` are read with the impl Self at invoke / iface-bind time, and `[[=expl]]` at iface carrier generation. On a `dyn` proxy (no concrete type), `[[=expl]]` on a `op_convert` template that has a body is not read (defaults to implicit). Trait-level annotations are always readable; prefer them when in doubt.

## Truncation (`truncate` / `truncated`)

Layout-only (not method annotations; same root, active / passive). **Does not affect iface** (iface still up/down along the full Trait DAG). Only tightens **storeable dyn upcasts** and prunes **MULTI_VT Redirect→cover** (fewer side-branch tables). Same-vt `dyn::as<>` views still work.

| Annotation | Placement | Meaning |
| --- | --- | --- |
| `truncate` | `Trait<void>` or a **base-specifier** | **Active.** On the Trait: `dyn` cannot upcast further. On an MI edge: that branch cannot reach bases brought only by it (including that direct base), so no extra cover table is generated for the path. An ancestor already marked `truncate` also blocks descendants from passing it along that edge |
| `truncated` | `Trait<void>` only | **Passive.** Inheriting this Interface is treated as a `truncate` edge: `dyn` cannot upcast **to** this Interface |

```cpp
struct [[=asf_ann::truncate]] Mid<void> : Base<void> { /* ... */ };
struct Leaf<void> : Mid<void>, Other<void> {};  // Leaf→Mid OK; Leaf→Base no
struct Edge<void> : A<void>, [[=asf_ann::truncate]] B<void> {};  // Edge→B no

struct [[=asf_ann::truncated]] B<void> { /* ... */ };
struct D<void> : A<void>, B<void> {};  // same as truncate on the edge; D→B no
```

### Redundant direct base (automatic truncation)

If a direct base is already an ancestor of another direct base — `T final : A, B` with `B : A` — the direct edge `T→A` duplicates the live path `T→B→…→A`. The library emits a **compile-time warning** (a `[[deprecated]]` marker; GCC dedups it to one warning per TU) and treats the edge as **truncated**: the direct edge is not a live edge — it does not count as a base for the `ASF_HAS_DYN_SINGLE_INHERIT` check and produces no upcast path of its own. The base itself stays reachable (and upcastable) through the dominating live path, so sections, covers, and dispatch are unchanged. A `truncate` / `truncated` edge cannot dominate, and a base whose only live path would pass through such an edge is not dominated. Chains (`A⊂B⊂C`), diamonds (`A⊂B` and `A⊂D`), and mixed cases behave the same way; see `tests/general/iface_dominant_base.cpp`.

See [multi_vt.md](multi_vt.md), [features.md](features.md)#asf_has_dyn_multi_vt, and `tests/general/iface_truncate.cpp`.

## op_construct

- Return `auto` or a concrete type (not `void`).
- `[[=asf_ann::imple]]`: the key is the internal `imple_t`. The Trait body is ignored. The call selects the Src constructor with the same parameters. When the only parameter is `self`, `const&` copies and `&&` moves. `auto&&` is an rvalue reference (the `mirror<T&&, …>` spelling is not settled).
- Custom default: omit `imple`. The concrete return type is the key (the body is used).
- Each `op_construct` overload is an ordinary dyn slot like any other method. Dispatch is by arguments only (including `self` value category). There is no separate `construct_move` marker, and construct is not fixed at the end of the vtable.

## Operators

The full method-name table is in [traits.md](traits.md)#operators. Write them as ordinary methods on the Trait. Do not use the `friend` keyword.

A Trait method becomes a member operator of the proxy, so the handle is the left operand: `(*h) + x`. A **dyn slot requires `self` as the first parameter, and at most one deduced template parameter**; a later `self`, or a second deduced parameter, is a compile error for dyn dispatch.

Reversed operands (other side on the left) are **iface only**: add another Trait overload with `self` later and mark it `static`. Matching is by parameter position, e.g. `op_add(int x, auto const& self)` → `4 + (*h)`. dyn `*` has no reversed operators. Unary operators stay on the Trait.

Example: `examples/*/03_operators.cpp`. Lifetime (`destroy` / `construct`): `examples/*/04_lifetime.cpp`.

## See also

- [traits.md](traits.md)
- [iface.md](iface.md)
- [features.md](features.md)
