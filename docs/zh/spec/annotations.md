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
| `imple` | `no_imple` | `no_imple` | 不走 `Trait<void>` 函数体。非 op 直接按名字找成员。op_* 不按这个名字找：`op_construct` 找构造函数（`auto&&` 是右值），`op_assign` 找 `operator=`，`op_convert` 找转换，`op_add` 等找对应运算符。对象或 sourced Self 均可 |
| `mirror` | `no_mirror` | `no_mirror` | 用户 `mirror` 没有 Src、绑 `iface` 时（无源 mirror 不能绑 `dyn`），布局里每个 Trait 的每个方法都要标 `mirror`。`mirror<T, Src>` 绑 `dyn` **不**要求整棵都标 `mirror`：mirror 段找 `T` 特化，非 mirror 段找 `Src` 特化。`op_construct` **不**继承 Trait 的 `mirror`；自己标 `[[=mirror]]` 则不合法。destroy 恒为 `vt[-1]`（`~Src`），不是 Trait 方法（见 [mirror.md](mirror.md)） |

## dyn 槽与 `static`（不是注解）

`static` 是 C++ 的**声明属性**（方法上的 `static` 关键字），**不是** `[[=asf_ann::…]]` 注解。这是刻意为之：模板上的方法级注解必须替换才能读取（GCC 会实例化函数体，见下节「编译器限制」），而声明属性反射直接可读、零实例化。所以 dyn 槽的退出用 `static`，不用注解。

规则：**非** ``static`` 且 ``self`` 首位、至多一个推导参数的方法是 dyn 槽；``static`` 方法仅用于 iface，没有「重新加入」的开关。``self`` 靠后或有第二个推导参数也按形状仅属 iface（那本来就进不了 dyn 槽）。

dyn 无法知道 `static` 方法的 self 期望哪个具体 `mirror<T>` 视图，而经虚表转一手也只是多此一举的 dyn 调用。需要从 dyn 拿 mirror 时，自己写一个普通的 dyn 方法（impl 指明具体 mirror 视图，例如以 `c_mirror<T>` 作 Self 或返回 `mirror<T>`）。

## 编译器限制（模板上的注解读取）

读函数模板上的注解需要替换它（GCC 会实例化函数体），所以模板上的方法级注解只在有具体 Self 的位置读取：`[[=imple]]` 与 `[[=mirror]]` 在调用 / iface 绑定时以 impl Self 读取，`[[=expl]]` 在 iface 载体生成时读取。`dyn` 的 proxy（无具体类型）上，带函数体的 `op_convert` 模板方法读不到 `[[=expl]]`（按隐式处理）。Trait 级注解总是可读；拿不准时优先标在 Trait 上。

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

见 [multi_vt.md](multi_vt.md)、[features.md](features.md)#asf_has_dyn_multi_vt、`tests/general/iface_truncate.cpp`。

## op_construct 要点

- 返回 `auto` 或具体类型（非 `void`）。
- `[[=asf_ann::imple]]`：键为内部 `imple_t`。不走 Trait 函数体，按该方法的参数在 Src 上找构造函数。参数只有 self 时，const& 是拷贝，&& 是移动。auto&& 按右值引用处理（`mirror<T&&, …>` 的写法待定）。
- 自定义默认：省略 `imple`，具体返回类型作为键（使用函数体）。
- 各 `op_construct` 重载与其它方法一样占普通 dyn 槽，只按参数（含 `self` 值类别）分发；没有单独的 `construct_move` 标记，也不固定在虚表尾部。

## 运算符

运算符方法名全表在 [traits.md](traits.md)#运算符。写成 Trait 上的普通方法。不要加 `friend`。

Trait 方法生成 proxy 的成员运算符，句柄在左侧：`(*h) + x`。**dyn 槽要求 `self` 是第一个参数，且最多一个模板推导参数**；`self` 不在首位、或还有第二个推导参数时动态派发编译错误。

反向（另一侧在左）只支持 **iface**：在 Trait 上再写一个 `self` 靠后的重载并标 `static`，按参数位置匹配，例如 `op_add(int x, auto const& self)` → `4 + (*h)`。dyn 无反向运算符。一元运算符只写在 Trait 上。

示例：`examples/*/03_operators.cpp`。生命周期（`destroy` / `construct`）：`examples/*/04_lifetime.cpp`。

## 相关

- [traits.md](traits.md)
- [iface.md](iface.md)
- [features.md](features.md)
