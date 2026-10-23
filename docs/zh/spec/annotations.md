# 注解

## 注解命名空间

```cpp
namespace asf::ann {
  /* expl/no_expl, imple/no_imple, mirror/no_mirror,
     truncate, truncated */
}
namespace asf_ann = asf::ann;
```

用法：C++26 注解语法 `[[=asf_ann::name]]`。方法对可挂在 **方法** 或 **`Trait<void>`** 上。挂在 `Trait<void>` 上时，该类声明的每个方法继承该注解。方法上的注解覆盖 Trait 上的。同一声明上同时写是、否两种形式是错的。

| 是 | 否 | 默认 | 含义 |
| --- | --- | --- | --- |
| `expl` | `no_expl` | `no_expl` | 转换显式（`expl`）或隐式（`no_expl`，类似 `operator T()`） |
| `imple` | `no_imple` | `no_imple` | 在 `Trait<T>` 之后、`Trait<void>` 默认之前，试从绑定对象 `T` 取实现。详见下文。 |
| `mirror` | `no_mirror` | `no_mirror` | 选择该方法的**呈现 Self**（方法标记，否则看**声明它的** `Trait<void>`）。用户 `mirror` 没有 Src 时只能绑 `iface`（无 `dyn`）。mirror → `iface`（有源或无源）：**绑定不设门禁**，每个方法都可调；`[[=mirror]]` → 传 mirror，否则传对象（`Src`）或物化出的 View。有源 → `dyn` 可混用：mirror 段用 `Trait<T>`，非 mirror 用 `Trait<Src>`。`op_construct` **不**跟 Trait 的 `mirror` 走（`self` 是对象 / `Src`）；自己标 `[[=mirror]]` 则不合法。destroy 恒为 `vt[-1]`（`~Src`）（见 [mirror.md](mirror.md)） |


## `imple`

对具体绑定类型 `T`，调用解析顺序：

1. 若有匹配方法，用 `Trait<T>` 特化。
2. 若该方法（或声明它的 `Trait<void>`）标了 `[[=imple]]`（且非 `[[=no_imple]]`）：从 `T` 取实现。
3. `Trait<void>` 上的默认函数体。

第 2 步是**结构匹配**，不是 C++ 重载解析，也不是 `std::is_invocable`。未命中 → 第 3 步。

### 普通方法（名不是 `op_*`）

在 `T` 上找一个同名可调用（成员或 static），且：

- self 以外每个参数与 Trait 方法类型相同（含 cv-ref）；
- self 与 Trait 方法的 cv-ref **种类**一致。

Trait 的 self 是 `mirror` 时，用 **View** 类型的修饰去对 Trait self（忽略 mirror 包装）。命中的是对象成员时还要：`const` 一致；左值 Trait self 可对无修饰或 `&` 成员；右值 Trait self 需 `&&`。

### `op_*` 方法

不按 `op_*` 这个标识符查。映射到语言设施：构造函数（`auto&&` self 表示右值）、`operator=`、转换、或目录里对应的 C++ 运算符。


## dyn 槽与 `static`（不是注解）

`static` 是 C++ 的**声明属性**（方法上的 `static` 关键字），**不是** `[[=asf_ann::…]]` 注解。这是刻意为之：模板上的方法级注解必须替换才能读取（GCC 会实例化函数体，见下节「编译器限制」），而声明属性反射直接可读、零实例化。所以 dyn 槽的退出用 `static`，不用注解。

规则：**非** ``static`` 且 ``self`` 首位、至多一个推导参数的方法是 dyn 槽；``static`` 方法仅用于 iface，没有「重新加入」的开关。``self`` 靠后或有第二个推导参数也按形状仅属 iface（那本来就进不了 dyn 槽）。返回类型须写在声明上（不允许未定 `auto` / `decltype(auto)` 返回）。

dyn 无法知道 `static` 方法的 self 期望哪个具体 `mirror<T>` 视图，而经虚表转一手也只是多此一举的 dyn 调用。需要从 dyn 拿 mirror 时，自己写一个普通的 dyn 方法（impl 指明具体 mirror 视图，例如以 `c_mirror<T>` 作 Self 或返回 `mirror<T>`）。

## 编译器限制（模板上的注解读取）

读函数模板上的注解需要替换它（GCC 会实例化函数体），所以模板上的方法级注解只在有具体 Self 的位置读取：`[[=imple]]` 在调用 / 绑定时以 impl Self 读取；`[[=mirror]]` 在**调用**时读（选择呈现 Self）、在绑定时用于普通对象的 mirror-Self 选择 / dyn 段混用；`[[=expl]]` 在 iface 载体生成时读取。`dyn` 的 proxy（无具体类型）上，带函数体的 `op_convert` 模板方法读不到 `[[=expl]]`（按隐式处理）。Trait 级注解总是可读；拿不准时优先标在 Trait 上。

## 截断（`truncate` / `truncated`）

布局专用（**不是**方法注解；同一词根的主动 / 被动形式）。**不影响 iface**（iface 仍按完整 Trait DAG 上下行）。只收紧 **dyn 可存储上行**，并剪掉 **MULTI_VT 的 Redirect→cover**（少旁支虚表）。同表 `dyn::as<>` 视口仍可用。

| 注解 | 挂点 | 含义 |
| --- | --- | --- |
| `truncate` | `Trait<void>` 或 **基类说明符** | **主动截断**。在自身上：`dyn` 不能再向上转换。在多继承边上：不能沿该分支走到**仅该分支带来的**基类（含该直接基），故不为该路径生成额外 cover 表。祖先已标 `truncate` 时，后代经该边也不能越过它继续上行 |
| `truncated` | 仅 `Trait<void>` | **被动截断**。别的类型继承该接口时，这条继承边自带 `truncate` 含义：`dyn` 不能上行转换到**这个** Interface |

```cpp
struct [[=asf_ann::truncate]] Mid<void> : Base<void> { /* ... */ };
struct Leaf<void> : Mid<void>, Other<void> {};           // Leaf→Mid OK；Leaf→Base 否
struct Edge<void> : A<void>, [[=asf_ann::truncate]] B<void> {};  // Edge→B 否

struct [[=asf_ann::truncated]] B<void> { /* ... */ };
struct D<void> : A<void>, B<void> {};  // 等同于边上写 truncate；D→B 否
```

### 冗余直接基（自动截断）

直接基已是另一直接基的祖先时——如 `T final : A, B` 且 `B : A`——直接边 `T→A` 与活路径 `T→B→…→A` 重复。库发出**编译期警告**（`[[deprecated]]` 标记；GCC 按声明去重，每个 TU 一条），并把该边**视为截断**：直接边不是活边——不计入 `ASF_HAS_DYN_SINGLE_INHERIT` 的基类数，也不产生自己的上行路径。该基仍经占支配地位的活路径可达（且可上行），sections、cover 与分发均不变。`truncate` / `truncated` 边不能支配；唯一活路径须经过这类边的基不被支配。链式（`A⊂B⊂C`）、菱形（`A⊂B` 与 `A⊂D`）与混合情形同理；见 `tests/general/iface_dominant_base.cpp`。

见 [multi_vt.md](multi_vt.md)、[features.md](features.md)#asf_has_dyn_multi_vt、`tests/general/iface_truncate.cpp`。

## 相关

- [traits.md](traits.md) — 特殊名（`op_construct` 等）、运算符目录、proxy 表面
- [iface.md](iface.md)
- [features.md](features.md)
- 示例：`examples/*/03_operators.cpp`、`examples/*/04_lifetime.cpp`
