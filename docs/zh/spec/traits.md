# Trait

Trait 是 ASF 的抽象单元：一组可在 `iface` / `dyn` 上调用的方法。

## 声明形状

```cpp
template <typename T = void>
struct Trait;

template <>
struct Trait<void> : Base0<void>, Base1<void> {  // 可选接口 MI
  void foo(auto& self);
  auto bar(auto const& self) -> int { return 0; }  // 默认实现
};

template <>
struct Trait<MyType> final {
  void foo(MyType& self) { /* ... */ }
};
```

| 特化 | 角色 |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `Trait<void>` | 方法列表；可继承其他 `Base<void>` |
| `Trait<T>` | 类型 `T` 的实现；首参对应声明里的 `auto`，通常是 `T&` / `const T&`。标 `[[=mirror]]` 时是 `mirror<T>` / `c_mirror<T>`（`auto&` / `auto const&`） |
| `Trait<void> final` | 该 Trait 的一切 `Trait<As>` 亦视为 final |
| `Trait<As> final` | 仅对该 `As` 为 final（dyn 叶常见写法） |

`iface` 不要求 `final`。

`dyn` **指针 / mirror 绑定（默认开启中间叶）**：虚表由 final 叶注入；非 final 中间 Trait 只 ADL 取已注入的表，**自己不定义**。因此必须显式实例化 `instantiate_dyn` 或先（或同时）绑定覆盖该中间节点的 **final 叶**，否则链接失败。祖先已是 final 时，禁止再绑定派生 Trait（`static_assert`）。完整规则见 [dyn.md](dyn.md)#指针--mirror-绑定与-final重要；宏与多 final UB 见 [features.md](features.md)#asf_has_dyn_intermediate。

## 方法与虚表

- 默认：方法进入 **dyn 虚表槽**（类似虚函数）。
- **dyn 槽**：`self` 必须是第一个参数，且最多一个模板推导参数（就是 self）。
- 接口属性注解可标在 **类型**（`Trait<void>`）或 **方法**（接口）上。标在类型上时，该声明里的每个方法都继承这条注解。标在方法上时，覆盖类型上的同名注解。同一声明上同时写一对相反属性是错的。成对：`[[=expl]]`/`[[=no_expl]]`、`[[=imple]]`/`[[=no_imple]]`、`[[=mirror]]`/`[[=no_mirror]]`。（`static` 是声明属性，不是注解。）见 [annotations.md](annotations.md)。
- 第二个推导参数不能动态派发（编译错误）；该重载标 `static`，仅用于 iface。
- `static`：不进 dyn 槽，仅用于 `iface`（无重新加入）。
- `dyn` / `iface` 句柄自身只暴露 **本 Trait 声明的方法**。
- 相关 Trait 视口是成员 `as<To>()`（`dyn`：同表 / 仅上行；`iface`：继承链上可上可下）。
- 调用表面是 `proxy<To>()` / `rproxy<To>()`，自由函数是 `get_proxy<To>`、`get_rproxy<To>` 与 `get_forward_proxy<To>`。对应表见 [cast.md](cast.md)。

## 特殊方法名

| 名字 | 含义 |
| -------------- | ------------------------------------------------------------------------------------------ |
| *（destroy）* | 不是 Trait 方法。每张虚表都有 `vt[-1]` destroy（`~Src` / `~Storage`）。平凡析构共用一个空函数指针。句柄 `destroy()` 调用该槽 |
| `op_construct` | 擦除构造；返回 `auto`/具体类型（非 `void`）。`[[=imple]]` 不按名查找：按声明参数选构造函数（`auto&&` 是右值）。各重载是普通 dyn 槽，只按参数分发（含 `self` 值类别） |
| `op_assign*` | `(*h) = x` 具体 RHS 赋值（非 Src/`auto`） |
| `op_convert*` | `To(*h)` 转换；`[[=expl]]` 标为显式 |

## 运算符

方法名写在 Trait 上。 proxy 生成下表的 C++ 运算符。不使用 `friend`。 `self` 可以在任意参数位置。 **dyn 槽要求** `self` **是第一个参数，且最多一个模板推导参数**。 `self` 靠后或还有第二个推导参数只支持 iface，该重载标 `static`，按参数位置匹配。一元运算符只有 Trait 上的那一个重载。 dyn 无反向。

| 方法名 | 运算符 | 形状 |
| --------------- | ----- | ----------------------- |
| `op_add` | `+` | 二元 |
| `op_sub` | `-` | 二元 |
| `op_neg` | `-` | 一元 |
| `op_mul` | `*` | 二元 |
| `op_div` | `/` | 二元 |
| `op_mod` | `%` | 二元 |
| `op_xor` | `^` | 二元 |
| `op_bitand` | `&` | 仅二元。一元 `&` 不是 Trait 方法 |
| `op_bitor` | `\|` | 二元 |
| `op_compl` | `~` | 一元 |
| `op_not` | `!` | 一元 |
| `op_lt` | `<` | 二元 |
| `op_gt` | `>` | 二元 |
| `op_iadd` | `+=` | 二元 |
| `op_isub` | `-=` | 二元 |
| `op_imul` | `*=` | 二元 |
| `op_idiv` | `/=` | 二元 |
| `op_imod` | `%=` | 二元 |
| `op_ixor` | `^=` | 二元 |
| `op_ibitand` | `&=` | 二元 |
| `op_ibitor` | `\|=` | 二元 |
| `op_lshift` | `<<` | 二元 |
| `op_rshift` | `>>` | 二元 |
| `op_ilshift` | `<<=` | 二元 |
| `op_irshift` | `>>=` | 二元 |
| `op_eq` | `==` | 二元 |
| `op_ne` | `!=` | 二元 |
| `op_le` | `<=` | 二元 |
| `op_ge` | `>=` | 二元 |
| `op_cmp` | `<=>` | 二元 |
| `op_and` | `&&` | 二元 |
| `op_or` | `\|\|`| 二元 |
| `op_inc` | `++` | 无其它参数为前缀；一个 `int` 参数为后缀 |
| `op_dec` | `--` | 无其它参数为前缀；一个 `int` 参数为后缀 |
| `op_index` | `[]` | `self` 是被索引的对象 |
| `op_call` | `()` | `self` 是被调用的对象，其余参数是实参 |
| `op_comma` | `,` | 二元 |
| `op_ptr_to_mem` | `->*` | 二元 |

`op_construct*`、`op_assign*`、`op_convert*` 不在此表。

## proxy 上的声明

Trait 上只写方法名（`foo`、`op_add`）。对应的 C++ 运算符、转换、赋值由库生成到 `*h` / `proxy()` / `rproxy()`（在 `dyn` / `c_dyn` / `iface` 上，见 [proxy](#proxy)）得到的 proxy。句柄不混入 Trait 运算符。

`sub_dyn`、`c_sub_dyn`、`dyn_view`、`c_dyn_view` 的 proxy 仍有同名方法成员，但没有目录运算符，也没有来自 `op_convert*` / `op_assign*` 的转换与赋值。

每一个方法名单独决定是否生成那一条声明。 Trait 没有 `op_add`，proxy 上就没有 `operator+`。有这个名字、但当前表面的条件不满足（例如 const proxy 而该方法不是 const self），这条声明同样不存在。各表面的条件写在下面的表里。

普通（非 `op_*`）Trait 方法出现为 proxy 的同名成员函数，参数是 Trait 声明去掉 `self` 之后剩下的参数。何时存在：

- 可变 `dyn` 的 proxy：该方法是 dyn 槽（`self` 在第一个参数）。
- 可变 `iface` 的 proxy：`Trait<void>` 有这个名字。
- `c_dyn` 的 proxy：self 是 const，并且满足与可变 `dyn` 相同的条件。
- `iface<Trait, const T>` / `iface<Trait, mirror<const T, Src>>` 的 proxy：`Trait<void>` 有这个名字，且 self 是 const。

`op_construct*`、`op_assign*`、`op_convert*` 不是具名成员；后两者的运算符见 proxy 小节。

### proxy

`*h`、`proxy()`、`rproxy()` 得到的 `proxy_t` 与 `rproxy_t`，用于 `dyn`、`c_dyn`、`iface`。

| 表面 | 该运算符何时存在 |
| --- | --- |
| 可变 `dyn` 的 proxy | 该方法是 dyn 槽（`self` 在第一个参数）。 |
| 可变 `iface` 的 proxy | `Trait<void>` 有该名字。 |
| `c_dyn` 的 proxy | self 是 const，并且满足可变 `dyn` 那一行的条件。 |
| `iface<Trait, const T>`、`iface<Trait, mirror<const T, Src>>` 的 proxy | `Trait<void>` 有该名字，且 self 是 const。 |

`+`、`*` 这类运算符各对应一个方法名。`operator-` 例外：一元 `operator-()` 调 `op_neg`，二元 `operator-(rhs)` 调 `op_sub`。两个名字只要有一个满足上表，proxy 上就会同时出现这两个 `operator-` 重载。

二元成员。每个名字有非 const 与 const 两个重载。实参转给 Trait 方法。`self` 是 proxy 里的句柄。运算符见 [运算符](#运算符)。

```cpp
decltype(auto) operator+(auto&&... args);
decltype(auto) operator+(auto&&... args) const;
```

`op_bitand` 只接一个右操作数：

```cpp
decltype(auto) operator&(auto&& rhs);
decltype(auto) operator&(auto&& rhs) const;
```

一元。名字：`op_neg`、`op_compl`、`op_not`。

```cpp
decltype(auto) operator-();
decltype(auto) operator-() const;
decltype(auto) operator~();
decltype(auto) operator~() const;
decltype(auto) operator!();
decltype(auto) operator!() const;
```

`op_inc` 与 `op_dec`。无实参是前缀。一个实参是后缀；proxy 传入 `0`。

```cpp
decltype(auto) operator++();
decltype(auto) operator++(int);
decltype(auto) operator++() const;
decltype(auto) operator++(int) const;
decltype(auto) operator--();
decltype(auto) operator--(int);
decltype(auto) operator--() const;
decltype(auto) operator--(int) const;
```

`op_index` 与 `op_call`。`self` 是 proxy 里的句柄。

```cpp
decltype(auto) operator[](auto&&... args);
decltype(auto) operator[](auto&&... args) const;
decltype(auto) operator()(auto&&... args);
decltype(auto) operator()(auto&&... args) const;
```

`op_convert*` 在 proxy 的转换载体上，每个重载一条声明。`To` 是该重载的返回类型。

| 条件 | 声明 |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| 返回具体类型 `To`，且没有 `[[=expl]]` | `operator To() const` |
| 返回具体类型 `To`，且有 `[[=expl]]` | `explicit operator To() const` |
| `[[=imple]]` | `template <typename To> explicit(E) operator To() const`。`E` 在该重载为 `[[=expl]]` 时为真。`To` 不能是 self，也不能是 `mirror`、`identity_mirror` |

`op_assign*` 在同一载体上。从不 explicit。

| 条件 | 声明 |
| --------------------------- | --------------------------------------------------------------- |
| RHS 键是具体类型 `From` | `decltype(auto) operator=(From rhs)` |
| `[[=imple]]`，或 RHS 是 `auto` | `template <typename From> decltype(auto) operator=(From&& rhs)` |

多个 `operator=` 用 `using` 并入同一重载集。`op_construct*` 不生成运算符。dyn 的 proxy 没有反向自由运算符。

**反向**（4 + (*h)）。条件：该名字有 self 在第二参数位置的重载。**仅 iface proxy**。与目录相同的二元名（`op_add`、`op_comma` 等）。`U` 去 cvref 后不能是句柄。

```cpp
template <typename U>
friend decltype(auto) operator+(U const& a, proxy& b);
template <typename U>
friend decltype(auto) operator+(U const& a, proxy const& b);
```

proxy 的拷贝与移动拷贝其中的观察器。`construct(proxy)` 深拷贝对象。`construct(rproxy)` 深移动对象。

## 继承与布局（摘要）

- 布局按祖先 **grid-merge sections**，叶表含全部祖先方法。
- **Cover** =「仿佛该 Trait 为 final」时的独立布局，不一定是真实叶表的前缀切片。
- `ASF_HAS_DYN_MULTI_VT=1`：非 section-prefix 祖先插入 Redirect→cover，以支持全路径上行。
- 详见 [multi_vt.md](multi_vt.md)。开关：[features.md](features.md)#asf_has_dyn_multi_vt。

## 相关

- [annotations.md](annotations.md)
- [iface.md](iface.md) / [dyn.md](dyn.md)
- 示例：`examples/*/01_static.cpp`、`examples/*/05_mi_cast.cpp`、`tests/general/iface_inherit.cpp`
