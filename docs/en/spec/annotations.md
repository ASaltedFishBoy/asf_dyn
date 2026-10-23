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
| `imple` | `no_imple` | `no_imple` | After `Trait<T>`, try an impl on the bound object `T` before the `Trait<void>` default. Details below. |
| `mirror` | `no_mirror` | `no_mirror` | Chooses the **presented Self** for that method (method tag, else the **declaring** `Trait<void>`). User `mirror` with no Src → `iface` only (no `dyn`). Mirror → `iface`: **bind ungated**. **Sourced**: every method callable; `[[=mirror]]` → pass the mirror, else the object (`Src`). **Unsourced**: only `[[=mirror]]` methods are callable. Sourced → `dyn` may mix: mirror sections use `Trait<T>`, non-mirror `Trait<Src>`. `op_construct` never follows the Trait `mirror` tag (`self` is the object / `Src`); marking it `[[=mirror]]` is ill-formed. Destroy is always `vt[-1]` (`~Src`) (see [mirror.md](mirror.md)) |

## `imple`

Resolve order for a concrete bound type `T`:

1. `Trait<T>` specialization, if it has a matching method.
2. If the method (or its declaring `Trait<void>`) is `[[=imple]]` (and not `[[=no_imple]]`): an implementation taken from `T`.
3. The default body on `Trait<void>`.

Step 2 is **structural match**, not C++ overload resolution and not `std::is_invocable`. No hit → step 3.

### Ordinary methods (name is not `op_*`)

Find one same-named callable on `T` (member or static) such that:

- every parameter other than self has the same type as the Trait method, including cv-ref;
- self has the same cv-ref **kind** as the Trait method.

If the Trait self parameter is a `mirror`, compare the **View** type's qualifications to the Trait self (ignore the mirror wrapper). If the hit is an object member, also require: `const` matches; an lvalue Trait self matches an unqualified or `&`-qualified member; an rvalue Trait self needs `&&`.

### `op_*` methods

Not found by the `op_*` identifier. Map to the language facility: constructor (`auto&&` self means rvalue), `operator=`, conversion, or the matching C++ operator from the catalog.

## Dyn slots and `static` (not an annotation)

`static` is a C++ **declaration property** (the `static` keyword on a method), **not** a `[[=asf_ann::…]]` annotation. That is deliberate: method-level annotations on templates must be substituted to be read (GCC instantiates the body — see *Compiler limitation* below), while a declaration property is readable by reflection for free. So the dyn-slot opt-out is `static`, not an annotation.

Rule: a **non**-`static` method with `self` first and at most one deduced template parameter is a dyn slot; a `static` method is iface-only, with no opt-back-in. A later `self`, or a second deduced parameter, is also iface-only by shape (those never reach a dyn slot anyway). The return type must be written on the declaration (undetermined `auto` / `decltype(auto)` return is not allowed).

Why no opt-back-in: a dyn cannot know which concrete `mirror<T>` view a `static` self expects, and routing the call through the vtable anyway would just be a dyn method with extra steps. If you need a mirror out of a dyn, write an ordinary dyn method whose impl names the concrete mirror view (e.g. takes `c_mirror<T>` as Self or returns `mirror<T>`).

## Compiler limitation (annotation reads on templates)

Reading an annotation on a function template requires substituting it (GCC instantiates the body), so method-level annotations on templates are read only where a concrete self is available: `[[=imple]]` is read with the impl Self at invoke / bind; `[[=mirror]]` is read at **call** (to choose the presented Self) and at bind for plain-object mirror-Self selection / dyn section mix; `[[=expl]]` at iface carrier generation. On a `dyn` proxy (no concrete type), `[[=expl]]` on a `op_convert` template that has a body is not read (defaults to implicit). Trait-level annotations are always readable; prefer them when in doubt.

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

## See also

- [traits.md](traits.md) — special names (`op_construct`, …), operator catalog, proxy surface
- [iface.md](iface.md)
- [features.md](features.md)
- Examples: `examples/*/03_operators.cpp`, `examples/*/04_lifetime.cpp`
