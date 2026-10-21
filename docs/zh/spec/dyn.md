# dyn 族句柄

头文件：`dyn.hpp`（经 `interface.hpp`）。

`dyn<Trait, Ptr = void*>` 持有 `{ complete erased Ptr, vt }`。默认 `Ptr = void*` 不拥有。转换保持完整 `Ptr`，除非某条构造另说。

```cpp
template <template <class> class Trait, class Ptr = void*>
class dyn;

template <template <class> class Trait, class Ptr = void*>
class c_dyn;

template <template <class> class Trait, class Ptr = void*>
class sub_dyn;

template <template <class> class Trait, class Ptr = void*>
class c_sub_dyn;
```

```cpp
template <template <class> class Trait, class Deleter = std::default_delete<void>>
using unique_dyn = dyn<Trait, std::unique_ptr<void, /* default unique_dyn deleter */>>;
template <template <class> class Trait>
using shared_dyn = dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_dyn = dyn<Trait, std::weak_ptr<void>>;
template <template <class> class Trait, std::size_t Size,
          std::size_t Align = alignof(std::max_align_t), class Ops = cache_ops<>>
using cache_dyn = dyn<Trait, cache_ptr<Size, Align, Ops>>;

template <template <class> class Trait>
using shared_sub_dyn = sub_dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_sub_dyn = sub_dyn<Trait, std::weak_ptr<void>>;

template <template <class> class Trait>
using shared_c_dyn = c_dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_c_dyn = c_dyn<Trait, std::weak_ptr<void>>;
template <template <class> class Trait>
using shared_c_sub_dyn = c_sub_dyn<Trait, std::shared_ptr<void>>;
template <template <class> class Trait>
using weak_c_sub_dyn = c_sub_dyn<Trait, std::weak_ptr<void>>;
```

`unique_dyn<Trait>` 与 `unique_dyn<Trait, std::default_delete<void>>` 是同一类型。指针 kind 见 [pointers.md](pointers.md)。构造辅助见 [cast.md](cast.md)。

## 构造 — `dyn`

```cpp
dyn() noexcept;
dyn(std::nullptr_t) noexcept;
dyn(dyn const&) = default;
dyn(dyn&&) = default;
dyn& operator=(dyn const&) = default;
dyn& operator=(dyn&&) = default;

template <class U>
dyn(U* p);   // observation Ptr only; not void; not const U; not a naked object
```

`U*` 绑定要求 final 叶（或 `ASF_HAS_DYN_INTERMEDIATE`）。见 [pointer / mirror bind](#指针--mirror-绑定与-final)。

```cpp
template <class T, class Src>
dyn(mirror<T, Src> const& v);   // sourced; not a heap Ptr; T is not const
```

不要求每一段都标 `[[=mirror]]`。规则见 [mirror.md](mirror.md)。

```cpp
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP const&, Ptr>)
dyn(dyn<From, SrcP> const& o);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP &&, Ptr>)
dyn(dyn<From, SrcP>&& o);
template <template <class> class From, class SrcP>
dyn& operator=(dyn<From, SrcP> const& o);
template <template <class> class From, class SrcP>
dyn& operator=(dyn<From, SrcP>&& o);
```

Trait 上行（含 From 即 Trait）与 `SrcP` 到本 `Ptr` 一起检查。同一 `Ptr`：拷贝/移动（或 cache 走 vt）。否则 `Ptr` 从该值类别的 `SrcP` 构造。不是 `construct`。显隐跟指针类型走（`weak_ptr` 从 `shared_ptr` 隐式）。 `Ptr` 相同时与 `trait_cast<Trait>(o)` 同义。MULTI_VT 可能换 `vt`。[multi_vt.md](multi_vt.md)。句柄转换见 [pointers.md](pointers.md)。

## 构造 — `c_dyn`

```cpp
c_dyn() noexcept;
c_dyn(std::nullptr_t) noexcept;
c_dyn(c_dyn const&) = default;
c_dyn(c_dyn&&) = default;
c_dyn& operator=(c_dyn const&) = default;
c_dyn& operator=(c_dyn&&) = default;

template <class U>
c_dyn(U* p);   // observation Ptr; const pointee allowed

template <class T, class Src>
c_dyn(mirror<T, Src> const& v);

template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP const&, Ptr>)
c_dyn(c_dyn<From, SrcP> const& o);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP &&, Ptr>)
c_dyn(c_dyn<From, SrcP>&& o);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP const&, Ptr>)
c_dyn(dyn<From, SrcP> const& o);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP &&, Ptr>)
c_dyn(dyn<From, SrcP>&& o);
```

转换 / 绑定规则与 `dyn` 相同，const-self 槽。

## 声明 — `dyn<Trait, Ptr>`

除注明外不是 `weak_ptr`。`proxy_t` / `rproxy_t` / `c_proxy_t` 是本句柄的 proxy（里面是观察器 `void*`）。

```cpp
using proxy_t = /* mutable Trait proxy */;
using rproxy_t = /* move-semantic Trait proxy */;
using c_proxy_t = /* const Trait proxy */;

proxy_t proxy() const;
rproxy_t rproxy() const;
c_proxy_t c_proxy() const;            // c_dyn: same as proxy()
proxy_t operator->() const;
proxy_t operator*() const;

template <template <class> class To>  // To is a Trait base of this Trait
auto proxy() const -> /* To-shaped handle's proxy_t */;
template <template <class> class To>
auto rproxy() const -> /* To-shaped handle's rproxy_t */;
template <template <class> class To>
auto c_proxy() const -> /* To-shaped handle's c_proxy_t */;
```

`To == Trait` 时是本句柄的 `proxy_t` / `rproxy()` / `c_proxy()`。否则 `as<To>().proxy()` / `rproxy()` / `c_proxy()`。`c_dyn` 上 `c_proxy` 与 `proxy` 是同一类型。

```cpp
template <template <class> class To>  // To is a Trait base
dyn_view<Trait, To> as() const;       // dyn
template <template <class> class To>
c_dyn_view<Trait, To> as() const;     // c_dyn
```

同一张 `vt`，不做 convert。可存储上行是转换构造 / `trait_cast<To>(h)`，不是 `as`。

```cpp
Ptr& get() noexcept;
Ptr const& get() const noexcept;

void* ptr() const noexcept;              // dyn
void const* ptr() const noexcept;        // c_dyn

void reset();
void destroy();   // dyn; not shared_ptr / weak_ptr. Always vt[-1] (~Src)

std::size_t size() const noexcept
  requires(/* Trait has dyn op_construct */);  // empty handle -> 0
std::size_t align() const noexcept
  requires(/* Trait has dyn op_construct */);  // sizeof / alignof Storage, vt tail

explicit operator bool() const noexcept;
bool operator==(dyn const& d, std::nullptr_t) noexcept;
bool operator==(dyn const& a, dyn const& b) noexcept;

template <template <class> class Other>
bool operator==(dyn<Trait, Ptr> const& a, dyn<Other, Ptr> const& b) noexcept;
```

`operator bool` / `== nullptr`：非空原始指针，或 `weak_ptr` 上 `!expired()`。跨 Trait 的 `==` 比 `ptr()`。不声明 `!=`（C++20 可改写）。没有 `<=>`。

有 dyn `op_construct` 时才有 `size()` / `align()`：Storage 的 `sizeof` / `alignof`（vt 方法槽之后的尾槽）。空句柄为 0。没有 `op_construct`：没有这些成员。

```cpp
std::pair<Ptr, dyn<Trait, void*>> release();  // 左值
```

不能是 `cache_ptr` / `weak_ptr`。移出 `Ptr`；`dyn<Trait, void*>` 保留同一张 `vt` 并观察。`*this` 为空。

```cpp
dyn<Trait, std::shared_ptr<void>> lock() const;  // weak_dyn only
bool expired() const noexcept;                   // weak_dyn only
```

同名方法、转换、构造、赋值不在 dyn 句柄上。与 iface 相同：`*h` / `h->`（[traits.md](traits.md)#proxy）。

`#if ASF_HAS_DYN_RTTI`:

```cpp
const std::type_info& typeid_of() const noexcept;  // empty handle -> typeid(void)
```

proxy 运算符见 [traits.md](traits.md)。dyn 无反向友元。`*dyn` 拷贝/移动是浅的。深操作：`construct(*d)` / `construct(d.rproxy())`。

## 声明 — `sub_dyn` / `c_sub_dyn`

叶/final 表上某一 Trait 的 section 切片。无二次隐式上行 / `trait_cast`。与 `ASF_HAS_DYN_MULTI_VT` 独立。proxy 只有同名方法，无目录运算符。`weak_ptr` 没有 `->` / `*`。

```cpp
sub_dyn() noexcept;
sub_dyn(std::nullptr_t) noexcept;
sub_dyn(sub_dyn const&) = default;
sub_dyn(sub_dyn&&) = default;
sub_dyn& operator=(sub_dyn const&) = default;
sub_dyn& operator=(sub_dyn&&) = default;

template <template <class> class Final>
explicit sub_dyn(dyn<Final, Ptr> const& handle) noexcept;
template <template <class> class Final>
explicit sub_dyn(dyn<Final, Ptr>&& handle) noexcept;

proxy_t operator->();
proxy_t operator*();

sub_dyn<Trait, std::shared_ptr<void>> lock() const;  // weak_sub_dyn

explicit c_sub_dyn(sub_dyn<Trait, Ptr> const& w) noexcept;
explicit c_sub_dyn(sub_dyn<Trait, Ptr>&& w) noexcept;
template <template <class> class Final>
explicit c_sub_dyn(dyn<Final, Ptr> const& handle) noexcept;
template <template <class> class Final>
explicit c_sub_dyn(dyn<Final, Ptr>&& handle) noexcept;
template <template <class> class Final>
explicit c_sub_dyn(c_dyn<Final, Ptr> const& handle) noexcept;
template <template <class> class Final>
explicit c_sub_dyn(c_dyn<Final, Ptr>&& handle) noexcept;

proxy_t operator->() const;
proxy_t operator*() const;
```

`get` / `ptr` / `operator bool` 与同 `Ptr` 的 `dyn` 一致。`reset` 在 `Ptr` 非代理析构，或 `pointer_trait::release` 能接这个句柄时存在。同名方法：`*h` / `h->`。自由函数：[cast.md](cast.md) 的 `to_sub_dyn` / `to_c_sub_dyn`。

`sub_dyn` 没有负前缀（`destroy`、`typeid_of`）。这些槽在整张表的原点，section 无法判断自己是否在原点：即使 Trait 无基类，也可能被并列继承。代理析构 `Ptr` 只有 `pointer_trait::release` 能接这个句柄时才允许。

## 指针 / mirror 绑定与 `final`

从 `T*` / sourced mirror（含 `to_dyn`）构造时，虚表由 **leaf** 提供。默认开 `ASF_HAS_DYN_INTERMEDIATE`（[features.md](features.md)）：

- final 叶（`Trait<void> final` 或 `Trait<As> final`）：注入该叶表。
- 非 final 中间 Trait：只 ADL 取已注入的表；自己不注入。

必须：对覆盖该中间节点的某个 final 叶显式实例化 `instantiate_dyn<Trait, As>`，或对该叶做过指针/mirror 绑定（或其它 ODR-use）。否则链接失败。

禁止：祖先已是 final 时，不能再对派生 Trait 做指针/mirror 绑定（`static_assert`）。

多个 final 注册同一基类：同 TU 先实例化者胜出；跨 TU 为内部 UB。

`iface` 不要求 `final`。

```cpp
template <template <class> class Trait, class As>
struct instantiate_dyn;
```

显式实例化：`template struct asf::instantiate_dyn<Trait, As>;`。

## 参见

- [pointers.md](pointers.md)
- [cast.md](cast.md)
- [iface.md](iface.md)
- [features.md](features.md)
- [multi_vt.md](multi_vt.md)
