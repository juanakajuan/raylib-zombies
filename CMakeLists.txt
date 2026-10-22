cmake_minimum_required(VERSION 3.20)
project(raylib_zombies LANGUAGES C)

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# Warning flags apply to our targets only, never to raylib.
function(zombies_strict target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

add_library(game_core STATIC src/game.c)
target_include_directories(game_core PUBLIC src)
target_link_libraries(game_core PUBLIC raylib m)
zombies_strict(game_core)

add_executable(zombies src/main.c src/render.c)
target_link_libraries(zombies PRIVATE game_core)
zombies_strict(zombies)

enable_testing()
add_executable(zombies_test tests/game_test.c)
target_link_libraries(zombies_test PRIVATE game_core)
zombies_strict(zombies_test)
add_test(NAME core_loop COMMAND zombies_test)
