# ASF 文档

English: [README.md](../../README.md) · [docs/en](../en/README.md).

阅读顺序分三层：

1. **[仓库 README](../../README.zh.md)** — 动机、Hello World、构建与编译开关
2. **本页** — Trait / iface / dyn 如何配合（mirror 可选）
3. **[API 规格](spec/README.md)** — 类型与函数

---

## 示例

与仓库根 README Hello World 同构。编译示例：

```bash
g++ -std=c++26 -freflection -Iinclude examples/zh/01_static.cpp -lstdc++exp -o 01_static
```

自包含示例（`include/` 相对仓库根）：

```cpp
#include "interface.hpp"
#include "cast.hpp"

#include <print>
#include <string>

template <typename T = void>
struct Greeter;

template <>
struct Greeter<void> {  // 接口
  void greet(auto& self);
  auto who(auto const& self) -> std::string const& {
    return self.name;
  }
};

struct Person {  // 被绑定类型
  std::string name;
};

template <>  // final 叶：对 Person 的实现
struct Greeter<Person> final {
  void greet(Person& self) {
    std::println("hello {}", self.name);
  }
};



int main() {
  Person p{"Ada"};  // 对象定义
  asf::dyn<Greeter> d{&p};
  // 若对某基使用 dyn，且全程序没有任何覆盖该基的 final 叶上的，类似此处dyn的指针/mirror绑定或instantiate_dyn，则须选定一个 final 使用 template struct asf::instantiate_dyn<Greeter, Person>; 语句实例化。

  asf::iface<Greeter, Person> i{p};
  i->greet();
  std::println("iface who = {}", i->who());

  d->greet();
  std::println("dyn who = {}", d->who());

  auto d2 = asf::to_dyn<Greeter>(&p);
  d2->greet();
}
```

`Greeter<void>` 为接口。 `Greeter<Person> final` 为实现叶。 `template struct asf::instantiate_dyn<Greeter, Person>;` 为实例化 dyn。 `Person p{...};` 为对象定义。

## 概念

### Trait

Trait 是一组可多态调用的方法，写成类模板（见上节示例）：

- `Trait<void>`：接口。可 `: Base0<void>, Base1<void>, ...` 做接口多重继承。
- `Trait<T>`：对 `T` 的实现；首参为 `self`（对象或 mirror）。 `dyn` 叶必须对至少一个类型的特化标 `final`；也可以对接口标 `final`，将会应用到所有特化。
- 方法默认进入 **dyn 虚表**；`static` 可排除（仅 iface）。
- 特殊名：`op_construct`、`op_assign*`、`op_convert*`、`op_add` 等。销毁恒为 `vt[-1]`（`~Src`）。

见 [spec/traits.md](spec/traits.md)、[spec/annotations.md](spec/annotations.md)。

### iface vs dyn

| | `iface<Trait, T>` | `dyn<Trait[, Ptr]>` |
| --- | --- | --- |
| 类型信息 | 保留 `T`（及可选 `Src`） | 擦除：仅完整 `Ptr` + vt |
| 调用 | 直接 / 静态绑定 | 虚表槽 |
| 典型用途 | 泛型边界内静态多态、无擦除 | ABI 边界、异构容器、跨 TU |
| 所有权 | 不拥有对象（观察） | 由完整擦除 `Ptr` 决定（默认 `void*` 不拥有） |

二者均经 `operator->` 返回临时 **proxy**，再调用 Trait 方法。

见 [spec/iface.md](spec/iface.md)、[spec/dyn.md](spec/dyn.md)。

### 转换与切片

- **同表视口**：`as<To>()`（仅成员）返回 `dyn_view` / `c_dyn_view`。
- **proxy**：`proxy<To>()` = `get_proxy<To>(h)` = `get_forward_proxy<To>(左值)`；`rproxy<To>()` = `get_rproxy<To>(h)` = `get_forward_proxy<To>(右值)`。
- **上行 / 改绑**：dyn 上行 = `dyn_cast` / 转换构造；iface = 成员 `trait_cast`；`to_dyn` 绑定来源（**不是** dyn 到 dyn 的上行）。可能 Redirect/cover（见 `ASF_HAS_DYN_MULTI_VT`）。
- **运行时下行**：`dyn_cast<To>`（需 `ASF_HAS_DYN_RTTI`；实验性，过度接受下行，见 [features.md](spec/features.md)）。
- **section 切片**：`sub_dyn<To>(d)` = `to_sub_dyn<To>(d)`。

见 [spec/cast.md](spec/cast.md)、[spec/features.md](spec/features.md)。

### mirror（可选）

`self` 可为对象或 mirror；句柄可由 `T*` 绑定。

`mirror<T>` 是字段引用束；`mirror<T, Src>` 还保留源对象；view 指向 `Src` 时仍用普通 `dyn`（destroy 恒为 `vt[-1]` / `~Src`）。见 [spec/mirror.md](spec/mirror.md)。

---

## 阅读顺序

1. README Hello World
2. [traits.md](spec/traits.md) -> [iface.md](spec/iface.md)
3. [dyn.md](spec/dyn.md) -> [pointers.md](spec/pointers.md)
4. [cast.md](spec/cast.md) -> [features.md](spec/features.md)
5. 运算符：[annotations.md](spec/annotations.md) 与 `examples/*/03_operators.cpp`
6. 字段视图 / 别名绑定：[mirror.md](spec/mirror.md)

---

## 示例与测试

| 路径 | 内容 |
| --- | --- |
| `examples/en/` / `examples/zh/` | 双语示例（UTF-8） |
| `examples/*/01_static.cpp` | 静态 `iface` / `to_iface` |
| `examples/*/02_dyn.cpp` | `dyn` / `c_dyn` / `static`（仅 iface）方法 |
| `examples/*/03_operators.cpp` | `op_add` / `op_eq` / `op_convert` / `op_assign` |
| `examples/*/04_lifetime.cpp` | 销毁、`construct`、拷贝 proxy、拥有型 Ptr |
| `examples/*/05_mi_cast.cpp` | 多重继承上行与下行 |
| `examples/*/06_mirror.cpp` | mirror：同字段约定、引用束布局 |
| `tests/general/dyn_*_tests.cpp` | 创建、调用、生命周期、视口、RTTI（对象/指针绑定） |
| `tests/general/iface_grid_vt.cpp` | 菱形 / 网格多重继承虚表 |
| `tests/general/multi_cu/` | 跨 TU 中间节点 ODR 行为 |
