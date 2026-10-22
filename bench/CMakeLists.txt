# Comparison benchmark against microsoft/proxy. Off by default
# (-DASF_BENCH=ON): the baseline library is fetched from GitHub at configure
# time, pinned to a commit (nothing vendored into the repo).
include(FetchContent)
FetchContent_Declare(
  microsoft-proxy
  GIT_REPOSITORY https://github.com/microsoft/proxy
  # main @ 2026-01-29 (Add notification banner (#381))
  GIT_TAG dc3d95c763ec04b0b2821addd643b024b07cd1c9
  GIT_SHALLOW TRUE)
FetchContent_Populate(microsoft-proxy)

add_executable(asf_bench bench.cpp)
target_include_directories(asf_bench PRIVATE
  ${CMAKE_SOURCE_DIR}/include ${microsoft-proxy_SOURCE_DIR}/include)
asf_target_defs(asf_bench)
target_compile_options(asf_bench PRIVATE -O2 -DNDEBUG)
set_target_properties(asf_bench PROPERTIES EXCLUDE_FROM_ALL TRUE)
