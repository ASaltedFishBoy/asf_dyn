# 基准：asf_dyn vs microsoft/proxy

复刻 [microsoft/proxy](https://github.com/microsoft/proxy) 的 operation
benchmark：100 类型系列 × 20 万对象，Small/Large 载荷，invoke 与 relocate
行（proxy / proxy_view / virtual / std::any / asf dyn 各形态 / iface /
direct）。

English: [README.md](README.md).

## 构建与运行

默认关闭，需显式打开。对照基线在配置时用 `FetchContent` 拉取，钉在
`main` @ `dc3d95c763ec04b0b2821addd643b024b07cd1c9`（2026-01-29）。仓库内不
vendoring。

```bash
cmake -S . -B build-bench -G Ninja -DASF_BENCH=ON
cmake --build build-bench --target asf_bench
./build-bench/bench/asf_bench
```

无直连 GitHub 时可注入 git 配置，例如
`GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=http.https://github.com.proxy
GIT_CONFIG_VALUE_0=http://127.0.0.1:7890 cmake -S . -B build-bench ...`。

## 结果

GCC 16.2 MinGW，-O2 `-static`，100 类型系列 × 20 万对象，搬迁 ×25 轮，逐格
30 轮中位数（ns/call，ns/move）。句柄：proxy / asf sbo 24 B（两种尺寸同壳，
Large 为壳内堆指针），cache_dyn 24 B / 56 B，unique_dyn / observer /
proxy_view 16 B。

| 行 | Small invoke | Small relocate | Large invoke | Large relocate |
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

各主行为的结构性开销说明（API 面，非计时数字）：
[docs/en/spec/features.md#cost-model](../docs/en/spec/features.md#cost-model)。
