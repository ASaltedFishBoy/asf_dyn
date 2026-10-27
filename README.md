# ASF: Trait / iface / dyn

[![CI](https://github.com/ASaltedFishBoy/asf_dyn/actions/workflows/ci.yml/badge.svg)](https://github.com/ASaltedFishBoy/asf_dyn/actions/workflows/ci.yml)

**Chinese:** [README.zh.md](README.zh.md)

C++26 reflection-based **Trait** polymorphism: static handle **iface**, erased handle **dyn**, and optional field view **mirror**.

Namespace `asf`. Conceptually close to Rust `trait` / `dyn Trait`, implemented with C++26 reflection and vtable layout.

## Why ASF (vs. other erasure / “proxy” libraries)

Many C++ polymorphism stacks pick one story: virtual bases, `std::function`-style erasure, or a small-handle proxy (e.g. [P2530 `std::proxy`](https://wg21.link/p2530) / [microsoft/proxy](https://github.com/microsoft/proxy)). ASF keeps one **Trait** definition and splits **how** you call it:

| Capability | ASF |
| --- | --- |
| **Custom owning / non-owning `Ptr`** | `dyn<Trait, Ptr>` takes the **full** erased pointer type (`void*`, `unique_ptr<void, Deleter>`, `shared_ptr`, `weak_ptr`, SBO **`cache_ptr`**, …). [`pointer_trait`](docs/en/spec/pointers.md) describes destroy vs observer vs deleter; optional **`pointer_trait<P>::ops`** wires shallow copy/move/destroy/bool without changing Trait vtables. |
| **Static path without erasure** | **`iface`** keeps `Self` concrete—no vtable on hot static dispatch. Erased **`dyn`** is opt-in when you need runtime polymorphism. |
| **Optional `cache_deref`** | [`cache_deref<Ptr>`](docs/en/spec/pointers.md#cache_deref) + `to_dyn` stores a **dereferenceable smart pointer** inside **`cache_dyn`** while the vtable dispatches on the **pointee** (Ptr-as-storage path). Omit it when you bind values, mirrors, or a storage `Ptr` directly. |
| **Upcast on the Trait DAG** | With default **`ASF_HAS_DYN_MULTI_VT`**, **`trait_cast` / converting ctors** follow **Redirect/cover** layout—multi-base Trait graphs upcast along the designed path, not only “same vtable prefix”. Single-inheritance and section-prefix modes are compile-time switches ([features.md](docs/en/spec/features.md)). |

You still pay for C++26 reflection (GCC 16+ today) and generated vtables per Trait/leaf—but you get a single, pointer-flexible trait system instead of bolting erasure onto one hard-coded handle type.

## Highlights

| Topic | Notes |
| --- | --- |
| **Trait** | Interface + impl: `Trait<void>` method table, `Trait<T>` bound type |
| **Handles** | `iface` keeps the concrete type; `dyn` erases to a full `Ptr` + vtable |
| **Pointers** | Second template parameter is the full erased `Ptr`: `void*` / `unique_ptr<void>` / `shared_ptr<void>` / `weak_ptr<void>` / `cache_ptr`, ... |
| **Casts** | `as` / `trait_cast` / `to_dyn` / `dyn_cast` along the Trait inheritance DAG |
| **Flags** | `ASF_HAS_DYN_*` for RTTI, intermediate bind, multi-vtable Redirect/cover, single inheritance, ... |

Docs:

- [Conceptual guide](docs/en/README.md) ([Chinese](docs/zh/README.md))
- [API index](docs/en/spec/README.md) ([Chinese](docs/zh/spec/README.md))
- [Pointer semantics](docs/en/spec/pointers.md) ([Chinese](docs/zh/spec/pointers.md))
- Doc hub: [docs/README.md](docs/README.md)

## Build

**Header-only.** Needs **C++26 reflection** (GCC 16+ with `-freflection`). Add `include/` to the include path; GCC usually also needs `stdc++exp`.

```cmake
target_include_directories(app PRIVATE path/to/asf_dyn/include)
target_compile_options(app PRIVATE -freflection -std=c++26)
target_link_libraries(app PRIVATE stdc++exp)
```

Single-file example:

```bash
g++ -std=c++26 -freflection -Iinclude examples/en/01_static.cpp -lstdc++exp -o 01_static
```

Tests:

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build
```

### Hello World

```cpp
#include "interface.hpp"
#include "cast.hpp"

#include <print>
#include <string>

template <typename T = void>
struct Greeter;

template <>
struct Greeter<void> {  // interface
  void greet(auto& self);
  auto who(auto const& self) -> std::string const& {
    return self.name;
  }
};

struct Person {  // bound value type
  std::string name;
};

template <>  // final leaf: impl for Person
struct Greeter<Person> final {
  void greet(Person& self) {
    std::println("hello {}", self.name);
  }
};

int main() {
  Person p{"Ada"};  // object definition
  asf::dyn<Greeter> d{&p};
  // If you dyn-bind a base and the whole program never ODR-uses a covering
  // final leaf (instantiate_dyn or a pointer/mirror bind like this), pick one
  // final and instantiate: template struct asf::instantiate_dyn<Greeter, Person>;

  asf::iface<Greeter, Person> i{p};
  i->greet();
  std::println("iface who = {}", i->who());

  d->greet();
  std::println("dyn who = {}", d->who());

  // factory
  auto d2 = asf::to_dyn<Greeter>(&p);
  d2->greet();
}
```

Notes:

1. `Greeter<void>` is the interface; `self` is an object or a mirror.
2. `Greeter<Person> final` is the impl leaf. `Person p{...};` is the object definition. See [dyn.md](docs/en/spec/dyn.md).
3. `h->method(...)` forwards through a temporary **proxy**.
4. Umbrella header `interface.hpp`; casts in `cast.hpp`.

More examples: [`examples/`](examples/) (`en/` / `zh/`, UTF-8).

### Rough Rust mapping (not 1:1)

| Rust | ASF |
| --- | --- |
| `trait Trait { ... }` | `Trait<void>` (method table) |
| `impl Trait for T { ... }` | `Trait<T>` (dyn leaf usually `final`) |
| `impl Trait for T` (static) | `iface<Trait, T>` |
| `&dyn Trait` / `&mut dyn Trait` | `dyn` / `c_dyn` + `void*` (non-owning by default) |
| `Box<dyn Trait>` | `unique_dyn<Trait>` (`unique_ptr<void>`; default `Deleter` is `std::default_delete`) |
| `Arc<dyn Trait>` / `Rc<dyn Trait>` | `shared_dyn<Trait>` (`shared_ptr<void>`) |
| `dyn Trait` upcast | converting ctor / `trait_cast` (see `ASF_HAS_DYN_MULTI_VT`) |
| `Any` + `downcast` | `ASF_HAS_DYN_RTTI` + `dyn_cast` |
| Field reflection (no direct analogue) | `mirror<T>` / `mirror<T, Src>` (optional) |

## Core ideas

- **iface**: keeps the concrete type; Trait methods bind statically on Self (object / mirror / src). `as<To>()` is a same-object base view. `get()` is not ref-qualified and returns `Self`: for a plain object that is the stored `T&`; for a mirror Self it is a copy of the stored mirror (unlike dyn `get()`, which returns `Ptr&`). An `iface` is always bound (no null state, no `operator bool`); `==` is identity by address.
- **dyn / c_dyn**: `dyn<Trait, Ptr>`; `Ptr` is the full erased type (deleter / Alloc included). Aliases: `unique_dyn` / `shared_dyn` / `cache_dyn`, ...
- **Owning Ptr**: default `void*` does not own; `unique_dyn<Trait[, Deleter]>` with omitted `Deleter` is `std::default_delete<void>`; custom Alloc uses `alloc_delete<Alloc>`; shared / weak / cache: [pointers.md](docs/en/spec/pointers.md).
- **Pointer bind and `final`**: vtable for `T*` / mirror -> `dyn` comes from a **final leaf**; ODR-use a covering final leaf or the link fails. See [dyn.md](docs/en/spec/dyn.md).
- **sub_dyn**: a section slice of one Trait on the leaf table; no secondary upcast.
- **Annotations**: `[[=imple]]` / `[[=expl]]` / `[[=mirror]]`, ... control conversions and views; `static` opts a method out of the dyn vtable; [annotations.md](docs/en/spec/annotations.md).
- **Feature flags**: RTTI, multi-vtable Redirect/cover, single inheritance, ... [features.md](docs/en/spec/features.md).
- **mirror** (optional): field-ref bundle; [mirror.md](docs/en/spec/mirror.md).

## Toolchain

| Compiler | Status | Flags |
| --- | --- | --- |
| GCC | 16.x (primary) | `-std=c++26 -freflection`, often `stdc++exp` |

MSVC / Clang C++26 reflection support is still evolving; this tree currently tracks GCC 16.x only.

## CMake options

| Macro | Default | Meaning |
| --- | --- | --- |
| `ASF_HAS_DYN_RTTI` | OFF | Vtable meta header / `dyn_cast` (**experimental**; see [features.md](docs/en/spec/features.md)) |
| `ASF_HAS_DYN_INTERMEDIATE` | ON | Non-`final` intermediate Trait may bind dyn; **needs final-leaf inject**; [dyn.md](docs/en/spec/dyn.md) |
| `ASF_HAS_DYN_MULTI_VT` | ON | Redirect/cover, full-path `trait_cast` upcast; mutex with `ASF_HAS_DYN_SINGLE_INHERIT` |
| `ASF_HAS_DYN_SINGLE_INHERIT` | OFF | Live-edge single inheritance (checked at dyn generation; `[[=truncate]]`/`[[=truncated]]` allowed); mutex with MULTI_VT (upcast then follows the MULTI_VT-off section-prefix rule) |

Example:

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ \
  -DASF_HAS_DYN_RTTI=ON -DASF_HAS_DYN_INTERMEDIATE=OFF
```

Details: [docs/en/spec/features.md](docs/en/spec/features.md). Feature flags are also baked into the ABI inline namespace as a binary tag (`v0_1_5_b...`).

## Headers

| Header | Contents |
| --- | --- |
| [`iface.hpp`](include/iface.hpp) | `iface` / `to_iface` |
| [`dyn.hpp`](include/dyn.hpp) | `dyn` / `c_dyn` / `sub_*` / `alloc_delete` / aliases |
| [`cast.hpp`](include/cast.hpp) | `trait_cast` / `c_trait_cast` / `to_dyn` / `construct` / `dyn_cast`, ... |
| [`ref.hpp`](include/ref.hpp) | `ref` / `cref` / `as_ref` |
| [`interface.hpp`](include/interface.hpp) | Umbrella (includes `dyn.hpp`) |
| [`mirror.hpp`](include/mirror.hpp) | `mirror` / `to_mirror` / `bind_mirror` (optional) |

## Documentation

| | |
| --- | --- |
| English (this file) | [README.md](README.md) |
| Chinese | [README.zh.md](README.zh.md) |
| Guides | [docs/en/README.md](docs/en/README.md) / [docs/zh/README.md](docs/zh/README.md) |
| API index | [docs/en/spec/README.md](docs/en/spec/README.md) / [docs/zh/spec/README.md](docs/zh/spec/README.md) |
| Spec body | [docs/en/spec/](docs/en/spec/) / [docs/zh/spec/](docs/zh/spec/) |

## License

[Boost Software License 1.0](LICENSE) (same license as Boost). Chosen for a header-only C++ library: source copies keep the notice, compiled binaries need none.
