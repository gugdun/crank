cmake_minimum_required(VERSION 3.21)

project(crank LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# =========================================================
# Options
# =========================================================

option(CRANK_ENABLE_ASAN "Enable AddressSanitizer" ON)
option(CRANK_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" ON)

# =========================================================
# Raylib
# =========================================================

include(FetchContent)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
)

FetchContent_MakeAvailable(raylib)

# =========================================================
# Executable
# =========================================================

add_executable(${PROJECT_NAME}
    src/main.c
    src/sjson.c
    src/bsp.c
    src/mesh.c
    src/lightmap.c
    src/texture.c
    src/render.c
    src/vis.c
    src/phys.c

    src/ecs/ecs.c
    src/ecs/entity.c

    src/res/res_map.c
    src/res/res_mesh.c
    src/res/res_texture.c

    src/sys/sys_input.c
    src/sys/sys_usercmd.c
    src/sys/sys_fpcam.c
    src/sys/sys_view.c
    src/sys/sys_sim.c
    src/sys/sys_map.c
    src/sys/sys_player.c
    src/sys/sys_skybox.c
)

target_include_directories(${PROJECT_NAME}
    PRIVATE src
)

# =========================================================
# Compiler warnings
# =========================================================

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# =========================================================
# Debug / Release flags
# =========================================================

if(MSVC)

    # Debug
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/Zi /Od>
    )

    # Release
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2>
    )

else()

    # Debug
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:-g3 -O0 -fno-omit-frame-pointer>
    )

    # Release
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O2>
    )

endif()

# =========================================================
# Sanitizers (Clang/GCC)
# =========================================================

if(NOT MSVC)

    if(CRANK_ENABLE_ASAN)
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address>
        )

        target_link_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address>
        )
    endif()

    if(CRANK_ENABLE_UBSAN)
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=undefined>
        )

        target_link_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=undefined>
        )
    endif()

endif()

# =========================================================
# Platform-specific libraries
# =========================================================

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        raylib
)

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            m
    )
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            opengl32
    )
endif()

# macOS usually does not need extra setup because raylib
# links frameworks automatically.
