# Multiple vtables

Handle: `{ ptr, vt }`. An upcast does not change the object address. It only chooses which physical table `vt` points at. Method indices are compiled against the **current Trait tag**. The call is `vt[index]`. The tag and the held table must use the same slots.

`ASF_HAS_DYN_MULTI_VT` (default on): for an ancestor that is not a **section prefix** of the current table, the layout inserts **Redirect → cover**. An implicit upcast / `trait_cast` can then store any Trait base, even when that base does not share a leading slice with the leaf.

- **Section prefix**: the target's section list is a prefix of the current table. Same `vt`. On the leaf below: `Readable`, `Writable`, `Record`, `Authored`.
- **Cover**: the layout as if that Trait were final (its own DAG). Not a slice of the leaf. Key `(Mask, Final, Trait=cover, Storage, As)`. Distinct from a bare table of that Trait on another tree.
- **Redirect**: inserted after each non-prefix section, once per cover, not piled at the table tail.
- **`[[=truncate]]` / `[[=truncated]]`**: prune Redirect/cover on a cut path. See [annotations.md](annotations.md)#truncation-truncate--truncated.

`vt` points at `[0]`. `vt[-1]` is destroy (`~Storage`). With RTTI on, meta sits at `vt[-2]`. The method origin stays `[0]`.

Off (`-DASF_HAS_DYN_MULTI_VT=OFF`): only sections go into the flat table. No Redirect, no cover. `dyn → dyn` is section-prefix / same table only. `as<>` can still call any ancestor method on the same leaf.

## Two diamonds

Two stacked diamonds. Primary bases are the left path (`Writable`, then `Authored`). One method per Trait. Grid-merge walks the primary base first, then sections from the other base that are not already present, then self. [`examples/en/05_mi_cast.cpp`](../../../examples/en/05_mi_cast.cpp) is only the upper diamond.

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

| Trait | sections | vs Document leaf |
| --- | --- | --- |
| `Readable` | Readable | section prefix |
| `Writable` | Readable, Writable | section prefix |
| `Printable` | Readable, Printable | second is Printable; the leaf's second is Writable |
| `Record` | Readable, Writable, Printable, Record | section prefix |
| `Authored` | …, Record, Authored | section prefix |
| `Sealed` | …, Record, Sealed | after Record the leaf has Authored, not Sealed |
| `Document` | …, Authored, Sealed, Document | the leaf |

Non-prefix sections get Redirect after that section. `redirect_cover_for` picks the largest ancestor whose sections are a prefix-superset of that section; here that is the section itself: Printable cover, Sealed cover. Both covers are keyed under `Final=Document`.

Default (RTTI off). Low address at the top. `vt` at `[0]`. Destroy at `[-1]` is omitted in the drawing.

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


leaf  Trait=Document                      cover  Trait=Printable
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

The leaf's `to Printable` and the Sealed cover's `to Printable` are the same slot kind and point at the **same** physical table `(Final=Document, Trait=Printable)`. Sealed's cover does not copy Printable's slots; it Redirects. That is table reuse.

| Handle | vt | Call lands on |
| --- | --- | --- |
| `dyn<Document> doc` | leaf `[0]` | `doc->id()` leaf Document; `doc.as<Printable>()->print()` leaf Printable section |
| `dyn<Readable>` / `Writable` / `Record` / `Authored` | leaf `[0]` | prefix indices on the leaf |
| `dyn<Printable> p = doc` | Printable cover `[0]` (leaf `to Printable`) | `p->print()` cover `[1]` |
| `dyn<Sealed> s = doc` | Sealed cover `[0]` (leaf `to Sealed`) | `s->seal()` Sealed cover; `s` upcast to Printable reads **that cover's** `to Printable`, still the same Printable table |

`as<Printable>()` on the leaf stays on the leaf: the index is Printable's section inside Document's layout. After a stored `dyn<Printable>`, indices follow Printable's own layout, so `print` is `[1]`. Leaf `[1]` is `rename`, so that stored upcast switches `vt`. The same rule sends `dyn<Sealed>` through `to Sealed`.

Unused alternative: Redirect at a negative index.

## `unsafe_downcast`

`unsafe_downcast<To>(h)` changes only the Trait tag: same `ptr`, same `vt` pointer. Compile time requires `From` to be a Trait base of `To` (`To` more derived, equality included). After that, indices from **To's layout** are applied to **the vt still held**.

Against the diagram:

| Start | Held vt | Target | Result |
| --- | --- | --- | --- |
| `dyn<Readable> r = doc` | leaf | `Document` | slots match the leaf |
| same | leaf | `Record` / `Authored` | prefix of the leaf, including the `to Printable` slot |
| same | leaf | `Printable` | UB. Printable treats `[1]` as `print`; leaf `[1]` is `rename` |
| same | leaf | `Sealed` | UB. Sealed's `[5]` is `seal`; leaf `[5]` is `author` |
| `trait_cast<Printable>(doc)` | Printable cover | `Document` | UB. Document wants slots the small cover does not have |
| `trait_cast<Sealed>(doc)` | Sealed cover | `Document` | UB. Document's `Authored` / `to Sealed` / `id` sit past this cover |

Leaf retargeted to Printable: indices from Printable's layout read the leaf.

```
index   Printable layout     leaf memory
[0]     title                title
[1]     print                rename     <-- print() hits rename
```

Sealed cover retargeted to Document: indices from Document's layout read the Sealed cover.

```
index   Document layout         Sealed cover memory
[0]     title                   title
[1]     rename                  rename
[2]     print                   print
[3]     to Printable            to Printable
[4]     tag                     tag
[5]     author                  seal        <-- Authored hits seal
[6]     seal                    (off table)
[7]     to Sealed               (off table)
[8]     id                      (off table)
```

After `unsafe_downcast<Document>(s)`, the handle tag is Document so indices follow Document's layout, but `vt` still points at the Sealed cover (key `(Final=Document, Trait=Sealed)`). Final only says which leaf tree this table belongs to; Trait is this table's slot layout. Those are different, so `author` hits `seal` and the last three slots are off the table. Every physical table has `vt[-1]` destroy; a destructor that returns does not mean the method slots match.

Safe when the held vt is the table being named. Typical: a section-prefix upcast that is still on the leaf, then `unsafe_downcast` back to the leaf or to another section prefix of that leaf. There is no safe `unsafe_downcast` from a cover to a more-derived Trait.

```cpp
asf::dyn<Readable> r = doc;                      // leaf
auto back = asf::unsafe_downcast<Document>(r);   // same leaf, slots match

auto p = asf::trait_cast<Printable>(doc);        // Printable cover
auto s = asf::trait_cast<Sealed>(doc);          // Sealed cover
// asf::unsafe_downcast<Document>(p);             // UB
// asf::unsafe_downcast<Document>(s);             // UB
```

With RTTI, a downcast or a cross-branch cast uses `dyn_cast` (switches to the physical vt from meta). That test is experimental and over-accepts; see [features.md](features.md)#asf_has_dyn_rtti. Without MULTI_VT there is no cover; the usual case is a same-table section prefix, and going from a prefix tag back to the leaf is the same table.

`cache_dyn`: the target chain must not add copy/move `op_construct` that the source chain does not have (destroy is always `vt[-1]`). See [cast.md](cast.md).

## Vtable key

Tables are keyed by `(Mask, Final, Trait, Storage, As)`. A cover belongs to the final root (Redirect points at that root's cover). It is not type-linked to a bare intermediate table of the same Trait.

| Region | Condition | Contents |
| --- | --- | --- |
| RTTI prefix | RTTI macro on | meta |
| Method slots | always (per Trait) | each method (including `op_construct` overloads) and Redirect |

With RTTI on, the negative prefix of the leaf and of each cover under the same Final points at meta. `tables[]` lists those physical tables. `find(trait_id)` returns one of them, so `dyn_cast` switches `vt`. `unsafe_downcast` keeps the current `vt`.
