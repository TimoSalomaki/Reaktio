include_guard(GLOBAL)

function(reaktio_split_csv OUTPUT_VAR INPUT_VALUE)
    set(result "")
    if(NOT "${INPUT_VALUE}" STREQUAL "")
        string(REPLACE "," ";" tokens "${INPUT_VALUE}")
        foreach(token IN LISTS tokens)
            string(STRIP "${token}" token)
            if(NOT "${token}" STREQUAL "")
                list(APPEND result "${token}")
            endif()
        endforeach()
    endif()
    set(${OUTPUT_VAR} "${result}" PARENT_SCOPE)
endfunction()

function(reaktio_normalize_relative_paths OUTPUT_VAR BASE_DIR INPUT_VALUE)
    reaktio_split_csv(tokens "${INPUT_VALUE}")
    set(result "")
    foreach(token IN LISTS tokens)
        if(IS_ABSOLUTE "${token}")
            list(APPEND result "${token}")
        else()
            list(APPEND result "${BASE_DIR}/${token}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES result)
    set(${OUTPUT_VAR} "${result}" PARENT_SCOPE)
endfunction()

function(reaktio_sanitize_shader_asset_name INPUT OUTPUT_VAR)
    string(TOLOWER "${INPUT}" sanitized)
    string(REGEX REPLACE "[^a-z0-9]+" "-" sanitized "${sanitized}")
    string(REGEX REPLACE "^-+" "" sanitized "${sanitized}")
    string(REGEX REPLACE "-+$" "" sanitized "${sanitized}")
    if("${sanitized}" STREQUAL "")
        set(sanitized "shader")
    endif()
    set(${OUTPUT_VAR} "${sanitized}" PARENT_SCOPE)
endfunction()

function(reaktio_register_cooked_shader_program)
    set(options "")
    set(oneValueArgs PROGRAM_ID RUNTIME_LABEL MANIFEST_DIR COOKED_ROOT VERTEX FRAGMENT VARYING_DEF INCLUDE_DIRS_VALUE DEFINES_VALUE)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_PROGRAM_ID OR NOT ARG_RUNTIME_LABEL OR NOT ARG_MANIFEST_DIR OR NOT ARG_COOKED_ROOT OR NOT ARG_VERTEX OR NOT ARG_FRAGMENT OR NOT ARG_VARYING_DEF)
        message(FATAL_ERROR "reaktio_register_cooked_shader_program requires PROGRAM_ID, RUNTIME_LABEL, MANIFEST_DIR, COOKED_ROOT, VERTEX, FRAGMENT, and VARYING_DEF.")
    endif()

    reaktio_sanitize_shader_asset_name("${ARG_PROGRAM_ID}" program_stem)
    reaktio_normalize_relative_paths(extra_include_dirs "${ARG_MANIFEST_DIR}" "${ARG_INCLUDE_DIRS_VALUE}")
    reaktio_split_csv(shader_defines "${ARG_DEFINES_VALUE}")
    list(APPEND extra_include_dirs "${ARG_MANIFEST_DIR}")
    list(REMOVE_DUPLICATES extra_include_dirs)

    set(program_output_dir "${ARG_COOKED_ROOT}/${program_stem}")
    set(vertex_source "${ARG_MANIFEST_DIR}/${ARG_VERTEX}")
    set(fragment_source "${ARG_MANIFEST_DIR}/${ARG_FRAGMENT}")
    set(varying_def_source "${ARG_MANIFEST_DIR}/${ARG_VARYING_DEF}")

    bgfx_compile_shaders(
        TYPE VERTEX
        SHADERS ${vertex_source}
        VARYING_DEF ${varying_def_source}
        OUTPUT_DIR ${program_output_dir}
        OUT_FILES_VAR vertex_outputs
        INCLUDE_DIRS ${extra_include_dirs}
        DEFINES ${shader_defines}
    )

    bgfx_compile_shaders(
        TYPE FRAGMENT
        SHADERS ${fragment_source}
        VARYING_DEF ${varying_def_source}
        OUTPUT_DIR ${program_output_dir}
        OUT_FILES_VAR fragment_outputs
        INCLUDE_DIRS ${extra_include_dirs}
        DEFINES ${shader_defines}
    )

    set(profile_names "")
    set(section_text "[program.${ARG_PROGRAM_ID}]\nruntime_label = ${ARG_RUNTIME_LABEL}\n")

    foreach(output_file IN LISTS vertex_outputs)
        get_filename_component(profile_dir "${output_file}" DIRECTORY)
        get_filename_component(profile_name "${profile_dir}" NAME)
        if(NOT profile_name IN_LIST profile_names)
            list(APPEND profile_names "${profile_name}")
        endif()
        file(RELATIVE_PATH relative_output "${ARG_COOKED_ROOT}" "${output_file}")
        string(APPEND section_text "vertex.${profile_name} = ${relative_output}\n")
    endforeach()

    foreach(output_file IN LISTS fragment_outputs)
        get_filename_component(profile_dir "${output_file}" DIRECTORY)
        get_filename_component(profile_name "${profile_dir}" NAME)
        if(NOT profile_name IN_LIST profile_names)
            list(APPEND profile_names "${profile_name}")
        endif()
        file(RELATIVE_PATH relative_output "${ARG_COOKED_ROOT}" "${output_file}")
        string(APPEND section_text "fragment.${profile_name} = ${relative_output}\n")
    endforeach()

    list(SORT profile_names)
    string(JOIN "," profile_list ${profile_names})
    string(APPEND section_text "profiles = ${profile_list}\n\n")

    set_property(GLOBAL APPEND PROPERTY REAKTIO_SHADER_ASSET_OUTPUTS ${vertex_outputs} ${fragment_outputs})
    set_property(GLOBAL APPEND PROPERTY REAKTIO_SHADER_MANIFEST_SECTIONS "${section_text}")
endfunction()

function(reaktio_cook_shader_manifest)
    set(options "")
    set(oneValueArgs MANIFEST_FILE COOKED_ROOT OUTPUT_MANIFEST TARGET_NAME GLOBAL_INCLUDE_DIRS_VALUE)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_MANIFEST_FILE OR NOT ARG_COOKED_ROOT OR NOT ARG_OUTPUT_MANIFEST OR NOT ARG_TARGET_NAME)
        message(FATAL_ERROR "reaktio_cook_shader_manifest requires MANIFEST_FILE, COOKED_ROOT, OUTPUT_MANIFEST, and TARGET_NAME.")
    endif()

    if(NOT EXISTS "${ARG_MANIFEST_FILE}")
        message(FATAL_ERROR "Shader authoring manifest does not exist: ${ARG_MANIFEST_FILE}")
    endif()

    set_property(GLOBAL PROPERTY REAKTIO_SHADER_ASSET_OUTPUTS)
    set_property(GLOBAL PROPERTY REAKTIO_SHADER_MANIFEST_SECTIONS)

    get_filename_component(manifest_dir "${ARG_MANIFEST_FILE}" DIRECTORY)
    file(READ "${ARG_MANIFEST_FILE}" manifest_text)
    string(REPLACE "\r\n" "\n" manifest_text "${manifest_text}")
    string(REPLACE "\r" "\n" manifest_text "${manifest_text}")
    string(REPLACE "\n" ";" manifest_lines "${manifest_text}")

    set(current_section "")
    set(current_runtime_label "")
    set(current_vertex "")
    set(current_fragment "")
    set(current_varying_def "")
    set(current_include_dirs "")
    set(current_defines "")

    macro(reaktio_finalize_current_shader_section)
        if(NOT "${current_section}" STREQUAL "")
            if(current_section MATCHES "^program\\.(.+)$")
                set(program_id "${CMAKE_MATCH_1}")
                if("${current_runtime_label}" STREQUAL "" OR "${current_vertex}" STREQUAL "" OR "${current_fragment}" STREQUAL "" OR "${current_varying_def}" STREQUAL "")
                    message(FATAL_ERROR "Shader manifest section [${current_section}] is missing runtime_label, vertex, fragment, or varying_def.")
                endif()
                reaktio_register_cooked_shader_program(
                    PROGRAM_ID "${program_id}"
                    RUNTIME_LABEL "${current_runtime_label}"
                    MANIFEST_DIR "${manifest_dir}"
                    COOKED_ROOT "${ARG_COOKED_ROOT}"
                    VERTEX "${current_vertex}"
                    FRAGMENT "${current_fragment}"
                    VARYING_DEF "${current_varying_def}"
                    INCLUDE_DIRS_VALUE "${current_include_dirs},${ARG_GLOBAL_INCLUDE_DIRS_VALUE}"
                    DEFINES_VALUE "${current_defines}"
                )
            elseif(NOT current_section STREQUAL "meta")
                message(FATAL_ERROR "Unsupported shader manifest section [${current_section}].")
            endif()
        endif()
    endmacro()

    foreach(raw_line IN LISTS manifest_lines)
        string(STRIP "${raw_line}" line)
        if("${line}" STREQUAL "" OR line MATCHES "^[#;]")
            continue()
        endif()

        if(line MATCHES "^\\[(.+)\\]$")
            set(next_section "${CMAKE_MATCH_1}")
            reaktio_finalize_current_shader_section()
            set(current_section "${next_section}")
            set(current_runtime_label "")
            set(current_vertex "")
            set(current_fragment "")
            set(current_varying_def "")
            set(current_include_dirs "")
            set(current_defines "")
            continue()
        endif()

        string(FIND "${line}" "=" separator)
        if(separator EQUAL -1)
            message(FATAL_ERROR "Shader manifest line is missing '=': ${line}")
        endif()

        math(EXPR value_start "${separator} + 1")
        string(SUBSTRING "${line}" 0 ${separator} key)
        string(SUBSTRING "${line}" ${value_start} -1 value)
        string(STRIP "${key}" key)
        string(STRIP "${value}" value)

        if(current_section MATCHES "^program\\.")
            if(key STREQUAL "runtime_label")
                set(current_runtime_label "${value}")
            elseif(key STREQUAL "vertex")
                set(current_vertex "${value}")
            elseif(key STREQUAL "fragment")
                set(current_fragment "${value}")
            elseif(key STREQUAL "varying_def")
                set(current_varying_def "${value}")
            elseif(key STREQUAL "include_dirs")
                set(current_include_dirs "${value}")
            elseif(key STREQUAL "defines")
                set(current_defines "${value}")
            else()
                message(FATAL_ERROR "Unsupported key '${key}' in shader manifest section [${current_section}].")
            endif()
        endif()
    endforeach()

    reaktio_finalize_current_shader_section()

    get_property(shader_asset_outputs GLOBAL PROPERTY REAKTIO_SHADER_ASSET_OUTPUTS)
    get_property(shader_manifest_sections GLOBAL PROPERTY REAKTIO_SHADER_MANIFEST_SECTIONS)
    list(LENGTH shader_manifest_sections shader_manifest_section_count)
    if(shader_manifest_section_count EQUAL 0)
        message(FATAL_ERROR "Shader manifest did not define any shader programs: ${ARG_MANIFEST_FILE}")
    endif()
    string(JOIN "" shader_manifest_text ${shader_manifest_sections})

    file(MAKE_DIRECTORY "${ARG_COOKED_ROOT}")
    set(generated_manifest_input "${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET_NAME}.generated.ini")
    set(manifest_content "[meta]\nschema = reaktio.cooked.shader_program_manifest.v1\n\n${shader_manifest_text}")
    file(GENERATE OUTPUT "${generated_manifest_input}" CONTENT "${manifest_content}")

    add_custom_command(
        OUTPUT "${ARG_OUTPUT_MANIFEST}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_COOKED_ROOT}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${generated_manifest_input}" "${ARG_OUTPUT_MANIFEST}"
        DEPENDS "${generated_manifest_input}" ${shader_asset_outputs}
        VERBATIM
    )

    add_custom_target(${ARG_TARGET_NAME} DEPENDS ${shader_asset_outputs} "${ARG_OUTPUT_MANIFEST}")
endfunction()