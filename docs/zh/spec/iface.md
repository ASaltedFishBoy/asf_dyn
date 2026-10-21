# iface

头文件：`iface.hpp`。

## 作用

`iface<Trait, T>` 是 **保留具体类型** 的 Trait 句柄。调用走实现侧绑定，不做类型擦除。

`iface<Trait, T>`：`Self` 是 `T&`；布局标了 `[[=mirror]]` 时为 Trait 实现侧 mirror。

`iface<Trait, const T>`：`const T&`，或带 const 视图的实现侧 mirror。

`iface<Trait, mirror<T, Src>>`：`Self` 是 `mirror<T, Src>`。

`iface<Trait, mirror<const T, Src>>`：`Self` 是 `mirror<const T, Src>`。

普通对象时 `Self` 的具体类型：

| 形式 | 非 mirror Trait | 标了 `[[=mirror]]` 的布局 |
| --- | --- | --- |
| `iface<Trait, T>` | `T&` | `mirror<T>`；当 `Trait<T>` 的某个实现方法以 `mirror<T, T>` 形状接收 self 时为 `identity_mirror<T>`（即 `mirror<T, T>`） |
| `iface<Trait, const T>` | `const T&` | `mirror<const T>`；同条件为 `mirror<const T, T>` |

存储随之而定：`Self` 是引用（`T&` / `const T&`）时句柄存对象指针；`Self` 是 mirror 时按值持有。

`c_iface<Trait, T>`：`iface<Trait, const T>`。

`c_iface<Trait, mirror<View, Src>>`：`iface<Trait, mirror<const View, Src>>`。

```cpp
template <template <class> class Trait, typename T>
class iface;

template <template <class> class Trait, typename T>
using c_iface = /* T 的 const-self，或 mirror 视图的 const-self */;
```

下文 `Arg` 是句柄的第二模板参数（`T` / `const T` / `mirror<…>`）。

## 构造

```cpp
template <class V>
explicit iface(V&& object);
```

普通对象的 `iface` 存 `T*` / `const T*`，赋值可重绑。`iface` **始终处于绑定状态**：没有空态、没有默认构造、也没有 `operator bool` / 空比较（对照：`dyn` 是指针语义——可空、有默认构造、有 `operator bool`）。`==` 是按地址的身份比较，不是判空。mirror 后端的 `iface` 持有 mirror 值：sourced 也经 `src_of` / 私有 `Src*` 身份比较；mirror 本身不可默认构造。

普通对象的 `iface` 存 `T*` / `const T*`，赋值可重绑。`iface` **始终处于绑定状态**：没有空态、没有默认构造、也没有 `operator bool` / 空比较。`==` 是按地址的身份比较，不是判空。mirror 后端的 `iface` 持有 mirror 值：sourced 也经 `src_of` / 私有 `Src*` 身份比较；mirror 本身不可默认构造。

```cpp
iface(iface const&) = default;
iface& operator=(iface const&) = default;

template <template <class> class From, typename Arg>
iface(iface<From, Arg> o);
template <template <class> class From, typename Arg>
iface& operator=(iface<From, Arg> const& o);
```

Trait 基可上可下。本句柄的 `Self` 须能从 `Arg&` 或 `Arg const&` 构造。保持 `T` / `Src`。同 Trait 可变 → const-self 也是这条： `const T` / `mirror<const T, Src>` 能从可变 `Arg` 构造。

所有 `iface` 形式都没有默认构造；每个 `iface` 都在构造时绑定。

```cpp
template <template <class> class Trait, class V>
constexpr auto to_iface(V&& object);
```

`V` 是 sourced `mirror<View, Src>`：`iface<Trait, mirror<View, Src>>`。

`V` 是非 sourced 的 mirror-like：`iface<Trait, View>`。

否则：`iface<Trait, std::remove_reference_t<V>>`（`const Person&` → `iface<Trait, const Person>`）。

工厂：它选定第二个模板实参；`iface` 自己不能（需要 `Trait` 加 `T` / `const T` / `mirror<…>`）。之后按上面的 `explicit iface(V&&)` 构造（`Self` 来自 `V`）。

```cpp
template <template <class> class Trait, class V>
constexpr auto to_c_iface(V&& object);
```

`to_iface` 的 const 视口（与 `to_dyn` 旁的 `to_c_dyn` 对称）：普通对象得 `iface<Trait, const T>`；sourced `mirror<View, Src>` 得 `iface<Trait, mirror<const View, Src>>`；非 sourced mirror-like 得 `iface<Trait, const View>`。

## 声明 — 可变 `iface`

`iface<Trait, T>` 与 `iface<Trait, mirror<T, Src>>`。

```cpp
using proxy_t = /* 本句柄的 proxy */;
using rproxy_t = /* 本句柄的 rproxy */;
using c_proxy_t = /* const-self proxy */;
```

sourced：`using mirror_t` 是持有的 `mirror<T, Src>`。

```cpp
proxy_t proxy() const;
rproxy_t rproxy() const;
c_proxy_t c_proxy() const;
proxy_t operator->() const;
proxy_t operator*() const;

template <template <class> class To>  // To 是本 Trait 的 Trait 基
auto proxy() const -> typename /* To 形状的 iface */::proxy_t;
template <template <class> class To>
auto rproxy() const -> typename /* To 形状的 iface */::rproxy_t;
template <template <class> class To>
auto c_proxy() const -> typename /* To 形状的 iface */::c_proxy_t;
```

To 形状指 `iface<To, T>` 或 `iface<To, mirror<T, Src>>`。同 Trait 的 `proxy<To>()` / `rproxy<To>()` / `c_proxy<To>()` 就是本句柄的 `proxy_t` / `rproxy_t` / `c_proxy_t`。

```cpp
template <template <class> class To>  // To 在本 Trait 继承链上（可上可下）
iface<To, T> as() const;                       // 非 sourced
template <template <class> class To>
iface<To, mirror<T, Src>> as() const;          // sourced

template <template <class> class To>  // Trait 基可上可下；目标 Self 可构造
iface<To, T> trait_cast() const;
template <template <class> class To>
iface<To, mirror<T, Src>> trait_cast() const;

template <template <class> class To>
iface<To, const T> c_trait_cast() const;
template <template <class> class To>
iface<To, mirror<const T, Src>> c_trait_cast() const;

explicit operator c_iface<Trait, T>() const;                 // 非 sourced，同 Trait
explicit operator iface<Trait, mirror<const T, Src>>() const;  // sourced，同 Trait

Self get() const;
```

普通对象与 sourced：

```cpp
bool operator==(iface const& a, iface const& b) noexcept;  // 按地址的身份比较

template <template <class> class Trait2, typename View, typename Src2>
bool operator==(iface const& a,
                iface<Trait2, mirror<View, Src2>> const& b) noexcept;
```

普通对象上 `==` 比存储指针；sourced 上比 `src_of` / 私有 `Src*` 身份。跨 Trait 的 `==` 要求两侧都是可比的 sourced（Trait 相关、同一 mirror 类、`Src` 相关）。不声明 `!=`（C++20 可改写）。没有 `<=>`。

同名方法、转换、构造、赋值不在 iface 句柄上。与 dyn 相同：`*h` / `h->`（[dyn.md](dyn.md)、[traits.md](traits.md)#proxy）。句柄 `==` 是身份 (`src_of`)。Trait 运算符只在 `*h`: [traits.md](traits.md)#proxy。

## 声明 — const-self `iface`

`iface<Trait, const T>`、`iface<Trait, mirror<const T, Src>>`、`c_iface<…>`。

```cpp
using proxy_t = /* 本句柄的 const-self proxy */;
using rproxy_t = /* 本句柄的 const-self rproxy */;
using c_proxy_t = proxy_t;

proxy_t proxy() const;
rproxy_t rproxy() const;
c_proxy_t c_proxy() const;            // same as proxy()
proxy_t operator->() const;
proxy_t operator*() const;

template <template <class> class To>  // To 是 Trait 基
auto proxy() const -> typename /* To 形状的 const-self iface */::proxy_t;
template <template <class> class To>
auto rproxy() const -> typename /* To 形状的 const-self iface */::rproxy_t;
template <template <class> class To>
auto c_proxy() const -> typename /* To 形状的 const-self iface */::c_proxy_t;

template <template <class> class To>  // To 在本 Trait 继承链上（可上可下）
iface<To, const T> as() const;
template <template <class> class To>
iface<To, mirror<const T, Src>> as() const;

template <template <class> class To>
iface<To, const T> trait_cast() const;
template <template <class> class To>
iface<To, mirror<const T, Src>> trait_cast() const;

template <template <class> class To>
iface<To, const T> c_trait_cast() const;
template <template <class> class To>
iface<To, mirror<const T, Src>> c_trait_cast() const;

Self get() const;
```

普通对象与 sourced 的 `==` 与可变同形，`Self` 换成本特化的。同名方法 / 转换 / 构造 / 赋值：`*h` / `h->`，与可变相同。

## 自由函数（同行为）

```cpp
template <template <class> class To, template <class> class From, typename Arg>
auto get_proxy(iface<From, Arg> const& h)
    -> decltype(h.template proxy<To>());

template <template <class> class To, template <class> class From, typename Arg>
auto get_rproxy(iface<From, Arg> const& h)
    -> decltype(h.template rproxy<To>());

template <template <class> class To, template <class> class From, typename Arg>
auto get_forward_proxy(iface<From, Arg> const& h)
    -> decltype(h.template proxy<To>());
template <template <class> class To, template <class> class From, typename Arg>
auto get_forward_proxy(iface<From, Arg>&& h)
    -> decltype(std::move(h).template rproxy<To>());

template <template <class> class To, template <class> class From, typename Arg>
iface<To, Arg> trait_cast(iface<From, Arg> const&);

template <template <class> class To, template <class> class From, typename Arg>
c_iface<To, Arg> c_trait_cast(iface<From, Arg> const&);
```

`get_proxy` / `get_rproxy` 取 `const&`。`get_forward_proxy` 左值为 `proxy<To>()`，右值为 `rproxy<To>()`。proxy 辅助要求 `To` 是 Trait 基。`trait_cast` / `c_trait_cast` 要求 Trait 基可上可下，且目标 `Self` 可构造。全表见 [cast.md](cast.md)。

## 与 dyn

`to_dyn` / `to_c_dyn` 接受 `iface<From, Arg>`，条件是 `Arg` 自己能变成目标 `dyn` / `c_dyn`。普通对象不能直接 `to_dyn`，`iface<Trait, Person>` 同样不能。`mirror<View, Src>` 可以时，`iface<Trait, mirror<View, Src>>` 就可以。dyn→dyn 上行用 `trait_cast`，不是 `to_dyn`。见 [cast.md](cast.md)、[dyn.md](dyn.md)。

## 参见

- [traits.md](traits.md)
- [mirror.md](mirror.md)
- [annotations.md](annotations.md)
- 示例：`examples/*/01_static.cpp`
