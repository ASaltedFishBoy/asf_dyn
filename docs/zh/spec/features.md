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

当前 **0.1.0**。内联 ABI 命名空间为 `v0_1_0_b` + **二进制特性标**（MSB→LSB： `RTTI` / `INTERMEDIATE` / `MULTI_VT` / `SINGLE_INHERIT`）。默认 (`0,1,1,0`) 得 `v0_1_0_b0110`。源码仍写 `asf::dyn` 等；mangling 带上该段，使同一进程内不同 ASF 版本**或选项组合** 编译出的 `.dll`/`.so` 不会在 slots / 虚表 / erased thunk 的 COMDAT 上静默合并。破坏 ABI 时抬 `ASF_VERSION_*`（及 `iface_config.hpp` 中 `vMAJOR_MINOR_PATCH` 令牌）。只有有意强制某个标签时才覆盖 `ASF_ABI_NAMESPACE`。运行时：`asf::abi_namespace_name` / `asf::dyn_feature_bits`。

---

## `ASF_HAS_DYN_RTTI`**（实验性）**

| | |
| --- | --- |
| **默认** | `0`（CMake：`OFF`） |
| **开启** | 虚表带 **meta 头**；提供 `dyn_cast` |

**实验性**：`dyn_cast` **只保证创建节点及其上方的节点**。纯链 `1-2-3-4` 绑在 `3`：`3-1-2` 受保证；`3-1-4` 可能过也可能不过。实现按叶 Final 的布局 / cover 集合查 Trait，不是精确的「此对象动态类型是否为 To」判定。创建节点之下或 cover 上查到不是承诺。生产路径请优先隐式上行 / `trait_cast` / `as`；需要运行时下行时自证类型，或用 `unsafe_downcast` 并自行承担 UB。API 与判定规则仍可能收紧或改掉。

meta 结构：一个 `{ find, type }` 头（`find` 是函指，`type` 是指向 Storage 的 `const std::type_info&`），`tables[0..x)` 紧随该头（`this + 1`，不是指针字段）。`tables[]` 装 Final 键下 x 张**互不相同**的物理虚表（叶 + 各 Redirect cover），按物理表索引而非按目标——多个目标可共享同一项。`find` 按 Final 生成为 x 次对编译期 `trait_id` 地址的比较（`trait_id<T>::id` 是每个 Trait 一个 static inline `char`，以地址为身份），返回 **tables 下标**（未命中为 `(std::size_t)-1`）：不传 `self` 的纯 Final 级映射，其下所有键共享一个函数；调用方再索引自己实例的 `tables[k]`。所有被比地址都是局部可见的编译期常量，该 vec 运行时从不改写，比较链可能被编译器编成 switch。每个物理虚表键各有一份 meta（非共享父树）。

```
vt[-2] --> +-------------------------------+  vtable_meta_inst<X>
           | meta: find -------------------+--> rtti_find_index<Final>（每个
           |       type = typeid(Storage)  |    Final 一个函数，所有键共享）
           +-------------------------------+
           | tables[0] --------------------+--> 叶表
           | tables[1] --------------------+--> cover 表
           |  ...                          |
           | tables[X-1] ------------------+--> cover 表
           +-------------------------------+
            每 (Mask, Final, Storage, As) 键一份实例；tables() = meta + 1

rtti_find_index<Final>(to_id):                // 不传 self；纯 Final 级映射
  to_id == &trait_id<T0>::id ?  return k0      // k 编译期（只看 Final 布局）
  to_id == &trait_id<T1>::id ?  return k1
  ...                               常量地址比较链；可能被编成 switch
  否则                           ?  (size_t)-1
调用方：k = meta->find(to_id);  vt = (k != -1) ? meta->tables()[k] : nullptr
```

`dyn_cast`：能静态上行则走转换构造（`upcast_vt`）；否则查 meta；找不到时为空（见上：查到 ≠ 语义正确）。

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

从中间节点构造的 dyn，其 meta / `dyn_cast` 附着在 **实际拿到的那张 leaf 虚表** 上，不保证能下降到「哪一个」final 的相关树——赢家由同 TU STMP / 跨 TU 链接择一决定，不是中间 Trait 自身的逻辑 DAG。

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

**活边单继承**：dyn 生成（`dyn_object_slots` / `instantiate_dyn`）时，若某 section 的**活**直接 Trait 基类超过 1 个则 `static_assert`。`[[=truncate]]` / `[[=truncated]]` 边**允许**，不计入该限制。`iface` 在绑 dyn 前不受影响。

本开关不另开上行通道。与 MULTI_VT 互斥，故走关闭 MULTI_VT 的 section-prefix 规则；活边 SI 下可达基恰好都是 prefix，活链上的隐式上行 / `trait_cast` 仍然可用。

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ \
  -DASF_HAS_DYN_MULTI_VT=OFF \
  -DASF_HAS_DYN_SINGLE_INHERIT=ON
```

### 关闭（默认）

Trait 多重继承对 dyn 允许（非 prefix 上行仍受 `ASF_HAS_DYN_MULTI_VT` 约束）。

---

## 成本模型

暂未附带基准；以下为各主要行为的结构性成本。

| 行为 | 成本 | 说明 |
| --- | --- | --- |
| `iface` 调用方法 | 静态绑定、可内联（大体无成本） | 直接调 impl / 默认体；编译期类型完整，通常与手写直调一致 |
| `dyn` 调用方法 | 一次虚表间接跳转 | `vt[i]` 取槽 + 间接 call，与虚函数同级；擦除后基本不可内联 |
| `dyn` 指针绑定（`T*` / mirror 观察） | 零分配、零拷贝 | 只存指针与 vt 地址；虚表是编译期静态表 |
| `to_dyn` 拥有值 | 一次分配 + 构造 | `::operator new` / Alloc；`cache_dyn` 零分配（值搬进随行缓冲） |
| 上行视图（`as<To>`） | 纯类型级重包，同一 vt（无成本） | 绝不切换 vt 指针 |
| 上行转换（`trait_cast`） | 编译期索引，至多一次 vt 指针切换 | 无 typeid / 字符串查找；MULTI_VT 的 Redirect 表也是静态地址 |
| `dyn_cast`（RTTI 开） | `meta.find`：x 个编译期 `trait_id` 地址比较（可能编成 switch），命中后 `tables[k]` 直接索引 | 实验特性 |
| 析构 | `vt[-1]` 一次间接调用 | 删除器种再加一次 `Free`（默认即 `::operator delete`） |
| proxy（`h->` / `*h`） | 包指针的小对象 | 内联后零成本 |
| `mirror` 构造 / 字段访问 | 字段即引用 | 构造是指针打包；只有 `value_of` 逐字段拷贝 |
| 虚表静态体积 | 每（Mask, Final, Trait, Storage, As）键一份 | MULTI_VT 的 cover 表是主要来源；SINGLE_INHERIT 或关 MULTI_VT 削减 |

可被静态局部优化掉的一半：`iface` 上的全部、dyn 的 proxy 包装、上行索引——这些在编译期就近消除（大体无成本）。不可消除的一半：每次 `dyn` 调用与析构的一次间接跳转，以及拥有型分配。

---
## CMake 示例

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ \
  -DASF_HAS_DYN_RTTI=ON \
  -DASF_HAS_DYN_INTERMEDIATE=ON \
  -DASF_HAS_DYN_MULTI_VT=ON
```

部分测试目标会强制覆盖选项（例如 `dyn_rtti_tests` 始终打开 RTTI）。
