# Benchmark: asf_dyn vs microsoft/proxy

Mirrors [microsoft/proxy](https://github.com/microsoft/proxy)'s operation
benchmark: 100 type series x 200k objects, Small/Large payloads, invoke and
relocate rows (proxy / proxy_view / virtual / std::any / asf dyn flavors /
iface / direct).

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

## Conclusions

Recorded in the docs: `docs/en/spec/features.md` ("Comparison with
microsoft/proxy") / `docs/zh/spec/features.md`（与 microsoft/proxy 对照）.
