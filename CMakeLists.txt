cmake_minimum_required(VERSION 3.28)
project(asf_dyn VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ASF_HAS_DYN_RTTI
  "dyn vtable RTTI / dyn_cast (meta node tag + per-Final walk dispatch; see docs)"
  OFF)
option(ASF_HAS_DYN_INTERMEDIATE
  "non-final Trait may bind dyn (needs covering final leaf inject; final ancestor forbids derived). See docs/*/spec/dyn.md. OFF => final-only."
  ON)
option(ASF_HAS_DYN_MULTI_VT
       "Redirect/cover multi-vtable + full-path dyn upcast (default ON). OFF => sections-only layout; dyn->dyn section-prefix / same vt only. Mutually exclusive with ASF_HAS_DYN_SINGLE_INHERIT."
       ON)
option(ASF_HAS_DYN_SINGLE_INHERIT
       "Require SI among live dyn bases at dyn generation (default OFF); truncate/truncated edges allowed. Mutually exclusive with ASF_HAS_DYN_MULTI_VT."
       OFF)
option(ASF_MODULE
  "Build tests/examples against the C++26 named module 'asf' (GCC 16.2; see cmake/AsfModules.cmake). OFF keeps the textual-include build (include/asf.hpp header mode)."
  ON)
if(ASF_HAS_DYN_SINGLE_INHERIT AND ASF_HAS_DYN_MULTI_VT)
  message(FATAL_ERROR
    "ASF_HAS_DYN_SINGLE_INHERIT and ASF_HAS_DYN_MULTI_VT are mutually exclusive; "
    "set -DASF_HAS_DYN_MULTI_VT=OFF when enabling ASF_HAS_DYN_SINGLE_INHERIT.")
endif()
add_compile_options(-freflection -Wall -Wextra -Wpedantic)
# MULTI_VT / SINGLE_INHERIT: always pass explicit 0/1.
# INTERMEDIATE is per-target (asf_target_defs): _off copies pass 0 only,
# so the command line never has both =1 and =0.
# RTTI: per-target below (dyn_rtti_tests always on).
if(ASF_HAS_DYN_MULTI_VT)
  add_compile_definitions(ASF_HAS_DYN_MULTI_VT=1)
else()
  add_compile_definitions(ASF_HAS_DYN_MULTI_VT=0)
endif()
if(ASF_HAS_DYN_SINGLE_INHERIT)
  add_compile_definitions(ASF_HAS_DYN_SINGLE_INHERIT=1)
else()
  add_compile_definitions(ASF_HAS_DYN_SINGLE_INHERIT=0)
endif()
# vt node-tag high-bit width: empty = header default (64-bit 16 / 32-bit 0).
set(ASF_DYN_VT_TAG_HI_BITS "" CACHE STRING
    "vt node-tag width in the pointer high bits; empty = header default (64-bit: 16, 32-bit: 0 = low alignment bits). LA57 machines: set 7.")
if(NOT ASF_DYN_VT_TAG_HI_BITS STREQUAL "")
  add_compile_definitions(ASF_DYN_VT_TAG_HI_BITS=${ASF_DYN_VT_TAG_HI_BITS})
endif()
link_libraries(stdc++exp)
# MinGW defaults to shared libgcc/libstdc++/winpthread; without mingw64/bin on
# PATH the exe fails with STATUS_DLL_NOT_FOUND. Fully static-link so exes are
# self-contained (this project has no other DLLs of its own).
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND WIN32))
  add_link_options(-static)
endif()

function(asf_target_defs tgt)
  cmake_parse_arguments(ASF_TD "" "INTERMEDIATE" "" ${ARGN})
  if(ASF_HAS_DYN_RTTI)
    target_compile_definitions(${tgt} PRIVATE ASF_HAS_DYN_RTTI=1)
  endif()
  if(DEFINED ASF_TD_INTERMEDIATE)
    target_compile_definitions(${tgt} PRIVATE
      ASF_HAS_DYN_INTERMEDIATE=${ASF_TD_INTERMEDIATE})
  elseif(ASF_HAS_DYN_INTERMEDIATE)
    target_compile_definitions(${tgt} PRIVATE ASF_HAS_DYN_INTERMEDIATE=1)
  else()
    target_compile_definitions(${tgt} PRIVATE ASF_HAS_DYN_INTERMEDIATE=0)
  endif()
endfunction()

# --- C++26 named module variants (feature defines are baked into the BMI) ---
# Module name maps 1:1 to the consumer's effective (RTTI, INTERMEDIATE) pair;
# include/asf.hpp selects the same name from those macros. Build all
# combinations any target in this configure can resolve to.
if(ASF_MODULE)
  include(cmake/AsfModules.cmake)
  asf_add_module(asf RTTI 0 INTERMEDIATE 1)
  asf_add_module(asf_noint RTTI 0 INTERMEDIATE 0)
  asf_add_module(asf_rtti RTTI 1 INTERMEDIATE 1)
  if(ASF_HAS_DYN_RTTI AND NOT ASF_HAS_DYN_INTERMEDIATE)
    asf_add_module(asf_rtti_noint RTTI 1 INTERMEDIATE 0)
  endif()
endif()

# Bind a test/example target to its module variant (no-op when ASF_MODULE=OFF).
function(asf_maybe_bind tgt)
  if(ASF_MODULE)
    asf_bind_module(${tgt} ${ARGN})
  endif()
endfunction()

# Examples: en/ + zh/ (same code, bilingual comments).
set(ASF_EXAMPLE_NAMES
  01_static 02_dyn 03_operators 04_lifetime 05_mi_cast 06_mirror 07_pointer_trait)
foreach(lang IN ITEMS en zh)
  foreach(name IN LISTS ASF_EXAMPLE_NAMES)
    set(tgt ex_${lang}_${name})
    add_executable(${tgt} examples/${lang}/${name}.cpp)
    target_include_directories(${tgt} PRIVATE include)
    asf_target_defs(${tgt})
    asf_maybe_bind(${tgt})
  endforeach()
endforeach()

# --- single-TU tests ---
# general/ : object/pointer bind only (no user-mirror -> dyn/iface)
# mirror/  : mirror feature suites (to_mirror bind, value_of, ann::mirror, …)
function(asf_test dir name)
  set(tgt ${name})
  if(dir STREQUAL "mirror" AND NOT name STREQUAL "design_probe")
    set(tgt mirror_${name})
  endif()
  add_executable(${tgt} tests/${dir}/${name}.cpp)
  target_include_directories(${tgt} PRIVATE include tests)
  asf_target_defs(${tgt} ${ARGN})
  asf_maybe_bind(${tgt} ${ARGN})
  set(ASF_LAST_TEST_TGT ${tgt} PARENT_SCOPE)
endfunction()

# Dual-pass suite: one shared source under tests/general/ compiles twice.
# The mirror_<name> target recompiles it with ASF_TEST_VIA_MIRROR=1, which
# switches the asf_test::to_* observer binds in test_bind.hpp to the mirror
# path. Every merged suite always builds both passes.
function(asf_dual_test name)
  asf_test(general ${name} ${ARGN})
  set(tgt mirror_${name})
  add_executable(${tgt} tests/general/${name}.cpp)
  target_include_directories(${tgt} PRIVATE include tests)
  target_compile_definitions(${tgt} PRIVATE ASF_TEST_VIA_MIRROR=1)
  asf_target_defs(${tgt} ${ARGN})
  asf_maybe_bind(${tgt} ${ARGN})
  set(ASF_LAST_TEST_TGT ${tgt} PARENT_SCOPE)
endfunction()

# --- general ---
asf_dual_test(iface_inherit)
asf_test(general iface_truncate)
asf_test(general iface_single_inherit)
asf_test(general iface_dominant_base)
# Optional compile-fail: live MI under SINGLE_INHERIT (configure build-si).
if(ASF_HAS_DYN_SINGLE_INHERIT)
  add_executable(iface_single_inherit_mi_fail
    tests/general/iface_single_inherit_mi_fail.cpp)
  target_include_directories(iface_single_inherit_mi_fail PRIVATE include tests)
  asf_target_defs(iface_single_inherit_mi_fail)
  asf_maybe_bind(iface_single_inherit_mi_fail)
  set_target_properties(iface_single_inherit_mi_fail PROPERTIES
    EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
endif()
# Multi-final + intermediate ADL (same-TU STMP / cross-TU ODR).
asf_dual_test(multi_final_ub)
asf_dual_test(iface_final_vt)
# NxN diamond MI (I1..I9 shape).
asf_dual_test(iface_grid_vt)

asf_dual_test(dyn_integration_tests)
asf_dual_test(dyn_invocation_tests)
asf_dual_test(dyn_view_tests)
asf_dual_test(dyn_lifetime_tests)
asf_dual_test(dyn_creation_tests)
asf_dual_test(cache_deref_tests)
asf_dual_test(dyn_dispatch_tests)
# RTTI suite always enables meta header / dyn_cast (independent of global option).
asf_dual_test(dyn_rtti_tests RTTI 1)
target_compile_definitions(dyn_rtti_tests PRIVATE ASF_HAS_DYN_RTTI=1)
target_compile_definitions(mirror_dyn_rtti_tests PRIVATE ASF_HAS_DYN_RTTI=1)

# --- mirror (full suites; may construct from user mirrors where applicable) ---
asf_test(mirror design_probe)
asf_test(mirror mirror_base_spec)

# Compile-fail (EXCLUDE_FROM_ALL): const-self impl wrote T& / mirror<T>.
set(ASF_COMPILE_FAIL_TGTS)

# Dual-pass compile-fail: the same const-self impl mistake in both spellings
# (T& on the general pass, mirror<T> on the mirror pass), one shared source.
foreach(name IN ITEMS cref_wrong_impl_dyn cref_wrong_impl_iface)
  add_executable(${name} tests/general/compile_fail/${name}.cpp)
  target_include_directories(${name} PRIVATE include tests)
  asf_target_defs(${name})
  asf_maybe_bind(${name})
  set_target_properties(${name} PROPERTIES
    EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
  add_executable(mirror_${name} tests/general/compile_fail/${name}.cpp)
  target_include_directories(mirror_${name} PRIVATE include tests)
  target_compile_definitions(mirror_${name} PRIVATE ASF_TEST_VIA_MIRROR=1)
  asf_target_defs(mirror_${name})
  asf_maybe_bind(mirror_${name})
  set_target_properties(mirror_${name} PROPERTIES
    EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
  list(APPEND ASF_COMPILE_FAIL_TGTS ${name} mirror_${name})
endforeach()

foreach(dir_name IN ITEMS
    "general|static_method_on_dyn"
    "general|identity_mirror_self_bind"
    "general|cache_destroy_unsupported"
    "general|cache_spec_too_small"
    "mirror|explicit_mirror_copy_init"
    "mirror|unsourced_mirror_dyn"
    "mirror|unsourced_mirror_non_mirror_call")
  string(REPLACE "|" ";" dir_name_list ${dir_name})
  list(GET dir_name_list 0 dir)
  list(GET dir_name_list 1 name)
  set(tgt ${name})
  if(dir STREQUAL "mirror")
    set(tgt mirror_${name})
  endif()
  add_executable(${tgt} tests/${dir}/compile_fail/${name}.cpp)
  target_include_directories(${tgt} PRIVATE include tests)
  asf_target_defs(${tgt})
  asf_maybe_bind(${tgt})
  set_target_properties(${tgt} PROPERTIES
    EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
  list(APPEND ASF_COMPILE_FAIL_TGTS ${tgt})
endforeach()

# Compile-fail (RTTI): reachable trait tree wider than the vt tag capacity.
add_executable(rtti_node_overflow tests/general/compile_fail/rtti_node_overflow.cpp)
target_include_directories(rtti_node_overflow PRIVATE include tests)
asf_target_defs(rtti_node_overflow)
asf_maybe_bind(rtti_node_overflow RTTI 1)
target_compile_definitions(rtti_node_overflow PRIVATE ASF_HAS_DYN_RTTI=1)
# Pin the tag capacity to 8 (hi-bit field of 3) so the 9-node chain
# overflows regardless of the platform default. A global override wins
# instead; the in-source capacity guard then fails with the reason.
if(ASF_DYN_VT_TAG_HI_BITS STREQUAL "")
  target_compile_definitions(rtti_node_overflow PRIVATE ASF_DYN_VT_TAG_HI_BITS=3)
endif()
set_target_properties(rtti_node_overflow PROPERTIES
  EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
list(APPEND ASF_COMPILE_FAIL_TGTS rtti_node_overflow)

# Verify the compile-fail targets actually fail: building them must error.
foreach(t IN LISTS ASF_COMPILE_FAIL_TGTS)
  add_test(NAME compile_fail_${t}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${t})
  set_tests_properties(compile_fail_${t} PROPERTIES WILL_FAIL TRUE)
endforeach()
if(ASF_HAS_DYN_SINGLE_INHERIT)
  add_test(NAME compile_fail_iface_single_inherit_mi_fail
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target iface_single_inherit_mi_fail)
  set_tests_properties(compile_fail_iface_single_inherit_mi_fail
    PROPERTIES WILL_FAIL TRUE)
endif()

# Cross-TU (general + mirror copies).
add_executable(multi_final_odr
  tests/general/multi_cu/multi_final_odr_main.cpp
  tests/general/multi_cu/multi_final_odr_tu_i2.cpp
  tests/general/multi_cu/multi_final_odr_tu_i3.cpp)
target_include_directories(multi_final_odr PRIVATE include tests tests/general/multi_cu)
asf_target_defs(multi_final_odr)
asf_maybe_bind(multi_final_odr)

add_executable(mirror_multi_final_odr
  tests/general/multi_cu/multi_final_odr_main.cpp
  tests/general/multi_cu/multi_final_odr_tu_i2.cpp
  tests/general/multi_cu/multi_final_odr_tu_i3.cpp)
target_include_directories(mirror_multi_final_odr PRIVATE include tests tests/general/multi_cu)
target_compile_definitions(mirror_multi_final_odr PRIVATE ASF_TEST_VIA_MIRROR=1)
asf_target_defs(mirror_multi_final_odr)
asf_maybe_bind(mirror_multi_final_odr)

# INTERMEDIATE=OFF copies of the multi-final tests: final bind + upcast only.
if(ASF_HAS_DYN_INTERMEDIATE)
  add_executable(multi_final_off tests/general/multi_final_ub.cpp)
  target_include_directories(multi_final_off PRIVATE include tests)
  asf_target_defs(multi_final_off INTERMEDIATE 0)
  asf_maybe_bind(multi_final_off INTERMEDIATE 0)

  add_executable(mirror_multi_final_off tests/general/multi_final_ub.cpp)
  target_include_directories(mirror_multi_final_off PRIVATE include tests)
  target_compile_definitions(mirror_multi_final_off PRIVATE ASF_TEST_VIA_MIRROR=1)
  asf_target_defs(mirror_multi_final_off INTERMEDIATE 0)
  asf_maybe_bind(mirror_multi_final_off INTERMEDIATE 0)

  add_executable(multi_final_odr_off
    tests/general/multi_cu/multi_final_odr_main.cpp
    tests/general/multi_cu/multi_final_odr_tu_i2.cpp
    tests/general/multi_cu/multi_final_odr_tu_i3.cpp)
  target_include_directories(multi_final_odr_off PRIVATE
    include tests tests/general/multi_cu)
  asf_target_defs(multi_final_odr_off INTERMEDIATE 0)
  asf_maybe_bind(multi_final_odr_off INTERMEDIATE 0)

  add_executable(mirror_multi_final_odr_off
    tests/general/multi_cu/multi_final_odr_main.cpp
    tests/general/multi_cu/multi_final_odr_tu_i2.cpp
    tests/general/multi_cu/multi_final_odr_tu_i3.cpp)
  target_include_directories(mirror_multi_final_odr_off PRIVATE
    include tests tests/general/multi_cu)
  target_compile_definitions(mirror_multi_final_odr_off PRIVATE ASF_TEST_VIA_MIRROR=1)
  asf_target_defs(mirror_multi_final_odr_off INTERMEDIATE 0)
  asf_maybe_bind(mirror_multi_final_odr_off INTERMEDIATE 0)
endif()

enable_testing()
foreach(t IN ITEMS
    iface_inherit iface_truncate iface_single_inherit iface_dominant_base
    multi_final_ub iface_final_vt iface_grid_vt
    dyn_integration_tests dyn_invocation_tests dyn_view_tests
    dyn_lifetime_tests dyn_creation_tests cache_deref_tests dyn_dispatch_tests
    dyn_rtti_tests
    design_probe
    mirror_iface_inherit mirror_multi_final_ub mirror_iface_final_vt
    mirror_iface_grid_vt mirror_dyn_integration_tests
    mirror_dyn_invocation_tests mirror_dyn_view_tests
    mirror_dyn_lifetime_tests mirror_dyn_creation_tests
    mirror_cache_deref_tests
    mirror_dyn_dispatch_tests
    mirror_dyn_rtti_tests)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
add_test(NAME multi_final_odr COMMAND multi_final_odr --report)
add_test(NAME mirror_multi_final_odr COMMAND mirror_multi_final_odr --report)
if(ASF_HAS_DYN_INTERMEDIATE)
  add_test(NAME multi_final_off COMMAND multi_final_off)
  add_test(NAME mirror_multi_final_off COMMAND mirror_multi_final_off)
  add_test(NAME multi_final_odr_off COMMAND multi_final_odr_off --report)
  add_test(NAME mirror_multi_final_odr_off COMMAND mirror_multi_final_odr_off --report)
endif()

option(ASF_BENCH "Fetch microsoft/proxy and build the comparison benchmark" OFF)
if(ASF_BENCH)
  add_subdirectory(bench)
endif()
