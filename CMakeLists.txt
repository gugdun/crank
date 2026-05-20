cmake_minimum_required(VERSION 3.11)
project(crank)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Debug: gdb symbols + AddressSanitizer
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -fno-omit-frame-pointer -fsanitize=address,undefined")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")

# Release: optimised
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Fetch Raylib
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib
)
FetchContent_MakeAvailable(raylib)

add_executable(${PROJECT_NAME}
    src/main.c
    src/sjson.c
    src/bsp.c
    src/mesh.c
    src/lightmap.c
    src/texture.c
    src/render.c
    src/ecs/ecs.c
    src/ecs/entity.c
    src/res/res_map.c
    src/res/res_mesh.c
    src/res/res_texture.c
    src/sys/sys_fpcam.c
    src/sys/sys_map.c
    src/sys/sys_skybox.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib m)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
endif()
