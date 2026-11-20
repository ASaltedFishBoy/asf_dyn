# Hand-rolled C++26 module build for the 'asf' module family (GCC 16.2).
#
# Why not CMake's native C++ modules (FILE_SET CXX_MODULES)? Two reasons:
#   - CMake drives GCC with -fmodule-mapper=, which disables GCC's gcm.cache
#     fallback; any import not listed in the mapper (there is no hook to add
#     one) fails to resolve.
#   - The project's remote verification host runs CMake 3.26, which cannot
#     scan C++26 modules anyway.
# Instead we compile the module interface units with plain custom commands
# (no scanning): GCC writes gcm.cache/<name>.gcm into the build dir, and
# consumers resolve `import <name>;` from there. BMI rebuild ordering is
# explicit: consumers get add_dependencies() on the module target plus
# OBJECT_DEPENDS on the .gcm so header edits rebuild both sides.
#
# Variants (same interface source, different feature defines -> different
# module names; the ABI inline namespace already encodes the feature bits):
#   asf        default feature set
#   asf_noint  ASF_HAS_DYN_INTERMEDIATE=0 (for the *_off suites)
#   asf_rtti   ASF_HAS_DYN_RTTI=1 (for dyn_rtti_tests when global RTTI is OFF)
#
# asf_add_module(<name> [RTTI 0|1] [INTERMEDIATE 0|1])
#   Generates asf_mod/<name>.cppm (from src/asf.cppm via module-name
#   substitution), compiles it into gcm.cache/<name>.gcm + asf_mod/<name>.o,
#   and creates:
#     asf_mod_<name>      custom target driving the BMI + object
#     asfmod_<name>       STATIC lib carrying the module object (link this)
# asf_bind_module(<tgt> [INTERMEDIATE 0|1] [RTTI 0|1])
#   Wires an existing consumer target to the right module variant.

set(ASF_MODULE_SOURCE ${CMAKE_SOURCE_DIR}/src/asf.cppm)
set(ASF_MODULE_DIR ${CMAKE_BINARY_DIR}/asf_mod)
set(ASF_GCM_DIR ${CMAKE_BINARY_DIR}/gcm.cache)
file(MAKE_DIRECTORY ${ASF_MODULE_DIR})
file(MAKE_DIRECTORY ${ASF_GCM_DIR})
# Regenerate the variant .cppm files when the interface source changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${ASF_MODULE_SOURCE})

# Public headers the BMI depends on (rebuild the BMI on any edit).
set(ASF_MODULE_HEADER_DEPS
  ${CMAKE_SOURCE_DIR}/include/cast.hpp
  ${CMAKE_SOURCE_DIR}/include/dyn.hpp
  ${CMAKE_SOURCE_DIR}/include/iface.hpp
  ${CMAKE_SOURCE_DIR}/include/interface.hpp
  ${CMAKE_SOURCE_DIR}/include/mirror.hpp
  ${CMAKE_SOURCE_DIR}/include/ref.hpp
  ${CMAKE_SOURCE_DIR}/include/detail/ann.hpp
  ${CMAKE_SOURCE_DIR}/include/detail/dyn_intermediate.hpp
  ${CMAKE_SOURCE_DIR}/include/detail/iface_config.hpp
  ${CMAKE_SOURCE_DIR}/include/detail/iface_proxy.hpp
  ${CMAKE_SOURCE_DIR}/include/detail/iface_trait.hpp
  ${CMAKE_SOURCE_DIR}/include/detail/iface_vtable.hpp
  ${ASF_MODULE_SOURCE})

function(asf_add_module name)
  cmake_parse_arguments(A "" "RTTI;INTERMEDIATE" "" ${ARGN})
  if(NOT DEFINED A_RTTI)
    if(ASF_HAS_DYN_RTTI)
      set(A_RTTI 1)
    else()
      set(A_RTTI 0)
    endif()
  endif()
  if(NOT DEFINED A_INTERMEDIATE)
    if(ASF_HAS_DYN_INTERMEDIATE)
      set(A_INTERMEDIATE 1)
    else()
      set(A_INTERMEDIATE 0)
    endif()
  endif()

  # Generate the variant interface unit from the committed source.
  file(READ ${ASF_MODULE_SOURCE} _cppm)
  string(REPLACE "export module asf;" "export module ${name};" _cppm "${_cppm}")
  set(_src ${ASF_MODULE_DIR}/${name}.cppm)
  file(GENERATE OUTPUT ${_src} CONTENT "${_cppm}")

  set(_defs
    -DASF_HAS_DYN_RTTI=${A_RTTI}
    -DASF_HAS_DYN_INTERMEDIATE=${A_INTERMEDIATE}
    -DASF_HAS_DYN_MULTI_VT=$<BOOL:${ASF_HAS_DYN_MULTI_VT}>
    -DASF_HAS_DYN_SINGLE_INHERIT=$<BOOL:${ASF_HAS_DYN_SINGLE_INHERIT}>)
  if(NOT ASF_DYN_VT_TAG_HI_BITS STREQUAL "")
    list(APPEND _defs -DASF_DYN_VT_TAG_HI_BITS=${ASF_DYN_VT_TAG_HI_BITS})
  endif()

  set(_gcm ${ASF_GCM_DIR}/${name}.gcm)
  set(_obj ${ASF_MODULE_DIR}/${name}.o)
  add_custom_command(
    OUTPUT ${_gcm} ${_obj}
    COMMAND ${CMAKE_CXX_COMPILER}
            -std=c++26 -freflection -fmodules-ts -Wall -Wextra -Wpedantic
            ${_defs}
            -I ${CMAKE_SOURCE_DIR}/include
            -x c++ -c ${_src} -o ${_obj}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS ${ASF_MODULE_HEADER_DEPS}
    COMMENT "module ${name} (gcm.cache/${name}.gcm)"
    VERBATIM)
  add_custom_target(asf_mod_${name} DEPENDS ${_gcm} ${_obj})

  # Serialize module BMI builds: concurrent GCC module compiles sharing one
  # gcm.cache crash some GCC builds (crosstool GCC 16 on ygl dies with a bus
  # error in module_state::write_cluster). Module compiles are ~15 s each;
  # consumers still build fully parallel afterwards.
  get_property(_last_mod GLOBAL PROPERTY ASF_LAST_MOD_TARGET)
  if(_last_mod)
    add_dependencies(asf_mod_${name} ${_last_mod})
  endif()
  set_property(GLOBAL PROPERTY ASF_LAST_MOD_TARGET asf_mod_${name})

  # Static lib carrying the module object for consumers to link.
  add_library(asfmod_${name} STATIC ${_obj})
  set_source_files_properties(${_obj} PROPERTIES
    EXTERNAL_OBJECT TRUE GENERATED TRUE)
  set_target_properties(asfmod_${name} PROPERTIES LINKER_LANGUAGE CXX)
  add_dependencies(asfmod_${name} asf_mod_${name})
  if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND WIN32))
    target_link_options(asfmod_${name} INTERFACE -static)
  endif()
endfunction()

function(asf_bind_module tgt)
  cmake_parse_arguments(A "" "INTERMEDIATE;RTTI" "" ${ARGN})
  # Effective feature bits of this consumer (same rule as asf_target_defs).
  if(DEFINED A_INTERMEDIATE)
    set(_int ${A_INTERMEDIATE})
  elseif(ASF_HAS_DYN_INTERMEDIATE)
    set(_int 1)
  else()
    set(_int 0)
  endif()
  if((DEFINED A_RTTI AND A_RTTI) OR ASF_HAS_DYN_RTTI)
    set(_rtti 1)
  else()
    set(_rtti 0)
  endif()
  # Module name = consumer's (RTTI, INTERMEDIATE) pair; matches the
  # selection logic in include/asf.hpp.
  if(_rtti AND NOT _int)
    set(_mod asf_rtti_noint)
  elseif(_rtti)
    set(_mod asf_rtti)
  elseif(NOT _int)
    set(_mod asf_noint)
  else()
    set(_mod asf)
  endif()
  target_compile_options(${tgt} PRIVATE -fmodules-ts)
  target_compile_definitions(${tgt} PRIVATE ASF_USE_MODULE=1)
  target_link_libraries(${tgt} PRIVATE asfmod_${_mod})
  add_dependencies(${tgt} asf_mod_${_mod})
  get_target_property(_srcs ${tgt} SOURCES)
  foreach(_s IN LISTS _srcs)
    set_source_files_properties(${_s} PROPERTIES
      OBJECT_DEPENDS ${ASF_GCM_DIR}/${_mod}.gcm)
  endforeach()
endfunction()
