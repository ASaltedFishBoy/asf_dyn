# ASF: Trait / iface / dyn

[![CI](https://github.com/ASaltedFishBoy/asf_dyn/actions/workflows/ci.yml/badge.svg)](https://github.com/ASaltedFishBoy/asf_dyn/actions/workflows/ci.yml)

**English:** [README.md](README.md)

基于 C++26 反射的 **Trait** 多态：静态句柄 **iface**、擦除句柄 **dyn**，以及可选的字段视图 **mirror**（可跳过）。

命名空间为 `asf`；概念上接近 Rust 的 `trait` / `dyn Trait`，但实现依赖 C++26 反射与虚表布局。

## 要点

| 主题 | 说明 |
| --- | --- |
| **Trait** | 声明接口与实现：`Trait<void>` 方法表，`Trait<T>` 绑定具体类型 |
| **句柄** | `iface` 保留具体类型；`dyn` 擦除为完整 `Ptr` + 虚表 |
| **指针** | `dyn` 的第二个模板参数为完整擦除 `Ptr`：`void*` / `unique_ptr<void>` / `shared_ptr<void>` / `weak_ptr<void>` / `cache_ptr` 等 |
| **转换** | Trait 继承 DAG 上的 `as` / `trait_cast` / `to_dyn` / `dyn_cast` 等 |
| **开关** | `ASF_HAS_DYN_*` 控制 RTTI、中间节点、多虚表 Redirect/cover、单继承 等 |

文档分层：

- [概念导读](docs/zh/README.md)（本仓库中文概览；[English](docs/en/README.md)）
- [API 索引](docs/zh/spec/README.md)（类型与函数总表）
- [指针语义](docs/zh/spec/pointers.md)（`Ptr` / `unique_ptr` / `alloc_delete` / `cache_ptr`）
- 文档总站：[docs/README.md](docs/README.md)

## 构建

库为 **header-only**，需要 **C++26 反射**（GCC 16+ 使用 `-freflection`）。将 `include/` 加入包含路径；GCC 通常还需链接 `stdc++exp`。

```cmake
target_include_directories(app PRIVATE path/to/asf_dyn/include)
target_compile_options(app PRIVATE -freflection -std=c++26)
target_link_libraries(app PRIVATE stdc++exp)
```

单文件示例：

```bash
g++ -std=c++26 -freflection -Iinclude examples/en/01_static.cpp -lstdc++exp -o 01_static
```

测试：

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

  // 工厂
  auto d2 = asf::to_dyn<Greeter>(&p);
  d2->greet();
}

```
要点：

1. `Greeter<void>` 为接口；`self` 为对象或 mirror。
2. `Greeter<Person> final` 为实现叶。`Person p{...};` 为对象定义。见 [dyn.md](docs/zh/spec/dyn.md)。
3. `h->method(...)` 经临时 **proxy** 转发。
4. 伞头 `interface.hpp`；转换见 `cast.hpp`。

更多示例见 [`examples/`](examples/)（`en/` / `zh/` 双语，UTF-8）。

### 与 Rust 概念对照（非一一对应）

| Rust | ASF |
| --- | --- |
| `trait Trait { ... }` | `Trait<void>`（方法表） |
| `impl Trait for T { ... }` | `Trait<T>`（dyn 叶通常 `final`） |
| `impl Trait for T`（静态） | `iface<Trait, T>` |
| `&dyn Trait` / `&mut dyn Trait` | `dyn` / `c_dyn` + `void*`（默认不拥有） |
| `Box<dyn Trait>` | `unique_dyn<Trait>`（`unique_ptr<void>`；`Deleter` 默认 `std::default_delete`） |
| `Arc<dyn Trait>` / `Rc<dyn Trait>` | `shared_dyn<Trait>`（`shared_ptr<void>`） |
| `dyn Trait` 向上转型 | 隐式构造 / `trait_cast`（见 `ASF_HAS_DYN_MULTI_VT`） |
| `Any` + `downcast` | `ASF_HAS_DYN_RTTI` + `dyn_cast` |
| 字段反射（无直接对应） | `mirror<T>` / `mirror<T, Src>`（可选） |

## 核心概念

- **iface**：保留具体类型；Trait 方法在 Self（对象 / mirror / src）上静态绑定。`as<To>()` 为同对象基类视口；`get()` 非 ref 限定，返回 `Self`：普通对象是存储的 `T&`；mirror Self 是所存 mirror 的拷贝（与 dyn 的 `get()` 返回 `Ptr&` 不同）。`iface` 始终处于绑定状态（没有空态，也没有 `operator bool`）；`==` 是按地址的身份比较。
- **dyn / c_dyn**：`dyn<Trait, Ptr>`；`Ptr` 为完整擦除类型（含 deleter / Alloc）。别名：`unique_dyn` / `shared_dyn` / `cache_dyn` 等。
- **拥有型 Ptr**：默认 `void*` 不拥有；`unique_dyn<Trait[, Deleter]>` 省略 `Deleter` 时等同 `std::default_delete<void>`；自定义 Alloc 用 `alloc_delete<Alloc>`；shared / weak / cache 见 [pointers.md](docs/zh/spec/pointers.md)。
- **指针绑定与 `final`**：从 `T*` / mirror 建 `dyn` 时虚表来自 **final 叶**；须 ODR-use 覆盖该节点的 final 叶，否则链接失败。详见 [dyn.md](docs/zh/spec/dyn.md)。
- **sub_dyn**：叶表上某一 Trait 的 section 切片，无法上行转换。
- **注解**：`[[=imple]]` / `[[=expl]]` / `[[=mirror]]` 等控制转换与视图；`static` 使方法退出 dyn 虚表；见 [annotations.md](docs/zh/spec/annotations.md)。
- **特性开关**：RTTI、多虚表 Redirect/cover、单继承等见 [features.md](docs/zh/spec/features.md)。
- **mirror**（可选）：字段引用束；见 [mirror.md](docs/zh/spec/mirror.md)。

## 工具链

| 编译器 | 状态 | 选项 |
| --- | --- | --- |
| GCC | 16.x（主要开发） | `-std=c++26 -freflection`，常需 `stdc++exp` |

MSVC / Clang 的 C++26 反射支持仍在演进；当前仓库仅以 GCC 16.x 为准。

## 编译选项（CMake）

| 宏 | 默认 | 含义 |
| --- | --- | --- |
| `ASF_HAS_DYN_RTTI` | OFF | 虚表 meta 头 / `dyn_cast`（**实验性**，见 [features.md](docs/zh/spec/features.md)） |
| `ASF_HAS_DYN_INTERMEDIATE` | ON | 允许非 `final` 中间 Trait 建 dyn；**须 final 叶注入**；见 [dyn.md](docs/zh/spec/dyn.md) |
| `ASF_HAS_DYN_MULTI_VT` | ON | Redirect/cover，多路径 `trait_cast` 上行；与 `ASF_HAS_DYN_SINGLE_INHERIT` 互斥 |
| `ASF_HAS_DYN_SINGLE_INHERIT` | OFF | 活边单继承（dyn 生成时检查；允许 `[[=truncate]]`/`[[=truncated]]`）；与 MULTI_VT 互斥（此时上行走关闭 MULTI_VT 的 section-prefix 规则） |

示例：

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ \
  -DASF_HAS_DYN_RTTI=ON -DASF_HAS_DYN_INTERMEDIATE=OFF
```

细节：[docs/zh/spec/features.md](docs/zh/spec/features.md)。特性开关也按二进制标签编入 ABI 内联命名空间（`v0_1_0_b...`）。

## 头文件

| 头文件 | 内容 |
| --- | --- |
| [`iface.hpp`](include/iface.hpp) | `iface` / `to_iface` |
| [`dyn.hpp`](include/dyn.hpp) | `dyn` / `c_dyn` / `sub_*` / `alloc_delete` / 别名 |
| [`cast.hpp`](include/cast.hpp) | `trait_cast` / `c_trait_cast` / `to_dyn` / `construct` / `dyn_cast` 等 |
| [`ref.hpp`](include/ref.hpp) | `ref` / `cref` / `as_ref` |
| [`interface.hpp`](include/interface.hpp) | 伞头，含 `dyn.hpp` |
| [`mirror.hpp`](include/mirror.hpp) | `mirror` / `to_mirror` / `bind_mirror`（可选） |

## 文档

| 语言 | 入口 |
| --- | --- |
| 中文 | [README.zh.md](README.zh.md)（本文件） |
| English | [README.md](README.md) |
| 概念导读 | [docs/zh/README.md](docs/zh/README.md)、[en](docs/en/README.md) |
| API 规格索引 | [docs/zh/spec/README.md](docs/zh/spec/README.md) |
| 规格正文 | [docs/zh/spec/](docs/zh/spec/) · [docs/en/spec/](docs/en/spec/) |

## 许可

[Boost Software License 1.0](LICENSE)（与 Boost 相同）。为纯头文件 C++ 库而选：源码副本保留声明，编译产物无需附带。
