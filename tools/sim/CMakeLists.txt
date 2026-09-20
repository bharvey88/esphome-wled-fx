cmake_minimum_required(VERSION 3.16)
project(wled_fx_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without this CMake passes no optimisation flag at all, and an unoptimised
# build of 223 effects over a 16384 pixel canvas is slow enough to change what
# is practical to run. RelWithDebInfo keeps the line numbers the sanitizers
# need. Pass -DCMAKE_BUILD_TYPE=Debug to override.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

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

# The audio processing core is framework free, so the same pipeline the firmware
# runs can be fed synthetic PCM here. It needs no effects and no canvas, so it
# gets its own small executable rather than a flag on the simulator.
add_executable(wled_fx_audio_test
    audio_test.cpp
    ${ENGINE_DIR}/wf_audio.cpp
    ${ENGINE_DIR}/wf_audio_core.cpp
    ${ENGINE_DIR}/wf_fft.cpp
    ${ENGINE_DIR}/wf_math.cpp
    ${ENGINE_DIR}/wf_platform.cpp
)
target_compile_definitions(wled_fx_audio_test PRIVATE WLED_FX_HOST_BUILD)
target_include_directories(wled_fx_audio_test PRIVATE ${ENGINE_DIR})
target_compile_options(wled_fx_audio_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(WLED_FX_SANITIZE)
    target_compile_options(wled_fx_audio_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(wled_fx_audio_test PRIVATE -fsanitize=address,undefined)
endif()

# Behaviour tests: the questions a contact sheet cannot answer, such as which
# way a scrolling effect moves and whether a control leaks into the next effect.
add_executable(wled_fx_effect_test
    effect_test.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/groups_generated.cpp
    ${ENGINE_SOURCES}
)
target_compile_definitions(wled_fx_effect_test PRIVATE WLED_FX_HOST_BUILD WLED_FX_ALL_EFFECTS)
target_include_directories(wled_fx_effect_test PRIVATE ${ENGINE_DIR})
target_compile_options(wled_fx_effect_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(WLED_FX_SANITIZE)
    target_compile_options(wled_fx_effect_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(wled_fx_effect_test PRIVATE -fsanitize=address,undefined)
endif()
