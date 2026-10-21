# ASF API 规格

命名空间：`asf`（注解别名 `asf_ann = asf::ann`）。除另有说明，公开 API 即下列头文件。

上手不需要 mirror：Trait / iface / dyn 可从对象与指针使用；字段视图见文末 [mirror.md](mirror.md)。

## 头文件

| 头文件 | 内容 |
| --------------------------------- | --------------------------------------------------------------------- |
| [<iface.hpp>](iface.md) | `iface`、`to_iface` / `to_c_iface`、工厂特化 |
| [<dyn.hpp>](dyn.md) | `dyn` / `c_dyn` / `sub_*` 及指针别名 |
| [<cast.hpp>](cast.md) | `trait_cast` / `c_trait_cast` / `to_dyn` / `construct` / `dyn_cast` 等 |
| [<ref.hpp>](ref.md) | `ref` / `cref` / `as_ref` / `as_cref` |
| [<interface.hpp>](../README.md) | 伞头：包含 `dyn.hpp`（因而含 iface / dyn；亦拉入 mirror） |
| [<mirror.hpp>](mirror.md) | `mirror`、`to_mirror`、`bind_mirror`、`to_pure_mirror`（可选） |

实现细节在 `include/detail/`，不保证稳定。

## 概念文档

| 文档 | 内容 |
| -------------------------------- | --------------------------------------------------------------------------------- |
| [traits.md](traits.md) | Trait 声明、继承、`final`（含 dyn 叶绑定规则）、方法槽 |
| [annotations.md](annotations.md) | `[[=expl]]`/`[[=no_expl]]`、`[[=imple]]`/`[[=no_imple]]`、`[[=mirror]]`/`[[=no_mirror]]`；`static` 为 dyn 槽退出方式 |
| [pointers.md](pointers.md) | `void*` / `unique_ptr` / `alloc_delete` / `dyn_adopt_delete`（默认 Free 别名 `dyn_default_delete`）/ `shared_ptr` / `weak_ptr` / `cache_ptr` |
| [features.md](features.md) | `ASF_HAS_DYN_*` 开关 |
| [multi_vt.md](multi_vt.md) | 多虚表、Redirect/cover、`unsafe_downcast` |
| [mirror.md](mirror.md) | 字段视图（可选，最后读） |

## 类型与句柄

### 静态句柄

| 名称 | 文档 | 摘要 |
| ------------------- | -------------------- | ------------------------------------------------ |
| `iface<Trait, T>` | [iface.md](iface.md) | 保留具体类型的 Trait 句柄；sourced 形式为 `mirror<View, Src>` |
| `c_iface<Trait, T>` | [iface.md](iface.md) | const-self：普通类型 const，或 mirror 的 View const |
| `to_iface<Trait>` / `to_c_iface<Trait>` | [iface.md](iface.md) | 从对象或 mirror 构造 `iface`（`to_c_iface` 为 const 视口） |

### 动态句柄

| 名称 | 文档 | 摘要 |
| -------------------------------------------------------- | ------------------------------------------- | ---------------- |
| `dyn` / `c_dyn` | [dyn.md](dyn.md) | 类型擦除句柄（const 变体） |
| `sub_dyn` / `c_sub_dyn` | [dyn.md](dyn.md) | 叶表上的 section 切片 |
| `unique_dyn` / `shared_dyn` / `weak_dyn` / `cache_dyn` 等 | [dyn.md](dyn.md)、[pointers.md](pointers.md) | `Ptr` 族别名 |

### 转换与构造

| 名称 | 文档 | 摘要 |
| ----------------------------------------------------- | ------------------------------------- | ------------------------------------- |
| `trait_cast` / `c_trait_cast` | [cast.md](cast.md) | `iface` / `dyn` 的转换， `dyn` 仅接受上行 |
| `to_dyn` / `to_c_dyn` / `to_sub_dyn` 等 | [cast.md](cast.md) | 构造 dyn 族句柄（不是 dyn 到 dyn 的上行） |
| `instantiate_dyn<Trait, As>` | [cast.md](cast.md) / [dyn.md](dyn.md) | 类型；显式实例化 final 叶对象绑定虚表 |
| `construct` | [cast.md](cast.md) | 深构造，可重指定 `Ptr` |
| `get_proxy` / `get_rproxy` / `get_forward_proxy` | [cast.md](cast.md) | `const&` `proxy` / `rproxy`；forward 按值类别 |
| `dyn_cast` / `unsafe_downcast` | [cast.md](cast.md) | 运行时 / 不安全转换（`unsafe_downcast` 仅自由函数） |

### 视图与密封指针（可选）

| 名称 | 文档 | 摘要 |
| -------------- | ---------------------- | ------------ |
| `mirror` | [mirror.md](mirror.md) | 反射字段视图 |
| `ref` / `cref` | [ref.md](ref.md) | 密封为「引用语义」的指针 |

## 运行时可见常量

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

定义于 [detail/iface_config.hpp](../../../include/detail/iface_config.hpp)。含义见 [features.md](features.md)。
