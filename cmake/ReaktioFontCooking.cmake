include_guard(GLOBAL)

function(reaktio_cook_font_manifest)
    set(options "")
    set(oneValueArgs MANIFEST_FILE COOKED_ROOT TOOL_TARGET TARGET_NAME)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_MANIFEST_FILE OR NOT ARG_COOKED_ROOT OR NOT ARG_TOOL_TARGET OR NOT ARG_TARGET_NAME)
        message(FATAL_ERROR "reaktio_cook_font_manifest requires MANIFEST_FILE, COOKED_ROOT, TOOL_TARGET, and TARGET_NAME.")
    endif()

    if(NOT EXISTS "${ARG_MANIFEST_FILE}")
        message(FATAL_ERROR "Font authoring manifest does not exist: ${ARG_MANIFEST_FILE}")
    endif()

    get_filename_component(manifest_dir "${ARG_MANIFEST_FILE}" DIRECTORY)
    file(READ "${ARG_MANIFEST_FILE}" manifest_text)
    string(REPLACE "\r\n" "\n" manifest_text "${manifest_text}")
    string(REPLACE "\r" "\n" manifest_text "${manifest_text}")
    string(REPLACE "\n" ";" manifest_lines "${manifest_text}")

    set(current_section "")
    set(font_sources "")
    set(found_font_section FALSE)

    foreach(raw_line IN LISTS manifest_lines)
        string(STRIP "${raw_line}" line)
        if("${line}" STREQUAL "" OR line MATCHES "^[#;]")
            continue()
        endif()

        if(line MATCHES "^\\[(.+)\\]$")
            set(current_section "${CMAKE_MATCH_1}")
            if(current_section MATCHES "^font\\.")
                set(found_font_section TRUE)
            endif()
            continue()
        endif()

        string(FIND "${line}" "=" separator)
        if(separator EQUAL -1)
            message(FATAL_ERROR "Font manifest line is missing '=': ${line}")
        endif()

        math(EXPR value_start "${separator} + 1")
        string(SUBSTRING "${line}" 0 ${separator} key)
        string(SUBSTRING "${line}" ${value_start} -1 value)
        string(STRIP "${key}" key)
        string(STRIP "${value}" value)

        if(current_section MATCHES "^font\\." AND key STREQUAL "source")
            if(IS_ABSOLUTE "${value}")
                list(APPEND font_sources "${value}")
            else()
                list(APPEND font_sources "${manifest_dir}/${value}")
            endif()
        endif()
    endforeach()

    if(NOT found_font_section)
        message(FATAL_ERROR "Font manifest did not define any font sections: ${ARG_MANIFEST_FILE}")
    endif()

    list(REMOVE_DUPLICATES font_sources)
    set(font_cook_stamp "${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET_NAME}.stamp")

    add_custom_command(
        OUTPUT "${font_cook_stamp}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_COOKED_ROOT}"
        COMMAND $<TARGET_FILE:${ARG_TOOL_TARGET}> --manifest "${ARG_MANIFEST_FILE}" --cooked-root "${ARG_COOKED_ROOT}" --stamp "${font_cook_stamp}"
        DEPENDS ${ARG_TOOL_TARGET} "${ARG_MANIFEST_FILE}" ${font_sources}
        VERBATIM
    )

    add_custom_target(${ARG_TARGET_NAME} DEPENDS "${font_cook_stamp}")
endfunction()