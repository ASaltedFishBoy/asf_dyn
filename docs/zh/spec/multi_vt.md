# 多虚表

句柄是 `{ ptr, vt }`。对象地址在上行时保持不变；上行只选择 vt 指向哪一张物理表。方法下标在编译期按**当前 Trait 标签**的布局确定，调用取 `vt[下标]`。标签和手里的物理表必须是同一套槽。

`ASF_HAS_DYN_MULTI_VT`（默认开启）：祖先不是当前表的 **section-prefix** 时，layout 插入 **Redirect → cover**。隐式上行 / `trait_cast` 因此可存储任意 Trait 基，不必与叶表共享前缀切片。

- **section-prefix**：目标的 section 列表是当前表的前缀。同一张 `vt`。下图叶上的 `Readable`、`Writable`、`Record`、`Authored`。
- **Cover**：仿佛该 Trait 为 final 时的布局（自己的 DAG），不是叶表的一段切片。键 `(Mask, Final, Trait=cover, Storage, As)`。与另一棵树上该 Trait 的裸表是不同特化。
- **Redirect**：插在每个非 prefix section 之后；同一 cover 只插一次，不堆在表尾。
- **`[[=truncate]]` / `[[=truncated]]`**：剪掉被切断路径上的 Redirect/cover。见 [annotations.md](annotations.md)#截断truncate--truncated。

`vt` 指着 `[0]`。`vt[-1]` 是 destroy（`~Storage`）。RTTI 打开后 meta 在 `vt[-2]`，方法区原点仍是 `[0]`。

关闭（`-DASF_HAS_DYN_MULTI_VT=OFF`）：只有 sections 进 flat，无 Redirect/cover。`dyn → dyn` 只允许 section-prefix / 同表。`as<>` 仍可在同一张叶表上调用任意祖先方法。

## 两轮菱形

两个叠着的菱形。主基是左路（`Writable`，再 `Authored`）。每个 Trait 一个方法。grid-merge 先走主基，再补另一基里尚未出现的 section，最后是自己。[`examples/*/05_mi_cast.cpp`](../../../examples/zh/05_mi_cast.cpp) 只有上半个菱形。

```
              Readable
             /        \
        Writable      Printable
             \        /
              Record
             /        \
          Authored     Sealed
             \        /
              Document
```

```cpp
struct Writable<void>  : Readable<void> {};
struct Printable<void> : Readable<void> {};
struct Record<void>    : Writable<void>, Printable<void> {};
struct Authored<void>  : Record<void> {};
struct Sealed<void>    : Record<void> {};
struct Document<void>  : Authored<void>, Sealed<void> {};
```

| Trait | sections | 相对 Document 叶 |
| --- | --- | --- |
| `Readable` | Readable | section-prefix |
| `Writable` | Readable, Writable | section-prefix |
| `Printable` | Readable, Printable | 第 2 个是 Printable，叶的第 2 个是 Writable |
| `Record` | Readable, Writable, Printable, Record | section-prefix |
| `Authored` | …, Record, Authored | section-prefix |
| `Sealed` | …, Record, Sealed | Record 之后叶是 Authored，不是 Sealed |
| `Document` | …, Authored, Sealed, Document | 叶本身 |

非 prefix 的 section 后插 Redirect。`redirect_cover_for` 取 sections 是该节 prefix-超集的最大祖先；这里就是该节自身：Printable cover、Sealed cover。两张 cover 都挂在 `Final=Document` 下。

默认关闭 RTTI。低地址在上。`vt` 在 `[0]`。图中省略 `[-1]` destroy。

```
              Readable
             /        \
         Writable   Printable
             \        /
               Record
             /        \
        Authored     Sealed
             \        /
              Document


叶  Trait=Document                        cover  Trait=Printable
+---------------------------+             +---------------------+
| Readable     title        |             | Readable    title   | <--+
| Writable     rename       |             | Printable   print   |    |
| Printable    print        |             +---------------------+    |
| to Printable              | ---------------------------------------+
| Record       tag          |             cover  Trait=Sealed        |
| Authored     author       |             +---------------------+    |
| Sealed       seal         |             | Readable    title   |    |
| to Sealed                 | ----------> | Writable    rename  |    |
| Document     id           |             | Printable   print   |    |
+---------------------------+             | to Printable        | ---+
                                          | Record      tag     | 
                                          | Sealed      seal    | 
                                          +---------------------+ 
```

叶的 `to Printable` 与 Sealed cover 里的 `to Printable` 是同一槽种，且指向**同一张**物理表 `(Final=Document, Trait=Printable)`。Sealed cover 不复制 Printable 的槽，只 Redirect。这就是表复用。

| 句柄 | vt | 调用落到 |
| --- | --- | --- |
| `dyn<Document> doc` | 叶 `[0]` | `doc->id()` 叶 Document；`doc.as<Printable>()->print()` 叶 Printable 那一节 |
| `dyn<Readable>` / `Writable` / `Record` / `Authored` | 叶 `[0]` | 叶上的前缀下标 |
| `dyn<Printable> p = doc` | Printable cover `[0]`（叶 `to Printable`） | `p->print()` cover `[1]` |
| `dyn<Sealed> s = doc` | Sealed cover `[0]`（叶 `to Sealed`） | `s->seal()` Sealed cover；`s` 再上行到 Printable 读的是**这张 cover 里**的 `to Printable`，仍是同一张 Printable 表 |

`as<Printable>()` 留在叶上：下标按 Document 布局里 Printable 那一节。存成 `dyn<Printable>` 之后，下标按 Printable 自己的布局，`print` 在 `[1]`。叶 `[1]` 是 `rename`，所以这条存储上行换 vt。`dyn<Sealed>` 同理走 `to Sealed`。

备选布局（未使用）：Redirect 放在负索引。

## `unsafe_downcast`

`unsafe_downcast<To>(h)` 只改 Trait 标签：同一个 `ptr`、同一个 `vt` 指针。编译期只要求 `From` 是 `To` 的 Trait 基（`To` 更派生，含相等）。之后按 **To 的布局** 去索引**当时手里的那张 vt**。

对照上图：

| 起点 | 手里的 vt | 目标 | 结果 |
| --- | --- | --- | --- |
| `dyn<Readable> r = doc` | 叶 | `Document` | 槽与叶一致 |
| 同上 | 叶 | `Record` / `Authored` | 叶的前缀，含 `to Printable` 那一格 |
| 同上 | 叶 | `Printable` | UB。Printable 把 `[1]` 当作 `print`，叶 `[1]` 是 `rename` |
| 同上 | 叶 | `Sealed` | UB。Sealed 的 `[5]` 是 `seal`，叶 `[5]` 是 `author` |
| `trait_cast<Printable>(doc)` | Printable cover | `Document` | UB。小 cover 没有 Document 要的槽 |
| `trait_cast<Sealed>(doc)` | Sealed cover | `Document` | UB。Document 的 `Authored` / `to Sealed` / `id` 在这张 cover 之外 |

叶上把标签改成 Printable 时，下标按 Printable 布局去读叶：

```
index   Printable 布局     叶内存
[0]     title            title
[1]     print            rename     <-- print() 打到 rename
```

Sealed cover 上把标签改成 Document 时，下标按 Document 布局去读 Sealed cover：

```
index   Document 布局         Sealed cover 内存
[0]     title                   title
[1]     rename                  rename
[2]     print                   print
[3]     to Printable            to Printable
[4]     tag                     tag
[5]     author                  seal        <-- Authored 打到 seal
[6]     seal                    （表外）
[7]     to Sealed               （表外）
[8]     id                      （表外）
```

`unsafe_downcast<Document>(s)` 之后，句柄标签是 Document，下标按 Document 布局取；`vt` 仍指向 Sealed cover（键 `(Final=Document, Trait=Sealed)`）。Final 只说明这张表属于哪棵叶；Trait 才是这张表的槽布局。两者不是一回事，所以 `author` 打到 `seal`，后面三槽出表。`vt[-1]` destroy 每张物理表都有；析构能走完，不能据此认为方法槽对齐。

安全用法：vt 仍是要命名的那张物理表。典型是还停在叶上的 section-prefix 上行，再 `unsafe_downcast` 回叶，或回到该叶的某个 section-prefix。cover 上没有通向更派生 Trait 的安全 `unsafe_downcast`。

```cpp
asf::dyn<Readable> r = doc;                      // 叶
auto back = asf::unsafe_downcast<Document>(r);   // 同一张叶，槽对齐

auto p = asf::trait_cast<Printable>(doc);        // Printable cover
auto s = asf::trait_cast<Sealed>(doc);          // Sealed cover
// asf::unsafe_downcast<Document>(p);             // UB
// asf::unsafe_downcast<Document>(s);             // UB
```

有 RTTI 时，下行和交叉转换用 `dyn_cast`（换到 meta 里那张物理 vt）。判定仍是实验性的，会过度接受，见 [features.md](features.md)#asf_has_dyn_rtti。无 MULTI_VT 时没有 cover，常见只剩同表 section-prefix；从叶的前缀标签回到叶是同一张表。

`cache_dyn` 另有一条链约束：目标链不能多出源链没有的复制 / 移动 `op_construct`（destroy 恒为 `vt[-1]`）。见 [cast.md](cast.md)。

## 虚表键

虚表按 `(Mask, Final, Trait, Storage, As)` 区分。cover 表归属 final 根（Redirect 指向该根下的 cover），不与「裸中间 Trait 表」类型链接。

| 区段 | 条件 | 内容 |
| --- | --- | --- |
| RTTI 前缀 | RTTI 宏开启 | meta |
| 方法槽 | 始终（按 Trait） | 各方法（含 `op_construct` 重载）及 Redirect |

打开 RTTI 时，同一 Final 下叶和 cover 的负前缀指向 meta；`tables[]` 列出这些物理表。`find(trait_id)` 返回其中一张，`dyn_cast` 因此会换 vt。`unsafe_downcast` 留在当前 vt。
