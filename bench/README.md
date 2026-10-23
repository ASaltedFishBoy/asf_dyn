# Benchmark: asf_dyn vs microsoft/proxy

Mirrors [microsoft/proxy](https://github.com/microsoft/proxy)'s operation
benchmark: 100 type series x 200k objects, Small/Large payloads, invoke and
relocate rows (proxy / proxy_view / virtual / std::any / asf dyn flavors /
iface / direct).

Chinese: [README.zh.md](README.zh.md).

## Build and run

Off by default; enable explicitly. The baseline is fetched at configure time
(`FetchContent`), pinned to `main` @ `dc3d95c763ec04b0b2821addd643b024b07cd1c9`
(2026-01-29). Nothing is vendored into the repo.

```bash
cmake -S . -B build-bench -G Ninja -DASF_BENCH=ON
cmake --build build-bench --target asf_bench
./build-bench/bench/asf_bench
```

Hosts without direct GitHub access can inject git config for the fetch, e.g.
`GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=http.https://github.com.proxy
GIT_CONFIG_VALUE_0=http://127.0.0.1:7890 cmake -S . -B build-bench ...`.

## Results

GCC 16.2 MinGW, -O2 `-static`, 100 type series x 200k objects, relocate x25
reps, per-cell medians of 30 runs (ns/call, ns/move). Handles: proxy / asf
sbo 24 B (both sizes; Large is a heap pointer in the same shell), cache_dyn
24 B / 56 B, unique_dyn / observer / proxy_view 16 B.

| Row | Small invoke | Small relocate | Large invoke | Large relocate |
| --- | ---: | ---: | ---: | ---: |
| pro::proxy (owning) | 1.73 | 1.14 | 6.84 | 1.15 |
| asf unique_dyn (owning) | 1.86 | 1.19 | 6.18 | 1.19 |
| asf cache_dyn (in-handle) | 1.35 | 1.13 | 2.48 | 3.08 |
| asf sbo (cache/uptr-in-cache) | 1.39 | 1.12 | 6.89 | 1.11 |
| asf::dyn (observer) | 1.23 | 0.68 | 2.80 | 0.67 |
| pro::proxy_view | 1.28 | 0.69 | 2.80 | 0.68 |
| virtual (unique_ptr) | 2.46 | 0.56 | 4.10 | 0.56 |
| virtual (raw ptr) | 2.79 | — | 4.57 | — |
| std::any | — | 2.02 | — | 2.21 |
| direct / asf::iface | 0.00 | — | 0.00 | — |

Structural cost notes for each primary behavior (API surface, not timings):
[docs/en/spec/features.md](../docs/en/spec/features.md#cost-model).
