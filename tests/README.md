# Tests layout

| Directory | Role |
| --- | --- |
| [`general/`](general/) | Same scenarios, **object / pointer / value** bind only (`to_dyn(&obj)`, …). No user-mirror → handle. |
| [`mirror/`](mirror/) | Same scenarios, **user-mirror** bind (`to_dyn_m` / `to_mirror` + `[[=mirror]]` on every Trait in the layout). |

Helpers for the mirror suite: [`mirror_bind.hpp`](mirror_bind.hpp).

`design_probe` is mirror-only (mirror API surface).
