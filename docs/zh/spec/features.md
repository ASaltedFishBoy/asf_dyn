# 特性开关（`ASF_HAS_DYN_*`）

编译期宏，定义于 [`include/detail/iface_config.hpp`](../../../include/detail/iface_config.hpp)。 CMake 选项见仓库根 [`CMakeLists.txt`](../../../CMakeLists.txt) 与 [README](../../../README.zh.md)。

运行时可见常量：

```cpp
asf::version_major / version_minor / version_patch / version
asf::dyn_rtti_v
asf::dyn_intermediate_v
asf::dyn_multi_vt_v
asf::dyn_single_inherit_v
asf::dyn_feature_bits
asf::abi_namespace_name
```

## ABI 版本（`ASF_VERSION_*` / `ASF_ABI_NAMESPACE`）

当前 **0.2.0**。内联 ABI 命名空间为 `v0_2_0_b` + **二进制特性标**（MSB→LSB： `RTTI` / `INTERMEDIATE` / `MULTI_VT` / `SINGLE_INHERIT`）+ `_tNN` 后缀（`ASF_DYN_VT_TAG_HI_BITS` 值）。默认 (`0,1,1,0`) 加默认 16 位 tag 位宽得 `v0_2_0_b0110_t16`。源码仍写 `asf::dyn` 等；mangling 带上该段，使同一进程内不同 ASF 版本**或选项组合** 编译出的 `.dll`/`.so` 不会在 slots / 虚表 / erased thunk 的 COMDAT 上静默合并。破坏 ABI 时抬 `ASF_VERSION_*`（及 `iface_config.hpp` 中 `vMAJOR_MINOR_PATCH` 令牌）。只有有意强制某个标签时才覆盖 `ASF_ABI_NAMESPACE`。运行时：`asf::abi_namespace_name` / `asf::dyn_feature_bits`。

---

## `ASF_HAS_DYN_RTTI`

| | |
| --- | --- |
| **默认** | `0`（CMake：`OFF`） |
| **开启** | 虚表带 **meta 头**；提供 `dyn_cast` |

**语义保证**：句柄锚定在创建 Final 可达 Trait 树的某个节点：叶绑定锚在 Final 叶，中间节点绑定锚在构造 Trait 自身的节点；section-prefix 上行保持节点；Redirect 上行重新锚定在 cover 节点。锚点**构造即固定**——`dyn_cast` 命中只换虚表、不动 tag，cast 结果与历史无关。运行时判定扫描锚点节点的**存活祖先闭包**——与静态上行同一 reachable 集——只在候选层数等于目标静态 DAG 层数时比对身份。命中是**精确**的：对象动态类型确实实现了 `To`，句柄随之换到 `To` 自己的虚表。miss 是确定的：`To` 比锚点更深（在构造 Trait 之下）或不在其祖先中。闭包属于锚点：叶锚点可达全部存活祖先——菱形两支都在——而中间锚点的同辈分支与其下覆盖叶都是 miss。等价说法：绑定把句柄**构造为**该构造 Trait——擦除句柄的动态类型就是锚点，`dyn_cast` 在该构造类型上复现 `dynamic_cast`（祖先命中，之下与同辈 miss）。树宽于 tag 容量是**编译期错误**，`dyn_cast` 没有 UB 路径。静态类型已知时优先隐式上行 / `trait_cast` / `as`；运行时情形由 `dyn_cast` 精确回答。

meta 结构：一个 `{ type, node_count, walk, node_typeid }` 头（`type` 是指向 Storage 的 `const std::type_info&`），`tables[0..x)` 紧随该头（`this + 1`）。x 个节点是 Final 的 dyn 上行可达 Trait（`reachable_t<Final>`，自身在前）——每个逻辑 Trait 一个节点，与后代 cover section-prefix 共享表时 `tables[]` 会重复同一物理虚表。cast 树是**代码而非数据**：`walk` 是 per-Final 分派（每个 Final 一个函数，跨 Storage/As/Mask 键 COMDAT 共享），按节点 tag 选定锚点后扫描其 constexpr 展开的祖先闭包——level 与被比对的身份都是常量，只有 `to_id` / `to_level` 是运行期入参；`node_typeid` 是 per-Final 的节点 → Trait `typeid` 分派。不再有 `ids[]` / `links[]` 数组。句柄 vt 在指针 tag 位段携带**节点 tag**：构造锚点节点的下标（绑定时为构造 Trait 的节点——叶绑定即节点 0——，Redirect 上行后为 cover 节点）；产出 vt 的入口（构造 / redirect thunk / walk 命中）都返回预打 tag 的指针——walk 命中沿用输入 tag——槽位访问前摘 tag。tag 喂给 `walk` / `node_typeid` 分派并索引本 meta 块的 `tables[]`。tag 位段位于指针顶部 `ASF_DYN_VT_TAG_HI_BITS` 位（64 位默认 16 即 65536 个节点，假设 48 位规范形虚拟地址——没有任何编译器设施上报 VA 位宽，LA57 类机器须设为 7；`0`（32 位默认）回落对齐低位 `countr_zero(alignof(vt_fn))` = 3 位 = 8 个节点）；**最大节点下标超出 2^位宽 在 meta 实例化处直接报编译错误**，没有运行时逃逸。每个物理虚表键各有一份 meta（键间不共享）。

```
vt[-2] --> +-------------------------------+  vtable_meta_inst<X>
           | meta: type = typeid(Storage)  |
           |       node_count = X          |
           |       walk -------------+-----+--> rtti_walk_dispatch<Final>
           |       node_typeid ------+     +--> rtti_node_typeid_dispatch<Final>
           +-------------------------+
           | tables[0] --------------+-----+--> 叶表
           | tables[1] --------------+-----+--> cover 表（可能重复）
           |  ...                    |
           | tables[X-1] ------------+-----+--> cover 表
           +-------------------------+
            每 (Mask, Final, Storage, As) 键一份实例；tables() = meta + 1
            walk / node_typeid：per-Final 代码，跨键 COMDAT 共享

walk_cast(vt, to_id, to_level):                 // to_level = trait_level_v<To>
  i = meta->walk(node_of(vt), to_id, to_level)   // tag 选锚点；其祖先闭包是
      // constexpr 展开：level 与比对身份是常量，to_id / to_level 是运行期
  i < 0 -> miss；否则 tag_vt(meta->tables()[i], node_of(vt))   // 命中只换表，锚点 tag 不变
```

`dyn_cast`：能静态上行则走转换构造（`upcast_vt`）；否则从句柄锚点节点走 meta 祖先闭包扫描；miss 时为空（见上：查到 ≠ 语义正确）。

与 **MULTI_VT** 独立但互补：全路径隐式上行 / `trait_cast` **不**需要 meta；非 section-prefix 的 `dyn_cast` 目标仍依赖 Redirect（cover 虚表）。

RTTI 前缀占位：meta 在 `vt[-2]`。`op_construct` 与其它方法一样在方法槽里，按参数分发。

---

## `ASF_HAS_DYN_INTERMEDIATE`

| | |
| --- | --- |
| **默认** | `1`（CMake：`ON`） |
| **实现** | `include/detail/dyn_intermediate.hpp` |

### 开启（默认）

指针 / mirror 的 `to_dyn` 允许绑到**非 `final`** 的中间 Trait。

完整规则：[dyn.md](dyn.md)#指针--mirror-绑定与-final重要、[traits.md](traits.md)。摘要：

路径：

- **只有 dyn final** 走定义路径：把该叶的虚表注入给中间节点可 ADL 取到的入口。
- **非 final** 只 ADL 取已注入的表；**自己不注入**。

**必须**：程序中至少 ODR-use / 实例化一个覆盖该中间节点的 **final 叶**（`Trait<void> final` 或 `Trait<T> final`，例如 `template struct instantiate_dyn<Trait, T>` 或对该叶做过指针/mirror 绑定）。否则中间节点无可用虚表入口，**链接失败**。

**禁止**：祖先已是 final 时，不能再对派生 Trait 做指针/mirror 绑定（`Parent<void> final` 使一切 `Parent<As>` 为 final，或 `Parent<As> final`）→ `static_assert`。

### 多个 final 注册同一基类

- **同一 TU**：STMP 跳过第二次注入以免重复定义。**先实例化的 final 胜出**。
- **跨 TU**：每个 TU 可注入一次。链接器择一 COMDAT/ODR 符号（**非 weak**）。结果未指定：**内部 UB**。

见 `tests/general/multi_final_ub.cpp` 与 `tests/general/multi_cu/`。

### 与 RTTI

从中间节点构造的 dyn 仍跑在**实际拿到的那张 leaf 虚表**上（赢家由同 TU STMP / 跨 TU 链接择一决定），但其 meta 锚点是构造 Trait 自身的节点：`dyn_cast` 只看到该 Trait 的存活祖先，向覆盖叶方向的下降是确定的 miss。

### 关闭

`-DASF_HAS_DYN_INTERMEDIATE=OFF`（或宏 `0`）：

- 只有 **final** 可做指针/mirror 绑定。
- 不能从中间节点创建 dyn。
- 不再有 UB（UB 换来的是覆盖语义：中间节点绑的 `dyn<Base>` 按注入的 final 叶派发；不需要覆盖就关掉本开关）。基类只能从 final 叶上行达到。注入机制（`include/detail/dyn_intermediate.hpp`）依赖 GCC 特有的模板 friend 行为，换编译器的反射实现时需重写。

---

## `ASF_HAS_DYN_MULTI_VT`

| | |
| --- | --- |
| **默认** | `1`（CMake：`ON`） |
| **互斥** | 与 `ASF_HAS_DYN_SINGLE_INHERIT` 互斥（CMake `FATAL_ERROR`） |

### 开启（默认）

**多虚表**：祖先不是 section-prefix 时，layout 插入 **Redirect → cover**。隐式上行 / `trait_cast` 可存储任意 Trait 基。section-prefix 上行保持同一张 `vt`。**`[[=truncate]]` / `[[=truncated]]`** 剪掉被切断路径上的 Redirect/cover（[annotations.md](annotations.md)#截断truncate--truncated）。

布局、两轮菱形、表复用、Redirect、cover 与 `unsafe_downcast`：[multi_vt.md](multi_vt.md)。

### 关闭

`-DASF_HAS_DYN_MULTI_VT=OFF`（或宏 `0`）：只有 sections 进 flat（无 Redirect/cover）。`dyn → dyn` 只允许 **section-prefix / 同表**。`as<>` 仍可在同一张叶表上调用任意祖先方法。

### 与 RTTI

与 MULTI_VT 独立。全路径隐式上行 / `trait_cast` **不**需要 meta。`dyn_cast` 仍要 RTTI；非 prefix 目标是本开关的 cover。见 [multi_vt.md](multi_vt.md)。

### `unsafe_downcast`

只改 Trait 标签（同 ptr、同 vt）。Redirect 上行后句柄可能握 **cover**。把 cover 标签改成更派生 Trait 是 UB。对照表与错槽图：[multi_vt.md](multi_vt.md)#unsafe_downcast。

---

## `ASF_HAS_DYN_SINGLE_INHERIT`

| | |
| --- | --- |
| **默认** | `0`（CMake：`OFF`） |
| **互斥** | 与 `ASF_HAS_DYN_MULTI_VT` 互斥（CMake `FATAL_ERROR`；两宏同时为 1 时头文件 `#error`） |

### 开启

**活边单继承**：dyn 生成（`dyn_object_slots` / `instantiate_dyn`）时，若某 section 的**活**直接 Trait 基类超过 1 个则 `static_assert`。`[[=truncate]]` / `[[=truncated]]` 边**允许**，不计入该限制。被另一直接基支配的直接基（兄弟基的祖先，视同未继承，见 [annotations.md](annotations.md)）不计入。`iface` 在绑 dyn 前不受影响。

本开关不另开上行通道。与 MULTI_VT 互斥，故走关闭 MULTI_VT 的 section-prefix 规则；活边 SI 下可达基恰好都是 prefix，活链上的隐式上行 / `trait_cast` 仍然可用。

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ \
  -DASF_HAS_DYN_MULTI_VT=OFF \
  -DASF_HAS_DYN_SINGLE_INHERIT=ON
```

与 microsoft/proxy 的计时对照见 [bench/README.zh.md](../../bench/README.zh.md)。结构性成本说明见 [features.md 成本模型](../en/spec/features.md#cost-model)。

## CMake 示例

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ \
  -DASF_HAS_DYN_RTTI=ON \
  -DASF_HAS_DYN_INTERMEDIATE=ON \
  -DASF_HAS_DYN_MULTI_VT=ON
```

部分测试目标会强制覆盖选项（例如 `dyn_rtti_tests` 始终打开 RTTI）。
