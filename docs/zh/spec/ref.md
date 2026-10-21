# ref / cref

头文件：`ref.hpp`。不是 Trait 句柄。与 dyn 的 `Ptr` 不在同一层（[pointers.md](pointers.md)）。

## 作用

`ref<P>` / `cref<P>` 把指针语义的 `P` 封成只能当 **引用** 用：不能从裸 `P` 或 `nullptr` 构造、赋值。只能通过 `as_ref` / `as_cref` 纳入。不检空；类型不负责生命期。

const 在所指对象视图（`*` / `->` / `()`）上，不在 `P` 上。非 const 的 `cref` 对象仍可通过 `get()` 重定存储的 `P`。

## 指针语义 `P`

下面每条声明都要求 `P` 是指针语义：从 `P const&` 能得到指向 **非 `void`** 对象的指针。原生指针，或带 `.get()`、返回这类指针的智能指针式类型。

## 类型

```cpp
template <class P>
class cref;

template <class P>
using ref = /* 若 P 的所指对象是 const，则为 cref<P>；否则为可变的 ref */;
```

`cref<P>` 总是存在。`*` / `->` / `()` 看到 const 所指对象（如 `*const T*`）。

`ref<P>` 在 `P` 的所指对象不是 const 时：该所指对象的可变视图。

`ref<P>` 在 `P` 的所指对象是 const 时：与 `cref<P>` 同一类型（`std::same_as`）。

```cpp
template <class P>
constexpr ref<P> as_ref(P p) noexcept(std::is_nothrow_move_constructible_v<P>);

template <class P>
constexpr cref<P> as_cref(P p) noexcept(std::is_nothrow_move_constructible_v<P>);
```

`as_ref` 返回 `ref<P>`（const 所指对象时即 `cref<P>`）。`as_cref` 总是返回 `cref<P>`。

## 构造

可变 `ref<P>` 与 `cref<P>` 上：

```cpp
ref() = delete;
ref(std::nullptr_t) = delete;
ref& operator=(std::nullptr_t) = delete;
ref(P const&) = delete;
ref(P&&) = delete;
ref& operator=(P const&) = delete;
ref& operator=(P&&) = delete;

ref(ref const&) = default;
ref(ref&&) = default;
ref& operator=(ref const&) = default;
ref& operator=(ref&&) = default;
```

`cref` 同一清单（`ref` 换成 `cref`）。

`cref<P>` 还能从可变 `ref<P>` 转换（仅当该特化存在：`P` 的所指对象不是 const）：

```cpp
constexpr cref(ref<P> const&) noexcept;
constexpr cref(ref<P>&&) noexcept;
```

隐式。反向不存在。

## 成员

可变 `ref<P>` 与 `cref<P>` 都有。

```cpp
using pointer = P;
static constexpr bool is_const = /* 可变 ref 为 false，cref 为 true */;
```

存储的 `P`（重定 / 观察指针对象本身；**不** 给所指对象加 const）：

```cpp
constexpr P& get() noexcept;                    // 非 const 句柄
constexpr P const& get() const noexcept;        // const 句柄
constexpr operator P&() noexcept;               // 非 const 句柄
constexpr operator P const&() const noexcept;   // const 句柄
```

所指对象视图。`cref` 上所指对象带 const。可变 `ref` 上不带。

```cpp
constexpr decltype(auto) operator*() const;     // 从 P 得到的指针可解引用
constexpr auto operator->() const noexcept;     // 该指针是原生指针
template <class... Args>
constexpr decltype(auto) operator()(Args&&... args) const;
                                                // cref 时带 const 的所指对象可以 Args 调用
```

相等比的是从 `P` 取出的原始地址，不是 `P` 自带的 `==`。同一 `P`，两种视图都可。不声明 `!=`、`<=>`、`operator bool`、与 `nullptr` 比较。C++20 可从 `==` 改写 `!=`。

```cpp
constexpr bool operator==(ref<P> const& a, ref<P> const& b) noexcept;
constexpr bool operator==(cref<P> const& a, cref<P> const& b) noexcept;
```

存在可变 `ref<P>` 时，该 `P` 的 `ref == cref` 与 `cref == ref` 也有。

## 不提供

- 默认 / 空句柄
- 从裸 `P` 构造或赋值（用 `as_ref` / `as_cref`）
- 生命期或空检
- Trait 方法、虚表、`dyn` / `iface`

## 参见

- [pointers.md](pointers.md) — `void*` 与其他 dyn Ptr
- [dyn.md](dyn.md)
