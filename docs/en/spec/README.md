# ASF API spec

Namespace: `asf` (annotation alias `asf_ann = asf::ann`). Unless noted, the public API is the headers below.

You do not need mirror to start: Trait / iface / dyn work from objects and pointers; field views are in [mirror.md](mirror.md) at the end.

## Headers

| Header | Contents |
| --- | --- |
| [`<iface.hpp>`](iface.md) | `iface`, `c_iface`, `to_iface`, `to_c_iface` |
| [`<dyn.hpp>`](dyn.md) | `dyn` / `c_dyn` / `sub_*` and pointer aliases |
| [`<cast.hpp>`](cast.md) | `trait_cast` / `c_trait_cast` / `to_dyn` / `construct` / `dyn_cast`, and more |
| [`<ref.hpp>`](ref.md) | `ref` / `cref` / `as_ref` / `as_cref` |
| [`<interface.hpp>`](../README.md) | Umbrella: includes `dyn.hpp` (hence iface / dyn; also pulls in mirror) |
| [`<mirror.hpp>`](mirror.md) | `mirror`, `to_mirror`, `bind_mirror`, `to_pure_mirror` (optional) |

`include/detail/` is internal and not stable.

## Concepts

| Doc | Contents |
| --- | --- |
| [traits.md](traits.md) | Trait declaration, inheritance, `final` (incl. dyn leaf bind rules), method slots |
| [annotations.md](annotations.md) | `[[=expl]]`/`[[=no_expl]]`, `[[=imple]]`/`[[=no_imple]]`, `[[=mirror]]`/`[[=no_mirror]]`; `static` as the dyn-slot opt-out |
| [pointers.md](pointers.md) | `void*` / `unique_ptr` / `alloc_delete` / `dyn_adopt_delete` (default-Free alias `dyn_default_delete`) / `shared_ptr` / `weak_ptr` / `cache_ptr` |
| [features.md](features.md) | `ASF_HAS_DYN_*` flags |
| [multi_vt.md](multi_vt.md) | Multiple vtables, Redirect/cover, `unsafe_downcast` |
| [mirror.md](mirror.md) | Field views (optional, read last) |

## Types and handles

### Static handles

| Name | Doc | Summary |
| --- | --- | --- |
| `iface<Trait, T>` | [iface.md](iface.md) | Concrete-type Trait handle; sourced form is `mirror<View, Src>` |
| `c_iface<Trait, T>` | [iface.md](iface.md) | const-self: const on a plain type, or on the mirror view |
| `to_iface<Trait>` / `to_c_iface<Trait>` | [iface.md](iface.md) | Build an `iface` from an object or a mirror (const-view with `to_c_iface`) |

### Dynamic handles

| Name | Doc | Summary |
| --- | --- | --- |
| `dyn` / `c_dyn` | [dyn.md](dyn.md) | Type-erased handle (const variant) |
| `sub_dyn` / `c_sub_dyn` | [dyn.md](dyn.md) | Section slice of the leaf table |
| `unique_dyn` / `shared_dyn` / `weak_dyn` / `cache_dyn` … | [dyn.md](dyn.md), [pointers.md](pointers.md) | `Ptr` family aliases |

### Casts and construction

| Name | Doc | Summary |
| --- | --- | --- |
| `trait_cast` / `c_trait_cast` | [cast.md](cast.md) | Cast of `iface` / `dyn`; `dyn` accepts upcast only |
| `to_dyn` / `to_c_dyn` / `to_sub_dyn` … | [cast.md](cast.md) | Build a dyn-family handle (not dyn-to-dyn upcast) |
| `instantiate_dyn<Trait, As>` | [cast.md](cast.md) / [dyn.md](dyn.md) | Type; explicitly instantiate final-leaf object-bind vtables |
| `construct` | [cast.md](cast.md) | Deep construct; `Ptr` may be retargeted |
| `get_proxy` / `get_rproxy` / `get_forward_proxy` | [cast.md](cast.md) | `const&` `proxy` / `rproxy`; forward by value category |
| `dyn_cast` / `unsafe_downcast` | [cast.md](cast.md) | Runtime / unsafe casts (`unsafe_downcast` is free-function only) |

### Views and sealed pointers (optional)

| Name | Doc | Summary |
| --- | --- | --- |
| `mirror` | [mirror.md](mirror.md) | Reflected field view |
| `ref` / `cref` | [ref.md](ref.md) | Pointer sealed so it is used as a reference |

## Runtime-visible flags

```cpp
namespace asf {
  inline constexpr bool dyn_rtti_v;
  inline constexpr bool dyn_intermediate_v;
  inline constexpr bool dyn_multi_vt_v;
  inline constexpr bool dyn_single_inherit_v;
  inline constexpr unsigned dyn_feature_bits;
  inline constexpr char const* abi_namespace_name;
}
```

Defined in [`detail/iface_config.hpp`](../../../include/detail/iface_config.hpp). Meaning: [features.md](features.md).
