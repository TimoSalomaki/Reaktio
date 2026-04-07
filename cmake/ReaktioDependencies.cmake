function(reaktio_configure_dependencies)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/SDL/CMakeLists.txt")
        message(FATAL_ERROR "Vendored SDL3 is missing. Run tools/bootstrap-deps.ps1.")
    endif()

    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/bgfx.cmake/CMakeLists.txt")
        message(FATAL_ERROR "Vendored bgfx.cmake is missing. Run tools/bootstrap-deps.ps1.")
    endif()

    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/bgfx.cmake/bx/include/bx/bx.h")
        message(FATAL_ERROR "Vendored bx is missing. Run tools/bootstrap-deps.ps1.")
    endif()

    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/bgfx.cmake/bimg/include/bimg/bimg.h")
        message(FATAL_ERROR "Vendored bimg is missing. Run tools/bootstrap-deps.ps1.")
    endif()

    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/bgfx.cmake/bgfx/include/bgfx/bgfx.h")
        message(FATAL_ERROR "Vendored bgfx is missing. Run tools/bootstrap-deps.ps1.")
    endif()

    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/entt/src/entt/entt.hpp")
        message(FATAL_ERROR "Vendored EnTT is missing. Run tools/bootstrap-deps.ps1.")
    endif()

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

    set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_EXAMPLE_COMMON OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
    set(BGFX_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
    set(BGFX_CUSTOM_TARGETS ON CACHE BOOL "" FORCE)

    add_subdirectory("${PROJECT_SOURCE_DIR}/external/SDL" "${PROJECT_BINARY_DIR}/external/SDL" EXCLUDE_FROM_ALL)
    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/external/bgfx.cmake"
        "${PROJECT_BINARY_DIR}/external/bgfx.cmake"
        EXCLUDE_FROM_ALL
    )
endfunction()