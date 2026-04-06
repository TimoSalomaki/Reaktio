function(reaktio_add_dev_targets)
    file(
        GLOB_RECURSE reaktio_source_files
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/engine/*.cpp"
        "${PROJECT_SOURCE_DIR}/engine/*.hpp"
        "${PROJECT_SOURCE_DIR}/games/*.cpp"
        "${PROJECT_SOURCE_DIR}/games/*.hpp"
    )
    file(GLOB_RECURSE reaktio_doc_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/docs/*.md")

    find_program(CLANG_FORMAT_EXE NAMES clang-format)
    if(CLANG_FORMAT_EXE)
        add_custom_target(
            reaktio-format
            COMMAND "${CLANG_FORMAT_EXE}" -i ${reaktio_source_files}
            COMMENT "Formatting Reaktio source files"
            VERBATIM
        )
    else()
        add_custom_target(
            reaktio-format
            COMMAND "${CMAKE_COMMAND}" -E echo "clang-format not found on PATH."
            VERBATIM
        )
    endif()

    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        add_custom_target(
            reaktio-lint
            COMMAND
                "${CLANG_TIDY_EXE}"
                --quiet
                "${PROJECT_SOURCE_DIR}/engine/foundation/src/BuildInfo.cpp"
                "${PROJECT_SOURCE_DIR}/engine/foundation/src/Telemetry.cpp"
                "${PROJECT_SOURCE_DIR}/engine/gameplay/src/GameModeRegistry.cpp"
                "${PROJECT_SOURCE_DIR}/engine/platform/src/StackProbe.cpp"
                "${PROJECT_SOURCE_DIR}/engine/app/src/SmokeApplication.cpp"
                "${PROJECT_SOURCE_DIR}/games/templates/starter_mode/src/StarterMode.cpp"
                --
                -std=c++20
                -I"${PROJECT_SOURCE_DIR}/engine/foundation/include"
                -I"${PROJECT_SOURCE_DIR}/engine/gameplay/include"
                -I"${PROJECT_SOURCE_DIR}/engine/platform/include"
                -I"${PROJECT_SOURCE_DIR}/engine/app/include"
                -I"${PROJECT_SOURCE_DIR}/games/templates/starter_mode/include"
                -I"${PROJECT_SOURCE_DIR}/external/SDL/include"
                -I"${PROJECT_SOURCE_DIR}/external/bgfx.cmake/bgfx/include"
                -I"${PROJECT_SOURCE_DIR}/external/bgfx.cmake/bx/include"
                -I"${PROJECT_SOURCE_DIR}/external/bgfx.cmake/bimg/include"
            COMMENT "Running clang-tidy on Reaktio source files"
            VERBATIM
        )
    else()
        add_custom_target(
            reaktio-lint
            COMMAND "${CMAKE_COMMAND}" -E echo "clang-tidy not found on PATH."
            VERBATIM
        )
    endif()

    add_custom_target(reaktio-static-analysis DEPENDS reaktio-lint)
    add_custom_target(reaktio-docs SOURCES ${reaktio_doc_files})
endfunction()