# ASF docs

Chinese: [README.zh.md](../../README.zh.md) · [docs/zh](../zh/README.md).

Three levels:

1. **[Repository README](../../README.md)** — motivation, Hello World, build, flags
2. **This page** — how Trait / iface / dyn fit together
3. **[API spec](spec/README.md)** — types and functions

---

## Example

Same shape as the repository README Hello World. Build:

```bash
g++ -std=c++26 -freflection -Iinclude examples/en/01_static.cpp -lstdc++exp -o 01_static
```

Self-contained (`include/` relative to the repository root):

```cpp
#include "interface.hpp"
#include "cast.hpp"

#include <print>
#include <string>

template <typename T = void>
struct Greeter;

template <>
struct Greeter<void> {  // interface / method list
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

  auto d2 = asf::to_dyn<Greeter>(&p);
  d2->greet();
}
```

`Greeter<void>` is the interface. `Greeter<Person> final` is the implementation leaf. `template struct asf::instantiate_dyn<Greeter, Person>;` instantiates dyn. `Person p{...};` is the object definition.

## Concepts

### Trait

A Trait is a set of methods you can call polymorphically, written as a class template (see the example above):

- `Trait<void>`: the interface. It may `: Base0<void>, Base1<void>, ...` for interface multiple inheritance.
- `Trait<T>`: the implementation for `T`. The first parameter is `self` (object or mirror). A dyn leaf must mark `final` on the specialization for at least one type. Marking the interface `final` applies to every specialization.
- Methods enter the **dyn vtable** by default. `static` keeps them out (iface-only).
- Special names: `op_construct`, `op_assign*`, `op_convert*`, `op_add`, and so on. Destroy is always `vt[-1]` (`~Src`).

See [spec/traits.md](spec/traits.md) and [spec/annotations.md](spec/annotations.md).

### iface vs dyn

| | `iface<Trait, T>` | `dyn<Trait[, Ptr]>` |
| --- | --- | --- |
| Type info | Keeps `T` (and optional `Src`) | Erased: complete `Ptr` + vt only |
| Call | Direct / static bind | Vtable slot |
| Typical use | Static polymorphism inside a generic boundary, no erasure | ABI boundary, heterogeneous containers, across TUs |
| Ownership | Does not own the object (observes) | Decided by the complete erased `Ptr` (default `void*` does not own) |

Both return a temporary **proxy** from `operator->`, then call Trait methods.

See [spec/iface.md](spec/iface.md) and [spec/dyn.md](spec/dyn.md).

### Casts and slices

- **Same-table view**: `as<To>()` (member only) returns `dyn_view` / `c_dyn_view`.
- **proxy**: `proxy<To>()` = `get_proxy<To>(h)` = `get_forward_proxy<To>(lvalue)`; `rproxy<To>()` = `get_rproxy<To>(h)` = `get_forward_proxy<To>(rvalue)`.
- **Upcast / retarget**: converting constructor = `trait_cast`; `dyn` construction = `to_dyn` (not dyn-to-dyn upcast). May Redirect/cover (see `ASF_HAS_DYN_MULTI_VT`).
- **Runtime downcast**: `dyn_cast<To>` (needs `ASF_HAS_DYN_RTTI`; experimental, over-accepts downcasts — see [features.md](spec/features.md)).
- **Section slice**: `sub_dyn<To>(d)` = `to_sub_dyn<To>(d)`.

See [spec/cast.md](spec/cast.md) and [spec/features.md](spec/features.md).

### mirror (optional)

`self` may be an object or a mirror; handles may bind from `T*`.

`mirror<T>` is a field-ref bundle; `mirror<T, Src>` also keeps the source; view → Src still uses ordinary `dyn` (destroy is `vt[-1]` / `~Src`). See [spec/mirror.md](spec/mirror.md).

---

## Reading order

1. README Hello World
2. [traits.md](spec/traits.md) → [iface.md](spec/iface.md)
3. [dyn.md](spec/dyn.md) → [pointers.md](spec/pointers.md)
4. [cast.md](spec/cast.md) → [features.md](spec/features.md)
5. Operators: [annotations.md](spec/annotations.md) and `examples/*/03_operators.cpp`
6. Field views / alias binding: [mirror.md](spec/mirror.md)

---

## Examples and tests

| Path | Contents |
| --- | --- |
| `examples/en/` · `examples/zh/` | Bilingual examples (UTF-8) |
| `examples/*/01_static.cpp` | Static `iface` / `to_iface` |
| `examples/*/02_dyn.cpp` | `dyn` / `c_dyn` / `static` (iface-only) methods |
| `examples/*/03_operators.cpp` | `op_add` / `op_eq` / `op_convert` / `op_assign` |
| `examples/*/04_lifetime.cpp` | Destroy, `construct`, copy proxy, owning Ptr |
| `examples/*/05_mi_cast.cpp` | Multiple inheritance upcast and downcast |
| `examples/*/06_mirror.cpp` | mirror: same field convention, reference-bundle layout |
| `tests/general/dyn_*_tests.cpp` | Creation, invocation, lifetime, views, RTTI (object/pointer bind) |
| `tests/mirror/` | The same suites through user-mirror binds |
| `tests/general/iface_grid_vt.cpp` | Diamond / grid multiple-inheritance vtables |
| `tests/general/multi_cu/` | Cross-TU intermediate-node ODR behavior |
