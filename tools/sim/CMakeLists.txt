cmake_minimum_required(VERSION 3.16)
project(wled_fx_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WLED_FX_SANITIZE "Build the simulator with the address and undefined behaviour sanitizers" OFF)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wled_fx)

file(GLOB ENGINE_SOURCES ${ENGINE_DIR}/wf_*.cpp)

# The effect group table is built explicitly rather than by static initialisers,
# so that the same code works inside ESPHome where the sources become a static
# library. ESPHome's codegen emits this into main.cpp; here CMake writes it out
# from the same `#define WLED_FX_GROUP_<ID>` convention. The simulator always
# builds every effect, so no allow-list filtering is needed.
file(GLOB EFFECT_SOURCES ${ENGINE_DIR}/wf_effects_*.cpp)
set(GROUP_DECLS "")
set(GROUP_REFS "")
set(GROUP_COUNT 0)
foreach(effect_file ${EFFECT_SOURCES})
    file(READ ${effect_file} effect_text)
    string(REGEX MATCHALL "#define[ \t]+WLED_FX_GROUP_[A-Za-z0-9_]+" group_defines "${effect_text}")
    foreach(group_define ${group_defines})
        string(REGEX REPLACE "#define[ \t]+WLED_FX_GROUP_" "" group_id "${group_define}")
        string(APPEND GROUP_DECLS "extern const EffectGroup EFFECT_GROUP_${group_id};\n")
        if(GROUP_COUNT GREATER 0)
            string(APPEND GROUP_REFS ", ")
        endif()
        string(APPEND GROUP_REFS "&EFFECT_GROUP_${group_id}")
        math(EXPR GROUP_COUNT "${GROUP_COUNT} + 1")
    endforeach()
endforeach()
if(GROUP_COUNT EQUAL 0)
    message(FATAL_ERROR "No effect groups found in ${ENGINE_DIR}")
endif()
configure_file(groups_generated.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/groups_generated.cpp @ONLY)
message(STATUS "wled_fx: linking ${GROUP_COUNT} effect group(s)")

add_executable(wled_fx_sim
    main.cpp
    png_writer.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/groups_generated.cpp
    ${ENGINE_SOURCES}
)

# WLED_FX_HOST_BUILD swaps the platform shim over to the standard library, and
# WLED_FX_ALL_EFFECTS compiles in every registered effect.
target_compile_definitions(wled_fx_sim PRIVATE WLED_FX_HOST_BUILD WLED_FX_ALL_EFFECTS)
target_include_directories(wled_fx_sim PRIVATE ${ENGINE_DIR})
target_compile_options(wled_fx_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(WLED_FX_SANITIZE)
    target_compile_options(wled_fx_sim PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(wled_fx_sim PRIVATE -fsanitize=address,undefined)
endif()
