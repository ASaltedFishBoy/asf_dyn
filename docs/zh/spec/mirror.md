# mirror

头文件：`mirror.hpp`（经 `interface.hpp`）。

`mirror<T>` 是对 `T` 非静态数据成员的反射视图。成员是引用，供 Trait 方法把 `self` 当作字段包用，而不要求 `T` 继承任何基类。不是指针语义。

```cpp
template <typename T, typename Src = void>
class mirror;

template <typename T, typename Src = void>
class mirror_base;
```

- `mirror<T>`：纯视图。可从能转到该 `T` 的对象构造（`T&&` / `const T&` / 非引用 `T` 按 `T&`），或可转换为该 mirror 的类型。
- `mirror<T, Src>`：**sourced**。额外关联源类型 `Src`。
- `mirror<T, T>`：常见 identity sourced 形式。

约束：

- mirror 到 mirror 不能丢掉 cv。
- 字段引用限定跟随 `T` 的 const / 右值。
- `mutable` 成员在 `mirror<const T>` 上仍是可变引用。
- `T` 必须是类类型（`int` 等非类类型不能做 mirror）。
- 位域成员无法取引用，含位域的类不能做 mirror。
- 只有**直接**非静态数据成员进 mirror；基类成员不在 `mirror<T>` 里（按名绑定是另一个接口 `bind_mirror`，它会找基类；`to_mirror` 只接受源与已定义转换，`bind_mirror` 是名称绑定）。

示例：`examples/*/06_mirror.cpp`。

## 特质与别名

```cpp
template <class V>
inline constexpr bool is_mirror_v = /* V decays to mirror<T, Src> */;

template <class V>
inline constexpr bool is_sourced_mirror_v =
    /* V decays to mirror<T, Src> and Src is not void */;

template <class V>
concept mirror_like = is_mirror_v<V>;

template <class V>
concept sourced_mirror = is_sourced_mirror_v<V>;

template <class V>
using mirror_target_t = /* T of that mirror */;

template <class V>
using mirror_src_t = /* Src of that mirror, or void */;

template <typename T>
using identity_mirror = mirror<T, /* class of T */>;

template <typename T, typename Src = void>
using rmirror = mirror<T&&, Src>;
template <typename T, typename Src = void>
using c_mirror = mirror<const T, Src>;
template <typename T, typename Src = void>
using c_rmirror = mirror<const T&&, Src>;

template <typename To, typename FromMirror>
concept mirror_qualifies_to =
    /* FromMirror is a mirror of the same class as To, and its T& converts to To& */;
```

`is_mirror_v` / `mirror_like` 只认 `mirror<…>`（含 mirror 类型上的 cv/ref）。

## 构造 — bare `mirror<T>`

实现在 `mirror_base<T>` 上。默认 `mirror` 是薄外壳：

```cpp
template <typename T, typename Src>
class mirror : public mirror_base<T, Src> {
 public:
  using mirror_base<T, Src>::mirror_base;
};
```

```cpp
template <typename U>
  requires(!mirror_like<U> && /* object converts to T if T is a ref, else T& */)
mirror_base(U&& obj);

template <typename V>
  requires(/* same class, different T qualification */)
explicit mirror_base(V&& other);

template <typename... FieldRefs>
  requires(/* one argument per field, each convertible to that field's mirror ref */)
explicit mirror_base(FieldRefs&&... refs);

mirror_base(const mirror_base&) = default;
mirror(mirror const&) = default;
mirror& operator=(mirror const& other);
template <typename V>
  requires(/* V is a mirror that can construct this */)
mirror& operator=(V const& other);
template <typename U>
mirror(U&&) -> mirror<U&&>;
```

`operator=` 是指针重绑：先析构，再从源 mirror 拷贝构造。不写字段。字段写入是 `assign_mirror`。

用户类型可通过转换运算符绑定：

```cpp
struct Alias {
  std::string name;
  operator asf::mirror<Person>() { return asf::mirror<Person>{name}; }
};
```

## 构造 — sourced `mirror<T, Src>`

`mirror_base<T, Src>` 继承 `mirror<T>`，因此对 `mirror<T>` 的特化函数在 `mirror<T, Src>` 上也可用。

```cpp
// no public pointer; identity is a private Src* (cv follows T)
// no default constructor (replaceable reference)

template <typename U>
  requires(/* U's class is Src, and U converts to mirror<T> implicitly */)
mirror_base(U&& obj);

template <typename U>
  requires(/* U's class is Src, but only an explicit mirror<T> ctor exists */)
explicit mirror_base(U&& obj);

template <typename U, typename Other>
  requires(/* same T, sourced Other */)
mirror_base(mirror<U, Other> const& other);

template <typename U, typename Other>
  requires(/* same class, different T qualification */)
explicit mirror_base(mirror<U, Other> const& other);

mirror_base(const mirror_base&) = default;
mirror(mirror const&) = default;
mirror& operator=(mirror const& other);
template <typename V>
  requires(/* V is a mirror that can construct this */)
mirror& operator=(V const& other);
```
`operator=` 同样是指针重绑（析构 + 拷贝构造）。`Src` 身份是私有 `Src*`（cv 跟 `T`），不是公开 `void*`。身份转换跟本 `T`，不是协变 `Src*`。成员不叫 `ptr`，不会挡用户字段。

sourced 转换的强度跟随无源转换：仅当 `U&` 能隐式转 `mirror<T>` 时为隐式；当 `mirror<T>` 只有显式构造（如单字段 pack）时，sourced 构造也是显式。
`operator=` 同样是指针重绑（析构 + 拷贝构造）。`Src` 身份是私有 `Src*`（cv 跟 `T`），不是公开 `void*`。身份转换跟本 `T`，不是协变 `Src*`。成员不叫 `ptr`，不会挡用户字段。

## 自由函数

```cpp
template <typename T = void, typename V>
  requires(!mirror_like<V>)
constexpr auto to_mirror(V&& obj);
```

仅对象（不要已有 mirror）。对象须能转到该 mirror 所要的 `T`：`T` 是引用则原样（`T&` / `const T&` / `T&&`），不是引用则 `T&`。用户转换 / `identity_mirror` 也算。

- `to_mirror<T>(obj)` → `mirror<T, std::remove_cvref_t<V>>`
- `to_mirror(obj)` → `mirror<Obj, Obj>`

```cpp
template <typename T = void, typename V>
  requires(!mirror_like<V> /* and T's fields exist on V by name */)
constexpr auto bind_mirror(V&& obj);
```

调用形式与 `to_mirror` 相同。`T` 的每个字段绑到同名字段。对象不必能转到该 mirror 的 `T`。

- `bind_mirror<T>(obj)` → `mirror<T, std::remove_cvref_t<V>>`
- `bind_mirror(obj)` → `mirror<Obj, Obj>`

```cpp
template <typename T = void, typename V>
constexpr auto to_pure_mirror(V&& v);
```

裸 `mirror<T>`（无 `Src`）。对象或已有 mirror。

- `to_pure_mirror<T>(obj|mirror)` → `mirror<T>`
- `to_pure_mirror(obj)` → `mirror<std::remove_reference_t<V>>`
- `to_pure_mirror(mirror)` → `mirror<that mirror's T>`

```cpp
template <typename T, typename Src>
  requires(!std::is_void_v<Src>)
constexpr /* Src& / const Src& / Src&& following T */
src_of(mirror<T, Src> const& m);
```

仅 sourced。cv 与引用跟随 `T`，不跟 `m` 是否 const（const `mirror<Person, Person>` 仍得 `Person&`； `mirror<const Person, Person>` 得 `const Person&`； `rmirror` 得 `Src&&`）。

```cpp
template <typename T, typename ST>
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, T&& src);
template <typename T, typename ST>
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, T const& src);
template <typename T, typename ST>
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, c_mirror<T> src);
template <typename T, typename ST>
constexpr mirror<T&, ST> assign_mirror(mirror<T&, ST> dst, rmirror<T> const& src);

template <typename T, typename ST>
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, T&& src);
template <typename T, typename ST>
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, T const& src);
template <typename T, typename ST>
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, c_mirror<T> src);
template <typename T, typename ST>
constexpr mirror<T, ST> assign_mirror(mirror<T, ST> dst, rmirror<T> const& src);
```

按字段把 `src` 写进 `dst` 绑定的对象。目的是 `T&` 或 `T`。`src` 是 `T&&`（移） / `T const&`（拷）、无源 `c_mirror<T>`（拷）或无源 `rmirror<T>`（移）。可拷贝的 mirror 按值；rmirror 为 const&。返回 `dst` 的拷贝。不改 sourced dest 的身份。

```cpp
template <typename T, typename Src = void>
constexpr /* decay of T */ value_of(mirror<T, Src> const& m);
```

从已存字段引用重建 `T`。`Src` 是 `T` 时从 `src_of` 取出。拷贝/移动跟随 `T`（`T&&` 移出，否则拷出），不跟 mirror 的值类别。

## 定制：特化 `mirror`，继承 `mirror_base`

字段组装、`assign_mirror`、`value` 与 `src` 在公开的 `mirror_base<T[, Src]>`。`operator=` 是默认 `mirror` 壳上的指针重绑。替换 `mirror` 的特化应保留该重绑。要加函数时，特化 `mirror` 并继承对应的 `mirror_base`（不要改 `mirror_base` 本身）：

```cpp
template <>
class asf::mirror<Widget> : public asf::mirror_base<Widget> {
 public:
  using mirror_base<Widget>::mirror_base;
  int width() const { return w; }
};
```

`is_mirror_v` / `mirror_like` 仍只认 `mirror<…>`。

## 与 iface / dyn

- `iface<Trait, T>`: Self 来自实现侧 mirror（unsourced 或 sourced）。
- `iface<Trait, mirror<T, Src>>`: `Self = mirror<T, Src>`.
- 用户 `mirror` 没有 Src 时只能绑 `iface`（无 `dyn` 路径）。mirror → `iface`：绑定不设门禁。**有源**：每个方法都可调；`[[=mirror]]`（方法标记，否则声明 Trait）选择呈现 Self——mirror，或对象（`Src`）。**无源**：只允许调 `[[=mirror]]` 方法（非 mirror 方法不会剥 / 物化 View）。
- `dyn` 从 `mirror<T, Src>`： 不要求整棵布局都标 `mirror`。 view 与 `Src` 同类时 Storage 指向对象（每次调用重建 mirror）；view ≠ `Src` 或 `identity_mirror` 时 Storage 指向该 mirror（须比 dyn 活得久）。mirror 段用 `Trait<T>`，非 mirror 段用 `Trait<Src>`。
- 在 `dyn` 上，`op_construct` 不是 mirror 方法：不跟 Trait 的 `[[=mirror]]` 走。`self` 是对象（sourced 时是 `Src`）。自己标 `[[=mirror]]` 则不合法。
- destroy 不是 Trait 方法：每张虚表都有 `vt[-1]` destroy（`~Src`）。view 与 `Src` 不一致的 sourced mirror 仍用普通 `dyn`。

## 参见

- [iface.md](iface.md)
- [dyn.md](dyn.md)
- [cast.md](cast.md)
