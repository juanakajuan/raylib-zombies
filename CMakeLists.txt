cmake_minimum_required(VERSION 3.20)
project(raylib_zombies LANGUAGES C)

include(FetchContent)

function(zombies_raylib_valid dir out)
    set(version "")
    if(EXISTS "${dir}/src/raylib.h")
        file(STRINGS "${dir}/src/raylib.h" version REGEX "#define RAYLIB_VERSION +\"5\\.5\"")
    endif()
    if(version)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

if(NOT FETCHCONTENT_SOURCE_DIR_RAYLIB)
    if(IS_ABSOLUTE "$ENV{XDG_CACHE_HOME}")
        set(zombies_cache "$ENV{XDG_CACHE_HOME}/raylib-zombies")
    elseif(IS_ABSOLUTE "$ENV{HOME}")
        set(zombies_cache "$ENV{HOME}/.cache/raylib-zombies")
    endif()
endif()
if(zombies_cache)
    set(raylib_shared "${zombies_cache}/raylib-5.5")
    set(raylib_staging "${raylib_shared}.partial")
    zombies_raylib_valid("${raylib_shared}" raylib_ready)
    if(NOT raylib_ready)
        find_package(Git REQUIRED)
        file(MAKE_DIRECTORY "${zombies_cache}")
        file(LOCK "${zombies_cache}/raylib-5.5.lock" GUARD FILE TIMEOUT 600)
        zombies_raylib_valid("${raylib_shared}" raylib_ready)
        if(NOT raylib_ready)
            file(REMOVE_RECURSE "${raylib_staging}")
            message(STATUS "Cloning raylib 5.5 into ${raylib_shared}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -c advice.detachedHead=false clone --quiet --depth 1 --branch 5.5
                        https://github.com/raysan5/raylib.git "${raylib_staging}"
                RESULT_VARIABLE clone_result)
            if(NOT clone_result EQUAL 0)
                message(FATAL_ERROR "git clone of raylib 5.5 failed (${clone_result})")
            endif()
            file(REMOVE_RECURSE "${raylib_shared}")
            file(RENAME "${raylib_staging}" "${raylib_shared}")
        endif()
        file(LOCK "${zombies_cache}/raylib-5.5.lock" RELEASE)
    endif()
    set(FETCHCONTENT_SOURCE_DIR_RAYLIB "${raylib_shared}")
endif()
if(FETCHCONTENT_SOURCE_DIR_RAYLIB)
    message(STATUS "raylib source: ${FETCHCONTENT_SOURCE_DIR_RAYLIB}")
endif()
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)
if(zombies_cache)
    unset(FETCHCONTENT_SOURCE_DIR_RAYLIB CACHE)
endif()

# Warning flags apply to our targets only, never to raylib.
function(zombies_strict target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

add_library(game_core STATIC src/game.c src/settings.c src/input.c)
target_include_directories(game_core PUBLIC src)
target_link_libraries(game_core PUBLIC raylib m)
zombies_strict(game_core)

add_executable(zombies src/main.c src/render.c src/audio.c)
target_link_libraries(zombies PRIVATE game_core)
zombies_strict(zombies)
add_custom_target(copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:zombies>/assets)
add_dependencies(zombies copy_assets)

enable_testing()
add_executable(zombies_test tests/game_test.c src/audio.c)
target_link_libraries(zombies_test PRIVATE game_core)
zombies_strict(zombies_test)
add_test(NAME core_loop COMMAND zombies_test)
add_executable(input_test tests/input_test.c tests/legacy_routing.c)
target_link_libraries(input_test PRIVATE game_core)
target_compile_definitions(input_test PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
zombies_strict(input_test)
add_test(NAME input_routing COMMAND input_test)
add_test(NAME sound_levels COMMAND python3 -I ${CMAKE_SOURCE_DIR}/tools/check_sounds.py)

add_executable(zombies_bench tests/bench_update.c)
target_link_libraries(zombies_bench PRIVATE game_core)
zombies_strict(zombies_bench)
