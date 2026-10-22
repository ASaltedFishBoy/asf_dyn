# Tests layout

Most suites are **dual-pass**: one shared source under
[`general/`](general/) compiles twice (see `asf_dual_test` in
`CMakeLists.txt`). The plain target binds objects / pointers / values
(`asf::to_dyn(&obj)`); the `mirror_<name>` target recompiles the same file
with `ASF_TEST_VIA_MIRROR=1`, which switches the observer binds in
[`test_bind.hpp`](test_bind.hpp) to the sourced-mirror path
(`asf::to_mirror` + `[[=asf_ann::mirror]]` on every Trait in the layout).
Pass-specific cases stay in the shared file under
`#if ASF_TEST_VIA_MIRROR` / `#if !ASF_TEST_VIA_MIRROR`, so both passes are
always built and run.

| Directory | Role |
| --- | --- |
| [`general/`](general/) | Dual-pass suite sources (general pass + `mirror_*` pass from the same file). |
| [`mirror/`](mirror/) | Mirror-only suites ([`design_probe`](mirror/design_probe.cpp), [`mirror_base_spec`](mirror/mirror_base_spec.cpp)) and mirror compile-fail cases. |

Helpers: [`test_bind.hpp`](test_bind.hpp) (dual-pass bind layer),
[`mirror_bind.hpp`](mirror_bind.hpp) (legacy mirror-suite entry points used
by the mirror-only suites).