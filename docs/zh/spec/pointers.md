# 指针与所有权（dyn Ptr）

头文件：`dyn.hpp`（经 `interface.hpp`）。

`dyn` / `sub_dyn` 通过完整指针类型 `Ptr` 描述擦除存储 (`void*`, `std::unique_ptr<void, Deleter>`, …). 句柄别名见 [dyn.md](dyn.md)。构造辅助见 [cast.md](cast.md)。

每张物理虚表都有 `vt[-1]` 的 destroy：恒为 `~Src` / `~Storage`。 `~Src` 平凡时，所有这类槽都是同一个空函数 `vt_destroy_nop`。

库不探测 `get_deleter()`。释放只看 `pointer_trait<P>::kind`。

## 标签与 `pointer_trait`

```cpp
struct pointer_vt_destroy_tag {
  explicit pointer_vt_destroy_tag() = default;
};
struct pointer_deleter_tag {
  explicit pointer_deleter_tag() = default;
};
inline constexpr pointer_vt_destroy_tag pointer_vt_destroy{};
inline constexpr pointer_deleter_tag pointer_deleter{};

enum class pointer_kind { observer, vt_destroy, deleter };

template <class P>
struct pointer_trait {};
```

类型未特化，或 `kind` 缺失 / 认不出，就是观察器。

```cpp
template <class P>
struct pointer_trait {
  using kind = /* pointer_vt_destroy_tag | pointer_deleter_tag */;
  static void release(/* Handle& */);  // optional; library passes *this
};
```

- 观察器（默认）：释放时不跑 `destroy`。
- `pointer_vt_destroy_tag`: 释放时先跑 `destroy`，再让指针丢掉存储。可选 `release(Handle&)` 替换默认行为。 `sub_dyn` 上只有这调用成立时才允许vt_destroy 类 `Ptr`。
- `pointer_deleter_tag`: 释放时不跑 `destroy`。从vt_destroy 类源构造这个 `P` 时是 `P(raw, d)`（`d` 先 destroy 再释放）。

未特化的自定义删除器仍是观察器。可编译的完整文件是 `examples/zh/07_pointer_trait.cpp`。

库内特化写在标准指针类型本身上，不限于擦成 `void` 之后：

```cpp
template <class T, class U>
struct pointer_trait<std::unique_ptr<T, std::default_delete<U>>> {
  using kind = pointer_vt_destroy_tag;
};
template <class T, class Alloc>
struct pointer_trait<std::unique_ptr<T, alloc_delete<Alloc>>> {
  using kind = pointer_vt_destroy_tag;
};
template <class T, class Free>
struct pointer_trait<std::unique_ptr<T, dyn_adopt_delete<Free>>> {
  using kind = pointer_deleter_tag;
};
template <class T>
struct pointer_trait<std::shared_ptr<T>> {
  using kind = pointer_deleter_tag;
};
template <std::size_t Size, std::size_t Align, class Ops>
struct pointer_trait<cache_ptr<Size, Align, Ops>> {
  using kind = /* Ops::destroy_type:
                  op_unset            -> absent (observer)
                  otherwise           -> pointer_vt_destroy_tag */;
};
```

`std::weak_ptr<T>` 不特化（观察器）。`void*` 不是类类型，也不特化。

传删除器是浅交接，不是深克隆：对象仍在源已有的分配里。同类型：移动。vt_destroy 类源 `release()` 出裸指针，目的是 `P(raw, d)`。`d` 跑 `vt[-1]` 再释放：目的删除器类型能从 destroy 函指构造则用该类型；否则默认源删除器不转发，`d` 是 `dyn_default_delete`（`dyn_adopt_delete` 的默认 Free 形态，`::operator delete`）。其它源删除器类型传给 `dyn_adopt_delete<Free>` 作为 `d`。`vt[-1]` 不是目的删除器。

## `alloc_delete`

```cpp
template <class Alloc = std::allocator<std::byte>>
struct alloc_delete {
  using allocator_type = Alloc;
  Alloc alloc{};
  std::size_t size = 0;

  alloc_delete() noexcept;
  explicit alloc_delete(std::size_t n) noexcept;
  alloc_delete(Alloc a, std::size_t n) noexcept;
  void operator()(void* p) const noexcept;
};
```

句柄先跑 `vt[-1]`。这个删除器只按尺寸块经 `allocator_traits` 释放。`Alloc` 是 `std::allocator` 时不用（那条路仍是 `unique_dyn` / `::operator delete`）。

## `dyn_adopt_delete` / `dyn_default_delete`

```cpp
template <class Free = /* detail::new_delete */>
struct dyn_adopt_delete {
  void (*destroy)(void*) = nullptr;

  dyn_adopt_delete() noexcept;
  dyn_adopt_delete(void (*fn)(void*), Free = {}) noexcept;
  void operator()(void* p) const noexcept;
};

using dyn_default_delete = dyn_adopt_delete<>;
```

`operator()` 跑 `destroy`（来自 `vt[-1]`）再跑 `Free`。`Free` 是私有基类（EBO）：空 `Free` 不占字节，默认形态只有一个指针。默认 `Free` 是 `detail::new_delete`：`std::default_delete<void>` 的 void 安全替身（`::operator delete`；std 类型本身不能对 `void*` 调用）。`dyn_default_delete` 是这个默认 Free 形态的别名：源存储删除器是默认且目的没有能从该函指构造的 `deleter_type` 时，它就是目的 `d`。`unique_ptr<T, dyn_adopt_delete<Free>>` 是删除器种；把它收纳进 dyn 时会将 `destroy`（重）接线到 `vt[-1]`，即使源删除器的 `destroy` 为空，`~T` 也总在 `Free` 之前运行。

## `cache_ops` / `cache_ptr`

```cpp
struct op_trivial {};       // 字节拷贝，或载荷无需析构
struct op_trivial_relocate {};   // move: memcpy (源 vt 由 dyn 置空)
struct op_unsupported {};   // 该特殊成员被删除
struct op_unset {};         // 尚未选定
struct op_from_trait {      // 擦除 cache_dyn 形态：深虚表操作充当该浅槽
  explicit op_from_trait() = default;
};

template <class T>
struct op_copy_of {
  void operator()(void* dst, void const* src) const;
};
template <class T>
struct op_move_of {
  void operator()(void* dst, void* src) const;
};
template <class T>
struct op_destroy_of {
  void operator()(void* p) const;
};
template <class T>
struct op_assign_copy_of {
  void operator()(void* dst, void const* src) const;
};
template <class T>
struct op_assign_move_of {
  void operator()(void* dst, void* src) const;
};

template <class Destroy = op_unset, class Copy = op_unset,
          class Move = op_unset, class CopyAssign = op_unset,
          class MoveAssign = op_unset>
struct cache_ops {
  using destroy_type = Destroy;
  using copy_type = Copy;
  using move_type = Move;
  using copy_assign_type = CopyAssign;
  using move_assign_type = MoveAssign;
};
```

`Destroy` 不能是 `op_unsupported`（编译错误）；复制和移动可以用它剔掉。每个槽默认是 `op_unset`。非标签槽是按上表可调用的类型。库提供上面的 `op_*_of<T>` 目录，用户自写同签名的调用类型也可以。`op_from_trait` 是擦除后的 `cache_dyn` 形态：深虚表操作（`op_construct` / `vt[-1]`）接到这个浅槽上，Ptr 既不存类型也不存函指。

不属于标签的槽值是由类型本身携带的可调用算子（原地调用，无每对象状态）。为 `cache_dyn` 擦除时，能用的算子同样随类型携带（仍不存函指）。

虚表构造 / 析构槽只来自 Trait。`cache_ops` 只决定 Ptr 层浅路径用 Trait 槽（`op_from_trait`）、字节路径（`op_trivial` / `op_trivial_relocate`）、typed 可调用，还是留成不支持——需要不支持槽时是编译错误。

### 弱化检查（copy / move / destroy 对 `T`）

请求的 op 只能**弱化** `T` 能做的，或用 `op_unsupported` 剔掉（仅复制/移动）。Destroy 不能剔掉。两个赋值槽**不参与**此检查。

copy / move / destroy 各自从 `T` 取自然档、从 `Ops` 槽取请求档。通过条件：`request == 0`（已剔）或 `request <= natural`。档 `4`（既不是已知标签也不能按 `void*` 签名调用）永远失败。

| 档 | 自然（`T`） | 请求（`Ops` 槽） |
| --- | --- | --- |
| 3 | 特化可拷/特化可移/特化可析 | `op_trivial` |
| 2 | 可拷/可移/可析（非特化） | `op_from_trait`；符合该槽 `void*` 签名的 typed 可调用；`op_trivial_relocate`（仅 move——与 typed move 同档，不比 trivial 更强） |
| 0 | 不可构/不可析 | `op_unsupported` |

`Ops` 的生命周期槽全是 `op_unset` 时，库填 `cache_ops_of<T>`：特化 → `op_trivial`；否则可构/可析 → `op_*_of<T>`（destroy 从不 unsupported）；否则 copy/move → `op_unsupported`。赋值另行默认：特化可赋 → `op_trivial`；可赋 → `op_assign_*_of<T>`；否则 `op_unset`。

### 进入 `cache_dyn`：先校验，再擦除

值（或 typed `cache_ptr`）放进 `cache_dyn` 时先对 `T` 做弱化检查，再把各槽擦成类型擦除形态：

**Copy / move**

1. 已是 `op_unsupported` → 保持剔掉。
2. move 且显式 `op_trivial_relocate` → 留在 Ptr 层（不改写为 `op_from_trait`；VT 构造槽仍来自 Trait）。
3. 否则若 Trait 继承链能做对应深构造（`op_construct` 拷/移） → `op_from_trait`。
4. 否则 `op_trivial` 保留；可 wrap 的 typed 随类型携带；否则 → `op_unsupported`。

**Destroy** — 链提供拆解（`vt[-1]`）时擦向 `op_from_trait`；否则 `op_trivial` 或 typed destroy 随类型携带。从不剔掉。

**Assign** — 与 Trait 无关：`op_unset` / `op_from_trait` → `op_unset`；`op_trivial` / `op_trivial_relocate` 保留；其余随类型携带。

Trait 链没有生命周期槽时，只要 `cache_ptr` 自己仍能拷/移/析，仍可进入。下行只在 `cache_ops` 不自持生命周期时检查 Trait 链。

### 搬迁层次

cache 载荷搬迁按层取第一个可用者：`op_trivial` / `op_trivial_relocate`（字节；relocate 仅 memcpy，源 vt 由 dyn 置空）、对已就位目的地的 typed 赋值算子，最后才是该槽自身的构造+析构。只有赋值有替身：赋值算子不活时，向已占用 cache 赋值就退化为析构再构造。

自定义算子还可以按参与的公开句柄重载（任意 dyn 句柄形态，推导）。调用点只要存在完全成型的句柄，就先尝试句柄形式；`void*` 形式是每个算子必须提供的基线。构造算子只收到源句柄（目的地还是裸存储，不是句柄）；赋值算子两个都收到。析构在最终析构函数之外的各处都会收到活句柄（最终析构函数那里句柄已不存在）。只能以句柄调用的算子在存储层不活（层活性是存储层属性）；对赋值而言就是退化为析构+构造替身。

`cache_ptr` 到 `cache_ptr` 的转换只允许 `Size` 与 `Align` 不变或变大；缩小是编译错误。载荷必然放得下（它本来就活在更小或相等的源缓冲里），因此转移不需要运行时尺寸检查。typed -> erased 转换（每个槽由自己的算子转移）与擦除 `cache_dyn` -> `cache_dyn` 转换（链提供该构造时，经虚表构造进更大的缓冲）遵守同样的增长规则。

```cpp
template <std::size_t Size, std::size_t Align = alignof(std::max_align_t),
          class Ops = cache_ops<>>
class cache_ptr;

template <class T, class Ops = cache_ops<>>
using cache_ptr_for = cache_ptr<sizeof(T), alignof(T), Ops>;
```

缓冲跟着对象走。`cache_ptr` 不能靠偷指针搬家；dyn 移动经 `op_construct` 搬迁。

```cpp
using element_type = void;
using ops_type = Ops;
using cache_ptr_tag = void;
static constexpr std::size_t size = Size;
static constexpr std::size_t align = Align;
static constexpr std::size_t cache_capacity = Size;
static constexpr std::size_t cache_alignment = Align;

cache_ptr() noexcept;
cache_ptr(std::nullptr_t) noexcept;
cache_ptr(cache_ptr const&);            // 按 Ops
cache_ptr(cache_ptr&&);
cache_ptr& operator=(cache_ptr const&);
cache_ptr& operator=(cache_ptr&&);
~cache_ptr() = default;

// 仅增长转换（S1 <= Size && A1 <= Align，否则编译错误）：
template <std::size_t S1, std::size_t A1>
cache_ptr(cache_ptr<S1, A1, Ops> const&);
template <std::size_t S1, std::size_t A1>
cache_ptr(cache_ptr<S1, A1, Ops>&&);
template <std::size_t S1, std::size_t A1, class FromOps>
  // Ops 是 FromOps 的擦除形式
cache_ptr(cache_ptr<S1, A1, FromOps> const&);
template <std::size_t S1, std::size_t A1, class FromOps>
cache_ptr(cache_ptr<S1, A1, FromOps>&&);

void* get() const noexcept;
```

`cache_ptr` 不记录占用状态：`get()` 恒返回缓冲，ptr 层没有空/`nullptr` 状态或布尔查询。在 dyn 里，空与否由虚表判断（`vt_ == nullptr`）。没有占用位要存，`cache_dyn` 恰好是 `Size + sizeof(vt)` 字节（16 B 缓冲 -> 24 B 句柄）。


## 库提供的 Ptr

```cpp
// void*                                                 observer; default Ptr
// std::unique_ptr<void, alloc_delete<Alloc>>            vt_destroy
// std::unique_ptr<void, dyn_default_delete>            deleter
// std::unique_ptr<void, dyn_adopt_delete<Free>>         deleter
template <template <class> class Trait, class Deleter = std::default_delete<void>>
using unique_dyn = dyn<Trait, std::unique_ptr<void, /* default unique_dyn deleter */>>;
                                                          // vt_destroy; ::operator delete
template <template <class> class Trait>
using shared_dyn = dyn<Trait, std::shared_ptr<void>>;     // deleter
template <template <class> class Trait>
using weak_dyn = dyn<Trait, std::weak_ptr<void>>;         // observer
template <template <class> class Trait, std::size_t Size,
          std::size_t Align = alignof(std::max_align_t), class Ops = cache_ops<>>
using cache_dyn = dyn<Trait, cache_ptr<Size, Align, Ops>>;
```

`unique_dyn<Trait>` 与 `unique_dyn<Trait, std::default_delete<void>>` 是同一类型。

## `proxy_wrap`

持有可解引用的 `Ptr`，用来派发进 `cache_dyn`。缓冲的 Size/Align/Ops 来自 `cache_ptr` Spec（不是 `proxy_wrap` 自己）。它不是 dyn 的存储 `Ptr`。

```cpp
template <class Ptr>
class proxy_wrap;
```

```cpp
to_dyn<Trait, cache_ptr<sizeof(P), alignof(P)>>(proxy_wrap{std::make_unique<T>(...)});
to_dyn<Trait>(proxy_wrap{std::make_unique<T>(...)});  // Spec = sizeof/alignof(P)
```

拟合与 `Ops` 擦除都针对 **Ptr**（规则同 cache）。缓冲存 `Ptr`；`get()` 返回缓冲地址。虚表：`Storage=Ptr`，`As=pointee`；深构造 / 析构 / `storage_size` 走 `Ptr`；方法走 `*Ptr`。浅层 copy/move 被盖掉（`op_unsupported` 或调不到）时，对应的深 `op_construct` 槽可以空着。

## 生成时的额外实参

`Alloc` 不写进 `to_dyn` / `construct` 的模板参数。它是函数实参，转进 Ptr 构造。声明见 [cast.md](cast.md)。

```cpp
unique_dyn<Trait> to_dyn<Trait, std::unique_ptr>(U&& object);
dyn<Trait, std::unique_ptr<void, alloc_delete<Alloc>>>
to_dyn<Trait, std::unique_ptr>(Alloc alloc, U&& object);

unique_dyn<Trait> construct<std::unique_ptr>(/* *dyn */, Extra&&... extra);
dyn<Trait, std::unique_ptr<void, alloc_delete<Alloc>>>
construct<std::unique_ptr>(Alloc alloc, /* *dyn */, Extra&&... extra);

shared_dyn<Trait> construct<std::shared_ptr>(/* *dyn */, Extra&&... extra);
shared_dyn<Trait> construct<std::shared_ptr>(Alloc alloc, /* *dyn */, Extra&&... extra);
```

`construct(*d)` 无模板实参时返回 `dyn<Trait, SrcP>`（与源同一 `Ptr`）。`Alloc` 是可选的第一个实参，按 `*dyn` / `rproxy` 的位置判定。代理之后的实参派到 `op_construct`。`std::allocator` 仍是默认 `unique_dyn`（`::operator delete`），不是 `alloc_delete`。

## 创建

源须能变成目标 `Ptr`（或为 sourced mirror / 已有句柄）。

- 智能指针与自定义路径从不直接调 `::operator new/delete`。用 `allocator_traits`。默认 `unique_ptr` 的存储释放（`unique_dyn`）是 `::operator delete`。
- 深克隆出的 `shared_ptr`：用给定分配器（默认 `std::allocator`）。分配里额外存一个析构指针。
- `to_dyn<Trait>(T*|unique_ptr|shared_ptr|weak_ptr)`：按 `pointer_kind` 原则采纳，不看类型名：`T*` 观察；`default_delete` / `alloc_delete` 的 `unique_ptr` 是 vt_destroy；`shared_ptr` / `weak_ptr`（deleter 随控制块）直接采纳；`unique_ptr<T, dyn_default_delete>` 收为删除器种；`unique_ptr<T, D>` 的其它可 `T*` 调用自定义 `D` 经 `T` 型包装收为**删除器种**（不挂 `vt[-1]`，`D` 自备 `~T` 与释放）。
- 值 → `unique_ptr`： `to_dyn<Trait, std::unique_ptr>(value)` — 转发构造到最终存储（无中间 dyn）。可选分配器放第一个： `to_dyn<Trait, std::unique_ptr>(alloc, value)`。
- 已擦除句柄深构造：`construct<Ptr>(*dyn, extra...)` / `construct<Ptr>(d.rproxy(), extra...)`。可选分配器放第一个：`construct<std::unique_ptr>(alloc, *dyn, extra...)`。

view 与 `Src` 不一致的 sourced mirror 仍用普通 `dyn`。 destroy 恒为 `vt[-1]`（`~Src`）。见 [mirror.md](mirror.md)。

## 句柄转换

Trait 上行（含 From 即 Trait）与 `SrcP` 到本 `Ptr` 一起检查。同一 `Ptr`：拷贝/移动（或 cache 走 vt）。否则用该值类别的 `SrcP` 构造本 `Ptr`。显隐跟指针类型走（`weak_ptr` 从 `shared_ptr` 隐式）。不是 `construct`（深克隆）。目的 `Ptr` 是删除器类、源是vt_destroy 类时，按上面交接。 `Ptr` 相同时与 `trait_cast<Trait>(o)` 同义。

```cpp
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP const&, Ptr>)
dyn(dyn<From, SrcP> const& src);
template <template <class> class From, class SrcP>
explicit(!std::is_convertible_v<SrcP &&, Ptr>)
dyn(dyn<From, SrcP>&& src);
```

```cpp
std::pair<Ptr, dyn<Trait, void*>> release();
```

左值。不是 `cache_ptr` / `weak_ptr`。移出 `Ptr`；`dyn<Trait, void*>` 保留同一张 `vt` 并观察。源句柄为空。

## 参见

- [dyn.md](dyn.md)
- [cast.md](cast.md) (`construct`)
- [ref.md](ref.md)（密封引用，与 dyn Ptr 不同层）

