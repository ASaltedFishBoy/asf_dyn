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

Currently **0.2.0**. The inline ABI namespace is `v0_2_0_b` + a **binary feature tag** (MSB→LSB: `RTTI`, `INTERMEDIATE`, `MULTI_VT`, `SINGLE_INHERIT`) + a `_tNN` suffix (the `ASF_DYN_VT_TAG_HI_BITS` value). Defaults (`0,1,1,0`, width 16) yield `v0_2_0_b0110_t16`. Source still writes `asf::dyn` and the rest; mangling includes that segment so `.dll` / `.so` files built with different ASF versions **or option mixes** in one process do not silently merge COMDAT symbols for slots, static vtable data, and erased thunks. Bump `ASF_VERSION_*` (and the `vMAJOR_MINOR_PATCH` token in `iface_config.hpp`) when breaking the dyn/vtable symbol ABI. Override `ASF_ABI_NAMESPACE` only if you intentionally force a tag. Runtime: `asf::abi_namespace_name`, `asf::dyn_feature_bits`.

---

## `ASF_HAS_DYN_RTTI`

| | |
| --- | --- |
| **Default** | `0` (CMake: `OFF`) |
| **On** | Vtables carry a **meta header**; `dyn_cast` is available |

**Semantics:** a handle is anchored at a node of the construction Final's reachable-Trait tree: a leaf bind anchors at the Final leaf, an intermediate bind anchors at the construction Trait's own node, a section-prefix upcast keeps the node, and a Redirect upcast re-anchors at the cover node. The anchor is **construction-fixed** — a `dyn_cast` hit switches only the vtable, never the tag, so cast results are independent of cast history. The runtime test scans the anchor node's **live-ancestor closure** — the same reachable set the static upcasts use — comparing identity only when a candidate's level equals the target's static DAG depth. A hit is **exact**: the object's dynamic type genuinely realizes `To`, so the handle switches to `To`'s own vtable. A miss is definitive: `To` lies deeper than the anchor (below the construction trait) or outside its ancestry. The closure is the anchor's: from a leaf anchor every live ancestor is reachable — both arms of a diamond — while from an intermediate anchor a sibling branch and the covering leaf below are misses. A tree wider than the tag capacity is a **compile-time error**, so `dyn_cast` has no UB path. Prefer an implicit upcast / `trait_cast` / `as` when the static type is known; `dyn_cast` is the exact runtime test otherwise.

Meta, in short: one header `{ type, node_count, walk, node_typeid }` (`type` a `const std::type_info&` to Storage) with `tables[0..x)` as an immediate suffix (`this + 1`). The x nodes are the Final's dyn-upcast-reachable Traits (`reachable_t<Final>`, self first) — one node per logical Trait, so `tables[]` may repeat a physical vt shared by section prefix. The cast tree is **code, not data**: `walk` is a per-Final dispatch (one function per Final, COMDAT-shared across the key's Storage/As/Mask variants) that switches on the node tag and scans the constexpr-unrolled ancestor closure of that node — levels and compared identities are constants, only `to_id` / `to_level` are runtime; `node_typeid` is a per-Final node → trait `typeid` dispatch. No `ids[]` / `links[]` arrays exist. A handle vt carries the **node tag** in a tag field of the pointer: the index of the construction anchor node (the construction Trait's node at bind — node 0 for a leaf bind —, the cover node after a Redirect upcast); every vt producer (construction / redirect thunk / walk hit) returns a pre-tagged pointer — walk hits reuse the input tag — and slot accesses untag first. The tag feeds the `walk` / `node_typeid` dispatches and indexes this meta block's `tables[]`; the tag field occupies the pointer's top `ASF_DYN_VT_TAG_HI_BITS` bits (default 16 on 64-bit = 65536 nodes, assuming a 48-bit canonical VA — no compiler facility reports the VA width, so LA57-class machines must set it to 7; `0`, the 32-bit default, falls back to the low alignment bits `countr_zero(alignof(vt_fn))` = 3 bits = 8 nodes), and a block whose max node index exceeds `2^bits` is a **compile error** at meta instantiation — there is no runtime escape. Each physical vt key has its own meta copy (not a shared parent tree).

```
vt[-2] --> +-------------------------------+  vtable_meta_inst<X>
           | meta: type = typeid(Storage)  |
           |       node_count = X          |
           |       walk -------------+-----+--> rtti_walk_dispatch<Final>
           |       node_typeid ------+     +--> rtti_node_typeid_dispatch<Final>
           +-------------------------+
           | tables[0] --------------+-----+--> leaf vt
           | tables[1] --------------+-----+--> cover vt (may repeat)
           |  ...                    |
           | tables[X-1] ------------+-----+--> cover vt
           +-------------------------+
            one instance per (Mask, Final, Storage, As) key; tables() = meta+1
            walk / node_typeid: per-Final code, COMDAT-shared across keys

walk_cast(vt, to_id, to_level):            // to_level = trait_level_v<To>
  i = meta->walk(node_of(vt), to_id, to_level)
      // tag picks the anchor node; its ancestor closure is a constexpr
      // unroll: levels and identities are constants, to_id / to_level runtime
  i < 0 -> miss; otherwise tag_vt(meta->tables()[i], node_of(vt))
      // a hit switches only the table; the anchor tag is unchanged
```

`dyn_cast`: a static upcast uses the converting constructor (`upcast_vt`); otherwise a meta ancestor-closure scan from the handle's anchor node. Empty when the scan misses (see above: a hit is not the same as semantic correctness).

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

A dyn built from an intermediate node still runs on the **leaf vtable that was actually obtained** (the winner is same-TU STMP or cross-TU linking), but its meta anchor is the construction Trait's own node: `dyn_cast` sees exactly that Trait's live ancestry, and a downcast toward the covering leaf is a definitive miss.

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
| `dyn_cast` (RTTI on) | Per-Final walk dispatch: tag-selected anchor, constant level/identity compares over the unrolled ancestor closure; one `tables[]` load on hit, tag unchanged | No memory tree (closure unrolled into code); OFF by default |
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
