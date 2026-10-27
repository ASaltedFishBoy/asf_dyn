# cast 与构造辅助

头文件：`cast.hpp`。

调用形式左结合： `to_dyn<I>(&obj)->method()` 不需额外括号。

`as<To>()` 只有成员。 dyn: `dyn_view<Layout, To>` / `c_dyn_view<Layout, To>`（同一张 `vt`）。iface：把 Self 重包成继承链上的 Trait（可上可下）。见 [dyn.md](dyn.md)、[iface.md](iface.md)。

## `trait_cast` / `c_trait_cast`

```cpp
template <template <class> class Trait>
inline constexpr trait_cast_t<Trait> trait_cast;

template <template <class> class Trait>
inline constexpr c_trait_cast_t<Trait> c_trait_cast;
```

```cpp
template <template <class> class From, class Arg>
iface<Trait, Arg> trait_cast(iface<From, Arg>);

template <template <class> class From, class Ptr>
dyn<Trait, Ptr> trait_cast(dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
dyn<Trait, Ptr> trait_cast(dyn<From, Ptr>&&);

template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> trait_cast(c_dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> trait_cast(c_dyn<From, Ptr>&&);
```

iface：Trait 可互相为基；目标 `Self` 须能从源 `Self` 构造。与 `h.trait_cast<Trait>()` / 转换构造同义。指针语义：一个重载，不按句柄 const / 值类别分流。

dyn 族：仅上行。`Ptr` 从源推导。左值拷贝，右值移动。

```cpp
template <template <class> class From, class Arg>
c_iface<Trait, Arg> c_trait_cast(iface<From, Arg>);

template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> c_trait_cast(dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> c_trait_cast(dyn<From, Ptr>&&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> c_trait_cast(c_dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> c_trait_cast(c_dyn<From, Ptr>&&);
```

对象与用户 mirror 用 `to_iface` / `iface`，不走 `trait_cast`。

## `to_dyn` / `to_c_dyn`

```cpp
template <template <class> class Trait, class Spec = void>
inline constexpr to_dyn_t<Trait, Spec> to_dyn;

template <template <class> class Trait>
inline constexpr to_c_dyn_t<Trait> to_c_dyn;
```

不做 dyn->dyn 上行（用 `trait_cast`）。不接受裸对象（传 `T*` 或 sourced mirror）。

```cpp
template <class P>
/* 见下 */ to_dyn(P&& p);
```

`P` 是可绑定指针；结果随指针族：`T*` 得 `dyn<Trait>`（`void*`）；`unique_ptr` 得 `dyn<Trait, std::unique_ptr<void, D'>>`；`shared_ptr` 得 `dyn<Trait, std::shared_ptr<void>>`；`weak_ptr` 得 `dyn<Trait, std::weak_ptr<void>>`。保留 deleter。`vt` 由 Trait + pointee 决定。

接受的 `unique_ptr` deleter：`std::default_delete`、`alloc_delete`、`dyn_default_delete`，以及任何可以 `T*` 调用的自定义 `D`（收为**删除器种**：不挂 `vt[-1]`——`D` 应自备完整析构（`~T` + 释放））。见 [pointers.md](pointers.md)。

```cpp
template <class T, class Src>
dyn<Trait> to_dyn(mirror<T, Src> const& m);
```

```cpp
template <template <class> class From, class Arg>
dyn<Trait> to_dyn(iface<From, Arg> handle);
```

当 `Arg&` 能构造 `dyn<Trait>`。`iface<Trait, Person>` 不行。sourced mirror 能时，该 iface 也能。

```cpp
template <class U>
cache_dyn<Trait, sizeof(U), alignof(U), /* Trait-erased ops */>
to_dyn<Trait, cache_ptr>(U&& object);

template <class U>
unique_dyn<Trait> to_dyn<Trait, std::unique_ptr>(U&& object);
template <class Alloc, class U>
dyn<Trait, std::unique_ptr<void, alloc_delete<Alloc>>>
to_dyn<Trait, std::unique_ptr>(Alloc alloc, U&& object);
```

值（或 sourced-mirror 的 Src）转发构造到最终存储。无中间 dyn。Cache Spec 拟合（`sizeof` / `alignof` 对 Size/Align）不够则编译错误。`Alloc` 是可选的第一个实参。`std::allocator` 仍是默认 `unique_dyn`（`::operator delete`），不是 `alloc_delete`。

```cpp
template <class W>
cache_dyn<Trait, Size, Align, /* 按 Ptr 擦除的 ops */>
to_dyn<Trait, cache_ptr<Size, Align, Ops>>(W&& w);

template <class W>
cache_dyn<Trait, sizeof(Ptr), alignof(Ptr), /* ... */>
to_dyn<Trait>(W&& w);
```

带 **`using asf_adopt_proxy = void;`**（或同名成员，**只检存在性**）且符合 [pointers.md](pointers.md)#cache_deref adopt 协议的类型（含 **`cache_deref<Ptr>`**）持有可解引用的 `Ptr`。交给 **`to_dyn`**，用 `cache_ptr` Spec（或不写 Spec，用 `cache_ptr_for_storage<Ptr>`）。Ptr 放进 cache 缓冲；Spec 拟合不够则编译错误。虚表键是 `Ptr`，`As` 为 pointee。未声明 **`asf_adopt_proxy`** 的类型走普通 value/ptr 重载。

```cpp
template <template <class...> class Ptr, template <class> class From, class SrcP>
dyn<Trait, Ptr<void>> to_dyn(dyn<From, SrcP>&& handle);
```

`Ptr<void>` 必须是独立删除器种。浅交接：对象仍在源分配里；目标是 `P(raw, d)`（`d` 先 destroy 再释放）。不是深克隆。例子：`examples/zh/07_pointer_trait.cpp`。见 [pointers.md](pointers.md)。

`to_c_dyn<Trait>(…)` 接受规则相同，结果是 `c_dyn`。

指针/mirror 绑非 final 中间 Trait： [dyn.md](dyn.md)#指针--mirror-绑定与-final.

```cpp
template <template <class> class Trait, class As>
struct instantiate_dyn;
```

## `to_sub_dyn` / `to_c_sub_dyn`

```cpp
template <template <class> class Trait>
inline constexpr to_sub_dyn_t<Trait> to_sub_dyn;

template <template <class> class Trait>
inline constexpr to_c_sub_dyn_t<Trait> to_c_sub_dyn;
```

```cpp
template <template <class> class Final, class Ptr>
sub_dyn<Trait, Ptr> to_sub_dyn(dyn<Final, Ptr> const&);
template <template <class> class Final, class Ptr>
sub_dyn<Trait, Ptr> to_sub_dyn(dyn<Final, Ptr>&&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_sub_dyn(c_dyn<Final, Ptr> const&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_sub_dyn(c_dyn<Final, Ptr>&&);

template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(dyn<Final, Ptr> const&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(dyn<Final, Ptr>&&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(c_dyn<Final, Ptr> const&);
template <template <class> class Final, class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(c_dyn<Final, Ptr>&&);
template <class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(sub_dyn<Trait, Ptr> const&);
template <class Ptr>
c_sub_dyn<Trait, Ptr> to_c_sub_dyn(sub_dyn<Trait, Ptr>&&);
```

`Trait` 必须是 `Final` 的一个 section。与 `sub_dyn<Trait, Ptr>(d)` / `c_sub_dyn<Trait, Ptr>(d)` 同义。

## `get_proxy` / `get_rproxy` / `get_forward_proxy`

```cpp
template <template <class> class To>
inline constexpr get_proxy_t<To> get_proxy;

template <template <class> class To>
inline constexpr get_rproxy_t<To> get_rproxy;

template <template <class> class To>
inline constexpr get_forward_proxy_t<To> get_forward_proxy;
```

`To` 是句柄 Trait 的基 Trait。

```cpp
template <template <class> class From, class Ptr>
auto get_proxy(dyn<From, Ptr> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Ptr>
auto get_proxy(c_dyn<From, Ptr> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Arg>
auto get_proxy(iface<From, Arg> const& h) -> decltype(h.template proxy<To>());

template <template <class> class From, class Ptr>
auto get_rproxy(dyn<From, Ptr> const& h) -> decltype(h.template rproxy<To>());
template <template <class> class From, class Ptr>
auto get_rproxy(c_dyn<From, Ptr> const& h) -> decltype(h.template rproxy<To>());
template <template <class> class From, class Arg>
auto get_rproxy(iface<From, Arg> const& h) -> decltype(h.template rproxy<To>());

template <template <class> class From, class Ptr>
auto get_forward_proxy(dyn<From, Ptr> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Ptr>
auto get_forward_proxy(dyn<From, Ptr>&& h) -> decltype(std::move(h).template rproxy<To>());
template <template <class> class From, class Ptr>
auto get_forward_proxy(c_dyn<From, Ptr> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Ptr>
auto get_forward_proxy(c_dyn<From, Ptr>&& h) -> decltype(std::move(h).template rproxy<To>());
template <template <class> class From, class Arg>
auto get_forward_proxy(iface<From, Arg> const& h) -> decltype(h.template proxy<To>());
template <template <class> class From, class Arg>
auto get_forward_proxy(iface<From, Arg>&& h) -> decltype(std::move(h).template rproxy<To>());
```

`get_proxy` / `get_rproxy` 取 `const&`。`get_forward_proxy` 左值为 `proxy<To>()`，右值为 `rproxy<To>()`。`c_dyn` / `dyn_view` 同此。本 Trait 用句柄自己的 proxy；基 Trait 用该视口的 proxy。

## `construct`

从已擦除对象深构造。Trait 须有 dyn `op_construct`。拆除恒为 `vt[-1]`。`op_construct` 按参数分发，不限于拷贝 / 移动。代理后面的额外函数实参派到对应重载。`Alloc` 是可选的第一个实参：第一个是 proxy 就没有分配器；第二个是 proxy 则第一个是分配器。`OutPtr` 只随这次深构造换 `Ptr`，不能单独换。源须存活；`construct` 不做空句柄检查（`!h` 由调用方负责）。

从 sourced mirror 的 dyn 深构造要求 `op_construct` 返回 `mirror`（重绑到原来的源）。返回对象会让新 mirror 别名到随调用消亡的临时量，因此该槽发成桩，执行构造即陷入。

```cpp
template <template <class> class Trait, class SrcP, class... Extra>
dyn<Trait, SrcP> construct(/* *dyn or rproxy */, Extra&&... extra);
```

与源同一 `Ptr`。`construct(d.rproxy())` 在源非 const 时选右值 `op_construct`。`construct(*cd)` 恒选左值。

```cpp
template <template <class...> class OutPtr, template <class> class Trait, class SrcP, class... Extra>
dyn<Trait, /* erased OutPtr<void, …> */> construct(/* *dyn or rproxy */, Extra&&... extra);

template <template <class...> class OutPtr, class Alloc, template <class> class Trait, class... Extra>
auto construct(Alloc alloc, /* *dyn or rproxy */, Extra&&... extra);
```

`OutPtr` 是 `std::unique_ptr` 且没有 `alloc`：`unique_dyn<Trait>`。

`OutPtr` 是 `std::unique_ptr` 且 `Alloc alloc` 是第一个实参： `dyn<Trait, std::unique_ptr<void, alloc_delete<Alloc>>>`。 `std::allocator` 仍是 `unique_dyn`。

其他 `OutPtr` 保持原类型；`alloc` 只用于分配。

`OutPtr` 是 `cache_ptr`： `cache_dyn<Trait, …>`， `cache_ops` 按 Trait 推导。擦除源的存储放不下则空 `vt`（size/align 来自 vt）。

自定义 `OutPtr`（如 `fake_uptr`）：`dyn<Trait, OutPtr<void>>`。

从值直接拥有时优先 `to_dyn<Trait, std::unique_ptr>(value)` （可选 `alloc` 放第一个）。

## `unsafe_downcast` / `dyn_cast`

```cpp
template <template <class> class Trait>
inline constexpr unsafe_downcast_t<Trait> unsafe_downcast;

template <template <class> class From, class Ptr>
dyn<Trait, Ptr> unsafe_downcast(dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
dyn<Trait, Ptr> unsafe_downcast(dyn<From, Ptr>&&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> unsafe_downcast(c_dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> unsafe_downcast(c_dyn<From, Ptr>&&);
```

仅自由函数。同 ptr、同 `vt`。误用即 UB。 Redirect 上行后句柄可能握 cover `vt`；再命名更派生 Trait 即 UB。[multi_vt.md](multi_vt.md)。`cache_dyn` 的目标链不能多出源链没有的复制/移动 `op_construct`。

`#if ASF_HAS_DYN_RTTI`（实验性；只保证创建节点及其上方，或在 `ASF_HAS_DYN_INTERMEDIATE=OFF` 下精确，[features.md](features.md)）：

```cpp
template <template <class> class Trait>
inline constexpr dyn_cast_t<Trait> dyn_cast;

template <template <class> class From, class Ptr>
/* dyn<Trait, Ptr> or empty */ dyn_cast(dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
/* dyn<Trait, Ptr> or empty */ dyn_cast(dyn<From, Ptr>&&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> dyn_cast(c_dyn<From, Ptr> const&);
template <template <class> class From, class Ptr>
c_dyn<Trait, Ptr> dyn_cast(c_dyn<From, Ptr>&&);

template <template <class> class Trait, class Ptr>
const std::type_info& dyn_typeid(dyn<Trait, Ptr> const& h);
```

能静态上行则静态上行，否则查 meta。仅 id 缺失时为空。

## 参见

- [dyn.md](dyn.md)
- [iface.md](iface.md)
- [features.md](features.md)
- [multi_vt.md](multi_vt.md)
