# Feature flags (`ASF_HAS_DYN_*`)

Compile-time macros, defined in [`include/detail/iface_config.hpp`](../../../include/detail/iface_config.hpp). CMake options: repository [`CMakeLists.txt`](../../../CMakeLists.txt) and the [README](../../../README.md).

Runtime-visible constants:

```cpp
asf::version_major / version_minor / version_patch / version
asf::dyn_rtti_v
asf::dyn_intermediate_v
asf::dyn_multi_vt_v
asf::dyn_single_inherit_v
asf::dyn_feature_bits
asf::abi_namespace_name
```

## ABI version (`ASF_VERSION_*` / `ASF_ABI_NAMESPACE`)

Currently **0.1.4**. The inline ABI namespace is `v0_1_4_b` + a **binary feature tag** (MSB→LSB: `RTTI`, `INTERMEDIATE`, `MULTI_VT`, `SINGLE_INHERIT`). Defaults (`0,1,1,0`) yield `v0_1_4_b0110`. Source still writes `asf::dyn` and the rest; mangling includes that segment so `.dll` / `.so` files built with different ASF versions **or option mixes** in one process do not silently merge COMDAT symbols for slots, static vtable data, and erased thunks. Bump `ASF_VERSION_*` (and the `vMAJOR_MINOR_PATCH` token in `iface_config.hpp`) when breaking the dyn/vtable symbol ABI. Override `ASF_ABI_NAMESPACE` only if you intentionally force a tag. Runtime: `asf::abi_namespace_name`, `asf::dyn_feature_bits`.

---

## `ASF_HAS_DYN_RTTI`

**Experimental.**

| | |
| --- | --- |
| **Default** | `0` (CMake: `OFF`) |
| **On** | Vtables carry a **meta header**; `dyn_cast` is available |

**Experimental:** `dyn_cast` is **guaranteed only to the creation node and the nodes above it**. On a linear chain `1 → 2 → 3 → 4` bound at `3`, `3-1-2` is guaranteed; `3-1-4` may succeed or fail. The implementation looks up Traits in the leaf Final's layout / cover set. That is **not** a precise "is this object's dynamic type To?" test. A hit below the creation node (or on a cover) is **not** a promise. Prefer an implicit upcast / `trait_cast` / `as` on production paths; if you need a runtime downcast, prove the type yourself, or use `unsafe_downcast` and own the UB. The API and acceptance rules may still tighten or change.

That caveat disappears with `ASF_HAS_DYN_INTERMEDIATE=OFF` (final-only binds): every dyn is then created at a final leaf, so the leaf is always the object's true dynamic type and a `meta.find` hit is exactly "`To` is an ancestor of that leaf". `dyn_cast` acceptance is fully defined in that configuration — static upcast when possible, exact downcast via meta otherwise.

Meta, in short: one header `{ find, type }` (`find` a function pointer, `type` a `const std::type_info&` to Storage) with `tables[0..x)` as an immediate suffix (`this + 1`, not a pointer field). `tables[]` holds the x **distinct** physical vtables of the Final key (leaf + Redirect covers), indexed by physical table rather than by target — several targets may share an entry. `find` is generated per Final as x comparisons against compile-time `trait_id` addresses (`trait_id<T>::id` is one static inline `char` per Trait, identity by address) and returns the **tables index** (`(std::size_t)-1` on miss): a pure Final-level map that takes no `self`, one function shared by every key of the Final; the caller then indexes its own instance's `tables[k]`. Every compared address is a compile-time constant visible locally, and the vec is never written at runtime, so the comparison chain may be lowered to a switch by the compiler. Each physical vt key has its own meta copy (not a shared parent tree).

```
vt[-2] --> +-------------------------------+  vtable_meta_inst<X>
           | meta: find -------------------+--> rtti_find_index<Final> (one fn
           |       type = typeid(Storage)  |    per Final, shared by all keys)
           +-------------------------------+
           | tables[0] --------------------+--> leaf vt
           | tables[1] --------------------+--> cover vt
           |  ...                          |
           | tables[X-1] ------------------+--> cover vt
           +-------------------------------+
            one instance per (Mask, Final, Storage, As) key; tables() = meta+1

rtti_find_index<Final>(to_id):             // no self; pure Final-level map
  to_id == &trait_id<T0>::id ?  return k0   // k consteval (Final layout)
  to_id == &trait_id<T1>::id ?  return k1
  ...                               constant address chain; may lower to a switch
  otherwise                    ?  (size_t)-1
caller: k = meta->find(to_id);  vt = (k != -1) ? meta->tables()[k] : nullptr
```

`dyn_cast`: a static upcast uses the converting constructor (`upcast_vt`); otherwise a meta lookup. Empty only when the Trait is missing (see above: finding it is not the same as semantic correctness).

Independent of **MULTI_VT**, and complementary: a full-path implicit upcast / `trait_cast` does **not** need meta. A non-section-prefix `dyn_cast` target still depends on Redirect (cover vtables).

RTTI prefix: meta sits at `vt[-2]`. `op_construct` sits among the method slots like any other method; dispatch is by arguments.

---

## `ASF_HAS_DYN_INTERMEDIATE`

| | |
| --- | --- |
| **Default** | `1` (CMake: `ON`) |
| **Implementation** | `include/detail/dyn_intermediate.hpp` |

### On (default)

Pointer/mirror `to_dyn` is allowed for a **non-`final`** intermediate Trait.

Full rules: [dyn.md](dyn.md)#pointer--mirror-bind-and-final, [traits.md](traits.md). Summary:

Paths:

- **Dyn final only** takes the definition path: injects that leaf's vtable so intermediates can ADL-fetch it.
- **Non-final** only ADL-fetches; **does not inject**.

**Required**: the program must ODR-use / instantiate at least one **final leaf** that covers that intermediate node (`Trait<void> final` or `Trait<T> final`, for example `template struct instantiate_dyn<Trait, T>` or a pointer/mirror bind of that leaf). Otherwise the intermediate has no vtable entry and the **link fails**.

**Forbidden**: if a parent Trait is already final (`Parent<void> final` makes every `Parent<As>` final, or `Parent<As> final`), a derived Trait's pointer/mirror bind `static_assert`s.

### Several finals register the same base

- **Same TU**: STMP skips a second inject to avoid redefinition. **The first instantiated final wins**.
- **Across TUs**: each TU may inject once. The linker picks one COMDAT/ODR symbol (**not weak**). The result is unspecified: internal UB.

The UB buys override semantics: `dyn<Base>` bound at an intermediate dispatches to whichever final leaf injected the base's slots. If you do not need that, `ASF_HAS_DYN_INTERMEDIATE=OFF` removes both the UB and the override. The injection mechanism itself (`include/detail/dyn_intermediate.hpp`) relies on GCC-specific template-friend behavior and will need rework for other compilers' reflection implementations.

See `tests/general/multi_final_ub.cpp` and `tests/general/multi_cu/`.

### With RTTI

A dyn built from an intermediate node attaches meta / `dyn_cast` to the **leaf vtable that was actually obtained**. It does not promise a downcast into which final's tree. The winner is same-TU STMP or cross-TU linking, not the intermediate Trait's own logical DAG.

### Off

`-DASF_HAS_DYN_INTERMEDIATE=OFF` (or macro `0`):

- Only **final** may pointer/mirror-bind.
- You cannot create a dyn from an intermediate node.
- No longer UB. Reach a base only by upcast from a final leaf.

---

## `ASF_HAS_DYN_MULTI_VT`

| | |
| --- | --- |
| **Default** | `1` (CMake: `ON`) |
| **Mutex** | Mutually exclusive with `ASF_HAS_DYN_SINGLE_INHERIT` (CMake `FATAL_ERROR`) |

### On (default)

**Multiple vtables**: the layout inserts **Redirect → cover** for ancestors that are not a section prefix, so an implicit upcast / `trait_cast` can store any Trait base. A section-prefix upcast keeps the same `vt`. **`[[=truncate]]` / `[[=truncated]]`** prune Redirect/cover on a cut path ([annotations.md](annotations.md)#truncation-truncate--truncated).

Layout, two stacked diamonds, table reuse, Redirect, cover, and `unsafe_downcast`: [multi_vt.md](multi_vt.md).

### Off

`-DASF_HAS_DYN_MULTI_VT=OFF` (or macro `0`): only sections go into the flat table (no Redirect/cover). `dyn → dyn` allows only a **section prefix / the same table**. `as<>` can still call any ancestor method on that same leaf.

### With RTTI

Independent of MULTI_VT. A full-path implicit upcast / `trait_cast` does **not** need meta. `dyn_cast` still needs RTTI; a non-prefix target is this flag's cover. See [multi_vt.md](multi_vt.md).

### `unsafe_downcast`

Retypes the Trait tag only (same ptr, same vt). After a Redirect upcast the handle may hold a **cover**. Retargeting that cover to a more-derived Trait is UB. Full table and mismatch diagrams: [multi_vt.md](multi_vt.md)#unsafe_downcast.

---

## `ASF_HAS_DYN_SINGLE_INHERIT`

| | |
| --- | --- |
| **Default** | `0` (CMake: `OFF`) |
| **Mutex** | Mutually exclusive with `ASF_HAS_DYN_MULTI_VT` (CMake `FATAL_ERROR`; header `#error` if both macros are 1) |

### On

**Single-inheritance among live dyn bases**: at dyn generation (`dyn_object_slots` / `instantiate_dyn`), `static_assert` if any section has more than one **live** direct Trait base. `[[=truncate]]` / `[[=truncated]]` edges (see [annotations.md](annotations.md)#truncation-truncate--truncated) are **allowed** and do not count toward that limit. Direct bases **dominated** by another direct base (an ancestor of a sibling, see [annotations.md](annotations.md)#redundant-direct-base-merge-and-truncate-re-activation) are as if not inherited and do not count. `iface` is unaffected until you bind dyn.

This flag does not add a separate upcast mode. Mutex forces MULTI_VT off, which is the section-prefix rule. On an SI live DAG every reachable Trait base is a section prefix, so implicit upcast / `trait_cast` along the live chain still works.

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ \
  -DASF_HAS_DYN_MULTI_VT=OFF \
  -DASF_HAS_DYN_SINGLE_INHERIT=ON
```

### Off (default)

Trait multiple inheritance is allowed for dyn (subject to `ASF_HAS_DYN_MULTI_VT` for non-prefix upcasts).

---

## Cost model

Structural cost of each primary behavior (not wall-clock numbers). Timed
comparison with microsoft/proxy: [bench/README.md](../../bench/README.md).

| Behavior | Cost | Notes |
| --- | --- | --- |
| `iface` method call | Statically bound, inlinable (generally free) | Direct call to the impl / default body; full type at compile time, usually identical to a hand-written direct call |
| `dyn` method call | One vtable indirection | `vt[i]` load + indirect call, same class as a virtual function; essentially never inlinable after erasure |
| `dyn` pointer bind (`T*` / mirror, observing) | Zero alloc, zero copy | Stores the pointer and the vt address; vtables are compile-time statics |
| `to_dyn` owning a value | One allocation + construction | `::operator new` / Alloc; `cache_dyn` is allocation-free (the value moves into the traveling buffer) |
| Upcast view (`as<To>`) | Pure type-level rewrap, same vt (free) | Never switches the vt pointer |
| Upcast cast (`trait_cast`) | Compile-time index, at most one vt pointer swap | No typeid / string lookup; MULTI_VT Redirect tables are static addresses too |
| `dyn_cast` (RTTI on) | `meta.find`: x compile-time `trait_id` address comparisons (may lower to a switch), then one direct `tables[k]` index | Experimental |
| Teardown | One indirect `vt[-1]` call | Deleter-kind adds the `Free` call (default is `::operator delete`) |
| Proxy (`h->` / `*h`) | Small pointer wrapper | Zero-cost after inlining |
| `mirror` construction / field access | Fields are references | Construction packs pointers; only `value_of` copies field-wise |
| Static vtable size | One table per (Mask, Final, Trait, Storage, As) key | MULTI_VT cover tables are the main source; SINGLE_INHERIT or MULTI_VT=off shrinks it |

The statically elidable half: everything on `iface`, the dyn proxy wrappers, and upcast indexing — those are optimized locally at compile time (mostly free). The irreducible half: one indirection per `dyn` call and teardown, and any owning allocation.

---

## CMake example

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ \
  -DASF_HAS_DYN_RTTI=ON \
  -DASF_HAS_DYN_INTERMEDIATE=ON \
  -DASF_HAS_DYN_MULTI_VT=ON
```

Some test targets force the options (for example `dyn_rtti_tests` always turns RTTI on).
