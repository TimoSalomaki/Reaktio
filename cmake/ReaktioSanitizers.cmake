function(reaktio_enable_sanitizers target_name)
    if(MSVC)
        return()
    endif()

    set(sanitizer_flags)

    if(REAKTIO_ENABLE_ASAN)
        list(APPEND sanitizer_flags -fsanitize=address -fno-omit-frame-pointer)
    endif()

    if(REAKTIO_ENABLE_UBSAN)
        list(APPEND sanitizer_flags -fsanitize=undefined -fno-omit-frame-pointer)
    endif()

    if(NOT sanitizer_flags)
        return()
    endif()

    target_compile_options(${target_name} PRIVATE ${sanitizer_flags})
    target_link_options(${target_name} PRIVATE ${sanitizer_flags})
endfunction()