include_guard(GLOBAL)

function(reaktio_mesh_sanitize_asset_name INPUT OUTPUT_VAR)
    string(TOLOWER "${INPUT}" sanitized)
    string(REGEX REPLACE "[^a-z0-9]+" "-" sanitized "${sanitized}")
    string(REGEX REPLACE "^-+" "" sanitized "${sanitized}")
    string(REGEX REPLACE "-+$" "" sanitized "${sanitized}")
    if("${sanitized}" STREQUAL "")
        set(sanitized "mesh")
    endif()
    set(${OUTPUT_VAR} "${sanitized}" PARENT_SCOPE)
endfunction()

function(reaktio_mesh_normalize_bool INPUT_VALUE DEFAULT_VALUE OUTPUT_VAR)
    if("${INPUT_VALUE}" STREQUAL "")
        set(${OUTPUT_VAR} ${DEFAULT_VALUE} PARENT_SCOPE)
        return()
    endif()

    string(TOLOWER "${INPUT_VALUE}" lowered)
    if(lowered STREQUAL "true" OR lowered STREQUAL "1" OR lowered STREQUAL "yes" OR lowered STREQUAL "on")
        set(${OUTPUT_VAR} TRUE PARENT_SCOPE)
        return()
    endif()

    if(lowered STREQUAL "false" OR lowered STREQUAL "0" OR lowered STREQUAL "no" OR lowered STREQUAL "off")
        set(${OUTPUT_VAR} FALSE PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR "Invalid boolean value '${INPUT_VALUE}' in mesh manifest.")
endfunction()

function(reaktio_mesh_bool_to_text INPUT_VALUE OUTPUT_VAR)
    if(${INPUT_VALUE})
        set(${OUTPUT_VAR} "true" PARENT_SCOPE)
    else()
        set(${OUTPUT_VAR} "false" PARENT_SCOPE)
    endif()
endfunction()

function(reaktio_mesh_validate_pack_value NAME VALUE OUTPUT_VAR)
    if(NOT "${VALUE}" STREQUAL "0" AND NOT "${VALUE}" STREQUAL "1")
        message(FATAL_ERROR "Mesh manifest ${NAME} must be either 0 or 1, got '${VALUE}'.")
    endif()
    set(${OUTPUT_VAR} "${VALUE}" PARENT_SCOPE)
endfunction()

function(reaktio_mesh_validate_coordinate_system VALUE OUTPUT_VAR)
    if(NOT "${VALUE}" STREQUAL "lh-up+y"
       AND NOT "${VALUE}" STREQUAL "lh-up+z"
       AND NOT "${VALUE}" STREQUAL "rh-up+y"
       AND NOT "${VALUE}" STREQUAL "rh-up+z")
        message(FATAL_ERROR "Unsupported mesh coordinate_system '${VALUE}'. Expected lh-up+y, lh-up+z, rh-up+y, or rh-up+z.")
    endif()
    set(${OUTPUT_VAR} "${VALUE}" PARENT_SCOPE)
endfunction()

function(reaktio_cook_mesh_manifest)
    set(options "")
    set(oneValueArgs MANIFEST_FILE COOKED_ROOT TARGET_NAME)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_MANIFEST_FILE OR NOT ARG_COOKED_ROOT OR NOT ARG_TARGET_NAME)
        message(FATAL_ERROR "reaktio_cook_mesh_manifest requires MANIFEST_FILE, COOKED_ROOT, and TARGET_NAME.")
    endif()

    if(NOT EXISTS "${ARG_MANIFEST_FILE}")
        message(FATAL_ERROR "Mesh authoring manifest does not exist: ${ARG_MANIFEST_FILE}")
    endif()

    get_filename_component(manifest_dir "${ARG_MANIFEST_FILE}" DIRECTORY)
    file(READ "${ARG_MANIFEST_FILE}" manifest_text)
    string(REPLACE "\r\n" "\n" manifest_text "${manifest_text}")
    string(REPLACE "\r" "\n" manifest_text "${manifest_text}")
    string(REPLACE "\n" ";" manifest_lines "${manifest_text}")

    set(mesh_outputs "")
    set(current_section "")
    set(current_runtime_label "")
    set(current_source "")
    set(current_scale "1.0")
    set(current_flip_v "")
    set(current_ccw "")
    set(current_obb_steps "17")
    set(current_pack_normals "1")
    set(current_pack_uv "1")
    set(current_generate_tangents "")
    set(current_barycentric "")
    set(current_compress "")
    set(current_coordinate_system "lh-up+y")
    set(current_output_name "")

    macro(reaktio_finalize_current_mesh_section)
        if(NOT "${current_section}" STREQUAL "")
            if(current_section MATCHES "^mesh\.(.+)$")
                set(mesh_id "${CMAKE_MATCH_1}")
                if("${current_runtime_label}" STREQUAL "" OR "${current_source}" STREQUAL "")
                    message(FATAL_ERROR "Mesh manifest section [${current_section}] is missing runtime_label or source.")
                endif()

                set(source_path "${manifest_dir}/${current_source}")
                if(NOT EXISTS "${source_path}")
                    message(FATAL_ERROR "Mesh source does not exist: ${source_path}")
                endif()

                reaktio_mesh_normalize_bool("${current_flip_v}" FALSE resolved_flip_v)
                reaktio_mesh_normalize_bool("${current_ccw}" FALSE resolved_ccw)
                reaktio_mesh_normalize_bool("${current_generate_tangents}" FALSE resolved_generate_tangents)
                reaktio_mesh_normalize_bool("${current_barycentric}" FALSE resolved_barycentric)
                reaktio_mesh_normalize_bool("${current_compress}" TRUE resolved_compress)
                reaktio_mesh_validate_pack_value("pack_normals" "${current_pack_normals}" resolved_pack_normals)
                reaktio_mesh_validate_pack_value("pack_uv" "${current_pack_uv}" resolved_pack_uv)
                reaktio_mesh_validate_coordinate_system("${current_coordinate_system}" resolved_coordinate_system)

                if("${current_output_name}" STREQUAL "")
                    get_filename_component(mesh_stem "${current_source}" NAME_WE)
                else()
                    set(mesh_stem "${current_output_name}")
                endif()
                reaktio_mesh_sanitize_asset_name("${mesh_stem}" mesh_stem)

                set(mesh_output "${ARG_COOKED_ROOT}/${mesh_stem}.mesh.bin")
                set(metadata_output "${ARG_COOKED_ROOT}/${mesh_stem}.mesh.ini")
                get_filename_component(source_filename "${current_source}" NAME)
                string(REGEX REPLACE "^.*\\.([^.]+)$" "\\1" mesh_source_format "${source_filename}")
                string(TOLOWER "${mesh_source_format}" mesh_source_format)
                if("${mesh_source_format}" STREQUAL "${source_filename}")
                    message(FATAL_ERROR "Mesh source must have a file extension: ${current_source}")
                endif()

                set(geometryc_cli
                    -f "${source_path}"
                    -o "${mesh_output}"
                    -s "${current_scale}"
                    --obb "${current_obb_steps}"
                    --packnormal "${resolved_pack_normals}"
                    --packuv "${resolved_pack_uv}")
                if(resolved_flip_v)
                    list(APPEND geometryc_cli --flipv)
                endif()
                if(resolved_ccw)
                    list(APPEND geometryc_cli --ccw)
                endif()
                if(resolved_generate_tangents)
                    list(APPEND geometryc_cli --tangent)
                endif()
                if(resolved_barycentric)
                    list(APPEND geometryc_cli --barycentric)
                endif()
                if(resolved_compress)
                    list(APPEND geometryc_cli --compress)
                endif()
                list(APPEND geometryc_cli "--${resolved_coordinate_system}")

                add_custom_command(
                    OUTPUT "${mesh_output}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_COOKED_ROOT}"
                    COMMAND bgfx::geometryc ${geometryc_cli}
                    MAIN_DEPENDENCY "${source_path}"
                    VERBATIM
                )

                reaktio_mesh_bool_to_text(${resolved_flip_v} resolved_flip_v_text)
                reaktio_mesh_bool_to_text(${resolved_ccw} resolved_ccw_text)
                reaktio_mesh_bool_to_text(${resolved_generate_tangents} resolved_generate_tangents_text)
                reaktio_mesh_bool_to_text(${resolved_barycentric} resolved_barycentric_text)
                reaktio_mesh_bool_to_text(${resolved_compress} resolved_compress_text)

                set(generated_metadata_input "${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET_NAME}-${mesh_stem}.generated.ini")
                set(metadata_content
"[mesh]\nstorage = bgfx-geometry\nsource_format = ${mesh_source_format}\nscale = ${current_scale}\ncompressed = ${resolved_compress_text}\nflip_v = ${resolved_flip_v_text}\nccw = ${resolved_ccw_text}\nobb_steps = ${current_obb_steps}\npack_normals = ${resolved_pack_normals}\npack_uv = ${resolved_pack_uv}\ngenerate_tangents = ${resolved_generate_tangents_text}\nbarycentric = ${resolved_barycentric_text}\ncoordinate_system = ${resolved_coordinate_system}\npayload = ${mesh_stem}.mesh.bin\n")
                file(GENERATE OUTPUT "${generated_metadata_input}" CONTENT "${metadata_content}")

                add_custom_command(
                    OUTPUT "${metadata_output}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_COOKED_ROOT}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${generated_metadata_input}" "${metadata_output}"
                    DEPENDS "${generated_metadata_input}" "${mesh_output}"
                    VERBATIM
                )

                list(APPEND mesh_outputs "${mesh_output}" "${metadata_output}")
            elseif(NOT current_section STREQUAL "meta")
                message(FATAL_ERROR "Unsupported mesh manifest section [${current_section}].")
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
            reaktio_finalize_current_mesh_section()
            set(current_section "${next_section}")
            set(current_runtime_label "")
            set(current_source "")
            set(current_scale "1.0")
            set(current_flip_v "")
            set(current_ccw "")
            set(current_obb_steps "17")
            set(current_pack_normals "1")
            set(current_pack_uv "1")
            set(current_generate_tangents "")
            set(current_barycentric "")
            set(current_compress "")
            set(current_coordinate_system "lh-up+y")
            set(current_output_name "")
            continue()
        endif()

        string(FIND "${line}" "=" separator)
        if(separator EQUAL -1)
            message(FATAL_ERROR "Mesh manifest line is missing '=': ${line}")
        endif()

        math(EXPR value_start "${separator} + 1")
        string(SUBSTRING "${line}" 0 ${separator} key)
        string(SUBSTRING "${line}" ${value_start} -1 value)
        string(STRIP "${key}" key)
        string(STRIP "${value}" value)

        if(current_section MATCHES "^mesh\\.")
            if(key STREQUAL "runtime_label")
                set(current_runtime_label "${value}")
            elseif(key STREQUAL "source")
                set(current_source "${value}")
            elseif(key STREQUAL "scale")
                set(current_scale "${value}")
            elseif(key STREQUAL "flip_v")
                set(current_flip_v "${value}")
            elseif(key STREQUAL "ccw")
                set(current_ccw "${value}")
            elseif(key STREQUAL "obb_steps")
                set(current_obb_steps "${value}")
            elseif(key STREQUAL "pack_normals")
                set(current_pack_normals "${value}")
            elseif(key STREQUAL "pack_uv")
                set(current_pack_uv "${value}")
            elseif(key STREQUAL "generate_tangents")
                set(current_generate_tangents "${value}")
            elseif(key STREQUAL "barycentric")
                set(current_barycentric "${value}")
            elseif(key STREQUAL "compress")
                set(current_compress "${value}")
            elseif(key STREQUAL "coordinate_system")
                set(current_coordinate_system "${value}")
            elseif(key STREQUAL "output_name")
                set(current_output_name "${value}")
            else()
                message(FATAL_ERROR "Unsupported key '${key}' in mesh manifest section [${current_section}].")
            endif()
        endif()
    endforeach()

    reaktio_finalize_current_mesh_section()

    if(mesh_outputs STREQUAL "")
        message(FATAL_ERROR "Mesh manifest did not define any meshes: ${ARG_MANIFEST_FILE}")
    endif()

    add_custom_target(${ARG_TARGET_NAME} DEPENDS ${mesh_outputs})
endfunction()