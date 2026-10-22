cmake_minimum_required(VERSION 3.28)
project(asf_dyn VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ASF_HAS_DYN_RTTI
  "Experimental: dyn vtable RTTI / dyn_cast (over-accepts downcasts; see docs)"
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

# Examples: en/ + zh/ (same code, bilingual comments).
set(ASF_EXAMPLE_NAMES
  01_static 02_dyn 03_operators 04_lifetime 05_mi_cast 06_mirror 07_pointer_trait)
foreach(lang IN ITEMS en zh)
  foreach(name IN LISTS ASF_EXAMPLE_NAMES)
    set(tgt ex_${lang}_${name})
    add_executable(${tgt} examples/${lang}/${name}.cpp)
    target_include_directories(${tgt} PRIVATE include)
    asf_target_defs(${tgt})
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
  set(ASF_LAST_TEST_TGT ${tgt} PARENT_SCOPE)
endfunction()

# --- general ---
asf_test(general iface_inherit)
asf_test(general iface_truncate)
asf_test(general iface_single_inherit)
# Optional compile-fail: live MI under SINGLE_INHERIT (configure build-si).
if(ASF_HAS_DYN_SINGLE_INHERIT)
  add_executable(iface_single_inherit_mi_fail
    tests/general/iface_single_inherit_mi_fail.cpp)
  target_include_directories(iface_single_inherit_mi_fail PRIVATE include tests)
  asf_target_defs(iface_single_inherit_mi_fail)
  set_target_properties(iface_single_inherit_mi_fail PROPERTIES
    EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
endif()
# Multi-final + intermediate ADL (same-TU STMP / cross-TU ODR).
asf_test(general multi_final_ub)
asf_test(general iface_final_vt)
# NxN diamond MI (I1..I9 shape).
asf_test(general iface_grid_vt)

asf_test(general dyn_integration_tests)
asf_test(general dyn_invocation_tests)
asf_test(general dyn_view_tests)
asf_test(general dyn_lifetime_tests)
asf_test(general dyn_creation_tests)
asf_test(general dyn_dispatch_tests)
# RTTI suite always enables meta header / dyn_cast (independent of global option).
asf_test(general dyn_rtti_tests)
target_compile_definitions(dyn_rtti_tests PRIVATE ASF_HAS_DYN_RTTI=1)

# --- mirror (full suites; may construct from user mirrors where applicable) ---
asf_test(mirror design_probe)
asf_test(mirror mirror_base_spec)
asf_test(mirror iface_inherit)
asf_test(mirror multi_final_ub)
asf_test(mirror iface_final_vt)
asf_test(mirror iface_grid_vt)
asf_test(mirror dyn_integration_tests)
asf_test(mirror dyn_invocation_tests)
asf_test(mirror dyn_view_tests)
asf_test(mirror dyn_lifetime_tests)
asf_test(mirror dyn_creation_tests)
asf_test(mirror dyn_dispatch_tests)
asf_test(mirror dyn_rtti_tests)
target_compile_definitions(mirror_dyn_rtti_tests PRIVATE ASF_HAS_DYN_RTTI=1)

# Compile-fail (EXCLUDE_FROM_ALL): const-self impl wrote T& / mirror<T>.
set(ASF_COMPILE_FAIL_TGTS)
foreach(dir_name IN ITEMS
    "general|cref_wrong_impl_dyn"
    "general|cref_wrong_impl_iface"
    "general|static_method_on_dyn"
    "general|identity_mirror_self_bind"
    "mirror|cref_wrong_impl_dyn"
    "mirror|cref_wrong_impl_iface"
    "mirror|explicit_mirror_copy_init"
    "mirror|unsourced_mirror_dyn")
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
  set_target_properties(${tgt} PROPERTIES
    EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
  list(APPEND ASF_COMPILE_FAIL_TGTS ${tgt})
endforeach()

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

add_executable(mirror_multi_final_odr
  tests/mirror/multi_cu/multi_final_odr_main.cpp
  tests/mirror/multi_cu/multi_final_odr_tu_i2.cpp
  tests/mirror/multi_cu/multi_final_odr_tu_i3.cpp)
target_include_directories(mirror_multi_final_odr PRIVATE include tests tests/mirror/multi_cu)
asf_target_defs(mirror_multi_final_odr)

# INTERMEDIATE=OFF copies of the multi-final tests: final bind + upcast only.
if(ASF_HAS_DYN_INTERMEDIATE)
  add_executable(multi_final_off tests/general/multi_final_ub.cpp)
  target_include_directories(multi_final_off PRIVATE include tests)
  asf_target_defs(multi_final_off INTERMEDIATE 0)

  add_executable(mirror_multi_final_off tests/mirror/multi_final_ub.cpp)
  target_include_directories(mirror_multi_final_off PRIVATE include tests)
  asf_target_defs(mirror_multi_final_off INTERMEDIATE 0)

  add_executable(multi_final_odr_off
    tests/general/multi_cu/multi_final_odr_main.cpp
    tests/general/multi_cu/multi_final_odr_tu_i2.cpp
    tests/general/multi_cu/multi_final_odr_tu_i3.cpp)
  target_include_directories(multi_final_odr_off PRIVATE
    include tests tests/general/multi_cu)
  asf_target_defs(multi_final_odr_off INTERMEDIATE 0)

  add_executable(mirror_multi_final_odr_off
    tests/mirror/multi_cu/multi_final_odr_main.cpp
    tests/mirror/multi_cu/multi_final_odr_tu_i2.cpp
    tests/mirror/multi_cu/multi_final_odr_tu_i3.cpp)
  target_include_directories(mirror_multi_final_odr_off PRIVATE
    include tests tests/mirror/multi_cu)
  asf_target_defs(mirror_multi_final_odr_off INTERMEDIATE 0)
endif()

enable_testing()
foreach(t IN ITEMS
    iface_inherit iface_truncate iface_single_inherit
    multi_final_ub iface_final_vt iface_grid_vt
    dyn_integration_tests dyn_invocation_tests dyn_view_tests
    dyn_lifetime_tests dyn_creation_tests dyn_dispatch_tests
    dyn_rtti_tests
    design_probe
    mirror_iface_inherit mirror_multi_final_ub mirror_iface_final_vt
    mirror_iface_grid_vt mirror_dyn_integration_tests
    mirror_dyn_invocation_tests mirror_dyn_view_tests
    mirror_dyn_lifetime_tests mirror_dyn_creation_tests
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
